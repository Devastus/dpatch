#ifndef ARENA_ALLOCATOR_H
#define ARENA_ALLOCATOR_H

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#ifdef ARENA_DEBUG
#define DEBUG_PRINT(fmt, ...) {\
    printf(fmt, ##__VA_ARGS__);\
}
#else
#define DEBUG_PRINT(fmt, ...)
#endif

typedef unsigned char ARENA_BOOL;

typedef enum {
    ARENA_OK = 0,
    ARENA_NULL = -1,
    ARENA_OOM = -2,
    ARENA_FULL = -3,
} ArenaRetCode;

typedef struct ArenaAllocator_st ArenaAllocator;

ArenaRetCode arena_free();
void* arena_alloc(size_t size);
ArenaRetCode arena_init(size_t size, int prealloc_count, ARENA_BOOL lock);

#ifdef ARENA_ALLOCATOR_IMPL

typedef struct ArenaAllocator_st {
    size_t size;
    size_t current;
    char eom;
    ArenaAllocator* next;
    void* region;
} ArenaAllocator;

static ArenaAllocator* __arena_root;

static ArenaAllocator*
_arena_create(size_t size) {
    ArenaAllocator* new_arena = (ArenaAllocator*)malloc(sizeof(ArenaAllocator) + (sizeof(char) * (size)));
    if (!new_arena) return 0;

    new_arena->size = size;
    new_arena->current = 0;
    new_arena->next = 0;
    new_arena->eom = 0;
    new_arena->region = (void*)((ArenaAllocator*)new_arena + 1);
    memset(new_arena->region, 0, size);

    return new_arena;
}

ArenaRetCode
arena_init(size_t size, int prealloc_count, ARENA_BOOL lock) {
    __arena_root = _arena_create(size);
    if (!__arena_root) {
        perror("Arena allocator failed to allocate memory");
        exit(-1);
    }

    // Pre-allocate pages if count is given
    if (prealloc_count > 0) {
        int i = 1;
        ArenaAllocator* next = __arena_root;

        while (next) {
            if (i >= prealloc_count) {
                if (lock) next->eom = 1;
                break;
            }

            next->next = _arena_create(size);
            if (!next->next) {
                perror("Arena allocator failed to allocate memory");
                arena_free(__arena_root);
                exit(-1);
            }

            next = next->next;
            i++;
        }
    }

    DEBUG_PRINT("Arena allocator created - ptr: %lu, region size: %lu, prealloc_count: %d, lock: %i\n",
                __arena_root,
                size,
                prealloc_count,
                lock);
    DEBUG_PRINT("Total allocation size: %lu bytes\n", size * prealloc_count);

    return ARENA_OK;
}

/// Free the arena allocator of all data. Returns operation result code.
ArenaRetCode
arena_free() {
    if (!__arena_root) return ARENA_NULL;
    ArenaAllocator* cur = __arena_root;

    while (cur) {
        ArenaAllocator* next = cur->next;
        free(cur);
        cur = next;
    }

    __arena_root = NULL;
    return ARENA_OK;
}

/// Allocate data to arena, sets given pointer to the reserved data region. Returns operation result code.
void*
arena_alloc(size_t size) {
    if (!__arena_root) return NULL;
    if (size > __arena_root->size) return NULL;
    ArenaAllocator* cur = __arena_root;

    while (cur) {
        // Assign to current if there's enough space
        if ((cur->current + size) <= cur->size) {
            void* p = (void*)((char*)cur->region + cur->current);
            cur->current += size;
            DEBUG_PRINT("Reserving area %lu / %lu, size: %lu bytes\n", p, (cur->region + cur->current), size);
            return p;
        }

        // Try to allocate a new arena region
        if (!cur->next) {
            if (cur->eom) return NULL;
            cur->next = _arena_create(size);
            if (!cur->next) {
                perror("Arena allocator failed to create new region");
                arena_free(__arena_root);
                exit(-1);
            };
            DEBUG_PRINT("New arena region created at %lu\n", cur->next);
        }

        cur = cur->next;
    }

    return NULL;
}

#endif

#endif
