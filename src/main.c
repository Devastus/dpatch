#include <errno.h>
#include <signal.h>
#define ARENA_ALLOCATOR_IMPL
#define ALLOC_FUNC arena_alloc
#include "arena.h"
#define CONFIG_IMPL
#include "config.h"
#include "log.h"
#include "server.h"
#include "client.h"

#ifndef ALLOC_PAGE_SIZE
#define ALLOC_PAGE_SIZE 65536
#endif

#ifndef ALLOC_PAGE_COUNT
#define ALLOC_PAGE_COUNT 2
#endif

void
finish(int sig) {
    log_close();
    arena_free();
    exit(sig);
}

void
pipe_out(int sig) {
    // NOTE: We probably need to do some actual handling instead, but this'll do for now
    fprintf(stderr, "Pipe broken, signal: %d\n", sig);
}

void
daemonize() {

}

int
main(int argc, char** argv, char** envp) {
    arena_init(ALLOC_PAGE_SIZE, ALLOC_PAGE_COUNT, 1);

    Config* config = config_init(argc, argv);
    log_init(config->args.log_file);

    if (config->args.help) {
        print_help();
        exit(1);
    }

    if (config->args.run_mode != RUNMODE_CMD && config->args.mode_detached) {
        daemonize();
    }

    int ret_code = 0;
    signal(SIGINT, finish);
    signal(SIGTERM, finish);
    signal(SIGPIPE, pipe_out);

    switch (config->args.run_mode) {
        case RUNMODE_SERVER:
            ret_code = run_as_server(config);
            break;
        case RUNMODE_CMD:
            ret_code = run_cmd(config, argv);
            break;
        default:
            print_help();
            ret_code = -1;
            break;
    }

    finish(ret_code);
}

