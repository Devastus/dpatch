#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#ifdef TEST_STOP_ON_FAIL
#define TEST_RETURN return 1;
#else
#define TEST_RETURN
#endif

#define TEST_MSG_BUF_SIZE 4096
static char test_msg_buffer[TEST_MSG_BUF_SIZE] = {0};

#define TEST_COLOR_DEFAULT "\e[0m"
#define TEST_COLOR_RED "\e[91m"
#define TEST_COLOR_GREEN "\e[92m"

#define RUN_TEST(suite) (suite ## _test_body())

#define TEST_SUITE(name, expr) \
int name ## _test_body() {\
    printf("\nTest suite: '%s'\n===================================\n", #name);\
    int err_cnt = 0;\
    {\
        expr\
    }\
    printf("\nTest suite '%s' completed with %d errors.\n", #name, err_cnt);\
    return err_cnt;\
}\


#define TEST_CASE(case, expr) {\
    printf("\n%s\n------------------------\n", case);\
    int assert_cnt = 0;\
    int assert_err_cnt = 0;\
    {\
        expr\
    }\
    printf("------------------------\nResult: %d asserts, %d fails\n", assert_cnt, assert_err_cnt);\
    err_cnt += assert_err_cnt;\
}

#define PRINT_OK(fmt, ...) {\
    memset(test_msg_buffer, 0, TEST_MSG_BUF_SIZE);\
    sprintf(test_msg_buffer, fmt, ##__VA_ARGS__);\
    printf("%s[OK]%s %s\n", TEST_COLOR_GREEN, TEST_COLOR_DEFAULT, test_msg_buffer);\
}

#define PRINT_FAIL(fmt, ...) {\
    assert_err_cnt++;\
    memset(test_msg_buffer, 0, TEST_MSG_BUF_SIZE);\
    sprintf(test_msg_buffer, fmt, ##__VA_ARGS__);\
    printf("%s[FAIL]%s %s\n", TEST_COLOR_RED, TEST_COLOR_DEFAULT, test_msg_buffer);\
    TEST_RETURN\
}

#define TEST_ASSERT(expr){\
    assert_cnt++;\
    if (!(expr)) {\
        PRINT_FAIL("%s (%s:%i)", #expr, __FILE__, __LINE__);\
    } else {\
        PRINT_OK("%s", #expr);\
    }\
}

#define TEST_ASSERT_BYTE_EQ(v1, v2) {\
    assert_cnt++;\
    char* ptr1 = (char*)(v1);\
    char* ptr2 = (char*)(v2);\
    int length = sizeof(*v1);\
    if (length != sizeof(*v2)) {\
        PRINT_FAIL("%s == %s (%s:%i)", #v1, #v2, __FILE__, __LINE__);\
    } else {\
        int err = 0;\
        for (int i = 0; i < length; i++) {\
            if (ptr1[i] != ptr2[i]) {\
                PRINT_FAIL("%s == %s (%s:%i)", #v1, #v2, __FILE__, __LINE__);\
                err = 1;\
                break;\
            }\
        }\
        if (!err) PRINT_OK("%s == %s", #v1, #v2);\
    }\
}

#define TEST_ASSERT_EQ(v1, v2){\
    TEST_ASSERT(v1 == v2);\
}

#define TEST_ASSERT_NOT(v1, v2){\
    TEST_ASSERT(v1 != v2);\
}

#endif
