#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pthread.h>
#include <err.h>
#include <bits/getopt_core.h>
#include "rwlock.h"
#include "queue.h"
#include "asgn2_helper_funcs.h"

#define MAX_HEADER_KEY_LENGTH   128
#define MAX_HEADER_VALUE_LENGTH 128
#define BUFFER_SIZE             2048
static pthread_mutex_t listMutex;

int num_threads = 4;

typedef struct node {
    int conn_fd;
    char *uri;
    struct node *next;
    rwlock_t *rwlock;
} node_t;

typedef struct list {
    node_t *head;
    node_t *tail;
} list_t;

typedef struct threadArgs {
    list_t *list;
    queue_t *queue;
} threadArgs_t;

node_t *searchNode(list_t *list, char *uri) {
    node_t *temp = list->head;
    for (; temp != NULL; temp = temp->next)
        if (strcmp(temp->uri, uri) == 0)
            return temp;
    return temp;
}

node_t *insertNode(int conn_fd, list_t *list, char *uri) {
    node_t *temp = malloc(sizeof(node_t));
    temp->conn_fd = conn_fd;
    temp->next = NULL;
    temp->rwlock = rwlock_new(N_WAY, 1);
    temp->uri = malloc(strlen(uri) + 1);
    strcpy(temp->uri, uri);
    if (list->head == NULL)
        list->head = temp;
    if (list->tail != NULL)
        list->tail->next = temp;
    list->tail = temp;
    return temp;
}

void throwInvalidPort() {
    write(STDERR_FILENO, "Invalid Port\n", 13);
    exit(1);
}

void log_entry(const char *method, const char *uri, int status_code, ssize_t request_id) {
    fprintf(stderr, "%s,%s,%d,%zd\n", method, uri, status_code, request_id);
    fflush(stderr);
}

void send_error_response(int client_fd, int status_code, const char *status) {
    char response[1024];
    snprintf(response, sizeof(response), "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n\r\n%s\n",
        status_code, status, strlen(status) + 1, status);
    // Send the response to the client
    ssize_t bytes_sent = write_n_bytes(client_fd, response, strlen(response));
    if (bytes_sent == -1) {
        perror("Error sending error response to client");
    }
}
int get_file_size(const char *filename) {
    struct stat file_status;
    if (stat(filename, &file_status) < 0) {
        return -1;
    }

    return file_status.st_size;
}
void handle_get(
    int client_fd, const char *uri, char *buffer, ssize_t bytes_read, ssize_t request_id) {

    int file_size = get_file_size(&uri[1]);

    struct stat status;
    if (stat(uri + 1, &status) != 0) {
        send_error_response(client_fd, 404, "Not Found");
        log_entry("GET", uri, 404, request_id);
        return;
    }
    if (!(status.st_mode & S_IRUSR) || S_ISDIR(status.st_mode)) {
        send_error_response(client_fd, 403, "Forbidden");
        log_entry("GET", uri, 403, request_id);
        return;
    }

    // Open the file for reading
    int file_fd = open(uri + 1, O_RDONLY);
    if (file_fd == -1) {
        // File not found
        send_error_response(client_fd, 500, "Internal Server Error");
        log_entry("GET", uri, 500, request_id);
        return;
    }

    // Send the success response with custom status phrase

    memset(buffer, '\0', BUFFER_SIZE);
    char *response = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n";
    sprintf(buffer, response, file_size);
    log_entry("GET", uri, 200, request_id);
    bytes_read = strlen(buffer);

    while (1) {
        write_n_bytes(client_fd, buffer, bytes_read);
        bytes_read = read_n_bytes(file_fd, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            break;
        }
    }

    close(file_fd);
}

