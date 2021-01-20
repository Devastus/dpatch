#ifndef DPATCH_SERVER_H
#define DPATCH_SERVER_H

#include <errno.h>
#include <unistd.h>
#include <wait.h>
#include "arena.h"
#define PROTOCOL_IMPL
#include "protocol.h"
#include "net.h"
#define STACK_IMPL
#include "stack.h"
#define INI_IMPL
#include "ini.h"

#define PACKET_STACK_COUNT 10
#define PROCESS_STACK_COUNT 4
#define TASK_STACK_COUNT 5
#define CMD_BINPATH "/bin/sh"
#define PROTOCOL_TOKEN_COUNT 30
#define TASK_BUF_SIZE 8192
#define TASK_VAR_COUNT 30

#ifdef SERVER_DEBUG
#define DEBUG_PRINT(fmt, ...) (printf(fmt, ##__VA_ARGS__))
#else
#define DEBUG_PRINT(fmt, ...)
#endif

typedef struct Task_st {
    int buf_loc;
    char* buf;
    char* name;
    char* cmd;
    char* dir;
    char* wait;
    Stack* vars;
} Task;

typedef struct TaskProcess_st {
    pid_t pid;
    char* task_name;
} TaskProcess;

typedef struct Server_st {
    unsigned char running;
    Task task;
    char* workspace;
    ProtocolTokenStream* token_stream;
    Stack* client_stack;
    Stack* process_stack;
    Stack* packet_stack;
    /* Stack* task_stack; */
    int* out_fd;
    int* err_fd;
} Server;

typedef struct ClientPacket_st {
    int socket;
    int len;
    char* data;
} ClientPacket;

/*****************************************************
 * TASKS & WORKSPACES
 ****************************************************/

static void workspace_task_list(void* ws, const char* section, const char* name, const char* value) {

}

static void workspace_task_get(void* data, char* section, char* name, char* value) {
    Task* task = (Task*)data;
    if (strcmp(section, task->name) == 0) {
        int len = strlen(name);
        memcpy(task->buf + task->buf_loc, name, len);
        task->buf_loc += len;
        char* ptr = task->buf + task->buf_loc;

#define WRITE(v) (len = strlen(v), memcpy(ptr, value, len), ptr[len] = '\0', task->buf_loc += len + 1, 0)

        if (strcmp(name, "cmd") == 0) {
            task->cmd = task->buf + task->buf_loc;
            WRITE(value);
        }
        else if (strcmp(name, "dir") == 0) {
            task->dir = task->buf + task->buf_loc;
            WRITE(value);
        }
        else if (strcmp(name, "wait") == 0) {
            task->wait = task->buf + task->buf_loc;
            WRITE(value);
        }
        else {
            // Add arbitrary variables
        }
    }
}

static inline int
get_from_ws(Server* server, IniHandler handler, void* data) {
    FILE* fp = fopen(server->workspace, "r");
    if (!fp) {
        return -1;
    }

    if (ini_parse(fp, handler, data) != 0) {
        return -1;
    }

    fclose(fp);
    return 0;
}

static int
server_task_launch(Server* server, char* task_name, char** envs) {
    memset(server->task.buf, 0, TASK_BUF_SIZE);
    stack_reset(server->task.vars);
    server->task.name = task_name;

    if (get_from_ws(server, workspace_task_get, &server->task) != 0) {
        fprintf(stderr, "Task '%s' does not exist\n", task_name);
        return -1;
    }

    if (server->task.dir != NULL) printf("task dir: %s\n", server->task.dir);

    pid_t child_pid = fork();

    // Error
    if (child_pid < 0) {
        perror("Unable to fork child process");
        return -1;
    }
    // Child process
    else if (child_pid == 0) {
        dup2(server->out_fd[1], STDOUT_FILENO);
        dup2(server->err_fd[1], STDERR_FILENO);

        char* args[] = { CMD_BINPATH, "-c", server->task.cmd, NULL };

        if (server->task.dir != NULL) chdir(server->task.dir);
        int status = execve(CMD_BINPATH, args, envs);
        exit(status);
    }
    // Parent
    else {
        DEBUG_PRINT("Parent pid: %d, child pid: %d\n", getpid(), child_pid);
        TaskProcess process = {
            child_pid,
            task_name,
        };
        stack_push(server->process_stack, &process);
    }
    return 0;
}

static int
server_workspace_get(Server* server) {
    return 0;
}

/*****************************************************
 * NETWORK & IO
 ****************************************************/

static inline int
server_send(Connection* conn,
            ConnectionOpts* opts,
            Server* server,
            int client_sock,
            ProtocolMsgType type,
            char* buf)
{
    server->token_stream->type = type;
    protocol_tokenstream_reset(server->token_stream, PROTOCOL_TOKEN_COUNT);
    protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, buf);
    return netmsg_send(client_sock, conn->out_buf, opts->buffer_size, server->token_stream);
}

