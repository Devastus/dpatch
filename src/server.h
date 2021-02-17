#ifndef DPATCH_SERVER_H
#define DPATCH_SERVER_H

#include <errno.h>
#include <unistd.h>
#include <wait.h>
#include <time.h>
#include "arena.h"
#include "net.h"
#define STORE_IMPL
#include "store.h"
#define STACK_IMPL
#include "stack.h"
#define INI_IMPL
#include "ini.h"
#define PROTOCOL_IMPL
#include "protocol.h"
#include "log.h"

#define FMT_SERVER(fmt, ...) "[server] " fmt, ##__VA_ARGS__
#define FMT_TARGET(target, fmt, ...) "[%s] " fmt, target, ##__VA_ARGS__

#define SERVER_RESPOND_FMT(server, config, packet, type, fmt, ...) {\
    char buf[config->settings.connection.buffer_size];\
    snprintf(buf, config->settings.connection.buffer_size, fmt, ##__VA_ARGS__);\
    server_respond(server, config, packet, type, buf);\
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
            Config* config,
            int client_sock,
            ProtocolMsgType type,
            char* buf)
{
    server->token_stream->type = type;
    protocol_tokenstream_reset(server->token_stream);
    protocol_tokenstream_add_token(server->token_stream, PROTOCOL_TOKEN_ARG, buf);
    return protocol_send(client_sock,
                         server->conn.out_buf,
                         config->settings.connection.buffer_size,
                         server->token_stream);
}

