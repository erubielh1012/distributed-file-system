#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <openssl/md5.h>
#include <math.h>

#define MAX_MESSAGE_SIZE 1024
#define NUM_SERVERS 4
#define NUM_CHUNKS 4
#define SERVER_LIST "dfc.conf"
#define TIMEOUT_SEC 1

typedef struct {
    char filename[256];
    int chunks[NUM_CHUNKS];
} FileEntry;


int connect_to_server(char *server_address, int server_port, int timeout_sec);
int send_packet(int sockfd, char *method, char *filename, int chunk, int size);
int enough_servers_available(int *server_active);
int download_chunks(char server_addresses[NUM_SERVERS][256], int server_ports[NUM_SERVERS], int chunks_downloaded[NUM_CHUNKS], char *filename);
int download_one_chunk(int sockfd, char *filename, int request_chunk);
int parse_packet(const char *packet, int packet_bytes, char *method, char *filename, int *chunk, int *size);
void servers_to_send_chunk(int *server1, int *server2, int x, int chunk_number);
int upload_chunk_to_server(char *server_address, int server_port, char *filename, int chunk_number, char *chunk_data, int current_chunk_size);
int list_files(char server_addresses[NUM_SERVERS][256], int server_ports[NUM_SERVERS]);

int main(int argc, char *argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [filename]\n", argv[0]);
        exit(1);
    }

    int server_active[NUM_SERVERS] = {0};
    int chunks_downloaded[NUM_CHUNKS] = {0};
    char server_name[256];
    char server_addresses[NUM_SERVERS][256];
    int server_ports[NUM_SERVERS];
    unsigned char digest[MD5_DIGEST_LENGTH];

    char *command = argv[1];
    char *filename = argc > 2 ? argv[2] : NULL;

    
    if (strcmp(command, "get") == 0) {
        if (filename == NULL) {
            fprintf(stderr, "Usage: %s get <filename>\n", argv[0]);
            exit(1);
        }
        
        // GET command: download a file from the DFS servers
        // STEP 1: Read dfc.conf file to get the list of DFS servers.
        FILE *fd = fopen(SERVER_LIST, "r");
        if (fd == NULL) {
            fprintf(stderr, "Error: Could not open %s\n", SERVER_LIST);
            exit(1);
        }
        
        // STEP 2: Attempt connection to 4 servers (1-second timeout per server).
        for (int i = 0; i < NUM_SERVERS; i++) {

            char line[256];

            fgets(line, sizeof(line), fd);
            char temp[256];
            sscanf(line, "server %255s %255s", server_name, temp);
            char *pos = strstr(temp, ":");
            *pos = '\0';
            strcpy(server_addresses[i], temp);
            server_ports[i] = atoi(pos + 1);

            // STEP 2B: Attempt to connect to the server with a 1-second timeout.
            int sockfd = connect_to_server(server_addresses[i], server_ports[i], TIMEOUT_SEC);
            if (sockfd != -1) {
                server_active[i] = 1;
                close(sockfd);
            } else {
                server_active[i] = -1;
            }
        }
        fclose(fd);

        // STEP 3: Compute x = HASH(filename) % 4 to determine chunk placement
        MD5((unsigned char*)filename, strlen(filename), digest);
        int hash = digest[0] % NUM_SERVERS;
        int x = hash % NUM_SERVERS;

        // STEP 4: If two neighboring servers are not available, unable to retrieve the file. exit with error message. [later we can change this to a tracking table]
        if (enough_servers_available(server_active)) {
            // STEP 3: For each available server, download the file chunks and store in temp. files. make sure to track which chunks are downloaded.
            if (download_chunks(server_addresses, server_ports, chunks_downloaded, filename) == -1) {
                fprintf(stderr, "Error: Failed to download file chunks. Cannot reconstruct.\n");
                exit(1);
            }
            // STEP 4: Check if all 4 chunks are downloaded. if not, print an error message and exit.
            for (int i = 0; i < NUM_CHUNKS; i++) {
                if (chunks_downloaded[i] == 0) {
                    fprintf(stderr, "Error: File is incomplete. Cannot reconstruct\n");
                    exit(1);
                }
            }
            // STEP 5: Create output file from the chunks.
            char buffer[MAX_MESSAGE_SIZE+1];
            FILE *output_file = fopen(filename, "wb");
            if (output_file == NULL) {
                perror("fopen");
                exit(1);
            }
            for (int i = 0; i < 4; i++) {
                memset(buffer, 0, sizeof(buffer));
                char temp_filename[256];
                sprintf(temp_filename, "%s.%d", filename, i);
                FILE *temp_file = fopen(temp_filename, "rb");
                if (temp_file == NULL) {
                    fprintf(stderr, "Error: Could not open %s.%d\n", filename, i);
                    continue;
                }
                while (1) {
                    ssize_t bytes_read = fread(buffer, 1, sizeof(buffer), temp_file);
                    if (bytes_read > 0) {
                        fwrite(buffer, 1, bytes_read, output_file);
                    } else {
                        break;
                    }
                }
                // delete the temp. file
                remove(temp_filename);
                fclose(temp_file);
            }
            printf("File %s downloaded successfully\n", filename);
            fclose(output_file);
        } else {
            fprintf(stderr, "Error: Not enough servers are available to retrieve the file\n");
        }
    }
    else if (strcmp(command, "put") == 0) {
        if (filename == NULL) {
            fprintf(stderr, "Usage: %s put <filename>\n", argv[0]);
            exit(1);
        }

        // PUT command: upload a file to the DFS servers
        // STEP 1: Read dfc.conf file to get the list of DFS servers
        FILE *fd = fopen(SERVER_LIST, "r");
        if (fd == NULL) {
            fprintf(stderr, "Error: Could not open %s\n", SERVER_LIST);
            exit(1);
        }
        // STEP 2: Attempt connection to 4 servers (1-second timeout per server).
        for (int i = 0; i < NUM_SERVERS; i++) {
            memset(server_name, 0, sizeof(server_name));

            char line[256];

            if (fgets(line, sizeof(line), fd) == NULL) {
                fprintf(stderr, "Error: Could not read line from %s\n", SERVER_LIST);
                exit(1);
            }
            char temp[256];
            sscanf(line, "server %255s %255s", server_name, temp);
            char *pos = strstr(temp, ":");
            if (pos == NULL) {
                fprintf(stderr, "Error: Could not find colon in line: %s\n", line);
                exit(1);
            }
            *pos = '\0';
            strcpy(server_addresses[i], temp);
            server_ports[i] = atoi(pos + 1);
        }
        fclose(fd);

        // STEP 4: Generate a hash from filename (use MD5 hash)
        MD5((unsigned char*)filename, strlen(filename), digest);
        int hash = digest[0] % NUM_SERVERS;
        int x = hash % NUM_SERVERS;
        // STEP 5: Split the file into 4 chunks
        struct stat st;
        if (stat(filename, &st) == -1) {
            perror("stat");
            exit(1);
        }
        size_t file_size = st.st_size;
        int base_chunk_size = (int)(file_size / NUM_CHUNKS);
        int remainder = (int)(file_size % NUM_CHUNKS);
        int chunk_number = 0;
        printf("Filename: %s, File size: %zu\n", filename, file_size);
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            fprintf(stderr, "Error: Could not open %s\n", filename);
            exit(1);
        }
        while (chunk_number < NUM_CHUNKS) {
            int server1, server2;
            int current_chunk_size = base_chunk_size + (chunk_number < remainder ? 1 : 0);

            char *chunk_data = malloc(current_chunk_size);
            if (chunk_data == NULL) {
                perror("malloc");
                fclose(file);
                exit(1);
            }
            
            size_t bytes_read = fread(chunk_data, 1, current_chunk_size, file);
            if ((int)bytes_read != current_chunk_size) {
                fprintf(stderr, "Error: Failed to read chunk %d from file\n", chunk_number);
                free(chunk_data);
                fclose(file);
                exit(1);
            }

            // STEP 6: Upload each pair to DFS server based on modular arithmetic (x = HASH(filename) % y)
            servers_to_send_chunk(&server1, &server2, x, chunk_number);
            printf("Chunk number: %d, Size: %d, DFS%d, DFS%d\n",
                chunk_number, current_chunk_size, server1+1, server2+1);


            // STEP 7: Upload the chunk to the servers
            int result1 = upload_chunk_to_server(server_addresses[server1], server_ports[server1], filename, chunk_number, chunk_data, current_chunk_size);
            
            // may have to send a done message to the server
            int result2 = upload_chunk_to_server(server_addresses[server2], server_ports[server2], filename, chunk_number, chunk_data, current_chunk_size);
            
            
            if (result1 == -1 && result2 == -1) {
                fprintf(stderr, "Error: Failed to upload chunk %d to servers\n", chunk_number);
                fclose(file);
                exit(1);
            }
            if (result1 == -1 || result2 == -1) {
                fprintf(stderr, "Error: Chunk %d was only stored on one server\n", chunk_number);
            }

            free(chunk_data);
            chunk_number++;
        }
        fclose(file);
        printf("File %s uploaded successfully\n", filename);

        /* 
        Outgoing packet structure:
            PUT [filename]\r\n
            Chunk: #\r\n
            Size: #\r\n
            \r\n
            Data ...\r\n

        Incoming packet structure:
            [200 OK] or [404 Not Found]\r\n
        */
    }
    else if (strcmp(command, "list") == 0) {
        // LIST command: list all files stored across DFS servers
        
        // STEP 1: Read dfc.conf
        FILE *fd = fopen(SERVER_LIST, "r");

        if (fd == NULL) {
            fprintf(stderr, "Error: Could not open %s\n", SERVER_LIST);
            exit(1);
        }
        // STEP 2: Connect to each DFS server
        for (int i = 0; i < NUM_SERVERS; i++) {
            char line[256];
            char temp[256];
            
            if (fgets(line, sizeof(line), fd) == NULL) {
                fprintf(stderr, "Error: Could not read line from %s\n", SERVER_LIST);
                exit(1);
            }
            
            sscanf(line, "server %255s %255s", server_name, temp);
            
            char *pos = strstr(temp, ":");
            
            if (pos == NULL) {
                fprintf(stderr, "Error: Could not find colon in line: %s\n", line);
                exit(1);
            }
            
            *pos = '\0';
            
            strcpy(server_addresses[i], temp);
            server_ports[i] = atoi(pos + 1);
        }
        
        fclose(fd);
        
        // STEP 3: Send a LIST request
        list_files(server_addresses, server_ports);
        
        /* 
        Outgoing packet structure:
            LIST\r\n

        Incoming packet structure:
            [200 OK] or [404 Not Found]\r\n
            Size: #\r\n
            \r\n
            Data ...\r\n
        */
    } else {
        printf("Unknown command: %s\n", command);
        exit(1);
    }

    return 0;
}


