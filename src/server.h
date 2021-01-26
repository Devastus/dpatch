#ifndef DPATCH_SERVER_H
#define DPATCH_SERVER_H

#include <errno.h>
#include <unistd.h>
#include <wait.h>
#include <time.h>
#include "arena.h"
#define PROTOCOL_IMPL
#include "protocol.h"
#include "net.h"
#define STORE_IMPL
#include "store.h"
#define STACK_IMPL
#include "stack.h"
#define INI_IMPL
#include "ini.h"

/* #define PACKET_STACK_COUNT 10 */
#define PROCESS_STACK_COUNT 5
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

#define CREATE_MSG_FMT(buf, size, time, target, msg) {\
    snprintf(buf, size, "(%s) [%s] %s", time, target, msg);\
}

#define CREATE_MSG_ARGS(buf, size, time, target, fmt, ...) {\
    int msg_size = size - 30;\
    char msg_buf[msg_size];\
    snprintf(msg_buf, msg_size, fmt, ##__VA_ARGS__);\
    CREATE_MSG_FMT(buf, size, t_buf, target, msg_buf);\
}

#define CREATE_MSG_AUTOTIMED(buf, size, target, fmt, ...) {\
    time_t t = time(0);\
    char t_buf[30];\
    strftime(t_buf, 30, "%d/%m/%Y %H:%M:%S", localtime(&t));\
    CREATE_MSG_ARGS(buf, size, t_buf, target, fmt, ##__VA_ARGS__);\
}

#define SERVER_PRINT_BROADCAST(server, ptype, buf, size) {\
    printf("%s", buf);\
    server_broadcast(server, 0, ptype, buf, size);\
}

typedef struct Task_st {
    int buf_loc;
    int var_count;
    char* cmd;
    char* dir;
    char* wait;
    char* name;
    char* buf;
    char** vars;
} Task;

typedef struct TaskProcess_st {
    time_t start_time;
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
    Store* process_store;
    Store* task_store;
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
    /* int sent = server_send(server, opts, packet->socket, PROTOCOL_MSG_ACK, ""); */
    close(packet->socket);
    stack_remove_at_fast(server->client_stack, packet->client);

    /* if (sent < 1) { */
    /*     fprintf(stderr, "Failed to send acknowledgement message back to client '%d'\n", packet->socket); */
    /*     return -1; */
    /* } */
    /* return sent; */
    return 1;
}

/*****************************************************
 * TASKS & WORKSPACES
 ****************************************************/

static char*
task_write(Task* task, char* value, char end_char) {
    int len = strlen(value);
    char* ptr = task->buf + task->buf_loc;
    memcpy(ptr, value, len);

    ptr[len] = end_char;
    task->buf_loc += len + 1;
    return ptr;
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
            task->vars[task->var_count] = task_write(task, name, '=');
            task->var_count++;
        }

        task_write(task, value, '\0');
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

static inline KeyValue
get_task(Server* server, char* task_name, char** envs) {
    if (!task_name) return KEYVALUE_NONE;

    // Allocate new Task into task stack
    KeyValue res = store_push_empty(server->task_store);
    if (!res.value) {
        fprintf(stderr, "Task store capacity reached\n");
        return KEYVALUE_NONE;
    }
    Task* new_task = res.value;

    new_task->buf_loc = 0;
    new_task->buf     = (char*)new_task + sizeof(Task);
    new_task->vars    = (char**)(new_task->buf + TASK_BUF_SIZE);
    new_task->name    = task_write(new_task, task_name, '\0');

    // Try parse Task data from active workspace INI file, remove from task stack if parse fails
    if (get_from_ws(server, workspace_task_get, new_task) != 0) {
        fprintf(stderr, "Task '%s' does not exist\n", task_name);
        store_remove_at(server->task_store, res.key);
        return KEYVALUE_NONE;
    }

    // If task has no cmd, it cannot be executed
    if (!new_task->cmd) {
        fprintf(stderr, "Task '%s' is invalid: missing 'cmd' value\n", task_name);
        store_remove_at(server->task_store, res.key);
        return KEYVALUE_NONE;
    }

    // Apply environment variables to existing Task variables
    if (envs != NULL) {
        int e = 0;
        char* env = envs[e];
        while (env != NULL) {
            new_task->vars[e + new_task->var_count] = task_write(new_task, env, '\0');
            e++;
            env = envs[e];
        }
    }

    return res;
}

