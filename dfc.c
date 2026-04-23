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

int connect_to_server(char *server_address, int server_port, int timeout_sec);
int send_packet(int sockfd, char *method, char *filename, int chunk, int size);
int enough_servers_available(int *server_active);
int download_chunks(int server_active[NUM_SERVERS], int chunks_downloaded[NUM_CHUNKS], char *filename);
int parse_packet(const char *packet, int packet_bytes, char *method, char *filename, int *chunk, int *size);
void servers_to_send_chunk(int *server1, int *server2, int x, int chunk_number);

int main(int argc, char *argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <command> [filename]\n", argv[0]);
        exit(1);
    }

    int server_active[NUM_SERVERS] = {0};
    int chunks_downloaded[NUM_CHUNKS] = {0};
    char server_name[256];
    char server_address[256];
    char server_port[256];
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
            memset(server_name, 0, sizeof(server_name));
            memset(server_port, 0, sizeof(server_port));

            char *line = NULL;

            fgets(line, sizeof(line), fd);
            char temp[256];
            sscanf(line, "server %s %s", server_name, temp);
            char *pos = strstr(temp, ":");
            *pos = '\0';
            strncpy(server_address, temp, sizeof(server_address) - 1);
            server_address[sizeof(server_address) - 1] = '\0';
            strncpy(server_port, pos + 1, sizeof(server_port) - 1);
            server_port[sizeof(server_port) - 1] = '\0';

            // STEP 2B: Attempt to connect to the server with a 1-second timeout.
            int sockfd = connect_to_server(server_address, atoi(server_port), TIMEOUT_SEC);
            server_active[i] = sockfd;
        }
        fclose(fd);

        // STEP 3: Compute x = HASH(filename) % 4 to determine chunk placement
        MD5((unsigned char*)filename, strlen(filename), digest);
        int hash = digest[0] % NUM_SERVERS;
        int x = hash % NUM_SERVERS;

        // STEP 4: If two neighboring servers are not available, unable to retrieve the file. exit with error message. [later we can change this to a tracking table]
        if (enough_servers_available(server_active)) {
            // STEP 3: For each available server, download the file chunks and store in temp. files. make sure to track which chunks are downloaded.
            download_chunks(server_active, chunks_downloaded, filename);
            // STEP 4: Check if all 4 chunks are downloaded. if not, print an error message and exit.
            for (int i = 0; i < 4; i++) {
                if (chunks_downloaded[i] == 0) {
                    fprintf(stderr, "Error: Chunk %d not downloaded\n", i);
                }
            }
            // STEP 5: Create output file from the chunks.
            char buffer[MAX_MESSAGE_SIZE];
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
            memset(server_port, 0, sizeof(server_port));
            memset(server_address, 0, sizeof(server_address));

            char line[256];

            if (fgets(line, sizeof(line), fd) == NULL) {
                fprintf(stderr, "Error: Could not read line from %s\n", SERVER_LIST);
                exit(1);
            }
            char temp[256];
            sscanf(line, "server %%255s %%255s", server_name, temp);
            char *pos = strstr(temp, ":");
            if (pos == NULL) {
                fprintf(stderr, "Error: Could not find colon in line: %s\n", line);
                exit(1);
            }
            *pos = '\0';
            strncpy(server_address, temp, sizeof(server_address) - 1);
            server_address[sizeof(server_address) - 1] = '\0';
            strncpy(server_port, pos + 1, sizeof(server_port) - 1);
            server_port[sizeof(server_port) - 1] = '\0';

            // STEP 2B: Attempt to connect to the server with a 1-second timeout.
            int sockfd = connect_to_server(server_address, atoi(server_port), TIMEOUT_SEC);
            server_active[i] = sockfd;
        }
        fclose(fd);

        // STEP 2: Check if files are incomplete: if two neighboring servers are not available, then the file is incomplete
        if (enough_servers_available(server_active)) {
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
            int chunk_size = ceil(file_size / NUM_CHUNKS);
            int chunk_number = 0;
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                fprintf(stderr, "Error: Could not open %s\n", filename);
                exit(1);
            }
            while (chunk_number < NUM_CHUNKS) {
                int server1, server2;
                // STEP 6: Upload each pair to DFS server based on modular arithmetic (x = HASH(filename) % y)
                servers_to_send_chunk(&server1, &server2, x, chunk_number);
                // STEP 7: Upload the chunk to the servers
                if (server_active[server1] != -1) {
                    send_packet(server_active[server1], "PUT", filename, chunk_number, chunk_size);
                    char buffer[MAX_MESSAGE_SIZE];
                    int bytes_sent = 0;
                    while (bytes_sent < chunk_size) {
                        ssize_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
                        if (bytes_read > 0) {
                            send(server_active[server1], buffer, bytes_read, 0);
                            bytes_sent += bytes_read;
                        } else {
                            break;
                        }
                    }
                } else {
                    fprintf(stderr, "Error: Server %s is not available\n", server_name);
                    exit(1);
                }
                // may have to send a done message to the server
                if (server_active[server2] != -1) {
                    send_packet(server_active[server2], "PUT", filename, chunk_number, chunk_size);
                    char buffer[MAX_MESSAGE_SIZE];
                    int bytes_sent = 0;
                    while (bytes_sent < chunk_size) {
                        ssize_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
                        if (bytes_read > 0) {
                            send(server_active[server2], buffer, bytes_read, 0);
                            bytes_sent += bytes_read;
                        } else {
                            break;
                        }
                    }
                } else {
                    fprintf(stderr, "Error: Server %s is not available\n", server_name);
                    exit(1);
                }
                chunk_number++;
            }
            fclose(file);
            printf("File %s uploaded successfully\n", filename);
        } else {
            // STEP 3: If the file is incomplete, print an error message and exit with error message.
            fprintf(stderr, "Error: Not enough servers are available to upload the file\n");
        }


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
        
        // STEP 1: Read dfc.conf file to get the list of DFS servers
        // STEP 2: Check if files are incomplete: if two neighboring servers are not available, then the file is incomplete
        // STEP 2: Connect to ONE DFS server (any file that has been previously stored is known to all servers)
        // STEP 3: Send a LIST request to the server
        // STEP 3: Retreive a list of files stored on the server
        // STEP 4: Check a vaild connection to the rest of the servers (if any server is not responding, then it is not available)

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

    memset(&server_addr, 0, sizeof(server_addr));

    if (connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
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
            if (server_active[(i - 1) % NUM_SERVERS] == -1 || server_active[(i + 1) % NUM_SERVERS] == -1) {
                return 0;
            }
        }
    }
    return 1;
}

