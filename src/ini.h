#ifndef DPATCH_INI_H
#define DPATCH_INI_H

#include <stdio.h>

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

#ifndef INI_BUF_SIZE
#define INI_BUF_SIZE 2048
#endif
#define INI_SECTION_BUF_SIZE (INI_BUF_SIZE / 8)
#define INI_KEY_BUF_SIZE (INI_BUF_SIZE / 8)
#define INI_VALUE_BUF_SIZE (INI_BUF_SIZE / 2)
#define INI_LINE_BUF_SIZE (INI_BUF_SIZE / 4)

typedef enum {
    INI_OK = 0,
    INI_NULL,
    INI_INVALID,
} IniRetCode;

typedef void(*IniHandler)(void*, char*, char*, char*);
typedef void(*IniSectionHandler)(void*, char*);

IniRetCode ini_parse(FILE* fp, IniHandler handler, void* data);
/* IniRetCode ini_parse_section(FILE* fp, char* section, IniSectionHandler handler, void* data); */

#ifdef INI_IMPL

typedef enum {
    PARSEMODE_STARTLINE,
    PARSEMODE_SECTION,
    PARSEMODE_KEY,
    PARSEMODE_VALUE,
    PARSEMODE_MULTILINE,
    PARSEMODE_HANDLE,
} IniParseMode;

typedef struct {
    char* line;
    char* section;
    char* key;
    char* value;
    int value_loc;
    int save_loc;
    IniParseMode mode;
} IniParseState;

static inline int
is_space(char c) {
    return c == ' ' || c == '\t';
}

static inline int
trim_start_end(char* buf, int* start, int* end) {
    while(is_space(buf[*start])) {
        *start += 1;
    }
    while(is_space(buf[*end]) || buf[*end] == '\n') {
        *end -= 1;
    }
}

static int
copy_to_buf(char* in_buf, int start, int end, char* out_buf, int out_loc) {
    if (out_loc > 0) {
        out_buf[out_loc] = '\n';
        out_loc++;
    }
    for(int i = start; i <= end; i++) {
        int i2 = out_loc + (i - start);
        out_buf[i2] = in_buf[i];
    }

    int loc = out_loc + (end - start) + 1;
    out_buf[loc] = '\0';
    return loc;
}

static inline int
copy_trimmed_to_buf(char* in_buf, int start, int end, char* out_buf, int out_loc) {
    trim_start_end(in_buf, &start, &end);
    copy_to_buf(in_buf, start, end, out_buf, out_loc);
}

static int
parse_line(IniHandler handler, void* data, IniParseState* state) {
    IniRetCode status = INI_OK;
    int i = 0;

    while (state->line[i] != '\0') {
        char c = state->line[i];

        // Process newline/line start
        if (i == 0) {
            if (is_space(c)) {
                if (state->mode == PARSEMODE_VALUE) {
                    state->mode = PARSEMODE_MULTILINE;
                    state->save_loc = i+1;
                }
                else {
                    status = INI_INVALID;
                    break;
                }
            }
            else {
                if (state->mode == PARSEMODE_VALUE) handler(data, state->section, state->key, state->value);

                if (c == '\n') {
                    if (state->mode != PARSEMODE_MULTILINE) {
                        state->mode = PARSEMODE_STARTLINE;
                        state->save_loc = 0;
                        state->value_loc = 0;
                    }
                    break;
                }
                else if (c == '[') {
                    state->mode = PARSEMODE_SECTION;
                    state->save_loc = i+1;
                    state->value_loc = 0;
                }
                else if (c == '#') {
                    break;
                }
                else {
                    state->mode = PARSEMODE_KEY;
                    state->save_loc = 0;
                    state->value_loc = 0;
                }
            }
        }
        // Process line characters
        else {
            if (state->mode == PARSEMODE_SECTION) {
                if (c == ']') {
                    copy_trimmed_to_buf(state->line, state->save_loc, i-1, state->section, 0);
                    state->mode = PARSEMODE_STARTLINE;
                    state->save_loc = 0;
                }
            }
            else if (state->mode == PARSEMODE_KEY) {
                if (c == '=') {
                    copy_trimmed_to_buf(state->line, 0, i-1, state->key, 0);
                    state->mode = PARSEMODE_VALUE;
                    state->save_loc = i+1;
                }
            }
            else if (state->mode == PARSEMODE_MULTILINE) {
                if (is_space(c)) {
                    state->save_loc = i;
                }
                else {
                    state->mode = PARSEMODE_VALUE;
                }
            }
        }

        i++;
    }

    // If we've started reading a value, copy to end of line
    if (status == INI_OK && state->mode == PARSEMODE_VALUE) {
        state->value_loc = copy_trimmed_to_buf(state->line,
                                               state->save_loc,
                                               i-1,
                                               state->value,
                                               state->value_loc);
    }

    return status;
}