static inline int
server_broadcast(Connection* conn,
                 ConnectionOpts* opts,
                 Server* server,
                 int ignore_sock,
                 ProtocolMsgType type,
                 char* buf)
{
    server->token_stream->type = type;
    protocol_tokenstream_reset(server->token_stream, PROTOCOL_TOKEN_COUNT);
    protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, buf);
    return netmsg_broadcast(server->client_stack->data,
            server->client_stack->count,
            ignore_sock,
            conn->out_buf,
            opts->buffer_size,
            server->token_stream);
}

static inline int
server_ack(Connection* conn,
           ConnectionOpts* opts,
           Server* server,
           int socket)
{
    return server_send(conn, opts, server, socket, PROTOCOL_MSG_ACK, "");
}

static int
server_eval_netmsg(Connection* conn, ConnectionOpts* opts, Server* server, ClientPacket* packet) {
    if (netmsg_read(packet->data, opts->buffer_size, server->token_stream) != 0) {
        fprintf(stderr, "Received an invalid message from client\n");
        return -1;
    }

    unsigned char type = 0;
    char* args[server->token_stream->length];
    char* vars[server->token_stream->length];
    if (protocol_parse_token_stream(server->token_stream, &type, args, vars) != 0) {
        fprintf(stderr, "Failed to parse tokens from message\n");
        return -1;
    }

    switch (server->token_stream->type) {
        case PROTOCOL_MSG_TASK_INVOKE: {
            if (server_ack(conn, opts, server, packet->socket) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }

            if (server_task_launch(server, args[0], vars) != 0) {
                fprintf(stderr, "Failed to launch task '%s'\n", args[0]);
                return -1;
            }

            char buf[256];
            snprintf(buf, 256, "Launching task '%s'\n", args[0]);
            printf("%s", buf);
            server_broadcast(conn,
                    opts,
                    server,
                    packet->socket,
                    PROTOCOL_MSG_PRINT_OUT,
                    buf);
            break;
        }

        case PROTOCOL_MSG_WORKSPACE_GET: {
            // TODO: Get workspace info
            /* server_acknowledge(conn, packet); */
            break;
        }

        case PROTOCOL_MSG_WORKSPACE_USE: {
            if (server_ack(conn, opts, server, packet->socket) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }

            if (access(args[0], R_OK) != 0) {
                return -1;
            }
            strcpy(server->workspace, args[0]);

            char buf[256];
            snprintf(buf, 256, "Using workspace '%s'\n", args[0]);
            printf("%s", buf);
            server_broadcast(conn,
                    opts,
                    server,
                    packet->socket,
                    PROTOCOL_MSG_PRINT_OUT,
                    buf);
            break;
        }

        default:
            if (server_ack(conn, opts, server, packet->socket) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }
            break;
    }
    return 0;
}

static int
server_check_activity(Connection* conn, ConnectionOpts* opts, Server* server) {
    int max_sock_desc = conn->socket;

    // Add valid client sockets to descriptor set
    for (int i = 0; i < opts->max_clients; i++) {
        int sock_desc = ((int*)server->client_stack->data)[i];
        if (sock_desc > 0) FD_SET(sock_desc, &conn->read_flags);
        if (sock_desc > max_sock_desc) max_sock_desc = sock_desc;
    }

    // Add pipe stdout read descriptor to descriptor set
    if (server->out_fd[0] > 0) FD_SET(server->out_fd[0], &conn->read_flags);
    if (server->out_fd[0] > max_sock_desc) max_sock_desc = server->out_fd[0];

    // Add pipe stderr read descriptor to descriptor set
    if (server->err_fd[0] > 0) FD_SET(server->err_fd[0], &conn->read_flags);
    if (server->err_fd[0] > max_sock_desc) max_sock_desc = server->err_fd[0];

    struct timeval waitd = {0, 33333}; // 30fps
    return select(max_sock_desc + 1, &conn->read_flags, NULL, NULL, &waitd);
}

static int
server_handle_incoming(Connection* conn, ConnectionOpts* opts, Stack* client_stack) {
    if (FD_ISSET(conn->socket, &conn->read_flags)) {
        // Try accepting a new connection from main socket
        socklen_t addr_len = 0;
        int new_socket = accept(conn->socket, (struct sockaddr*)&conn->address, &addr_len);
        if (new_socket < 0) {
            perror("Error accepting new connection");
            return -1;
        }

        if (client_stack->count >= opts->max_clients) {
            close(new_socket);
            return 0;
        }

        stack_push(client_stack, &new_socket);
        DEBUG_PRINT("New connection\n");
        return 1;
    }
    return 0;
}

static void
server_broadcast_fd(Connection* conn, ConnectionOpts* opts, Server* server, ProtocolMsgType type, int fd) {
    memset(conn->in_buf, 0, opts->buffer_size);
    if (FD_ISSET(fd, &conn->read_flags)) {
        int value_read = read(fd, conn->in_buf, opts->buffer_size);
        if (value_read > 0) {
            printf("%s", conn->in_buf);
            protocol_tokenstream_reset(server->token_stream, PROTOCOL_TOKEN_COUNT);
            protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, conn->in_buf);
            netmsg_broadcast(server->client_stack->data,
                             server->client_stack->count,
                             0,
                             conn->out_buf,
                             value_read,
                             server->token_stream);
        }
    }
}

