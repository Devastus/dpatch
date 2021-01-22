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
#define TASK_NAME_SIZE 128

#ifdef SERVER_DEBUG
#define DEBUG_PRINT(fmt, ...) (printf(fmt, ##__VA_ARGS__))
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#define SERVER_PRINT_BROADCAST(server, fmt, ...) {\
    char buf[256];\
    snprintf(buf, 256, fmt, ##__VA_ARGS__);\
    fprintf(stdout, "%s", buf);\
    server_broadcast(server, 0, PROTOCOL_MSG_TASK_COMPLETE, buf, 256);\
}

typedef struct Task_st {
    int buf_loc;
    char* cmd;
    char* dir;
    char* wait;
    char* name;
    char* buf;
    char** vars;
} Task;

typedef struct TaskProcess_st {
    pid_t pid;
    char* task_name;
    int out_fd_r;
    int err_fd_r;
} TaskProcess;

typedef struct ClientPacket_st {
    int socket;
    int client;
    int len;
    char* data;
} ClientPacket;

typedef struct Server_st {
    unsigned char running;
    Connection conn;
    char* workspace;
    ProtocolTokenStream* token_stream;
    Stack* client_stack;
    Stack* process_stack;
    Stack* task_stack;
} Server;

/*****************************************************
 * NETWORK & IO
 ****************************************************/

static int
server_send(Server* server,
            ConnectionOpts* opts,
            int client_sock,
            ProtocolMsgType type,
            char* buf)
{
    server->token_stream->type = type;
    protocol_tokenstream_reset(server->token_stream, PROTOCOL_TOKEN_COUNT);
    protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, buf);
    return netmsg_send(client_sock, server->conn.out_buf, opts->buffer_size, server->token_stream);
}

static int
server_broadcast(Server* server,
                 int ignore_sock,
                 ProtocolMsgType type,
                 char* buf,
                 int buffer_size)
{
    server->token_stream->type = type;
    protocol_tokenstream_reset(server->token_stream, PROTOCOL_TOKEN_COUNT);
    protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, buf);
    return netmsg_broadcast(server->client_stack->data,
                            server->client_stack->count,
                            ignore_sock,
                            server->conn.out_buf,
                            buffer_size,
                            server->token_stream);
}

static int
server_ack_close(Server* server,
           ConnectionOpts* opts,
           ClientPacket* packet)
{
    int sent = server_send(server, opts, packet->socket, PROTOCOL_MSG_ACK, "");
    close(packet->socket);
    stack_remove_at(server->client_stack, packet->client);

    if (sent < 1) {
        fprintf(stderr, "Failed to send acknowledgement message back to client '%d'\n", packet->socket);
        return -1;
    }
    return sent;
}

/*****************************************************
 * TASKS & WORKSPACES
 ****************************************************/

static void
task_write(Task* task, char* value) {
    int len = strlen(value);
    memcpy(task->buf + task->buf_loc, value, len);
    (task->buf + task->buf_loc)[len] = '\0';
    task->buf_loc += len + 1;
}

static void
workspace_task_list(void* ws, const char* section, const char* name, const char* value) {

}

static void
workspace_task_get(void* data, char* section, char* name, char* value) {
    Task* task = (Task*)data;
    if (strcmp(section, task->name) == 0) {

        if (strcmp(name, "cmd") == 0) {
            task->cmd = task->buf + task->buf_loc;
        }
        else if (strcmp(name, "dir") == 0) {
            task->dir = task->buf + task->buf_loc;
        }
        else if (strcmp(name, "wait") == 0) {
            task->wait = task->buf + task->buf_loc;
        }
        else {
            // TODO: Add rest as arbitrary key-value variable pairs
        }

        task_write(task, value);
    }
}

static inline int
get_from_ws(Server* server, IniHandler handler, void* data) {
    FILE* fp = fopen(server->workspace, "r");
    if (!fp) {
        return -1;
    }

    int status = 0;
    if (ini_parse(fp, handler, data) != 0) {
        status = -1;
    }
    fclose(fp);

    return status;
}

