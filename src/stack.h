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

/// Create a new stack of given capacity and item size, returns a pointer to the new stack or NULL if failed
Stack* stack_new(size_t capacity, size_t item_size);
/// Push a new item into the stack, returns the index of the new item or -1 if failed or stack capacity is full
size_t stack_push(Stack* s, void* item);
/// Push a new empty item into stack, returns item or NULL if failed or stack capacity is full
void* stack_push_new(Stack* s);
/// Pop an item from the end of the stack, returns item or NULL if stack is empty
void* stack_pop(Stack* s);
/// Get an item at the location of the stack, returns NULL if failed or index is out of stack bounds
void* stack_get(Stack* s, int i);
/// Remove an item at the location of the stack and preserving ordering, returns 1 if succesful or 0 if failed
unsigned char stack_remove_at(Stack* s, int i);
/// Remove an item at the location of the stack without preserving item order, eq. a faster delete.
/// Returns 1 if succesful or 0 if failed.
unsigned char stack_remove_at_fast(Stack* s, int i);
/// Reset the stack back to empty, returns 1 if succesful or 0 if failed
unsigned char stack_reset(Stack* s);

#ifdef STACK_IMPL

#define STACK_ITEM(s, i) ((char*)((s)->data) + ((i) * (s)->item_size))

Stack*
stack_new(size_t capacity, size_t item_size) {
    Stack* s = (Stack*)MMALLOC(sizeof(Stack) + (item_size * capacity));
    if (!s) {
        fprintf(stderr, "Stack allocation failed");
        return NULL;
    }
    s->count = 0;
    s->capacity = capacity;
    s->item_size = item_size;
    s->data = (void*)(s + 1);
    return s;
}

size_t
stack_push(Stack* s, void* item) {
    if (s->count >= s->capacity) return -1;
    memcpy((char*)s->data + (s->count * s->item_size), item, s->item_size);
    s->count++;
    return s->count-1;
}

void*
stack_push_new(Stack* s) {
    if (s->count >= s->capacity) return NULL;
    char* ptr = (char*)s->data + (s->count * s->item_size);
    memset(ptr, 0, s->item_size);
    s->count++;
    return (void*)ptr;
}

void*
stack_pop(Stack* s) {
    if (s->count < 1) return NULL;
    void* p = (void*)STACK_ITEM(s, s->count - 1);
    s->count--;
    return p;
}

void*
stack_get(Stack* s, int i) {
    if (i >= s->count) return NULL;
    return (void*)STACK_ITEM(s, i);
}

unsigned char
stack_remove_at(Stack* s, int i) {
    if (i >= s->count) return 0;
    for (; i < (s->count - 1); i++) {
        memcpy(STACK_ITEM(s, i),
               STACK_ITEM(s, i + 1),
               s->item_size);
    }
    memset(STACK_ITEM(s, i), 0, s->item_size);
    s->count--;
    return 1;
}

unsigned char
stack_remove_at_fast(Stack* s, int i) {
    if (i >= s->count) return 0;
    void* last = (void*)STACK_ITEM(s, s->count - 1);
    if (s->count > 1) {
        void* ptr = (void*)STACK_ITEM(s, i);
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
