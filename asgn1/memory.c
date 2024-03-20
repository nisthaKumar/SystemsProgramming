#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_BUFFER_SIZE 4096

char buffer[MAX_BUFFER_SIZE];
char commandType[4], filename[50], contentLength[20], content[MAX_BUFFER_SIZE];
void throwOperationFailed() {
    write(STDERR_FILENO, "Operation Failed\n", 17);
    exit(1);
}

void throwInvalidCommand() {
    write(STDERR_FILENO, "Invalid Command\n", 16);
    exit(1);
}

void get() {

    if (contentLength[0] != '\0')
        throwInvalidCommand();

    struct stat file_stat;
    if (stat(filename, &file_stat) != 0)
        throwInvalidCommand();

    if (S_ISDIR(file_stat.st_mode))
        throwInvalidCommand();
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
        throwOperationFailed();
    ssize_t bytesRead;
    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
        write(STDOUT_FILENO, buffer, bytesRead);

    char lastChar;
    if (read(STDIN_FILENO, &lastChar, 1) > 0 && lastChar != '\n')
        throwInvalidCommand();
    close(fd);
}
int findStartPositionOfContent() {
    const int numNewLinesToFind = 3;
    int numNewLinesFound = 0;
    int currentPosition = 0;

    while (numNewLinesFound < numNewLinesToFind) {
        if (buffer[currentPosition] == '\n') {
            numNewLinesFound++;
        }
        currentPosition++;
    }

    return currentPosition;
}

void set(int bytesRead) {

    for (int i = 0; contentLength[i] != '\0'; i++) {
        if (contentLength[i] < '0' || contentLength[i] > '9') {
            throwInvalidCommand();
        }
    }

    int content_length = atoi(contentLength);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd == -1)
        throwOperationFailed();
    int totalBytesWritten = 0;
    char *p = &buffer[findStartPositionOfContent()];
    int bytesToRead = bytesRead - findStartPositionOfContent();
    int bytesWritten;

    while (1) {
        if (bytesToRead == -1)
            throwOperationFailed();
        if (bytesToRead == 0)
            break;
        if (content_length < (totalBytesWritten + bytesToRead))
            bytesToRead = content_length - totalBytesWritten;
        while (bytesToRead != 0) {
            bytesWritten = write(fd, p, bytesToRead);
            if (bytesWritten == -1)
                throwOperationFailed();
            bytesToRead = bytesToRead - bytesWritten;
            p += bytesWritten;
            totalBytesWritten += bytesWritten;
        }

        bytesToRead = read(STDIN_FILENO, buffer, MAX_BUFFER_SIZE);
        p = buffer;
    }
    fprintf(stdout, "OK\n");
}

int main() {
    int bytesRead = 0;
    while (1) {
        int out = read(STDIN_FILENO, &(buffer[bytesRead]), MAX_BUFFER_SIZE - bytesRead);
        if (out == -1)
            throwOperationFailed();
        bytesRead += out;
        if (bytesRead == MAX_BUFFER_SIZE || out == 0)
            break;
    }

    sscanf(buffer, "%s\n%s\n%s\n%[^\n]", commandType, filename, contentLength, content);

    if (strcmp(commandType, "get") == 0) {
        if (buffer[bytesRead - 1] == '\n')
            get();
        else
            throwInvalidCommand();
    } else if (strcmp(commandType, "set") == 0) {

        set(bytesRead);
    }

    else {
        //	printf("Testing error");
        throwInvalidCommand();
    }

    return 0;
}