static int
server_respond(Server* server,
               Config* config,
               ClientPacket* packet,
               ProtocolMsgType msg_type,
               char* msg)
{
    int sent = server_send(server, config, packet->socket, msg_type, msg);
    close(packet->socket);
    stack_remove_at_fast(server->client_stack, packet->client);
    if (sent < 1) {
        LOG_WARN(FMT_SERVER("Failed to send response to socket '%d'", packet->socket));
        return -1;
    }
    return sent;
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
get_task(Server* server, Config* config, char* task_name, char** envs) {
    if (!task_name) return KEYVALUE_NONE;

    // Allocate new Task into task stack
    KeyValue res = store_push_empty(server->task_store);
    if (!res.value) {
        LOG_WARN(FMT_SERVER("Task store capacity reached"));
        return KEYVALUE_NONE;
    }
    Task* new_task = res.value;

    new_task->buf_loc = 0;
    new_task->buf     = (char*)new_task + sizeof(Task);
    new_task->vars    = (char**)(new_task->buf + config->settings.general.task_buf_size);
    new_task->name    = task_write(new_task, task_name, '\0');

    // Try parse Task data from active workspace INI file, remove from task stack if parse fails
    if (get_from_ws(server, workspace_task_get, new_task) != 0) {
        LOG_WARN(FMT_SERVER("Task '%s' does not exist", task_name));
        store_remove_at(server->task_store, res.key);
        return KEYVALUE_NONE;
    }

    // If task has no cmd, it cannot be executed
    if (!new_task->cmd) {
        LOG_WARN(FMT_SERVER("Task '%s' is invalid: missing 'cmd' value", task_name));
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
server_task_launch(Server* server, Config* config, Task* new_task) {
    if (!new_task) {
        return -1;
    }

    if (server->process_store->open_cnt < 1) {
        LOG_WARN(FMT_SERVER("Process store capacity reached"));
        return -1;
    }

    // Create pipes for child -> server communication
    int out_fd[2] = {0};
    if (pipe(out_fd) < 0 || socket_set_nonblock(out_fd[0]) != 0) {
        perror("Unable to create STDOUT pipe descriptors for child process");
        return -1;
    }

    int err_fd[2] = {0};
    if (pipe(err_fd) < 0 || socket_set_nonblock(err_fd[0]) != 0) {
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

        char* args[] = { config->settings.general.cmd_bin_path, "-c", new_task->cmd, NULL };
        if (new_task->dir != NULL) {
            if (chdir(new_task->dir) != 0) {
                perror("Failed to change working directory");
                exit(errno);
            }
        }

        if (execve(config->settings.general.cmd_bin_path, args, new_task->vars) != 0) {
            perror("Failed to execute command");
        }

        close(out_fd[1]);
        close(err_fd[1]);
        exit(errno);
    }
    // Parent
    else {
        LOG_DEBUG("Parent pid: %d, child pid: %d", getpid(), child_pid);

        // Push a new task process to store
        KeyValue res = store_push_empty(server->process_store);
        if (!res.value) {
            LOG_WARN(FMT_SERVER("Failed to push new process to the process store"));
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
server_check_task_queue(Server* server, Config* config, char* completed_task_name, int name_len) {
    if (server->process_store->open_cnt < 1) {
        return -1;
    }

    for (int i = server->task_store->capacity-1; i >= 0; i--) {
        KeyValue res = store_get(server->task_store, i);
        if (!res.value) continue;
        Task* task = res.value;

        LOG_DEBUG("Task name: %s, task wait: %s", task->name, task->wait);

        if (task->wait == NULL || strncmp(task->wait, completed_task_name, name_len) == 0) {
            int status = server_task_launch(server, config, task);
            if (status != 0) {
                LOG_WARN(FMT_SERVER("Failed to launch task '%s'", task->name));
                return -1;
            }
            else {
                LOG_INFO(FMT_SERVER("Launching task '%s'", task->name));
                store_remove_at(server->task_store, res.key);
            }
            return 0;
        }
    }

    return -1;
}

static int
server_eval_packet(Config* config, Server* server, ClientPacket* packet) {
    if (protocol_read(packet->data, config->settings.connection.buffer_size, server->token_stream) != 0) {
        server_respond(server, config, packet, PROTOCOL_MSG_ERR, "Invalid command");
        LOG_WARN(FMT_SERVER("Received an invalid message from client"));
        return -1;
    }

    unsigned char type = 0;
    char* args[server->token_stream->length];
    char* vars[server->token_stream->length];
    if (protocol_parse_token_stream(server->token_stream, &type, args, vars) != 0) {
        server_respond(server, config, packet, PROTOCOL_MSG_ERR, "Invalid command");
        LOG_WARN(FMT_SERVER("Failed to parse tokens from client message"));
        return -1;
    }

    switch (server->token_stream->type) {
        case PROTOCOL_MSG_TASK_RUN: {
            KeyValue res = get_task(server, config, args[0], vars);
            if (!res.value) {
                SERVER_RESPOND_FMT(server, config, packet, PROTOCOL_MSG_ERR, "Task '%s' not found", args[0]);
                LOG_WARN(FMT_SERVER("Failed to find requested task '%s'", args[0]));
                return -1;
            }

            Task* new_task = res.value;
            if (server_task_wait_match(server, new_task)) {
                SERVER_RESPOND_FMT(server, config, packet, PROTOCOL_MSG_SUCCESS, "Task '%s' put in queue", args[0]);
                LOG_INFO(FMT_SERVER("Queuing task '%s'", args[0]));
            }
            else {
                int launch_status = server_task_launch(server, config, new_task);

                if (launch_status != 0) {
                    SERVER_RESPOND_FMT(server, config, packet, PROTOCOL_MSG_ERR, "Failed to run task '%s'", args[0]);
                    LOG_WARN(FMT_SERVER("Failed to start task '%s'", args[0]));
                    return -1;
                }
                else {
                    SERVER_RESPOND_FMT(server, config, packet, PROTOCOL_MSG_SUCCESS, "Task '%s' started succesfully", args[0]);
                    LOG_INFO(FMT_SERVER("Starting task '%s'", args[0]));
                    store_remove_at(server->task_store, res.key);
                }
            }

            break;
        }

        case PROTOCOL_MSG_WORKSPACE_SET: {
            if (access(args[0], R_OK) != 0) {
                server_respond(server, config, packet, PROTOCOL_MSG_ERR, "Workspace not found");
                LOG_WARN(FMT_SERVER("Failed to set active workspace as '%s'", args[0]));
                return -1;
            }

            strcpy(server->workspace, args[0]);
            SERVER_RESPOND_FMT(server, config, packet, PROTOCOL_MSG_SUCCESS, "Workspace '%s' set as active", args[0]);
            LOG_INFO(FMT_SERVER("Using workspace '%s'", args[0]));
            break;
        }

        // TODO: Get task info
        /* case PROTOCOL_MSG_TASK_INFO: { */
            /* break; */
        /* } */

        // TODO: Get workspace info
        /* case PROTOCOL_MSG_WORKSPACE_INFO: { */
            /* break; */
        /* } */

        // TODO: Get process info
        /* case PROTOCOL_MSG_PROC_INFO: { */
            /* break; */
        /* } */

        default:
            server_respond(server, config, packet, PROTOCOL_MSG_ERR, "Invalid command");
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
server_check_activity(Server* server, Config* config) {
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
    struct timeval waitd = {config->settings.connection.select_timeout_sec,
                            config->settings.connection.select_timeout_usec}; // 15fps
    return select(max_sock_desc + 1, &server->conn.read_flags, NULL, NULL, &waitd);
}

static int
server_handle_incoming(Connection* conn, Config* config, Stack* client_stack) {
    if (FD_ISSET(conn->socket, &conn->read_flags)) {
        // Try accepting a new connection from main socket
        socklen_t addr_len = 0;
        int new_socket = accept(conn->socket, (struct sockaddr*)&conn->address, &addr_len);
        if (new_socket < 1) {
            perror("Error accepting new connection");
            return -1;
        }

        // If over the max client limit, instantly close the connection
        if (client_stack->count >= config->settings.connection.max_clients) {
            close(new_socket);
            return 0;
        }

        if (socket_set_nonblock(new_socket) != 0) {
            perror("Failed to set connection socket as non-blocking");
            return -1;
        }

        if (socket_set_timeout(new_socket, SO_RCVTIMEO, config->settings.connection.sock_timeout_sec) != 0 ||
            socket_set_timeout(new_socket, SO_SNDTIMEO, config->settings.connection.sock_timeout_sec) != 0)
        {
            perror("Failed to set socket timeout options");
            return -1;
        }

        stack_push(client_stack, &new_socket);
        LOG_DEBUG("New connection %d", new_socket);
        return 1;
    }
    return 0;
}

static void
server_process_print(Server* server, Config* config, TaskProcess* process) {
    memset(server->conn.in_buf, 0, config->settings.connection.buffer_size);
    if (FD_ISSET(process->out_fd_r, &server->conn.read_flags)) {
        int value_read = read(process->out_fd_r, server->conn.in_buf, config->settings.connection.buffer_size);
        if (value_read > 0) {
            LOG_INFO(FMT_TARGET(process->task_name, "%s", server->conn.in_buf));
            memset(server->conn.in_buf, 0, value_read);
        }
    }

    if (FD_ISSET(process->err_fd_r, &server->conn.read_flags)) {
        int value_read = read(process->err_fd_r, server->conn.in_buf, config->settings.connection.buffer_size);
        if (value_read > 0) {
            LOG_WARN(FMT_TARGET(process->task_name, "%s", server->conn.in_buf));
        }
    }
}

/*****************************************************
 * RUN LOOP
 ****************************************************/

int
run_as_server(Config* config) {
    Server server = {0};
    server.conn = (Connection){0};
    if (connection_init(config, &server.conn) < 1) {
        LOG_ERR(FMT_SERVER("Unable to initialize connection"));
        /* fprintf(stderr, "[FATAL] Unable to initialize connection\n"); */
        return -1;
    }

    server.workspace     = arena_alloc(sizeof(char) * config->settings.general.workspace_buf_size);
    server.token_stream  = protocol_tokenstream_alloc(config->settings.general.protocol_token_count);
    server.client_stack  = stack_new(config->settings.connection.max_clients, sizeof(int));
    server.process_store = store_new(config->settings.general.process_store_count,
                                     sizeof(TaskProcess) +
                                     sizeof(char) * config->settings.general.task_name_size);
    server.task_store    = store_new(config->settings.general.task_store_count,
                                     sizeof(Task) +
                                     (sizeof(char) * config->settings.general.task_buf_size) +
                                     (sizeof(char*) * config->settings.general.task_var_max_count));
    if (!server.workspace     ||
        !server.token_stream  ||
        !server.client_stack  ||
        !server.process_store ||
        !server.task_store)
    {
        LOG_ERR(FMT_SERVER("Failed to allocate server data"));
        return -1;
    }

    LOG_INFO(FMT_SERVER("dpatch server started at port %d", config->args.port));
    server.running = 1;
    while(server.running) {
        // Check for any activity in sockets
        connection_init_set(&server.conn, config);
        int activity = server_check_activity(&server, config);
        if ((activity < 0) && (errno != EINTR)) {
            LOG_ERR(FMT_SERVER("Unknown error during select()"));
        }

        // Check for incoming connections
        int incoming = server_handle_incoming(&server.conn, config, server.client_stack);
        if (incoming < 0) {
            LOG_WARN(FMT_SERVER("Error receiving an incoming connection"));
        }

        // Read from client sockets
        for (int i = server.client_stack->count-1; i >= 0; i--) {
            int sock_desc = *(int*)stack_get(server.client_stack, i);
            if (sock_desc == 0) {
                stack_remove_at_fast(server.client_stack, i);
                continue;
            }

            if (FD_ISSET(sock_desc, &server.conn.read_flags)) {
                int value_read = read(sock_desc, server.conn.in_buf, config->settings.connection.buffer_size);

                if (value_read > 0) {
                    ClientPacket packet = {
                        .client = i,
                        .socket = sock_desc,
                        .len    = value_read,
                        .data   = server.conn.in_buf,
                    };
                    server_eval_packet(config, &server, &packet);
                }
                // Connection closed
                else if (value_read == 0) {
                    LOG_DEBUG("Connection closed - socket %i", sock_desc);
                    stack_remove_at_fast(server.client_stack, i);
                    close(sock_desc);
                }
                // Error
                else {
                    LOG_WARN(FMT_SERVER("Error reading data from client socket '%d'", sock_desc));
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

            // If we have an exit status, log it and remove the process from store
            if (w_pid != 0) {
                LOG_DEBUG("Completed process name: %s, pid: %d", process->task_name, process->pid);

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
                    time_t t = time(0);
                    time_t diff = t - process->start_time;
                    char diff_buf[20];
                    strftime(diff_buf, 20, "%H:%M:%S", localtime(&diff));
                    LOG_INFO(FMT_SERVER("Task '%s' finished in %s with status code '%d'",
                                        name_buf,
                                        diff_buf,
                                        WEXITSTATUS(status)));
                }

                close(process->out_fd_r);
                close(process->err_fd_r);
                store_remove_at(server.process_store, i);
                server_check_task_queue(&server, config, name_buf, name_len);
            }
            // Print STDOUT & STDERR messages from running child process
            else {
                server_process_print(&server, config, process);
            }
        }

    }

    server_cleanup(&server);
    return 0;
}

#endif
