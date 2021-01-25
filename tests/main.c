#include "testutil.h"
#include "test_protocol.c"
#include "test_arena.c"
#include "test_ini.c"
#include "test_store.c"

int main(int argc, char** arv) {
    int err = 0;
    err += RUN_TEST(protocol);
    err += RUN_TEST(arena);
    /* err += RUN_TEST(ini); */
    err += RUN_TEST(store);
    return err;
}