static int
server_task_launch(Server* server, char* task_name, char** envs) {
    int name_len = strlen(task_name);
    Task* new_task = stack_push_new(server->task_stack);
    new_task->buf_loc = 0;
    new_task->buf = (char*)new_task + sizeof(Task);
    new_task->vars = (char**)(new_task->buf + TASK_BUF_SIZE);
    new_task->name = new_task->buf;
    task_write(new_task, task_name);

    // Try parse Task data from active workspace INI file
    if (get_from_ws(server, workspace_task_get, new_task) != 0) {
        fprintf(stderr, "Task '%s' does not exist\n", task_name);
        return -1;
    }

    // If task is set to wait an existing process, simply return to queue it instead
    if (new_task->wait) {
        for (int i = server->process_stack->count-1; i >= 0; i--) {
            TaskProcess* process = (TaskProcess*)stack_get(server->process_stack, i);
            if (process && strcmp(process->task_name, new_task->wait) == 0) {
                return 1;
            }
        }
    }

    // Create pipes for child -> server communication
    int out_fd[2] = {0};
    int err_fd[2] = {0};
    if (pipe(out_fd) < 0) {
        perror("Unable to create STDOUT pipe descriptors for child process");
        return -1;
    }
    if (pipe(err_fd) < 0) {
        perror("Unable to create STDERR pipe descriptors for child process");
        return -1;
    }

    // Fork process
    pid_t child_pid = fork();

    // Error
    if (child_pid < 0) {
        perror("Unable to fork child process");
        return -1;
    }
    // Child process
    else if (child_pid == 0) {
        // Set child process STDOUT & STDERR into pipes
        if (dup2(out_fd[1], STDOUT_FILENO) < 0) { perror("Failed to redirect STDOUT to pipe"); exit(errno); }
        if (dup2(err_fd[1], STDERR_FILENO) < 0) { perror("Failed to redirect STDERR to pipe"); exit(errno); }

        char* args[] = { CMD_BINPATH, "-c", new_task->cmd, NULL };
        if (new_task->dir != NULL) chdir(new_task->dir);

        if (execve(CMD_BINPATH, args, envs) != 0) {
            perror("Failed to execute command");
        }

        close(out_fd[1]);
        close(err_fd[1]);
        exit(errno);
    }
    // Parent
    else {
        DEBUG_PRINT("Parent pid: %d, child pid: %d\n", getpid(), child_pid);

        // Push a new task process to stack
        TaskProcess* process = stack_push_new(server->process_stack);
        process->out_fd_r = out_fd[0];
        process->err_fd_r = err_fd[0];
        process->pid = child_pid;
        process->task_name = ((char*)process) + sizeof(TaskProcess);
        memcpy(process->task_name, task_name, name_len);

        // Remove launched task data from stack
        stack_pop(server->task_stack);
        return 0;
    }
}

static int
server_workspace_get(Server* server) {
    return 0;
}

static int
server_check_task_queue(Server* server, char* completed_task_name, int name_len) {
    for (int i = server->task_stack->count-1; i >= 0; i--) {
        Task* t = (Task*)stack_get(server->task_stack, i);

        if (t->wait != NULL && strncmp(t->wait, completed_task_name, name_len) == 0) {
            server_task_launch(server, t->name, t->vars);
            stack_remove_at(server->task_stack, i);
            return 0;
        }
    }
    return -1;
}

static int
server_eval_netmsg(ConnectionOpts* opts, Server* server, ClientPacket* packet) {
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
            if (server_ack_close(server, opts, packet) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }

            int launch_status = server_task_launch(server, args[0], vars);
            if (launch_status < 0) {
                fprintf(stderr, "Failed to launch task '%s'\n", args[0]);
                return -1;
            }
            else if (launch_status > 0) {
                SERVER_PRINT_BROADCAST(server, "Queuing task '%s'\n", args[0]);
            }
            else {
                SERVER_PRINT_BROADCAST(server, "Launching task '%s'\n", args[0]);
            }

            break;
        }

        case PROTOCOL_MSG_WORKSPACE_GET: {
            // TODO: Get workspace info
            /* server_acknowledge(conn, packet); */
            break;
        }

        case PROTOCOL_MSG_WORKSPACE_USE: {
            if (server_ack_close(server, opts, packet) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }

            if (access(args[0], R_OK) != 0) {
                return -1;
            }

            strcpy(server->workspace, args[0]);
            SERVER_PRINT_BROADCAST(server, "Using workspace '%s'\n", args[0]);
            break;
        }

        default:
            if (server_ack_close(server, opts, packet) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }
            break;
    }
    return 0;
}

static inline void
set_sock_desc(int sock_desc, int* max_sock_desc, fd_set* read_flags) {
    if (sock_desc > 0) FD_SET(sock_desc, read_flags);
    if (sock_desc > *max_sock_desc) *max_sock_desc = sock_desc;
}

static inline void
server_cleanup(Server* server) {
    for (int i = server->client_stack->count-1; i >= 0; i--) {
        int sock = *(int*)stack_pop(server->client_stack);
        close(sock);
    }
}

static int
server_check_activity(Server* server, ConnectionOpts* opts) {
    int max_sock_desc = server->conn.socket;

    // Add client sockets to descriptor set
    for (int i = server->client_stack->count-1; i >= 0; i--) {
        int sock = *(int*)stack_get(server->client_stack, i);
        set_sock_desc(sock, &max_sock_desc, &server->conn.read_flags);
    }

    // Add process pipes to descriptor set
    for (int i = server->process_stack->count-1; i >= 0; i--) {
        TaskProcess* process = (TaskProcess*)stack_get(server->process_stack, i);
        if (!process) continue;

        set_sock_desc(process->out_fd_r, &max_sock_desc, &server->conn.read_flags);
        set_sock_desc(process->err_fd_r, &max_sock_desc, &server->conn.read_flags);
    }

    // Poll for file descriptor changes
    struct timeval waitd = {0, 33333}; // 30fps
    return select(max_sock_desc + 1, &server->conn.read_flags, NULL, NULL, &waitd);
}

