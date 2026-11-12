#define _POSIX_C_SOURCE 200809L
#include "netutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CLIENT_TIMEOUT_SECONDS 8

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--verbose] [--raw] address port [request]\n", program);
    fprintf(stderr, "Examples: %s 127.0.0.1 5050 INFO | PING | HOLD 3\n", program);
}

int main(int argc, char **argv)
{
    int verbose = 0;
    int raw_request = 0;
    int argument = 1;
    const char *address;
    unsigned short port;
    struct sockaddr_in server_address;
    char request[MAX_REQUEST];
    char response[MAX_RESPONSE];
    size_t request_length;
    int socket_fd;
    int received;

    while (argument < argc && argv[argument][0] == '-') {
        if (strcmp(argv[argument], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[argument], "--raw") == 0) {
            raw_request = 1;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        argument++;
    }
    if (argc - argument < 2 || argc - argument > 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    address = argv[argument++];
    if (parse_port(argv[argument++], &port) < 0 || inet_pton(AF_INET, address, &server_address.sin_addr) != 1) {
        fprintf(stderr, "Address must be IPv4 and port must be 1..65535.\n");
        return EXIT_FAILURE;
    }

    if (argument == argc) {
        memcpy(request, "INFO", 5);
    } else {
        int length = snprintf(request, sizeof(request), "%s", argv[argument]);
        if (length < 0 || (size_t)length >= sizeof(request) - (raw_request ? 0u : 1u)) {
            fprintf(stderr, "Request is too long (maximum 255 bytes).\n");
            return EXIT_FAILURE;
        }
    }
    request_length = strlen(request);
    if (!raw_request) {
        request[request_length++] = '\n';
        request[request_length] = '\0';
    }

    log_set_debug(verbose);
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    if (set_socket_timeout(socket_fd, CLIENT_TIMEOUT_SECONDS) < 0) {
        perror("setsockopt timeout");
        close(socket_fd);
        return EXIT_FAILURE;
    }
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &server_address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        close(socket_fd);
        return EXIT_FAILURE;
    }
    if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("connect");
        close(socket_fd);
        return EXIT_FAILURE;
    }
    log_debug("connected to %s:%u", address, port);
    if (send_all(socket_fd, request, request_length) < 0) {
        perror("send");
        close(socket_fd);
        return EXIT_FAILURE;
    }
    received = receive_line(socket_fd, response, sizeof(response), 1);
    if (received <= 0) {
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf(stderr, "Server response timed out.\n");
        } else {
            perror("receive");
        }
        close(socket_fd);
        return EXIT_FAILURE;
    }
    puts(response);
    close(socket_fd);
    return strncmp(response, "ERR ", 4) == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}