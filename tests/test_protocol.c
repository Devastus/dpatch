#define PROTOCOL_IMPL
#include "testutil.h"
#include "protocol.h"
#include <stdlib.h>

TEST_SUITE(protocol,

    ProtocolTokenStream* test_stream;

    TEST_CASE("protocol token stream alloc",
        test_stream = protocol_tokenstream_alloc(3);
        TEST_ASSERT_NOT(test_stream, NULL);
        test_stream->type = PROTOCOL_MSG_PING;

        protocol_tokenstream_add_token(test_stream, PROTOCOL_TOKEN_ARG, "arg1");
        TEST_ASSERT_EQ(strcmp(test_stream->tokens[0].value, "arg1"), 0);
        protocol_tokenstream_add_token(test_stream, PROTOCOL_TOKEN_VAR, "var1=asd");
        TEST_ASSERT_EQ(strcmp(test_stream->tokens[1].value, "var1=asd"), 0);
        protocol_tokenstream_add_token(test_stream, PROTOCOL_TOKEN_VAR, "var2=fgh");
        TEST_ASSERT_EQ(strcmp(test_stream->tokens[2].value, "var2=fgh"), 0);
        TEST_ASSERT_EQ(test_stream->length, 3);
    );

    TEST_CASE("token stream to buf and back",
        int bufsize = 64;
        char* buf = (char*)malloc(sizeof(char) * bufsize);
        memset(buf, 0, bufsize);

        int written = protocol_tokenstream_to_buf(test_stream, buf, bufsize, 0);
        TEST_ASSERT(written > 0);

        ProtocolTokenStream* res_stream = protocol_tokenstream_alloc(test_stream->length);
        protocol_buf_to_tokenstream(buf, written, 0, res_stream);

        TEST_ASSERT_EQ(res_stream->type, test_stream->type);
        TEST_ASSERT_EQ(res_stream->length, test_stream->length);

        TEST_ASSERT_EQ(res_stream->tokens[0].type, test_stream->tokens[0].type);
        TEST_ASSERT_EQ(strcmp(res_stream->tokens[0].value, test_stream->tokens[0].value), 0);

        TEST_ASSERT_EQ(res_stream->tokens[1].type, test_stream->tokens[1].type);
        TEST_ASSERT_EQ(strcmp(res_stream->tokens[1].value, test_stream->tokens[1].value), 0);

        TEST_ASSERT_EQ(res_stream->tokens[2].type, test_stream->tokens[2].type);
        TEST_ASSERT_EQ(strcmp(res_stream->tokens[2].value, test_stream->tokens[2].value), 0);
    );
)