static int
server_task_launch(Server* server, Task* new_task) {
    if (!new_task) {
        fprintf(stderr, "Task is NULL\n");
        return -1;
    };
    if (server->process_store->open_cnt < 1) {
        fprintf(stderr, "Process store capacity reached\n");
        return -1;
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
        if (new_task->dir != NULL) {
            if (chdir(new_task->dir) != 0) {
                perror("Failed to change working directory");
                exit(errno);
            }
        }

        if (execve(CMD_BINPATH, args, new_task->vars) != 0) {
            perror("Failed to execute command");
        }

        close(out_fd[1]);
        close(err_fd[1]);
        exit(errno);
    }
    // Parent
    else {
        DEBUG_PRINT("Parent pid: %d, child pid: %d\n", getpid(), child_pid);

        // Push a new task process to store
        KeyValue res = store_push_empty(server->process_store);
        if (!res.value) {
            fprintf(stderr, "Failed to push new process to the process store\n");
            return -1;
        }

        TaskProcess* process = res.value;
        process->start_time  = time(0);
        process->out_fd_r    = out_fd[0];
        process->err_fd_r    = err_fd[0];
        process->pid         = child_pid;
        process->task_name   = ((char*)process) + sizeof(TaskProcess);
        memcpy(process->task_name, new_task->name, strlen(new_task->name));
        return 0;
    }
}