int
daemonize() {
    return 0;
}

/*****************************************************
 * RUN LOOP
 ****************************************************/

int
run_as_server(ConnectionOpts* opts) {
    Connection conn = {0};
    if (connection_init(opts, &conn) < 1) {
        fprintf(stderr, "Unable to initialize connection\n");
        return -1;
    }

    // Create pipe descriptors for child process STDOUT writes
    int out_fd[2] = {0};
    if (pipe(out_fd) < 0) {
        perror("Unable to create STDOUT pipe descriptors for child processes");
        return -1;
    }

    // Create pipe descriptors for child process STDERR writes
    int err_fd[2] = {0};
    if (pipe(err_fd) < 0) {
        perror("Unable to create STDERR pipe descriptors for child processes");
        return -1;
    }

    Server server = {0};
    server.out_fd = out_fd;
    server.err_fd = err_fd;
    server.workspace = arena_alloc(sizeof(char) * 256);
    server.token_stream = protocol_tokenstream_alloc(PROTOCOL_TOKEN_COUNT);

    server.client_stack = stack_new(opts->max_clients, sizeof(int));
    server.process_stack = stack_new(PROCESS_STACK_COUNT, sizeof(TaskProcess));
    server.packet_stack = stack_new(PACKET_STACK_COUNT,
                                    (sizeof(int) * 2) + (sizeof(char) * opts->buffer_size));
    /* server.task_stack = stack_new(TASK_STACK_COUNT, */
    /*                               sizeof(Task) + */
    /*                               (sizeof(char) * TASK_BUF_SIZE) + */
    /*                               (TASK_VAR_COUNT * sizeof(char*))); */
    server.task.buf = arena_alloc(sizeof(char) * TASK_BUF_SIZE);
    server.task.vars = stack_new(TASK_VAR_COUNT, sizeof(char*));
    server.task.cmd = NULL;
    server.task.dir = NULL;

    server.running = TRUE;

    while(server.running) {
        // Check for any activity in sockets
        connection_init_set(&conn, opts);
        int activity = server_check_activity(&conn, opts, &server);
        if ((activity < 0) && (errno != EINTR)) {
            fprintf(stdout, "Error during select()\n");
        }

        // Check for incoming connections
        int incoming = server_handle_incoming(&conn, opts, server.client_stack);
        if (incoming < 0) {
            fprintf(stderr, "Error receiving an incoming connection\n");
        }

        // Read from client sockets
        for (int i = server.client_stack->count-1; i >= 0; i--) {
            int sock_desc = ((int*)server.client_stack->data)[i];

            if (FD_ISSET(sock_desc, &conn.read_flags)) {
                int value_read = read(sock_desc, conn.in_buf, opts->buffer_size);

                if (value_read > 0) {
                    ClientPacket packet = {
                        .socket = sock_desc,
                        .len = value_read,
                        .data = conn.in_buf,
                    };
                    stack_push(server.packet_stack, &packet);
                }
                else if (value_read == 0) {
                    DEBUG_PRINT("Connection closed - socket %i\n", sock_desc);
                    stack_remove_at(server.client_stack, i);
                    close(sock_desc);
                }
            }
        }

        // Process all network messages in stack
        ClientPacket* packet = (ClientPacket*)stack_pop(server.packet_stack);
        while (packet != NULL) {
            server_eval_netmsg(&conn, opts, &server, packet);
            packet = (ClientPacket*)stack_pop(server.packet_stack);
        }

        // Read and broadcast STDOUT & STDERR messages from child processes
        server_broadcast_fd(&conn, opts, &server, PROTOCOL_MSG_PRINT_OUT, server.out_fd[0]);
        server_broadcast_fd(&conn, opts, &server, PROTOCOL_MSG_PRINT_ERR, server.err_fd[0]);

        // Check running processes
        for (int i = server.process_stack->count-1; i >= 0; i--) {
            TaskProcess* process = (TaskProcess*)stack_get(server.process_stack, i);
            if (process == NULL) continue;

            int status = 0;
            pid_t w_pid = waitpid(process->pid, &status, WNOHANG | WUNTRACED);

            /* TODO: Implement task stack for queuing tasks.
             * Upon error or return, go through the task stack
             * and pop and launch tasks with matching name in 'wait'.
             */

            // Error
            if (w_pid < 0) {
                perror("Error waiting for task process");
                stack_remove_at(server.process_stack, i);
            }
            // PID returned with status
            else if (w_pid > 0) {
                char buf[256];
                snprintf(buf, 256, "Task '%d' returned with status code '%d'\n", w_pid, status);
                printf("%s", buf);
                server_broadcast(&conn, opts, &server, 0, PROTOCOL_MSG_TASK_COMPLETE, buf);
                stack_remove_at(server.process_stack, i);
            }
        }
    }

    close(server.out_fd[0]);
    close(server.out_fd[1]);
    return 0;
}

#endif
