#include "testutil.h"
#define ARENA_ALLOCATOR_IMPL
#include "arena.h"

TEST_SUITE(arena,
    TEST_CASE("arena should not allocate over pre-allocated and locked capacity",
        TEST_ASSERT(arena_init(32, 1, 1) == ARENA_OK);
        TEST_ASSERT(arena_alloc(64) == NULL);
        TEST_ASSERT(arena_alloc(32) != NULL);
        TEST_ASSERT(arena_alloc(32) == NULL);
        TEST_ASSERT(arena_free() == ARENA_OK);
        TEST_ASSERT(__arena_root == NULL);
    );

    TEST_CASE("arena should allow allocation over capacity without preallocation and lock",
        TEST_ASSERT(arena_init(1024, 0, 0) == ARENA_OK);
        TEST_ASSERT(arena_alloc(1025) == NULL);
        TEST_ASSERT(arena_alloc(512) != NULL);
        TEST_ASSERT(arena_alloc(512) != NULL);
        TEST_ASSERT(__arena_root->next == NULL);
        TEST_ASSERT(arena_alloc(512) != NULL);
        TEST_ASSERT(__arena_root->next != NULL);
        TEST_ASSERT(arena_free() == ARENA_OK);
        TEST_ASSERT(__arena_root == NULL);
    );
)
