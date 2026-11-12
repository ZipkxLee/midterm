#define _POSIX_C_SOURCE 200809L
#include "netutil.h"

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int runtime_debug_enabled;

static void log_message(const char *level, const char *format, va_list arguments)
{
    time_t now = time(NULL);
    struct tm timestamp;
    char time_text[32];
    char line[1024];
    int prefix_length;
    int message_length;
    ssize_t written;
    size_t line_length;

    localtime_r(&now, &timestamp);
    strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &timestamp);
    prefix_length = snprintf(line, sizeof(line), "%s %-5s pid=%ld ",
                             time_text, level, (long)getpid());
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(line)) {
        return;
    }
    message_length = vsnprintf(line + prefix_length, sizeof(line) - (size_t)prefix_length,
                               format, arguments);
    if (message_length < 0) {
        return;
    }
    line_length = (size_t)prefix_length + (size_t)message_length;
    if (line_length >= sizeof(line) - 1) {
        line_length = sizeof(line) - 2;
    }
    line[line_length++] = '\n';
    written = write(STDERR_FILENO, line, line_length);
    if (written != (ssize_t)line_length) {
        return;
    }
}

static void log_with_level(const char *level, const char *format, va_list arguments)
{
    log_message(level, format, arguments);
}

void log_set_debug(int enabled)
{
    runtime_debug_enabled = enabled;
}

void log_info(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    log_with_level("INFO", format, arguments);
    va_end(arguments);
}

void log_warn(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    log_with_level("WARN", format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    log_with_level("ERROR", format, arguments);
    va_end(arguments);
}

void log_debug(const char *format, ...)
{
#if DEBUG_BUILD
    if (runtime_debug_enabled) {
        va_list arguments;
        va_start(arguments, format);
        log_with_level("DEBUG", format, arguments);
        va_end(arguments);
    }
#else
    (void)format;
#endif
}

ssize_t send_all(int socket_fd, const void *buffer, size_t length)
{
    const char *cursor = buffer;
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(socket_fd, cursor + sent, length - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            errno = EPIPE;
            return -1;
        }
        sent += (size_t)result;
    }
    return (ssize_t)sent;
}

int receive_line(int socket_fd, char *buffer, size_t capacity, int robust_mode)
{
    size_t used = 0;

    if (capacity < 2) {
        errno = EINVAL;
        return -1;
    }

    if (!robust_mode) {
        ssize_t result = recv(socket_fd, buffer, capacity - 1, 0);
        if (result <= 0) {
            return (int)result;
        }
        used = (size_t)result;
        buffer[used] = '\0';
        char *newline = strchr(buffer, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        return 1;
    }

    for (;;) {
        char character;
        ssize_t result = recv(socket_fd, &character, 1, 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (result == 0) {
            if (used == 0) {
                return 0;
            }
            buffer[used] = '\0';
            return -3;
        }
        if (character == '\n') {
            buffer[used] = '\0';
            return 1;
        }
        if (character == '\r') {
            continue;
        }
        if (used == capacity - 1) {
            buffer[used] = '\0';
            return -2;
        }
        buffer[used++] = character;
    }
}

int set_socket_timeout(int socket_fd, int seconds)
{
    struct timeval timeout = { .tv_sec = seconds, .tv_usec = 0 };
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return -1;
    }
    return setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

int parse_port(const char *text, unsigned short *port)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || text[0] == '\0' || port == NULL) {
        return -1;
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < 1 || parsed > 65535) {
        return -1;
    }
    *port = (unsigned short)parsed;
    return 0;
}