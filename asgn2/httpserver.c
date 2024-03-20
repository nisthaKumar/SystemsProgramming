#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>
#include "asgn2_helper_funcs.h"
#define MAX_HEADER_KEY_LENGTH   128
#define MAX_HEADER_VALUE_LENGTH 128

#define BUFFER_SIZE 2048

void throwInvalidPort() {
    write(STDERR_FILENO, "Invalid Port\n", 13);
    exit(1);
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
void handle_get(int client_fd, const char *uri, char *buffer, ssize_t bytes_read) {

    int file_size = get_file_size(&uri[1]);

    struct stat status;
    if (stat(uri + 1, &status) != 0) {
        send_error_response(client_fd, 404, "Not Found");
        return;
    }
    if (!(status.st_mode & S_IRUSR) || S_ISDIR(status.st_mode)) {
        send_error_response(client_fd, 403, "Forbidden");
        return;
    }

    // Open the file for reading
    int file_fd = open(uri + 1, O_RDONLY);
    if (file_fd == -1) {
        // File not found
        send_error_response(client_fd, 500, "Internal Server Error");
        return;
    }

    // Send the success response with custom status phrase

    memset(buffer, '\0', BUFFER_SIZE);
    char *response = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n";
    sprintf(buffer, response, file_size);
    //printf("Buffer:%s\n", buffer);

    bytes_read = strlen(buffer);

    while (1) {
        write_n_bytes(client_fd, buffer, bytes_read);
        //printf("Bytes Written:%d\n", bytes_written);
        bytes_read = read_n_bytes(file_fd, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            break;
        }
    }

    close(file_fd);
}

void handle_put(int client_fd, const char *uri, char *buffer, ssize_t content_length,
    ssize_t header_length, ssize_t bytes_received) {

    // Check if the file already exists
    bool file_exists = access(uri + 1, F_OK) != -1;

    // Open the file for writing, creating it if it doesn't exist
    int file_fd = open(uri + 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file_fd == -1) {
        // Error opening file
        send_error_response(client_fd, 500, "Internal Server Error");
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
            return;
        }

        p = buffer;
    }

    if (close(file_fd) == -1) {
        // Error closing file
        send_error_response(client_fd, 500, "Internal Server Error");
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
        close(client_fd);
        return;
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

void handle_request(int client_fd) {
    char buffer[BUFFER_SIZE] = { '\0' };

    int bytes_read = read_until(client_fd, buffer, BUFFER_SIZE, "\r\n\r\n");
    if (bytes_read < 0) {
        send_error_response(client_fd, 500, "Internal Server Error");
        return;
    }

    char *method, *uri, *version;

    // Extract method, URI, and version from the buffer
    extract_request_info(buffer, &method, &uri, &version);

    if (strlen(version) != 8 || strlen(uri) > 64 || strlen(uri) < 2 || strlen(method) > 8
        || uri[0] != '/') {
        send_error_response(client_fd, 400, "Bad Request");
        free_mem(method, uri, version);
        return;
    }

    for (size_t i = 1; i < strlen(uri); ++i) {
        char ch = uri[i];
        if (!(isalnum(ch) || ch == '.' || ch == '-')) {
            send_error_response(client_fd, 400, "Bad Request");
            free_mem(method, uri, version);
            return;
        }
    }
    if (strcmp(version, "HTTP/1.1") != 0) {
        send_error_response(client_fd, 505, "Version Not Supported");
        free_mem(method, uri, version);
        return;
    }

    ssize_t header_length = (strstr(buffer, "\r\n\r\n") + 4) - buffer;

    ssize_t remaining_bytes = bytes_read - header_length;

    ssize_t content_length = get_content_length(buffer);
    // Check validity of header fields
    char *header_start = strstr(buffer, "\r\n") + 2; // Skip the request line
    char *header_end = strstr(buffer, "\r\n\r\n") + 1;
    //char *header_field = strtok(header_start, "\r\n");
    char *header_field = strstr(header_start, "\r\n");
    //ssize_t content_length = -1;
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

    // Handle GET and PUT requests
    if (strcmp(method, "GET") == 0) {
        // Handle GET request
        handle_get(client_fd, uri, buffer, bytes_read);
        free_mem(method, uri, version);
    } else if (strcmp(method, "PUT") == 0) {
        // Get the content length from the header
        //ssize_t content_length = get_content_length(buffer);
        if (content_length < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            free_mem(method, uri, version);
            return;
        }
        // Handle the PUT request with the message body

        handle_put(client_fd, uri, buffer, content_length, header_length, remaining_bytes);
        free_mem(method, uri, version);
    }
    //Handle unsupported method
    else {
        send_error_response(client_fd, 501, "Not Implemented");
        free_mem(method, uri, version);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse port number from command line argument
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
        throwInvalidPort();

    // Initialize listener socket
    Listener_Socket listener;
    if (listener_init(&listener, port) == -1)
        throwInvalidPort();

    while (1) {
        // Accept new connection
        int client_fd = listener_accept(&listener);
        if (client_fd == -1) {
            exit(0);
        }
        handle_request(client_fd);
        close(client_fd);
    }
    return 0;
}
