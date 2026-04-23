#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define MAX_MESSAGE_SIZE 1024

int send_error_message(int client_socket, char *message);
int parse_packet(const char *packet, int packet_bytes, char *method, char *filename, int *chunk, int *size);
int get_filenames_from_directory(char *directory, char *filename, char filenames[][256]);
int send_packet(int sockfd, char *method, char *filename, int chunk, int size);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <port>\n", argv[0]);
        exit(1);
    }
    
    char method[16];
    char filename[256];
    int chunk;
    int size;
    char filenames[2][256];
    char *directory = argv[1];
    int port = atoi(argv[2]);

    // Build server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        exit(1);
    }

    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    // Bind socket to port
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(server_socket, 10) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Server listening on port %d\n", port);

    // Accept Connections
    while (1) {
        memset(method, 0, sizeof(method));
        memset(filename, 0, sizeof(filename));
        chunk = -1;
        size = -1;
        
        // Accept connection from client
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }

        printf("Client connected\n");

        // Read client request
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            printf("Received message: %s\n", buffer);
        }

        // parse the request into method and filename
        parse_packet(buffer, bytes_read, method, filename, &chunk, &size);

        // Check method of request
        if (strcmp(method, "GET") == 0) {
            // STEP 1: Check if filename is valid
            if (filename[0] == '\0') {
                printf("Error: filename is NULL\n");
                send_error_message(client_socket, "Error: filename is NULL");
                continue;
            }

            // STEP 2: Check if file exists in directory using readdir()
            if (!get_filenames_from_directory(directory, filename, filenames)) {
                send_error_message(client_socket, "Error: file does not exist");
                continue;
            }

            for (int i = 0; i < 2; i++) {
                int fd = open(filename, O_RDONLY);

                // STEP 2B: If file is inaccessible, send error message to client
                if (fd == -1) {
                    send_error_message(client_socket, "Error: file is inaccessible");
                    continue;
                }

                struct stat st;
                if (stat(filenames[i], &st) != 0) {
                    send_error_message(client_socket, "Error: file is inaccessible");
                    continue;
                }
                size_t file_size = st.st_size;

                // STEP 3: Split the filename and chunk number. Ex. foo.txt.1 -> foo.txt and 1
                char *first = strchr(filenames[i], '.');
                char *second = strchr(first + 1, '.');
                *second = '\0';
                int chunk_num = atoi(second + 1);
    
                // STEP 4: If file exists, send file to client
                bzero(buffer, sizeof(buffer));
                send_packet(client_socket, "200 OK", filenames[i], chunk_num, file_size);
                while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
                    send(client_socket, buffer, bytes_read, 0);
                }
                close(fd);
            }
        
        } else if (strcmp(method, "PUT") == 0) {
            printf("CHKPT 1\n");
            // STEP 1: Read filename from request
            if (filename[0] == '\0') {
                printf("Error: filename is NULL\n");
                send_error_message(client_socket, "Error: filename is NULL");
                continue;
            }

            // STEP 2: Create a file for the chunk
            char filename_with_chunk[256];
            snprintf(filename_with_chunk, sizeof(filename_with_chunk), "%s.%d", filename, chunk);
            FILE *file = fopen(filename_with_chunk, "wb");
            if (file == NULL) {
                printf("Error: could not create file\n");
                send_error_message(client_socket, "Error: could not create file");
                continue;
            }
            printf("CHKPT 2: filename_with_chunk: %s\n", filename_with_chunk);
            // STEP 3: Start putting in the data from client
            int bytes_written = 0;
            ssize_t bytes_read = 0;
            while ((bytes_read = read(client_socket, buffer, sizeof(buffer))) > 0) {
                if (bytes_written >= size) {
                    send_error_message(client_socket, "Error: file is too large");
                    continue;
                }
                fwrite(buffer, 1, bytes_read, file);
                bytes_written += bytes_read;
            }
            printf("CHKPT 3: bytes_written: %d\n", bytes_written);
            fclose(file);
            printf("File %s uploaded successfully\n", filename);
        } else if (strcmp(method, "LIST") == 0) {
            // STEP 1: Retreive a list of files stored on the server
            // STEP 2: Send the list of files to the client
        } else {
            // Send error message to client
            printf("Unknown request: %s\n", method);
            close(client_socket);
            continue;
        }
    }

    return 0;
}


int parse_packet(const char *packet, int packet_bytes,
    char *method, char *filename, int *chunk, int *size) {

    if (!packet || packet_bytes <= 0 || !method || !filename || !chunk || !size) {
        return -1;
    }

    // Make a null-terminated local copy of exactly packet_bytes.
    char buf[MAX_MESSAGE_SIZE + 1];
    int n = packet_bytes > MAX_MESSAGE_SIZE ? MAX_MESSAGE_SIZE : packet_bytes;
    memcpy(buf, packet, n);
    buf[n] = '\0';

    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) return -1;
    *header_end = '\0';  // parse only headers

    char method_buf[16];
    char filename_buf[256];
    method_buf[0] = '\0';
    filename_buf[0] = '\0';
    *chunk = -1;
    *size = -1;

    char *save = NULL;
    char *line = strtok_r(buf, "\r\n", &save);
    while (line) {
        if (sscanf(line, "method: %15s", method_buf) == 1) {
            // parsed method
        } else if (sscanf(line, "filename: %255s", filename_buf) == 1) {
            // parsed filename
        } else if (sscanf(line, "chunk: %d", chunk) == 1) {
            // parsed chunk
        } else if (sscanf(line, "size: %d", size) == 1) {
            // parsed length
        }
        line = strtok_r(NULL, "\r\n", &save);
    }

    if (method_buf[0] == '\0' || filename_buf[0] == '\0' || *chunk < 0 || *size < 0) {
        return -1; // invalid packet
    }

    strcpy(method, method_buf);
    strcpy(filename, filename_buf);
return 0;
}

int send_error_message(int client_socket, char *message) {
    /* send an error message to the client */
    char error_message[1024] = "404 Not Found\r\n";
    strcat(error_message, message);
    send(client_socket, error_message, strlen(error_message), 0);
    return 0;
}

int get_filenames_from_directory(char *directory, char *filename, char filenames[][256]) {
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        printf("Error: directory is inaccessible\n");
        return 0;
    }
    struct dirent *entry;
    size_t filename_len = strlen(filename);
    int i = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, filename, filename_len) == 0 && entry->d_name[filename_len] == '.') {
            strcpy(filenames[i], entry->d_name);
            i++;
            if (i == 1) {
                closedir(dir);
                return 1;
            }
        }
    }
    closedir(dir);
    return 0;
}

int send_packet(int sockfd, char *method, char *filename, int chunk, int size) {
    char buffer[MAX_MESSAGE_SIZE];
    sprintf(buffer, "method: %s\r\n"
        "filename: %s\r\n"
        "chunk: %d\r\n"
        "size: %d\r\n"
        "\r\n",
        method, filename, chunk, size
    );
    if (send(sockfd, buffer, strlen(buffer), 0) == -1) {
        fprintf(stderr, "Error: Could not send packet to server\n");
        return -1;
    }
    return 0;
}
