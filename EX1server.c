#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void die(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        die("socket");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        die("setsockopt");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        die("bind");
    }

    if (listen(server_fd, 5) < 0) {
        die("listen");
    }

    printf("Server listening on port %d...\n", port);

    // OUTTER LOOP: Waits for new clients to connect
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("\nClient connected.\n");

        // INNER LOOP: Stays connected and keeps reading batches until the client hangs up
        while (1) {
            uint32_t header[2];
            ssize_t bytes_read = 0;
            size_t header_size = sizeof(header);
            char *header_ptr = (char*)header;

            // 1. Try to read the header
            while (bytes_read < (ssize_t)header_size) {
                ssize_t r = recv(client_fd, header_ptr + bytes_read, header_size - bytes_read, 0);
                if (r <= 0) break;
                bytes_read += r;
            }

            // If we read exactly 0 bytes right at the start, the client intentionally closed the connection cleanly at the end of its loop.
            if (bytes_read == 0) {
                printf("Client finished benchmarking and disconnected.\n");
                break; // Break the inner loop, go close the socket
            }

            // If it failed halfway through a header
            if (bytes_read != (ssize_t)header_size) {
                fprintf(stderr, "Failed to read header or connection dropped unexpectedly.\n");
                break; // Break the inner loop, go close the socket
            }

            uint32_t num_messages = ntohl(header[0]);
            uint32_t message_size = ntohl(header[1]);

            char *buffer = (char*)malloc(message_size);
            if (!buffer) {
                fprintf(stderr, "Failed to allocate buffer\n");
                break;
            }

            // 2. Read the actual messages
            int error = 0;
            for (uint32_t i = 0; i < num_messages; ++i) {
                size_t msg_bytes_read = 0;
                while (msg_bytes_read < message_size) {
                    ssize_t r = recv(client_fd, buffer + msg_bytes_read, message_size - msg_bytes_read, 0);
                    if (r <= 0) { error = 1; break; }
                    msg_bytes_read += r;
                }
                if (error) break;
            }

            if (error) {
                fprintf(stderr, "Error receiving messages mid-batch.\n");
                free(buffer);
                break; // Break the inner loop
            }

            // 3. Send the ACK
            const char *ack_msg = "ACK";
            // Note: Sending exactly 4 bytes including the null terminator because your client does recv(..., 4, ...)
            ssize_t sent = send(client_fd, ack_msg, 4, 0);
            if (sent < 0) {
                fprintf(stderr, "Failed to send ACK.\n");
                free(buffer);
                break;
            }

            free(buffer);
            // The inner loop immediately repeats to read the next header from the same client!
        }

        // We only reach here when the inner loop breaks (client disconnected or error)
        close(client_fd);
        printf("Connection closed. Waiting for new client...\n");
    }

    close(server_fd);
    return 0;
}