void handle_put(int client_fd, const char *uri, char *buffer, ssize_t content_length,
    ssize_t header_length, ssize_t bytes_received, ssize_t request_id) {

    // Check if the file already exists
    bool file_exists = access(uri + 1, F_OK) != -1;

    // Open the file for writing, creating it if it doesn't exist
    int file_fd = open(uri + 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file_fd == -1) {
        // Error opening file
        send_error_response(client_fd, 500, "Internal Server Error");
        log_entry("PUT", uri, 500, request_id);
        return;
    }
    char *p = &buffer[header_length];
    ssize_t bytes_to_write = 0;
    while (content_length != 0) {

        if (content_length < bytes_received)
            bytes_to_write = content_length;
        else
            bytes_to_write = bytes_received;

        ssize_t bytes_written = write_n_bytes(file_fd, p, bytes_to_write);

        if (bytes_written < 0) {
            close(file_fd);
            send_error_response(client_fd, 500, "Internal Server Error");
            log_entry("PUT", uri, 500, request_id);
            return;
        }
        content_length -= bytes_written;
        int bytes_to_read = 0;
        if (content_length > BUFFER_SIZE)
            bytes_to_read = BUFFER_SIZE;
        else
            bytes_to_read = content_length;

        bytes_received = read_n_bytes(client_fd, buffer, bytes_to_read);

        if (bytes_received == 0)
            break;
        if (bytes_received < 0) {
            close(file_fd);
            send_error_response(client_fd, 500, "Internal Server Error");
            log_entry("PUT", uri, 500, request_id);
            return;
        }

        p = buffer;
    }

    if (close(file_fd) == -1) {
        // Error closing file
        send_error_response(client_fd, 500, "Internal Server Error");
        log_entry("PUT", uri, 500, request_id);
        return;
    }

    // Send the success response with appropriate status phrase
    const char *status_phrase = file_exists ? "200 OK" : "201 Created";
    const char *body = file_exists ? "OK\n" : "Created\n";
    char formatted_response[256];
    snprintf(formatted_response, sizeof(formatted_response),
        "HTTP/1.1 %s\r\nContent-Length: %zd\r\n\r\n%s", status_phrase, strlen(body), body);

    ssize_t bytes_written
        = write_n_bytes(client_fd, formatted_response, strlen(formatted_response));
    if (bytes_written < 0) {
        send_error_response(client_fd, 500, "Internal Server Error");
        log_entry("PUT", uri, 500, request_id);
        close(client_fd);
        return;
    }
    if (file_exists) {
        log_entry("PUT", uri, 200, request_id);
    } else {
        log_entry("PUT", uri, 201, request_id);
    }
}

void extract_request_info(const char *buffer, char **method, char **uri, char **version) {
    // Allocate memory for method, uri, and version strings
    *method = (char *) malloc(strlen(buffer) + 1);
    *uri = (char *) malloc(strlen(buffer) + 1);
    *version = (char *) malloc(strlen(buffer) + 1);

    // Extract method, URI, and version from the buffer
    sscanf(buffer, "%s %s %s", *method, *uri, *version);
}

ssize_t get_content_length(const char *request) {
    const char *content_length_header = "Content-Length: ";
    const char *content_length_start = strstr(request, content_length_header);
    if (content_length_start == NULL) {
        return -1; // Content-Length header not found
    }

    content_length_start += strlen(content_length_header);
    ssize_t content_length;
    sscanf(content_length_start, "%zd", &content_length);
    return content_length;
}

ssize_t get_request_id(const char *request) {
    const char *request_id_header = "Request-Id: ";
    const char *request_id_start = strstr(request, request_id_header);
    if (request_id_start == NULL) {
        return -1; // Content-Length header not found
    }

    request_id_start += strlen(request_id_header);
    ssize_t request_id;
    sscanf(request_id_start, "%zd", &request_id);
    return request_id;
}

void free_mem(char *method, char *uri, char *version) {
    free(method);
    free(uri);
    free(version);
    return;
}

