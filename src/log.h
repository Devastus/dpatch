#ifndef CUTIL_LOG_H
#define CUTIL_LOG_H

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 5
#endif

#define LOG_MSG_MAX         8196
#define LOG_PATH_MAX        512
#define LOG_DATE_MAX        32
#define LOG_TAG_MAX         32
#define LOG_OUT_MAX         (LOG_DATE_MAX + LOG_TAG_MAX + LOG_MSG_MAX)

#define LOG_CLR_NORMAL      "\x1B[0m"
#define LOG_CLR_RED         "\x1B[31m"
#define LOG_CLR_GREEN       "\x1B[32m"
#define LOG_CLR_YELLOW      "\x1B[33m"
#define LOG_CLR_BLUE        "\x1B[34m"
#define LOG_CLR_MAGENTA     "\x1B[35m"
#define LOG_CLR_CYAN        "\x1B[36m"
#define LOG_CLR_WHITE       "\x1B[37m"
#define LOG_CLR_RESET       "\033[0m"

#define LOG_DATE_FMT "%Y/%m/%d %H:%M:%S"

#if LOG_LEVEL >= 1
#define LOG_ERR(fmt, ...) log__print(LOG_CLR_RED, "ERROR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERR(fmt, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_WARN(fmt, ...) log__print(LOG_CLR_YELLOW, "WARN", fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)
#endif

#if LOG_LEVEL >= 3
#define LOG_INFO(fmt, ...) log__print(LOG_CLR_GREEN, "INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif

#if LOG_LEVEL >= 4
#define LOG_DEBUG(fmt, ...) log__print(LOG_CLR_CYAN, "DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#if LOG_LEVEL >= 5
#define LOG_TRACE(fmt, ...) log__print(LOG_CLR_WHITE, "TRACE", fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(fmt, ...)
#endif

FILE* __log_output_fd = NULL;

int
log_init(char* output_file) {
    if (output_file != NULL) {
        __log_output_fd = fopen(output_file, "w");
        if (!__log_output_fd) {
            perror("Unable to open log file in write mode");
            return -1;
        }
    }
    return 0;
}

void
log_close() {
    if (__log_output_fd != NULL) {
        fflush(__log_output_fd);
        fclose(__log_output_fd);
        __log_output_fd = NULL;
    }
}

void
log__print(char* color, char* tag, char* fmt, ...) {
    time_t t = time(0);
    char t_buf[LOG_DATE_MAX];
    strftime(t_buf, LOG_DATE_MAX, LOG_DATE_FMT, localtime(&t));

    char in_buf[LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(in_buf, LOG_MSG_MAX, fmt, args);
    va_end(args);

    char out_buf[LOG_OUT_MAX];
    snprintf(out_buf, LOG_OUT_MAX, "%s%s %s%s %s", color, t_buf, tag, LOG_CLR_NORMAL, in_buf);

    fprintf(stdout, "%s\n", out_buf);

    if (__log_output_fd != NULL) {
        fprintf(__log_output_fd, "%s\n", out_buf);
        fflush(__log_output_fd);
    }
}

#endif
