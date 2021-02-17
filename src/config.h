#ifndef DPATCH_CONFIG_H
#define DPATCH_CONFIG_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

#define DEFAULT_ADDRESS "localhost"
#define DEFAULT_PORT 9999

#define ARG_PORT "-p"
#define ARG_WATCH_PATH "-w"
#define ARG_WS_FILE "-f"
#define ARG_LOG_FILE "-l"
#define ARG_HELP "-h"
#define ARG_QUIET "-q"
#define ARG_DETACHED "-d"

typedef enum {
    RUNMODE_CMD,
    RUNMODE_SERVER
} RunMode;

typedef struct Args_st {
    char run_mode;
    char mode_detached;
    char help;
    char quiet;
    char* watch_path;
    char* ws_file;
    char* log_file;
    int port;
    int* arg_indices;
    int arg_count;
} Args;

typedef struct Settings_st {
    struct {
        int process_store_count;
        int task_store_count;
        int protocol_token_count;
        int workspace_buf_size;
        char* cmd_bin_path;
        int task_buf_size;
        int task_var_max_count;
        int task_name_size;
    } general;
    struct {
        int max_clients;
        int max_pending_conn;
        int client_timeout_ms;
        int sock_timeout_sec;
        int select_timeout_sec;
        int select_timeout_usec;
        int inotify_timeout_ms;
        int buffer_size;
    } connection;
} Settings;

typedef struct Config_st {
    Args args;
    Settings settings;
} Config;

void print_help();
void config_collect_args(Config* config, int argc, char** argv);
void config_default_settings(Config* config);
Config* config_init(int argc, char** argv);

#ifdef CONFIG_IMPL

void
print_help() {
    fprintf(stdout,
            "Usage:\n"
            "  dpatch [-pfld] \n\tRun as agent\n"
            "  dpatch [-pwq] <run|r> name [-e...]\n\tRun a task with given name through a dpatch agent\n"
            "  dpatch [-pwq] <set|s> path/to/file.ini\n\tSet active workspace to given file path in a dpatch agent\n"
            "  dpatch [-pwq] <task|t> <name>\n\tGet task info with given task name from a dpatch agent\n"
            "  dpatch [-pwq] <workspace|ws|w>\n\tGet active workspace info from a dpatch agent\n"
            "  dpatch [-pwq] <process|proc> <name>\n\tGet ongoing processes info with given task name from a dpatch agent\n"
            "Options:\n"
            "  -p PORT\t\tSet the port to serve/connect to (default: 9999)\n"
            "  -f /file/path\t\tSet a file to load as workspace in agent (default: none)\n"
            "  -l /file/path\t\tSet a file to write logs into (default: none)\n"
            "  -w /dir/path\t\tRun given command when changes are noticed in given directory (ie. watch)\n"
            "  -q \t\t\tQuiet mode (no logging to terminal)\n"
            "  -d \t\t\tRun as a separate detached process\n"
            "  -e KEY=VALUE\t\tSet an environment variable for a task\n"
            "  -h \t\t\tSee quick help");
}

void
config_collect_args(Config* config, int argc, char** argv) {
    config->args = (Args) {
        .run_mode = RUNMODE_SERVER,
        .mode_detached = 0,
        .help = 0,
        .quiet = 0,
        .watch_path = NULL,
        .ws_file = NULL,
        .log_file = NULL,
        .port = 9999,
        .arg_indices = (int*)MMALLOC(sizeof(int) * argc),
        .arg_count = 0,
    };

    if (!config->args.arg_indices) {
        fprintf(stderr, "Unable to allocate command array");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (arg == NULL) break;

        if(strncmp(arg, ARG_DETACHED, 2) == 0) {
            config->args.mode_detached = 1;
        }
        else if(strncmp(arg, ARG_QUIET, 2) == 0) {
            config->args.quiet = 1;
        }
        else if(strncmp(arg, ARG_PORT, 2) == 0) {
            config->args.port = atoi(argv[i+1]);
            i++;
        }
        else if(strncmp(arg, ARG_WATCH_PATH, 2) == 0) {
            config->args.watch_path = argv[i+1];
            i++;
        }
        else if(strncmp(arg, ARG_WS_FILE, 2) == 0) {
            config->args.ws_file = argv[i+1];
            i++;
        }
        else if(strncmp(arg, ARG_LOG_FILE, 2) == 0) {
            config->args.log_file = argv[i+1];
            i++;
        }
        else if (strncmp(arg, ARG_HELP, 2) == 0) {
            config->args.help = 1;
        }
        // Treat as command arguments
        else {
            config->args.arg_indices[config->args.arg_count] = i;
            config->args.arg_count++;
        }
    }

    if (config->args.arg_count > 0) {
        config->args.run_mode = RUNMODE_CMD;
    }
}

void
config_default_settings(Config* config) {
    config->settings = (Settings) {
        .general = {
            .process_store_count = 5,
            .task_store_count = 5,
            .protocol_token_count = 30,
            .workspace_buf_size = 256,
            .cmd_bin_path = "/bin/sh",
            .task_buf_size = 1024,
            .task_var_max_count = 30,
            .task_name_size = 256,
        },
        .connection = {
            .max_clients = 30,
            .max_pending_conn = 3,
            .client_timeout_ms = 5000,
            .sock_timeout_sec = 5,
            .select_timeout_sec = 0,
            .select_timeout_usec = 66666,  // 15fps
            .inotify_timeout_ms = 1000,
            .buffer_size = 1024,
        },
    };
}

Config*
config_init(int argc, char** argv) {
    Config* config = (Config*)MMALLOC(sizeof(Config));
    if (!config) {
        fprintf(stderr, "Unable to allocate configuration data");
        exit(EXIT_FAILURE);
    }

    memset (config, 0, sizeof(Config));
    config_default_settings(config);
    config_collect_args(config, argc, argv);
    return config;
}

#endif

#endif
