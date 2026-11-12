#define _POSIX_C_SOURCE 200809L
#include "netutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#define REQUEST_TIMEOUT_SECONDS 5
#define MAX_HOLD_SECONDS 30

static void reap_children(int signal_number)
{
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

static void send_error(int client_fd, const char *message)
{
    char response[MAX_RESPONSE];
    int length = snprintf(response, sizeof(response), "ERR %s\n", message);
    if (length > 0 && (size_t)length < sizeof(response)) {
        (void)send_all(client_fd, response, (size_t)length);
    }
}

static void handle_request(int client_fd, const char *request, int robust_mode)
{
    char response[MAX_RESPONSE];
    int length = -1;

    if (strcmp(request, "PING") == 0 || (!robust_mode && strncmp(request, "PING", 4) == 0)) {
        length = snprintf(response, sizeof(response), "OK PONG\n");
    } else if (strcmp(request, "INFO") == 0 || (!robust_mode && strncmp(request, "INFO", 4) == 0)) {
        struct utsname system_info;
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
        char hostname[256] = "unknown";

        (void)gethostname(hostname, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
        if (uname(&system_info) == 0) {
            length = snprintf(response, sizeof(response),
                              "OK host=%s os=%s release=%s arch=%s cpus=%ld\n",
                              hostname, system_info.sysname, system_info.release,
                              system_info.machine, cpu_count);
        }
    } else if (strncmp(request, "HOLD ", 5) == 0) {
        char extra;
        int seconds = 0;
        if (sscanf(request, "HOLD %d %c", &seconds, &extra) == 1 &&
            seconds >= 1 && seconds <= MAX_HOLD_SECONDS) {
            (void)sleep((unsigned int)seconds);
            length = snprintf(response, sizeof(response), "OK held %d seconds\n", seconds);
        } else {
            send_error(client_fd, "HOLD duration must be 1..30 seconds");
            return;
        }
    } else {
        send_error(client_fd, "invalid request; use INFO, PING, or HOLD seconds");
        return;
    }

    if (length < 0 || (size_t)length >= sizeof(response)) {
        send_error(client_fd, "response could not be created");
        return;
    }
    if (send_all(client_fd, response, (size_t)length) < 0) {
        log_warn("failed to send response: %s", strerror(errno));
    }
}

static void handle_client(int client_fd, int robust_mode)
{
    char request[MAX_REQUEST];
    int result;

    if (robust_mode && set_socket_timeout(client_fd, REQUEST_TIMEOUT_SECONDS) < 0) {
        log_warn("cannot set client timeout: %s", strerror(errno));
    }
    result = receive_line(client_fd, request, sizeof(request), robust_mode);
    if (result == -2) {
        send_error(client_fd, "request too long (maximum 255 bytes)");
        log_warn("rejected oversized request");
        return;
    }
    if (result == -3) {
        send_error(client_fd, "incomplete request; newline required");
        log_warn("rejected request without terminating newline");
        return;
    }
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            send_error(client_fd, "request timed out");
            log_warn("client request timed out");
        } else {
            log_warn("failed to read request: %s", strerror(errno));
        }
        return;
    }
    if (result == 0) {
        log_debug("client disconnected before sending a request");
        return;
    }

    log_debug("received request: %s", request);
    handle_request(client_fd, request, robust_mode);
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--verbose] [--legacy-demo] [address] [port]\n", program);
}

int main(int argc, char **argv)
{
    const char *address = "127.0.0.1";
    const char *port_text = "5050";
    int verbose = 0;
    int robust_mode = 1;
    int argument = 1;
    unsigned short port;
    struct sockaddr_in server_address;
    struct sigaction child_action;
    int server_fd;
    int reuse_address = 1;

    while (argument < argc && argv[argument][0] == '-') {
        if (strcmp(argv[argument], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[argument], "--legacy-demo") == 0) {
            robust_mode = 0;
        } else {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        argument++;
    }
    if (argc - argument > 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc - argument >= 1) {
        address = argv[argument++];
    }
    if (argc - argument == 1) {
        port_text = argv[argument];
    }
    if (parse_port(port_text, &port) < 0 || inet_pton(AF_INET, address, &server_address.sin_addr) != 1) {
        print_usage(argv[0]);
        fprintf(stderr, "Address must be IPv4 and port must be 1..65535.\n");
        return EXIT_FAILURE;
    }

    log_set_debug(verbose);
    (void)signal(SIGPIPE, SIG_IGN);
    memset(&child_action, 0, sizeof(child_action));
    child_action.sa_handler = reap_children;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &child_action, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &server_address.sin_addr) != 1 ||
        bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0 ||
        listen(server_fd, 64) < 0) {
        perror("bind/listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    log_info("listening on %s:%u; mode=%s; compile_debug=%d runtime_debug=%d",
             address, port, robust_mode ? "robust" : "legacy-demo",
             DEBUG_BUILD, verbose);
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("accept failed: %s", strerror(errno));
            continue;
        }

        pid_t child_pid = fork();
        if (child_pid < 0) {
            log_error("fork failed: %s", strerror(errno));
            send_error(client_fd, "server is temporarily unable to accept a worker");
            close(client_fd);
            continue;
        }
        if (child_pid == 0) {
            close(server_fd);
            log_debug("handling new client connection");
            handle_client(client_fd, robust_mode);
            close(client_fd);
            _exit(EXIT_SUCCESS);
        }
        log_debug("forked worker pid=%ld", (long)child_pid);
        close(client_fd);
    }
}