int connect_to_server(char *server_address, int server_port, int timeout_sec) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_address);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
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

int enough_servers_available(int *server_active) {
    // servers cant be adjacent to each other so we need to check if there are at least 2 servers available
    for (int i = 0; i < NUM_SERVERS; i++) {
        if (server_active[i] == -1) {
            if (server_active[(i - 1 + NUM_SERVERS) % NUM_SERVERS] == -1 || server_active[(i + 1) % NUM_SERVERS] == -1) {
                return 0;
            }
        }
    }
    return 1;
}

int download_chunks(char server_addresses[NUM_SERVERS][256], int server_ports[NUM_SERVERS], int chunks_downloaded[NUM_CHUNKS], char *filename) {

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char *) filename, strlen(filename), digest);
    int x = digest[0] % NUM_SERVERS;

    for (int chunk = 0; chunk < NUM_CHUNKS; chunk++) {
        if (chunks_downloaded[chunk] == 1) {
            continue;
        }

        int server1, server2;
        servers_to_send_chunk(&server1, &server2, x, chunk);

        int servers_to_download_from[2] = {server1, server2};
        int found = 0;

        for (int i = 0; i < 2; i++) {
            int server = servers_to_download_from[i];
            int sockfd = connect_to_server(server_addresses[server], server_ports[server], TIMEOUT_SEC);
            
            if (sockfd == -1) {
                continue;
            }

            int result = download_one_chunk(sockfd, filename, chunk);
            close(sockfd);

            if (result == 0) {
                chunks_downloaded[chunk] = 1;
                found = 1;
                printf("Chunk %d downloaded from DFS%d\n", chunk, server+1);
                break;
            } else {
                fprintf(stderr, "Error: Failed to download chunk %d from DFS%d\n", chunk, server+1);
            }
        }
        if (!found) {
            fprintf(stderr, "Error: Failed to download chunk %d from any server\n", chunk);
            return -1;
        }
    }
    return 0;
}

