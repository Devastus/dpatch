#ifndef DPATCH_NET_H
#define DPATCH_NET_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <fcntl.h>
#include "config.h"
#include "arena.h"

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

typedef struct Connection_st {
    int socket;
    struct sockaddr_in address;
    fd_set read_flags;
    fd_set write_flags;
    char* in_buf;
    char* out_buf;
} Connection;

/***************************************************************
 * Connection & sockets
 **************************************************************/

int
socket_send(int socket, char* buf, int size) {
    int sent = 0;
    while (sent < size) {
        char* ptr = &buf[sent];
        int s = send(socket, ptr, size - sent, 0);
        if (s < 1) break;
        sent += s;
    }
    return sent;
}

int
socket_read(int socket, char* buf, int size) {
    int readc = 0;
    while (readc < size) {
        char* ptr = &buf[readc];
        int r = read(socket, ptr, size - readc);
        if (r < 1) break;
        readc += r;
    }
    return readc;
}

int
socket_set_nonblock(int sock) {
   int flags;
   flags = fcntl(sock, F_GETFL, 0);
   if (-1 == flags)
      return -1;
   return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int
socket_set_timeout(int sock, int timeout_flag, int timeout_secs) {
    struct timeval tv;
    tv.tv_sec = timeout_secs;
    tv.tv_usec = 0;
    return setsockopt(sock, SOL_SOCKET, timeout_flag, &tv, sizeof(struct timeval));
}

int
connection_init(Config* config, Connection* conn_ptr) {
    int opt = 1;
    *conn_ptr = (Connection){
        .socket = socket(AF_INET, SOCK_STREAM, 0),
        .address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons(config->args.port),
        },
        .in_buf = 0,
        .out_buf = 0,
    };

    if (conn_ptr->socket < 0) {
        perror("Unable to create socket");
        return conn_ptr->socket;
    }

    conn_ptr->in_buf = MMALLOC(sizeof(char) * config->settings.connection.buffer_size);
    if (!conn_ptr->in_buf) return -1;
    conn_ptr->out_buf = MMALLOC(sizeof(char) * config->settings.connection.buffer_size);
    if (!conn_ptr->in_buf) return -1;

    // Set socket to reuse address
    if (setsockopt(conn_ptr->socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Unable to set socket reuse address option");
        return -1;
    }

    // Set socket read & write timeouts
    if (socket_set_timeout(conn_ptr->socket, SO_RCVTIMEO, config->settings.connection.sock_timeout_sec) != 0 ||
        socket_set_timeout(conn_ptr->socket, SO_SNDTIMEO, config->settings.connection.sock_timeout_sec) != 0)
    {
        perror("Failed to set socket timeout options");
        return -1;
    }

    socklen_t addrlen = sizeof(conn_ptr->address);
    if (config->args.run_mode == RUNMODE_SERVER) {
        // Bind socket
        if (bind(conn_ptr->socket, (struct sockaddr*)&conn_ptr->address, addrlen) < 0) {
            perror("Unable to bind socket");
            return -1;
        }

        // Listen to socket
        if (listen(conn_ptr->socket, config->settings.connection.max_pending_conn) < 0) {
            perror("Unable to listen to socket");
            return -1;
        }

#ifdef NETWORK_DEBUG
        printf("Socket listening on port %i\n", config->args.port);
#endif
    }
    else {
        if (connect(conn_ptr->socket, (struct sockaddr*)&conn_ptr->address, addrlen) < 0) {
            perror("Unable to connect to server");
            return -1;
        }
#ifdef NETWORK_DEBUG
        printf("Socket connected to port %i\n", config->args.port);
#endif
    }

    return 1;
}

int
connection_close(Connection* conn) {
    if (conn->socket > 0) {
        close(conn->socket);
        conn->socket = 0;
    }
    return 0;
}

void
connection_init_set(Connection* conn, Config* config) {
    // Clear the descriptor sets
    FD_ZERO(&conn->read_flags);
    FD_ZERO(&conn->write_flags);

    // Add connection socket to descriptor read set
    FD_SET(conn->socket, &conn->read_flags);
    FD_SET(conn->socket, &conn->write_flags);

    memset(conn->in_buf, 0, config->settings.connection.buffer_size);
    memset(conn->out_buf, 0, config->settings.connection.buffer_size);
}

#endif