bool is_valid_header_field(const char *header_field) {
    char key[MAX_HEADER_KEY_LENGTH + 1];
    char value[MAX_HEADER_VALUE_LENGTH + 1];

    // Tokenize the header field

    int num_tokens = sscanf(header_field, "%128[^:]: %128[^\r\n]", key, value);

    if (num_tokens != 2) {
        return false;
    }
    int value_start_index = strlen(key) + 2; // 2 is for ": "
    if (!(value_start_index > 0 && header_field[value_start_index - 1] == ' '
            && header_field[value_start_index] != ' '))
        return false;

    // Validate the key
    for (int i = 0; key[i] != '\0'; ++i) {
        if (!(isalnum(key[i]) || key[i] == '.' || key[i] == '-')) {
            return false;
        }
    }
    if (strlen(key) > MAX_HEADER_KEY_LENGTH) {
        // Key length exceeds the limit
        return false;
    }

    // Validate the value
    for (int i = 0; value[i] != '\0'; ++i) {
        if (!isprint(value[i])) {
            // Value contains non-printable characters
            return false;
        }
    }
    if (strlen(value) > MAX_HEADER_VALUE_LENGTH) {
        // Value length exceeds the limit
        return false;
    }

    // Header field is valid
    return true;
}
void process_request(int client_fd, list_t *list) {
    char buffer[BUFFER_SIZE] = { '\0' };
    int bytes_read = read_until(client_fd, buffer, BUFFER_SIZE, "\r\n\r\n");
    char *method, *uri, *version;
    // Extract method, URI, and version from the buffer
    extract_request_info(buffer, &method, &uri, &version);

    if (bytes_read < 0) {
        send_error_response(client_fd, 500, "Internal Server Error");
        log_entry("GET", uri, 500, 1);
        return;
    }

    if (strlen(version) != 8 || strlen(uri) > 64 || strlen(uri) < 2 || strlen(method) > 8
        || uri[0] != '/') {
        send_error_response(client_fd, 400, "Bad Request");
        log_entry("GET", uri, 400, 1);
        free_mem(method, uri, version);
        return;
    }

    for (size_t i = 1; i < strlen(uri); ++i) {
        char ch = uri[i];
        if (!(isalnum(ch) || ch == '.' || ch == '-')) {
            send_error_response(client_fd, 400, "Bad Request");
            log_entry("GET", uri, 400, 1);
            free_mem(method, uri, version);
            return;
        }
    }
    if (strcmp(version, "HTTP/1.1") != 0) {
        send_error_response(client_fd, 505, "Version Not Supported");
        log_entry("GET", uri, 505, 1);
        free_mem(method, uri, version);
        return;
    }

    ssize_t header_length = (strstr(buffer, "\r\n\r\n") + 4) - buffer;

    ssize_t remaining_bytes = bytes_read - header_length;

    ssize_t content_length = get_content_length(buffer);
    ssize_t request_id = get_request_id(buffer);
    // Check validity of header fields
    char *header_start = strstr(buffer, "\r\n") + 2; // Skip the request line
    char *header_end = strstr(buffer, "\r\n\r\n") + 1;
    char *header_field = strstr(header_start, "\r\n");
    char *temp;
    while (header_field != NULL && header_field < header_end) {

        temp = (char *) malloc(header_field - header_start + 1);
        memset(temp, '\0', header_field - header_start + 1);
        memcpy(temp, header_start, header_field - header_start);
        if (!(is_valid_header_field(temp))) {
            send_error_response(client_fd, 400, "Bad Request");
            free_mem(method, uri, version);
            free(temp);
            return;
        }
        free(temp);
        header_start = header_field + 2;
        header_field = strstr(header_start, "\r\n");
    }

    pthread_mutex_lock(&listMutex);
    node_t *node = searchNode(list, uri);
    if (node == NULL) {
        node = insertNode(client_fd, list, uri);
    }
    pthread_mutex_unlock(&listMutex);
    // Handle GET and PUT requests
    if (strcmp(method, "GET") == 0) {
        reader_lock(node->rwlock);
        // Handle GET request

        handle_get(client_fd, uri, buffer, bytes_read, request_id);
        //log_entry("GET", uri, 200, "0"); // Log successful GET request

        free_mem(method, uri, version);
        reader_unlock(node->rwlock);
    } else if (strcmp(method, "PUT") == 0) {
        // Get the content length from the header
        //ssize_t content_length = get_content_length(buffer);

        if (content_length < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            log_entry("PUT", uri, 400, request_id); // Log failed PUT request due to bad request
            free_mem(method, uri, version);
            return;
        }
        writer_lock(node->rwlock);

        // Handle the PUT request with the message body
        handle_put(
            client_fd, uri, buffer, content_length, header_length, remaining_bytes, request_id);
        //log_entry("PUT", uri, 200, "0"); // Log successful PUT request
        free_mem(method, uri, version);
        writer_unlock(node->rwlock);
    }
    //Handle unsupported method
    else {
        send_error_response(client_fd, 501, "Not Implemented");
        log_entry(method, uri, 501, request_id); // Log unsupported method
        free_mem(method, uri, version);
    }
}
void *handle_request(void *args) {
    threadArgs_t *threadArgs = (threadArgs_t *) args;
    list_t *list = threadArgs->list;

    while (1) {
        uintptr_t conn_fd = -9999;
        queue_pop(threadArgs->queue, (void **) &(conn_fd));
        process_request(conn_fd, list);
        close(conn_fd);
    }
}

int main(int argc, char *argv[]) {
    int option = 0;
    while ((option = getopt(argc, argv, "t:")) != -1) {
        switch (option) {
        case 't':
            num_threads = atoi(optarg);
            if (num_threads <= 0) {
                warnx("invalid thread size");
                exit(EXIT_FAILURE);
            }
            break;
        case '?':
            if (optopt == 't') {
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            } else {
                fprintf(stderr, "Unknown option -%c\n", optopt);
            }
            exit(EXIT_FAILURE);
            break;
        }
    }

    if (optind >= argc) {
        throwInvalidPort();
    }

    int port = atoi(argv[optind]);
    if (port <= 0 || port > 65535) {
        throwInvalidPort();
    }

    Listener_Socket listener;
    if (listener_init(&listener, port) == -1) {
        throwInvalidPort();
    }
    queue_t *queue = queue_new(num_threads);
    list_t list;
    list.head = NULL;
    list.tail = NULL;

    threadArgs_t threadArgs;
    threadArgs.list = &(list);
    threadArgs.queue = queue;

    for (int i = 0; i < num_threads; i++) {
        pthread_t t;
        pthread_create(&t, NULL, handle_request, (void *) &threadArgs);
    }

    pthread_mutex_init(&listMutex, NULL);

    while (1) {
        int conn_fd = listener_accept(&listener);
        queue_push(queue, (void *) (uintptr_t) conn_fd);
    }
    return 0;
}