int download_one_chunk(int sockfd, char *filename, int request_chunk) {
    char buffer[MAX_MESSAGE_SIZE+1];
    printf("downloading chunk %d from %s\n", request_chunk, filename);
    if (send_packet(sockfd, "GET", filename, request_chunk, 0) == -1) {
        return -1;
    }

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_read = recv(sockfd, buffer, MAX_MESSAGE_SIZE, 0);
    if (bytes_read <= 0) {
        return -1;
    }

    buffer[bytes_read] = '\0';

    char method[16] = {0};
    char res_filename[256] = {0};
    int chunk = -1;
    int size = -1;

    if (parse_packet(buffer, bytes_read, method, res_filename, &chunk, &size) == -1) {
        return -1;
    }

    printf("Response: method: %s, filename: %s, chunk: %d, size: %d\n", method, res_filename, chunk, size);

    if (strcmp(method, "OK") != 0) {
        return -1;
    }

    if (chunk != request_chunk || size < 0) {
        return -1;
    }

    char *header_end = strstr(buffer, "\r\n\r\n");
    if (header_end == NULL) {
        return -1;
    }
    
    char temp_filename[256];
    snprintf(temp_filename, sizeof(temp_filename), "%s.%d", filename, request_chunk);
    
    FILE *temp_file = fopen(temp_filename, "wb");
    if (temp_file == NULL) {
        perror("fopen");
        return -1;
    }

    char *payload_start = header_end + 4;
    int header_bytes = (int)(payload_start - buffer);
    int payload_bytes = (int)bytes_read - header_bytes;

    int bytes_written = 0;

    if (payload_bytes > 0) {
        int to_write = payload_bytes > size ? size : payload_bytes;
        fwrite(payload_start, 1, to_write, temp_file);
        bytes_written += to_write;
    }

    while (bytes_written < size) {
        int remaining = size - bytes_written;
        int to_read = remaining < MAX_MESSAGE_SIZE ? remaining : MAX_MESSAGE_SIZE;
        
        bytes_read = recv(sockfd, buffer, to_read, 0);
        if (bytes_read <= 0) {
            break;
        }
        fwrite(buffer, 1, bytes_read, temp_file);
        bytes_written += bytes_read;
    }

    fclose(temp_file);

    if (bytes_written != size) {
        remove(temp_filename);
        return -1;
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

    *chunk = -1;
    *size = -1;

    char *save = NULL;
    char *line = strtok_r(buf, "\r\n", &save);
    while (line) {
        if (sscanf(line, "method: %s", method) == 1) {
            // parsed method
        } else if (sscanf(line, "filename: %s", filename) == 1) {
            // parsed filename
        } else if (sscanf(line, "chunk: %d", chunk) == 1) {
            // parsed chunk
        } else if (sscanf(line, "size: %d", size) == 1) {
            // parsed length
        }
        line = strtok_r(NULL, "\r\n", &save);
    }

    if (method[0] == '\0') {
        return -1;
    }

    // strcpy(method, method_buf);
    // strcpy(filename, filename_buf);
    return 0;
}

void servers_to_send_chunk(int *server1, int *server2, int x, int chunk_number) {
    *server1 = (x + chunk_number) % NUM_SERVERS;
    *server2 = (x + chunk_number + 1) % NUM_SERVERS;
}

int upload_chunk_to_server(char *server_address, int server_port, char *filename, int chunk_number, char *chunk_data, int current_chunk_size) {
    int sockfd = connect_to_server(server_address, server_port, TIMEOUT_SEC);
    if (sockfd == -1) {
        return -1;
    }

    send_packet(sockfd, "PUT", filename, chunk_number, current_chunk_size);

    int bytes_sent = 0;
    while (bytes_sent < current_chunk_size) {
        ssize_t sent = send(sockfd, chunk_data + bytes_sent, current_chunk_size - bytes_sent, 0);
        if (sent <= 0) {
            fprintf(stderr, "Error: Failed to send chunk %d to server %s:%d\n", chunk_number, server_address, server_port);
            close(sockfd);
            return -1;
        }
        bytes_sent += (int)sent;
    }
    close(sockfd);
    return 0;
}

int list_files(char server_addresses[NUM_SERVERS][256], int server_ports[NUM_SERVERS]) {
    FileEntry files[256];
    int file_count = 0;

    for (int server = 0; server < NUM_SERVERS; server++) {
        int sockfd = connect_to_server(server_addresses[server], server_ports[server], TIMEOUT_SEC);
        
        if (sockfd == -1) {
            fprintf(stderr, "Warning: Could not connect to DFS%d\n", server+1);
            continue;
        }

        if (send_packet(sockfd, "LIST", "none", 0, 0) == -1) {
            close(sockfd);
            continue;
        }
        
        char buffer[MAX_MESSAGE_SIZE+1];
        ssize_t bytes_read;

        while ((bytes_read = recv(sockfd, buffer, MAX_MESSAGE_SIZE, 0)) > 0) {
            buffer[bytes_read] = '\0';

            char *line = strtok(buffer, "\n");

            while (line != NULL) {
                char chunk_name[256];
                strncpy(chunk_name, line, sizeof(chunk_name) - 1);
                chunk_name[sizeof(chunk_name) - 1] = '\0';

                char *last_dot = strrchr(chunk_name, '.');

                if (last_dot != NULL) {
                    int chunk_num = atoi(last_dot + 1);
                    if (chunk_num >= 0 && chunk_num < NUM_CHUNKS) {
                        *last_dot = '\0';

                        int found = -1;
                        for (int i = 0; i < file_count; i++) {
                            if (strcmp(files[i].filename, chunk_name) == 0) {
                                found = i;
                                break;
                            }
                        }

                        if (found == -1) {
                            found = file_count;
                            strcpy(files[file_count].filename, chunk_name);

                            for (int j = 0; j < NUM_CHUNKS; j++) {
                                files[file_count].chunks[j] = 0;
                            }

                            file_count++;
                        }

                        files[found].chunks[chunk_num] = 1;
                    }
                }
                line = strtok(NULL, "\n");
            }
        }
        close(sockfd);
    }

    for (int i = 0; i < file_count; i++) {
        int complete = 1;

        for (int j = 0; j < NUM_CHUNKS; j++) {
            if (files[i].chunks[j] == 0) {
                complete = 0;
                break;
            }
        }

        if (complete) {
            printf("%s\n", files[i].filename);
        } else {
            printf("%s [incomplete]\n", files[i].filename);
        }
    }
    return 0;
}
