#include "testutil.h"
#include "test_protocol.c"
#include "test_arena.c"
#include "test_ini.h"

int main(int argc, char** arv) {
    TEST(protocol);
    TEST(arena);
    TEST(ini);
}
