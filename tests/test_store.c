#define STORE_IMPL
#include "store.h"
#include "testutil.h"

typedef struct {
    int key;
    char* text;
} TestStruct;

TEST_SUITE(store,
    Store* store;
    TestStruct* t1;

    TEST_CASE("store creation",
        store = store_new(2, sizeof(TestStruct));
        TEST_ASSERT_NOT(store, NULL);
        store_free(store);
    );

    TEST_CASE("store_push_empty should allocate a zeroed item",
        store = store_new(3, sizeof(TestStruct));
        TEST_ASSERT_EQ(store_length(store), 0);
        KeyValue res = store_push_empty(store);
        TEST_ASSERT_EQ(store_length(store), 1);
        TEST_ASSERT_EQ(res.key, 0);
        t1 = (TestStruct*)res.value;
        TEST_ASSERT_NOT(t1, NULL);
        TEST_ASSERT_EQ(t1->key, 0);
        TEST_ASSERT_EQ(t1->text, NULL);
    );

    TEST_CASE("store_push should copy given item to a new index",
        t1->key = 12;
        t1->text = "asdfgh";

        KeyValue res = store_push(store, t1);
        TestStruct* t2 = (TestStruct*)res.value;
        TEST_ASSERT_EQ(store_length(store), 2);
        TEST_ASSERT_EQ(res.key, 1);
        TEST_ASSERT_EQ(t1->key, t2->key);
        TEST_ASSERT_EQ(strcmp(t1->text, t2->text), 0);

        res = store_push(store, t2);
        TestStruct* t3 = (TestStruct*)res.value;
        TEST_ASSERT_EQ(store_length(store), 3);
        TEST_ASSERT_EQ(res.key, 2);
        TEST_ASSERT_EQ(t2->key, t3->key);
        TEST_ASSERT_EQ(strcmp(t2->text, t3->text), 0);
    )

    TEST_CASE("store_remove_at should mark item as not used",
        TEST_ASSERT_EQ(store_is_used(store, 1), 1);
        TEST_ASSERT_EQ(store_remove_at(store, 1), 1);
        TEST_ASSERT_EQ(store_is_used(store, 1), 0);
        TEST_ASSERT_EQ(store_length(store), 2);
    );

    TEST_CASE("store_push should reuse removed slots",
        KeyValue res = store_push(store, t1);
        TEST_ASSERT_EQ(res.key, 1);
        TEST_ASSERT_EQ(store_is_used(store, 1), 1);
        TEST_ASSERT_EQ(store_length(store), 3);
        TEST_ASSERT_EQ(store_remove_at(store, 0), 1);
        res = store_push(store, t1);
        TEST_ASSERT_EQ(res.key, 0);
    );

    TEST_CASE("store_push over capacity should return -1",
        KeyValue res = store_push(store, t1);
        TEST_ASSERT_EQ(res.key, -1);
        TEST_ASSERT_EQ(store_length(store), 3);
    );

    TEST_CASE("store_reset should reset everything to base state",
        store_reset(store);
        TEST_ASSERT_EQ(store_length(store), 0);
    );

    store_free(store);
)
