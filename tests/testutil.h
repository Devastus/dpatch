#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

static char test_msg_buffer[4096] = {0};

#define TEST_COLOR_DEFAULT "\e[0m"
#define TEST_COLOR_RED "\e[91m"
#define TEST_COLOR_GREEN "\e[92m"

#define TEST(suite) {\
    printf("\nTest suite: '%s'\n================================\n", #suite);\
    int err = suite ## _test_body();\
}

#define PRINT_OK(fmt, ...) {\
    memset(test_msg_buffer, 0, 4096);\
    sprintf(test_msg_buffer, fmt, ##__VA_ARGS__);\
    printf("%s[OK]%s %s\n", TEST_COLOR_GREEN, TEST_COLOR_DEFAULT, test_msg_buffer);\
}

#define PRINT_FAIL(fmt, ...) {\
    memset(test_msg_buffer, 0, 4096);\
    sprintf(test_msg_buffer, fmt, ##__VA_ARGS__);\
    printf("%s[FAIL]%s %s\n", TEST_COLOR_RED, TEST_COLOR_DEFAULT, test_msg_buffer);\
    return 1;\
}

#define TEST_ASSERT(expr){\
    if (!(expr)) {\
        PRINT_FAIL("%s (%s:%i)", #expr, __FILE__, __LINE__);\
    } else {\
        PRINT_OK("%s", #expr);\
    }\
}

#define TEST_ASSERT_BYTE_EQ(v1, v2) {\
    unsigned char* ptr1 = (unsigned char*)(v1);\
    unsigned char* ptr2 = (unsigned char*)(v2);\
    int length = sizeof(*v1);\
    if (length != sizeof(*v2)) PRINT_FAIL("%s == %s (%s:%i)", #v1, #v2, __FILE__, __LINE__);\
    for (int i = 0; i < length; i++) {\
        if (ptr1[i] != ptr2[i]) PRINT_FAIL("%s == %s (%s:%i)", #v1, #v2, __FILE__, __LINE__);\
    }\
    PRINT_OK("%s == %s", #v1, #v2);\
}

#define TEST_ASSERT_EQ(v1, v2){\
    TEST_ASSERT(v1 == v2);\
}

#define TEST_ASSERT_NOT(v1, v2){\
    TEST_ASSERT(v1 != v2);\
}

#define TEST_CASE(case, expr) {\
    printf("%s:\n", #case);\
    {\
        expr\
    }\
    printf("\n");\
}

#endif