static int
server_handle_incoming(Connection* conn, ConnectionOpts* opts, Stack* client_stack) {
    if (FD_ISSET(conn->socket, &conn->read_flags)) {
        // Try accepting a new connection from main socket
        socklen_t addr_len = 0;
        int new_socket = accept(conn->socket, (struct sockaddr*)&conn->address, &addr_len);
        if (new_socket < 1) {
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
server_broadcast_fd(Server* server, ConnectionOpts* opts, ProtocolMsgType type, int fd) {
    memset(server->conn.in_buf, 0, opts->buffer_size);
    if (FD_ISSET(fd, &server->conn.read_flags)) {
        int value_read = read(fd, server->conn.in_buf, opts->buffer_size);
        if (value_read > 0) {
            printf("%s", server->conn.in_buf);
            server_broadcast(server, 0, type, server->conn.in_buf, opts->buffer_size);
        }
    }
}

/*****************************************************
 * RUN LOOP
 ****************************************************/

int
run_as_server(ConnectionOpts* opts) {

    // Initialize server
    Server server = {0};
    server.conn = (Connection){0};
    if (connection_init(opts, &server.conn) < 1) {
        fprintf(stderr, "Unable to initialize connection\n");
        return -1;
    }

    server.workspace     = arena_alloc(sizeof(char) * 256);
    server.token_stream  = protocol_tokenstream_alloc(PROTOCOL_TOKEN_COUNT);

    server.client_stack  = stack_new(opts->max_clients, sizeof(int));
    server.process_stack = stack_new(PROCESS_STACK_COUNT, sizeof(TaskProcess) +
                                     sizeof(char) * TASK_NAME_SIZE);
    server.task_stack    = stack_new(TASK_STACK_COUNT,
                                     sizeof(Task) +
                                     (sizeof(char) * TASK_BUF_SIZE) +
                                     (TASK_VAR_COUNT * sizeof(char*)));

    server.running = TRUE;
    while(server.running) {
        // Check for any activity in sockets
        connection_init_set(&server.conn, opts);
        int activity = server_check_activity(&server, opts);
        if ((activity < 0) && (errno != EINTR)) {
            fprintf(stdout, "Error during select()\n");
        }

        // Check for incoming connections
        int incoming = server_handle_incoming(&server.conn, opts, server.client_stack);
        if (incoming < 0) {
            fprintf(stderr, "Error receiving an incoming connection\n");
        }

        // Read from client sockets
        for (int i = server.client_stack->count-1; i >= 0; i--) {
            int sock_desc = *(int*)stack_get(server.client_stack, i);
            if (sock_desc == 0) {
                stack_remove_at(server.client_stack, i);
                continue;
            }

            if (FD_ISSET(sock_desc, &server.conn.read_flags)) {
                int value_read = read(sock_desc, server.conn.in_buf, opts->buffer_size);

                if (value_read > 0) {
                    ClientPacket packet = {
                        .client = i,
                        .socket = sock_desc,
                        .len = value_read,
                        .data = server.conn.in_buf,
                    };
                    server_eval_netmsg(opts, &server, &packet);
                }
                // Connection closed
                else if (value_read == 0) {
                    DEBUG_PRINT("Connection closed - socket %i\n", sock_desc);
                    stack_remove_at(server.client_stack, i);
                    close(sock_desc);
                }
                // Error
                else {
                    fprintf(stderr, "Error reading data from client socket '%d'", sock_desc);
                }
            }
        }

        // Check running task processes
        for (int i = server.process_stack->count-1; i >= 0; i--) {
            TaskProcess* process = (TaskProcess*)stack_get(server.process_stack, i);
            if (process == NULL) continue;

            // Check process status
            int status = 0;
            pid_t w_pid = waitpid(process->pid, &status, WNOHANG | WUNTRACED);

            if (w_pid != 0) {
                int name_len = strlen(process->task_name);
                char name_buf[name_len+1];
                memset(name_buf, 0, name_len+1);
                memcpy(name_buf, process->task_name, name_len);

                // Error
                if (w_pid < 0) {
                    perror("Error waiting for task process");
                }
                // PID returned with status
                else if (w_pid > 0) {
                    SERVER_PRINT_BROADCAST(&server, "Task '%s' finished with status code '%d'\n",
                            name_buf,
                            status);
                }

                close(process->out_fd_r);
                close(process->err_fd_r);
                stack_remove_at(server.process_stack, i);
                server_check_task_queue(&server, name_buf, name_len);
            }
            else {
                // Read and broadcast STDOUT & STDERR messages from child process
                server_broadcast_fd(&server, opts, PROTOCOL_MSG_PRINT_OUT, process->out_fd_r);
                server_broadcast_fd(&server, opts, PROTOCOL_MSG_PRINT_ERR, process->err_fd_r);
            }
        }

    }

    server_cleanup(&server);
    return 0;
}

#endif
