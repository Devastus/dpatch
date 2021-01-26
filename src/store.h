#ifndef CUTIL_STORE_H
#define CUTIL_STORE_H

#include <stdlib.h>

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

#ifdef FREE_FUNC
#define MFREE(ptr) ALLOC_FUNC(ptr)
#else
#define MFREE(ptr) free(ptr)
#endif

#define KEYVALUE_NONE (KeyValue){ -1, NULL }

typedef struct KeyValue_st {
    size_t key;
    void* value;
} KeyValue;

typedef struct Store_st {
    int capacity;
    size_t item_size;
    int open_cnt;
    int* open;
    char* data;
} Store;

/// Create a new store of given capacity and item size, returns a pointer to the new store or NULL if failed
Store* store_new(int capacity, size_t item_size);
/// Push a new item into the store, returns the index of the new item or -1 if failed or store capacity is full
void store_free(Store* store);
/// Push a new item into store, returns key-value-pair of new item index and item data or KEYVALUE_NONE
KeyValue store_push(Store* store, void* data);
/// Push a new empty item into store, returns key-value-pair of new item index and item data or KEYVALUE_NONE
KeyValue store_push_empty(Store* store);
/// Get an item with given index, returns key-value-pair of item index and data or KEYVALUE_NONE
KeyValue store_get(Store* store, int idx);
/// Check if given store index is in use, returns 1 on success and 0 on failure
unsigned char store_is_used(Store* store, int idx);
/// Replace data in given store index, returns 1 on success and 0 on failure
unsigned char store_replace(Store* store, int idx, void* data);
/// Remove data from given store index, returns 1 on success and 0 on failure
unsigned char store_remove_at(Store* store, int idx);
/// Get amount of items in the store (ie. length)
int store_length(Store* store);
/// Reset store back to zeroed state (retains capacity and item size)
void store_reset(Store* store);

#ifdef STORE_IMPL

#define ITEM_IDX(store, idx) ((char*)store->data + (idx * (store->item_size + 1)))
#define ITEM_USED(ptr) (*ptr)
#define ITEM_VALUE(ptr) (ptr + 1)

#define KEYVALUE(store, idx) (KeyValue){ idx, (void*)ITEM_VALUE(ITEM_IDX(store, idx)) }

static inline int
get_push_index(Store* store) {
    return store->open_cnt > 0 ?
           store->open[--store->open_cnt] :
           -1;
}

static inline void
reset_open(Store* store) {
    store->open_cnt = store->capacity;
    for (int i = 0; i < store->capacity; i++) {
        int idx = (store->capacity - 1) - i;
        store->open[i] = idx;
    }
}

Store*
store_new(int capacity, size_t item_size) {
    Store* store = MMALLOC(sizeof(Store) +
                           sizeof(int) * capacity +
                           sizeof(char) * (item_size + 1) * capacity);
    if (!store) return NULL;

    store->capacity     = capacity;
    store->item_size    = item_size;
    store->open_cnt = capacity;
    store->open     = (int*)((char*)store + sizeof(Store));
    store->data         = (char*)store->open + (sizeof(int) * capacity);
    memset(store->data, 0, (item_size + 1) * capacity);
    reset_open(store);

    return store;
}

void
store_free(Store* store) {
    if (store != NULL) {
        MFREE(store);
    }
}

KeyValue
store_push(Store* store, void* data) {
    if (!store) return KEYVALUE_NONE;

    int idx = get_push_index(store);
    if (idx < 0) return KEYVALUE_NONE;

    char* ptr = ITEM_IDX(store, idx);
    ITEM_USED(ptr) = 1;

    memcpy(ITEM_VALUE(ptr), data, store->item_size);
    return KEYVALUE(store, idx);
}

KeyValue
store_push_empty(Store* store) {
    if (!store) return KEYVALUE_NONE;

    int idx = get_push_index(store);
    if (idx < 0) return KEYVALUE_NONE;

    char* ptr = ITEM_IDX(store, idx);
    ITEM_USED(ptr) = 1;

    memset(ITEM_VALUE(ptr), 0, store->item_size);
    return KEYVALUE(store, idx);
}

KeyValue
store_get(Store* store, int idx) {
    if (!store) return KEYVALUE_NONE;
    char* ptr = ITEM_IDX(store, idx);
    if (!ITEM_USED(ptr)) return KEYVALUE_NONE;
    return KEYVALUE(store, idx);
}

unsigned char
store_is_used(Store* store, int idx) {
    return ITEM_USED(ITEM_IDX(store, idx));
}

unsigned char
store_replace(Store* store, int idx, void* data) {
    if (!store) return 0;
    if (idx >= store->capacity) return 0;

    char* ptr = ITEM_IDX(store, idx);
    if (!ITEM_USED(ptr)) return 0;

    memcpy(ITEM_VALUE(ptr), data, store->item_size);
    ITEM_USED(ptr) = 1;
    return 1;
}

unsigned char
store_remove_at(Store* store, int idx) {
    if (!store) return 0;
    if (idx >= store->capacity) return 0;

    char* ptr = ITEM_IDX(store, idx);
    if (!ITEM_USED(ptr)) return 0;

    ITEM_USED(ptr) = 0;
    store->open[store->open_cnt] = idx;
    store->open_cnt++;
    return 1;
}

int
store_length(Store* store) {
    if (!store) return -1;
    return store->capacity - store->open_cnt;
}

void
store_reset(Store* store) {
    if (!store) return;
    memset(store->data, 0, sizeof(char) * (store->item_size + 1) * store->capacity);
    reset_open(store);
}

#endif

#endif