static unsigned char
server_task_wait_match(Server* server, Task* task) {
    if (task->wait) {
        for (int i = server->process_store->capacity-1; i >= 0; i--) {
            KeyValue res = store_get(server->process_store, i);
            if (!res.value) continue;
            TaskProcess* process = res.value;

            if (strcmp(process->task_name, task->wait) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int
server_check_task_queue(Server* server, char* completed_task_name, int name_len) {
    if (server->process_store->open_cnt < 1) {
        return -1;
    }

    for (int i = server->task_store->capacity-1; i >= 0; i--) {
        KeyValue res = store_get(server->task_store, i);
        if (!res.value) continue;
        Task* task = res.value;

        DEBUG_PRINT("task name: %s, task wait: %s\n", task->name, task->wait);

        if (task->wait == NULL || strncmp(task->wait, completed_task_name, name_len) == 0) {
            int status = server_task_launch(server, task);
            if (status != 0) {
                fprintf(stderr, "Failed to launch task '%s'\n", task->name);
                return -1;
            }
            else {
                char buf[256];
                CREATE_MSG_AUTOTIMED(buf, 256, "server", "Launching task '%s'\n", task->name);
                SERVER_PRINT_BROADCAST(server, PROTOCOL_MSG_PRINT_OUT, buf, 256);
                store_remove_at(server->task_store, res.key);
            }
            return 0;
        }
    }

    return -1;
}

static int
server_eval_packet(ConnectionOpts* opts, Server* server, ClientPacket* packet) {
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

            KeyValue res = get_task(server, args[0], vars);
            if (!res.value) {
                return -1;
            }

            Task* new_task = res.value;
            char buf[256];
            if (server_task_wait_match(server, new_task)) {
                CREATE_MSG_AUTOTIMED(buf, 256, "server", "Queuing task '%s'\n", args[0]);
                SERVER_PRINT_BROADCAST(server, PROTOCOL_MSG_PRINT_OUT, buf, 256);
            }
            else {
                int launch_status = server_task_launch(server, new_task);

                if (launch_status != 0) {
                    fprintf(stderr, "Failed to launch task '%s'\n", args[0]);
                    return -1;
                }
                else {
                    CREATE_MSG_AUTOTIMED(buf, 256, "server", "Launching task '%s'\n", args[0]);
                    SERVER_PRINT_BROADCAST(server, PROTOCOL_MSG_PRINT_OUT, buf, 256);
                    store_remove_at(server->task_store, res.key);
                }
            }

            break;
        }

        // TODO: Get workspace info
        /* case PROTOCOL_MSG_WORKSPACE_GET: { */
            /* server_acknowledge(conn, packet); */
            /* break; */
        /* } */

        case PROTOCOL_MSG_WORKSPACE_USE: {
            if (server_ack_close(server, opts, packet) < 1) {
                fprintf(stderr, "Failed to send acknowlegde message to socket '%d'\n", packet->socket);
                return -1;
            }

            if (access(args[0], R_OK) != 0) {
                return -1;
            }

            strcpy(server->workspace, args[0]);
            char buf[256];
            CREATE_MSG_AUTOTIMED(buf, 256, "server", "Using workspace '%s'\n", args[0]);
            SERVER_PRINT_BROADCAST(server, PROTOCOL_MSG_PRINT_OUT, buf, 256);
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
    for (int i = server->process_store->capacity-1; i >= 0; i--) {
        KeyValue res = store_get(server->process_store, i);
        if (!res.value) continue;
        TaskProcess* process = res.value;

        set_sock_desc(process->out_fd_r, &max_sock_desc, &server->conn.read_flags);
        set_sock_desc(process->err_fd_r, &max_sock_desc, &server->conn.read_flags);
    }

    // Poll for file descriptor changes
    struct timeval waitd = {0, 66666}; // 15fps
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

        // If over the max client limit, instantly close the connection
        if (client_stack->count >= opts->max_clients) {
            close(new_socket);
            return 0;
        }

        stack_push(client_stack, &new_socket);
        DEBUG_PRINT("New connection %d\n", new_socket);
        return 1;
    }
    return 0;
}

static void
server_broadcast_fd(Server* server, ConnectionOpts* opts, char* process_name, ProtocolMsgType type, int fd) {
    memset(server->conn.in_buf, 0, opts->buffer_size);
    if (FD_ISSET(fd, &server->conn.read_flags)) {
        int value_read = read(fd, server->conn.in_buf, opts->buffer_size);
        if (value_read > 0) {
            char buf[256];
            CREATE_MSG_AUTOTIMED(buf, 256, process_name, "%s", server->conn.in_buf);
            SERVER_PRINT_BROADCAST(server, type, buf, 256);
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
    server.process_store = store_new(PROCESS_STACK_COUNT,
                                     sizeof(TaskProcess) +
                                     sizeof(char) * TASK_NAME_SIZE);
    server.task_store    = store_new(TASK_STACK_COUNT,
                                     sizeof(Task) +
                                     (sizeof(char) * TASK_BUF_SIZE) +
                                     (sizeof(char*) * TASK_VAR_COUNT));
    if (!server.workspace     ||
        !server.token_stream  ||
        !server.client_stack  ||
        !server.process_store ||
        !server.task_store)
    {
        fprintf(stderr, "Failed to allocate server data\n");
        return -1;
    }

    fprintf(stdout, "dpatch server started at port %d\n", opts->port);
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
                stack_remove_at_fast(server.client_stack, i);
                continue;
            }

            if (FD_ISSET(sock_desc, &server.conn.read_flags)) {
                int value_read = read(sock_desc, server.conn.in_buf, opts->buffer_size);

                if (value_read > 0) {
                    ClientPacket packet = {
                        .client = i,
                        .socket = sock_desc,
                        .len    = value_read,
                        .data   = server.conn.in_buf,
                    };
                    server_eval_packet(opts, &server, &packet);
                }
                // Connection closed
                else if (value_read == 0) {
                    DEBUG_PRINT("Connection closed - socket %i\n", sock_desc);
                    stack_remove_at_fast(server.client_stack, i);
                    close(sock_desc);
                }
                // Error
                else {
                    fprintf(stderr, "Error reading data from client socket '%d'", sock_desc);
                }
            }
        }

        // Check running task processes
        for (int i = server.process_store->capacity-1; i >= 0; i--) {
            KeyValue res = store_get(server.process_store, i);
            if (!res.value) continue;
            TaskProcess* process = res.value;

            // Check process status
            int status = 0;
            pid_t w_pid = waitpid(process->pid, &status, WNOHANG | WUNTRACED);

            if (w_pid != 0) {
                DEBUG_PRINT("Completed process name: %s, pid: %d\n", process->task_name, process->pid);

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
                    char t_buf[30];
                    time_t t = time(0);
                    strftime(t_buf, 30, "%d/%m/%Y %H:%M:%S", localtime(&t));

                    time_t diff = t - process->start_time;
                    char diff_buf[20];
                    strftime(diff_buf, 20, "%H:%M:%S", localtime(&diff));

                    char buf[256];
                    CREATE_MSG_ARGS(buf,
                                    256,
                                    t_buf,
                                    "server", "Task '%s' finished in %s with status code '%d'\n",
                                    name_buf,
                                    diff_buf,
                                    WEXITSTATUS(status));
                    SERVER_PRINT_BROADCAST(&server, PROTOCOL_MSG_TASK_COMPLETE, buf, 256);
                }

                close(process->out_fd_r);
                close(process->err_fd_r);
                store_remove_at(server.process_store, i);
                server_check_task_queue(&server, name_buf, name_len);
            }
            else {
                // Read and broadcast STDOUT & STDERR messages from child process
                server_broadcast_fd(&server, opts, process->task_name, PROTOCOL_MSG_PRINT_OUT, process->out_fd_r);
                server_broadcast_fd(&server, opts, process->task_name, PROTOCOL_MSG_PRINT_ERR, process->err_fd_r);
            }
        }

    }

    server_cleanup(&server);
    return 0;
}

#endif
