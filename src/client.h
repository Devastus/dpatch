#ifndef DPATCH_CLIENT_H
#define DPATCH_CLIENT_H

#include <errno.h>
#include "protocol.h"
#include "net.h"

#define PROTOCOL_TOKEN_COUNT 30

static inline int
is_cmd(char* cmd, char** cmd_set, int cmd_set_count) {
    for (int i = 0; i < cmd_set_count; i++) {
        if(strcmp(cmd, cmd_set[i]) == 0) return 1;
    }
    return 0;
}

static inline int
client_check_activity(Connection* conn, ConnectionOpts* opts) {
    struct timeval waitd = {0, 66666}; // 15fps
    return select(conn->socket + 1, &conn->read_flags, NULL, NULL, &waitd);
}

static int
client_eval_packet(Connection* conn, ConnectionOpts* opts, ProtocolTokenStream* token_stream) {
    for (int i = 0; i < token_stream->length; i++) {
        ProtocolToken token = token_stream->tokens[i];
        switch(token_stream->type) {
            case PROTOCOL_MSG_PRINT_OUT:
            case PROTOCOL_MSG_TASK_COMPLETE:
                fprintf(stdout, "%s", token.value);
                break;
            case PROTOCOL_MSG_PRINT_ERR:
                fprintf(stderr, "%s", token.value);
                break;
            default:
                break;
        }
    }
    return 0;
}

static int
client_eval_cmds(char** argv,
                 int* cmd_indices,
                 int cmd_count,
                 ProtocolTokenStream* msg)
{
    if (cmd_count < 1) return -1;

    msg->type = -1;
    for (int i = 0; i < 2; i++) {
        char* cmd = argv[cmd_indices[i]];
        if (is_cmd(cmd, (char*[]){"task", "t"}, 2)) {
            msg->type = PROTOCOL_MSG_TASK_INVOKE;
        }
        else if (is_cmd(cmd, (char*[]){"workspace", "ws", "w"}, 3)) {
            msg->type = PROTOCOL_MSG_WORKSPACE_USE;
        }
        else if (is_cmd(cmd, (char*[]){"get", "g"}, 2)) {
            msg->type = cmd_count > 1 ?
                        PROTOCOL_MSG_TASK_GET :
                        PROTOCOL_MSG_WORKSPACE_GET;
            fprintf(stderr, "Get is not implemented yet\n");
            return -1;
        }
    }
    if (msg->type < 0) return -1;

    msg->length = cmd_count - 1;

    for (int i = 1; i < cmd_count; i++) {
        char* cmd = argv[cmd_indices[i]];
        if(cmd == NULL) break;

        ProtocolTokenType type = PROTOCOL_TOKEN_NONE;

        // Environment variable
        if (strcmp(cmd, "-e") == 0) {
            type = PROTOCOL_TOKEN_VAR;
            i++;
            cmd = argv[cmd_indices[i]];
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
 * RUN LOOP(S)
 ****************************************************/

int
run_cmd(ConnectionOpts* opts, char** argv, int* cmd_indices, int cmd_count) {
    ProtocolTokenStream* token_stream = protocol_tokenstream_alloc(PROTOCOL_TOKEN_COUNT);
    if (!token_stream) {
        fprintf(stderr, "Unable to allocate protocol token stream\n");
        return -1;
    }

    if (client_eval_cmds(argv, cmd_indices, cmd_count, token_stream) < 0) {
        fprintf(stderr, "Invalid command received.\n");
        return -1;
    }

    Connection conn;
    if (connection_init(opts, &conn) < 1) {
        return -1;
    }

    fprintf(stdout, "Sending command to dpatch server at port %d...\n", opts->port);

    if (netmsg_send(conn.socket, conn.out_buf, opts->buffer_size, token_stream) < 1) {
        fprintf(stderr, "Unable to send network message.\n");
        return -1;
    }

    // Expect response from server
    /* struct pollfd fd; */
    /* fd.fd        = conn.socket; */
    /* fd.events    = POLLIN; */
    /* int poll_ret = poll(&fd, 1, opts->client_timeout_ms); */

    /* if (poll_ret > 0) { */
    /*     int value_read = read(conn.socket, conn.in_buf, opts->buffer_size); */
    /*     if (value_read > 0) { */
    /*         if (netmsg_read(conn.in_buf, opts->buffer_size, token_stream) != 0) { */
    /*             fprintf(stderr, "Received an invalid message from server.\n"); */
    /*         } */
    /*         else { */
    /*             switch (token_stream->type) { */
    /*                 case PROTOCOL_MSG_PRINT_ERR: */
    /*                     fprintf(stderr, "Error sending command: %s\n", token_stream->tokens[0].value); */
    /*                     break; */
    /*                 default: */
    /*                     fprintf(stdout, "Command sent succesfully.\n"); */
    /*                     break; */
    /*             } */
    /*         } */
    /*     } */
    /* } */
    /* else { */
    /*     fprintf(stderr, "Connection timeout after %ims\n", opts->client_timeout_ms); */
    /* } */

    connection_close(&conn);
    return 0;
}

int
run_as_monitor(ConnectionOpts* opts) {
    ProtocolTokenStream* token_stream = protocol_tokenstream_alloc(10);
    if (!token_stream) {
        fprintf(stderr, "Unable to allocate protocol token stream\n");
        return -1;
    }

    Connection conn;
    if (connection_init(opts, &conn) < 1) {
        fprintf(stderr, "Unable to initialize connection\n");
        return -1;
    }

    fprintf(stdout, "dpatch monitor connected to server at port %d\n", opts->port);
    char running = 1;
    while (running > 0) {
        connection_init_set(&conn, opts);
        FD_SET(STDIN_FILENO, &conn.read_flags);
        client_check_activity(&conn, opts);

        // Read from socket
        if (FD_ISSET(conn.socket, &conn.read_flags)) {
            int value_read = read(conn.socket, conn.in_buf, opts->buffer_size);
            if (value_read > 0) {
                if (netmsg_read(conn.in_buf, value_read, token_stream) != 0) {
                    fprintf(stderr, "Received an invalid message from server\n");
                } else {
                    client_eval_packet(&conn, opts, token_stream);
                }
            }
            else if (value_read == 0) {
                fprintf(stdout, "Connection closed by the server\n");
                connection_close(&conn);
                break;
            }
        }
    }

    return 0;
}

#endif