/* static int */
/* parse_sections(IniSectionHandler handler, void* data, IniParseState* state) { */
/*     IniRetCode status = INI_OK; */
/*     int i = 0; */
/*     while (state->line[i] != '\0') { */
/*         char c = state->line[i]; */
/*         // Process newline/line start */
/*         if (i == 0) { */
/*             if (c == '[') { */
/*                 state->save_loc = i+1; */
/*                 state->mode = PARSEMODE_SECTION; */
/*             } */
/*             else { */
/*                 break; */
/*             } */
/*         } */
/*         // Process line characters */
/*         else { */
/*             if (state->mode == PARSEMODE_SECTION) { */
/*                 if (c == ']') { */
/*                     copy_trimmed_to_buf(state->line, state->save_loc, i, state->section, 0); */
/*                     handler(data, state->section); */
/*                     state->save_loc = 0; */
/*                     state->mode = PARSEMODE_STARTLINE; */
/*                 } */
/*             } */
/*         } */
/*     } */

/*     if (state->mode == PARSEMODE_SECTION) status = INI_INVALID; */
/*     return status; */
/* } */

IniRetCode
ini_parse(FILE* fp, IniHandler handler, void* data) {
    if (!fp || !handler) return INI_NULL;

#ifndef INI_ALLOC_HEAP
    char buf[INI_BUF_SIZE];
#else
    char* buf = MMALLOC(sizeof(char) * INI_BUF_SIZE);
#endif

    memset(buf, 0, INI_BUF_SIZE);

    IniParseState state = {0};
    state.line = buf;
    state.section = buf + INI_LINE_BUF_SIZE;
    state.key = state.section + INI_SECTION_BUF_SIZE;
    state.value = state.key + INI_KEY_BUF_SIZE;
    state.value_loc = 0;
    state.save_loc = 0;
    state.mode = PARSEMODE_STARTLINE;

    IniRetCode status = INI_OK;

    while (fgets(state.line, INI_LINE_BUF_SIZE, fp)) {
        status = parse_line(handler, data, &state);
        if (status != INI_OK) break;
    }

    if (status == INI_OK && state.mode == PARSEMODE_VALUE) {
        handler(data, state.section, state.key, state.value);
    }

#ifdef INI_ALLOC_HEAP
    free(buf);
#endif
    return status;
}

/* IniRetCode */
/* ini_parse_section(FILE* fp, char* section, IniSectionHandler handler, void* data) { */
/*     if (!fp || !handler) return INI_NULL; */

/* #ifndef INI_ALLOC_HEAP */
/*     char buf[INI_BUF_SIZE]; */
/* #else */
/*     char* buf = MMALLOC(sizeof(char) * INI_BUF_SIZE); */
/* #endif */

/*     memset(buf, 0, INI_BUF_SIZE); */

/*     IniParseState state = {0}; */
/*     state.line = buf; */
/*     state.section = buf + INI_LINE_BUF_SIZE; */
/*     state.key = state.section + INI_SECTION_BUF_SIZE; */
/*     state.value = state.key + INI_KEY_BUF_SIZE; */
/*     state.value_loc = 0; */
/*     state.save_loc = 0; */
/*     state.mode = PARSEMODE_STARTLINE; */

/*     IniRetCode status = INI_OK; */

/*     while (fgets(state.line, INI_LINE_BUF_SIZE, fp)) { */
/*         status = parse_sections(handler, data, &state); */
/*         if (status != INI_OK) break; */
/*     } */

/*     if (status == INI_OK && state.mode == PARSEMODE_SECTION) { */
/*         handler(data, state.section); */
/*     } */

/* #ifdef INI_ALLOC_HEAP */
/*     free(buf); */
/* #endif */
/*     return status; */
/* } */

#endif

#endif