int download_chunks(int server_active[NUM_SERVERS], int chunks_downloaded[NUM_CHUNKS], char *filename) {
    // download the file chunks and store in temp. files
    for (int i = 0; i < NUM_SERVERS; i++) {
        if (server_active[i] != -1) {
            int sockfd = server_active[i];
            // STEP 1: Send a GET request to the server
            send_packet(sockfd, "GET", filename, 0, 0);
            char buffer[MAX_MESSAGE_SIZE];
            ssize_t bytes_read = recv(sockfd, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                printf("Received message: %s\n", buffer);
            }
            // STEP 1B: parse the response to get the chunk number and size
            char method[16];
            char filename[256];
            int chunk;
            int size;
            parse_packet(buffer, bytes_read, method, filename, &chunk, &size);
            // check if chunk has been downloaded already
            if (chunks_downloaded[chunk] == 1) {
                continue;
            }
            // STEP 2: open a temp. file to store the file chunk
            char temp_filename[256];
            // name of file: filename.[chunk_number]
            snprintf(temp_filename, sizeof(temp_filename), "%s.%d", filename, chunk);
            FILE *temp_file = fopen(temp_filename, "wb");
            if (temp_file == NULL) {
                perror("fopen");
                continue;
            }
            // STEP 3: Receive the file chunks and store in temp. files
            memset(buffer, 0, sizeof(buffer));
            while (1) {
                bytes_read = recv(sockfd, buffer, sizeof(buffer), 0);
                if (bytes_read > 0) {
                    fwrite(buffer, 1, bytes_read, temp_file);
                } else {
                    break;
                }
            }
            fclose(temp_file);
            // STEP 4: Return the number of chunks downloaded
            chunks_downloaded[i] = 1;
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
    *server1 = (x + chunk_number + 2) % NUM_SERVERS;
    *server2 = (x + chunk_number - 1) % NUM_SERVERS;
}

