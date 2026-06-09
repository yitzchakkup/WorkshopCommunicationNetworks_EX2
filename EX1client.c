#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

void die(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// Function 1: Handles the warmup phase on an open socket
void run_warmup(int sock, uint32_t count, uint32_t size) {
    uint32_t header[2] = {htonl(count), htonl(size)};
    if (send(sock, header, sizeof(header), 0) < 0) {
        die("Warmup header send failed");
    }

    char *msg_buffer = (char*)malloc(size);
    if (!msg_buffer) die("malloc failed");
    memset(msg_buffer, 'a', size);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t sent = 0;
        while (sent < size) {
            ssize_t n = send(sock, msg_buffer + sent, size - sent, 0);
            if (n < 0) die("Warmup message send failed");
            sent += n;
        }
    }

    char ack_buffer[4] = {0};
    if (recv(sock, ack_buffer, sizeof(ack_buffer), 0) <= 0) {
        die("Warmup ACK receive failed");
    }

    free(msg_buffer);
}

// Function 2: Handles the benchmark phase and the timing
void run_benchmark(int sock, uint32_t count, uint32_t size) {
    uint32_t header[2] = {htonl(count), htonl(size)};
    if (send(sock, header, sizeof(header), 0) < 0) {
        die("Benchmark header send failed");
    }

    char *msg_buffer = (char*)malloc(size);
    if (!msg_buffer) die("malloc failed");
    memset(msg_buffer, 'a', size);

    // Start timer AFTER header is sent (optional, but removes header overhead)
    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) die("clock_gettime");

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t sent = 0;
        while (sent < size) {
            ssize_t n = send(sock, msg_buffer + sent, size - sent, 0);
            if (n < 0) die("Benchmark message send failed");
            sent += n;
        }
    }

    char ack_buffer[4] = {0};
    if (recv(sock, ack_buffer, sizeof(ack_buffer), 0) <= 0) {
        die("Benchmark ACK receive failed");
    }

    // Stop timer immediately after receiving ACK
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) die("clock_gettime");

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double total_megabytes = ((double)count * (double)size) / (1024.0 * 1024.0);
    double throughput_mbps = total_megabytes / elapsed;

    printf("%u\t%.2f\tMB/s\n", size, throughput_mbps);

    free(msg_buffer);
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Server IP> <Server Port>\n", argv[0]);
        return -1;
    }

    const char* server_ip = argv[1];
    int port = atoi(argv[2]);

    // 1. Open the connection ONCE
    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        die("Socket creation error");
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        die("Invalid address/ Address not supported");
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        die("Connection Failed");
    }

    // 2. Loop through all sizes using the same socket
    for (uint32_t size = 1; size <= 1024 * 1024; size *= 2) {

        // Warmup Phase
        const uint32_t warmup_cycles = 100;//100 as we checked a higher amount and results remained relitivly the same.
        run_warmup(sock, warmup_cycles, size);

        // Calculate dynamic batch size
        const long long total_data_per_test = 256LL * 1024 * 1024;
        // 256MB Dynamic Batch Size: Scaled dynamically to maintain a consistent 32MB data payload per test.
        // This ensures that smaller message sizes are sent frequently enough to average out microsecond-level timing
        // noise (system calls and context switching) without causing infinite, slow execution times.
        uint32_t batch_size = (uint32_t)(total_data_per_test / size);
        if (batch_size < 10) batch_size = 10;
        if (batch_size > 200000) batch_size = 200000;

        // Benchmark Phase
        run_benchmark(sock, batch_size, size);
    }

    // 3. Close connection at the very end
    close(sock);

    return 0;
}