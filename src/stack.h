#ifndef DPATCH_STACK_H
#define DPATCH_STACK_H

#include <stdlib.h>

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

typedef struct Stack_st {
    size_t count;
    size_t capacity;
    size_t item_size;
    void* data;
} Stack;

Stack* stack_new(size_t capacity, size_t item_size);
unsigned char stack_push(Stack* s, void* item);
void* stack_pop(Stack* s);
void* stack_get(Stack* s, int i);
unsigned char stack_remove_at(Stack* s, int i);
unsigned char stack_reset(Stack* s);

#ifdef STACK_IMPL

Stack*
stack_new(size_t capacity, size_t item_size) {
    Stack* s = (Stack*)MMALLOC(sizeof(Stack) + (item_size * capacity));
    if (!s) {
        fprintf(stderr, "Stack allocation failed");
        return NULL;
    }
    /* if (arena_alloc(arena, sizeof(Stack) + (item_size * capacity), (void**)&s) != ARENA_OK) { */
    /*     fprintf(stderr, "Stack allocation failed"); */
    /*     return NULL; */
    /* } */
    s->count = 0;
    s->capacity = capacity;
    s->item_size = item_size;
    s->data = (void*)(s + 1);
    return s;
}

unsigned char
stack_push(Stack* s, void* item) {
    if (s->count >= s->capacity) return 0;
    memcpy(s->data + (s->count * s->item_size), item, s->item_size);
    s->count++;
    return 1;
}

void*
stack_pop(Stack* s) {
    if (s->count < 1) return NULL;
    void* p = (void*)((char*)s->data + ((s->count - 1) * s->item_size));
    s->count--;
    return p;
}

void*
stack_get(Stack* s, int i) {
    if (i >= s->count) return NULL;
    return (void*)((char*)s->data + (i * s->item_size));
}

unsigned char
stack_remove_at(Stack* s, int i) {
    if (i >= s->count) return 0;
    void* last = (void*)((char*)s->data + ((s->count - 1) * s->item_size));
    if (s->count > 1) {
        void* ptr = (void*)((char*)s->data + (i * s->item_size));
        memcpy(ptr, last, s->item_size);
    }
    memset(last, 0, s->item_size);
    s->count--;
    return 1;
}

unsigned char
stack_reset(Stack* s) {
    if (!s) return 0;
    memset(s, 0, s->item_size * s->count);
    s->count = 0;
    return 1;
}

#endif

#endif
