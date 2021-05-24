#ifndef DPATCH_CLIENT_H
#define DPATCH_CLIENT_H

#include <errno.h>
#include <sys/inotify.h>
#include "config.h"
#include "net.h"
#include "protocol.h"

#define CLIENT_PRINT(config, fd, fmt, ...) if (!config->args.quiet) fprintf(fd, fmt, ##__VA_ARGS__)
#define INOTIFY_EVENT_BUF_SIZE ((sizeof(struct inotify_event) + 16) * 1024)

static inline int
is_cmd(char* cmd, char** cmd_set, int cmd_set_count) {
    for (int i = 0; i < cmd_set_count; i++) {
        if(strcmp(cmd, cmd_set[i]) == 0) return 1;
    }
    return 0;
}

static int
client_eval_cmds(char** argv,
                 Config* config,
                 ProtocolTokenStream* msg)
{
    if (config->args.arg_count < 1) return -1;

    msg->type = -1;
    for (int i = 0; i < 2; i++) {
        char* cmd = argv[config->args.arg_indices[i]];
        if (is_cmd(cmd, (char*[]){"run", "r"}, 2)) {
            msg->type = PROTOCOL_MSG_TASK_RUN;
        }
        else if (is_cmd(cmd, (char*[]){"set", "s"}, 2)) {
            msg->type = PROTOCOL_MSG_WORKSPACE_SET;
        }
        else if (is_cmd(cmd, (char*[]){"workspace", "ws", "w"}, 3)) {
            msg->type = PROTOCOL_MSG_WORKSPACE_INFO;
            CLIENT_PRINT(config, stderr, "Workspace info command is not implemented yet\n");
            return -1;
        }
        else if (is_cmd(cmd, (char*[]){"task", "t"}, 2)) {
            msg->type = PROTOCOL_MSG_TASK_INFO;
            CLIENT_PRINT(config, stderr, "Task info command is not implemented yet\n");
            return -1;
        }
        else if (is_cmd(cmd, (char*[]){"process", "proc", "p"}, 3)) {
            msg->type = PROTOCOL_MSG_PROC_INFO;
            CLIENT_PRINT(config, stderr, "Process info command is not implemented yet\n");
            return -1;
        }
    }
    if (msg->type < 0) {
        CLIENT_PRINT(config, stderr, "Invalid command received\n");
        return -1;
    }

    msg->length = config->args.arg_count - 1;

    for (int i = 1; i < config->args.arg_count; i++) {
        char* cmd = argv[config->args.arg_indices[i]];
        if(cmd == NULL) break;

        ProtocolTokenType type = PROTOCOL_TOKEN_NONE;

        // Environment variable
        if (strcmp(cmd, "-e") == 0) {
            type = PROTOCOL_TOKEN_VAR;
            i++;
            cmd = argv[config->args.arg_indices[i]];
        }
        // Argument
        else {
            type = PROTOCOL_TOKEN_ARG;
        }

        protocol_tokenstream_add_token(msg, type, cmd);
    }

    return 0;
}

/*****************************************************
 * RUN LOOP
 ****************************************************/

static int
send_cmd(Config* config, Connection* conn, char** argv, ProtocolTokenStream* token_stream) {
    CLIENT_PRINT(config, stdout, "Sending command to dpatch server at port %d...\n", config->args.port);

    if (protocol_send(conn->socket, conn->out_buf, config->settings.connection.buffer_size, token_stream) < 1) {
        CLIENT_PRINT(config, stderr, "Unable to send network message.\n");
        return -1;
    }
    return 0;
}

static int
poll_response(Config* config, Connection* conn, ProtocolTokenStream* token_stream) {
    struct pollfd fd;
    fd.fd        = conn->socket;
    fd.events    = POLLIN;
    int poll_ret = poll(&fd, 1, config->settings.connection.client_timeout_ms);

    if (poll_ret > 0) {
        int value_read = read(conn->socket, conn->in_buf, config->settings.connection.buffer_size);
        if (value_read > 0) {
            if (protocol_read(conn->in_buf, config->settings.connection.buffer_size, token_stream) != 0) {
                CLIENT_PRINT(config, stderr, "Received an invalid message from server.\n");
            }
            else {
                FILE* fd;
                char* fmt;
                if (token_stream->type == PROTOCOL_MSG_ERR) {
                    fd = stderr;
                    fmt = "Error: %s\n";
                }
                else {
                    fd = stdout;
                    fmt = "Success: %s\n";
                }
                CLIENT_PRINT(config, fd, fmt, token_stream->tokens[0].value);
            }
        }
    }
    else {
        CLIENT_PRINT(config, stderr, "Connection timeout after %ims\n", config->settings.connection.client_timeout_ms);
        return -1;
    }
    return 0;
}

static int
poll_watch(Config* config, char** argv, ProtocolTokenStream* token_stream, int inotify_fd, int watch_fd) {
    char event_buf[INOTIFY_EVENT_BUF_SIZE];
    int running = 1;

    while (running) {
        struct pollfd fd;
        fd.fd        = inotify_fd;
        fd.events    = POLLIN;
        int poll_ret = poll(&fd, 1, config->settings.connection.inotify_timeout_ms);

        if (poll_ret > 0) {
            int value_read = read(inotify_fd, &event_buf, INOTIFY_EVENT_BUF_SIZE);
            if (value_read > 0) {
                /* TODO:
                 * Check event type and create or remove watch descriptors respectively if
                 * (IN_CREATE || IN_DELETE) && IN_ISDIR == true
                 */

                protocol_tokenstream_reset(token_stream);
                if (client_eval_cmds(argv, config, token_stream) < 0) return -1;

                Connection conn;
                if (connection_init(config, &conn) < 1) return -1;
                if (send_cmd(config, &conn, argv, token_stream) != 0) return -1;
                if (poll_response(config, &conn, token_stream) != 0) return -1;
                connection_close(&conn);
            }
        }
    }

    return 0;
}

int
run_cmd(Config* config, char** argv) {
    ProtocolTokenStream* token_stream = protocol_tokenstream_alloc(config->settings.general.protocol_token_count);
    if (!token_stream) {
        CLIENT_PRINT(config, stderr, "Unable to allocate protocol token stream\n");
        return -1;
    }

    // Start watching path for changes and send command to server when change occurs
    if (config->args.watch_path) {

        /* TODO:
         * Recursively go through directories inside given path and apply watch to all of them.
         */

        int inotify_fd = inotify_init();
        if (inotify_fd < 0) {
            perror("Error initializing inotify");
            return -1;
        }

        int wd = inotify_add_watch(inotify_fd,
                                   config->args.watch_path,
                                   IN_MODIFY | IN_CREATE | IN_DELETE);
        if (wd < 0) {
            perror("Error adding watch to given path");
            close(inotify_fd);
            return -1;
        }

        int ret = poll_watch(config, argv, token_stream, inotify_fd, wd);

        close(wd);
        close(inotify_fd);
        return ret;
    }
    // Send one-shot command to server
    else {
        if (client_eval_cmds(argv, config, token_stream) < 0) return -1;

        Connection conn;
        if (connection_init(config, &conn) < 1) return -1;
        if (send_cmd(config, &conn, argv, token_stream) != 0) return -1;
        if (poll_response(config, &conn, token_stream) != 0) return -1;
        connection_close(&conn);
    }
    return 0;
}

#endif
