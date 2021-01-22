#include <errno.h>
#include <signal.h>
#define ARENA_ALLOCATOR_IMPL
#define ALLOC_FUNC arena_alloc
#include "arena.h"
#include "server.h"
#include "client.h"

#define TRUE 1
#define DEFAULT_PORT 9999

#define ARG_SERVER "-s"
#define ARG_DETACHED "-d"
#define ARG_PORT "-p"
#define ARG_MONITOR "-m"

typedef enum {
    RUNMODE_CMD,
    RUNMODE_MONITOR,
    RUNMODE_SERVER
} RunMode;

typedef struct Args_st {
    char run_mode;
    char mode_detached;
    int port;
    int* cmd_indices;
    int cmd_count;
} Args;

void
collect_args(Args* args, int argc, char** argv) {
    if (!args) args = &(Args){0};

    args->cmd_indices = (int*)arena_alloc(sizeof(int) * argc);
    if (!args->cmd_indices) {
        fprintf(stderr, "Unable to allocate command array");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) break;

        // MODES
        if (strncmp(argv[i], ARG_SERVER, 2) == 0) {
            args->run_mode = RUNMODE_SERVER;
        }
        else if(strncmp(argv[i], ARG_MONITOR, 2) == 0) {
            args->run_mode = RUNMODE_MONITOR;
        }
        else if(strncmp(argv[i], ARG_DETACHED, 2) == 0) {
            args->mode_detached = 1;
        }
        else if(strncmp(argv[i], ARG_PORT, 2) == 0) {
            args->port = atoi(argv[i+1]);
            i++;
        }
        // Treat as command arguments
        else {
            if (i > 0) {
                args->cmd_indices[args->cmd_count] = i;
                args->cmd_count++;
            }
        }
    }
}

void
finish(int sig) {
    arena_free();
    exit(sig);
}

int
main(int argc, char** argv, char** envp) {
    arena_init(65536, 3, 1);

    Args args = {0};
    collect_args(&args, argc, argv);

    /* if (args.run_mode == RUNMODE_SERVER && args.mode_detached) { */
    /*     daemonize(); */
    /* } */

    ConnectionOpts opts;
    opts = (ConnectionOpts){
        .mode = args.run_mode == RUNMODE_SERVER ? CONNECTION_SERVER : CONNECTION_CLIENT,
        .port = args.port > 0 ? args.port : DEFAULT_PORT,
        .max_clients = 30,
        .max_pending_conn = 3,
        .client_timeout_ms = 5000,
        .buffer_size = 1024,
    };

    int ret_code = 0;
    signal(SIGINT, finish);
    signal(SIGTERM, finish);

    switch (args.run_mode) {
        case RUNMODE_SERVER:
            ret_code = run_as_server(&opts);
            break;
        case RUNMODE_CMD:
            ret_code = run_cmd(&opts, argv, args.cmd_indices, args.cmd_count);
            break;
        case RUNMODE_MONITOR:
            ret_code = run_as_monitor(&opts);
            break;
        default:
            ret_code = -1;
            break;
    }

    finish(ret_code);
}

