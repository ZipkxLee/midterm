#ifndef NETUTIL_H
#define NETUTIL_H

#include <stddef.h>
#include <sys/types.h>

#define MAX_REQUEST 256
#define MAX_RESPONSE 2048

void log_set_debug(int enabled);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);
void log_debug(const char *format, ...);
ssize_t send_all(int socket_fd, const void *buffer, size_t length);
int receive_line(int socket_fd, char *buffer, size_t capacity, int robust_mode);
int set_socket_timeout(int socket_fd, int seconds);
int parse_port(const char *text, unsigned short *port);

#endif