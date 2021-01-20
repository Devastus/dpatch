#define PROTOCOL_IMPL
#include "testutil.h"
#include "protocol.h"
#include <stdlib.h>

int
protocol_test_body() {
    ProtocolTokenStream test_stream = {
        .type = PROTOCOL_MSG_PING,
        .length = 3,
        .tokens = (ProtocolToken[]){
            {
                .type = PROTOCOL_TOKEN_ARG,
                .value = "arg1"
            },
            {
                .type = PROTOCOL_TOKEN_VAR,
                .value = "var1=asd"
            },
            {
                .type = PROTOCOL_TOKEN_VAR,
                .value = "var2=fgh"
            },
        }
    };

    int bufsize = 64;
    char* buf = (char*)malloc(sizeof(char) * bufsize);
    memset(buf, 0, bufsize);

    int written = protocol_tokenstream_to_buf(&test_stream, buf, bufsize, 0);
    TEST_ASSERT(written > 0);

    ProtocolTokenStream* res_stream = protocol_tokenstream_alloc(test_stream.length);
    protocol_buf_to_tokenstream(buf, written, 0, res_stream);

    TEST_ASSERT_EQ(res_stream->type, test_stream.type);
    TEST_ASSERT_EQ(res_stream->length, test_stream.length);

    TEST_ASSERT_EQ(res_stream->tokens[0].type, test_stream.tokens[0].type);
    TEST_ASSERT_EQ(strcmp(res_stream->tokens[0].value, test_stream.tokens[0].value), 0);

    TEST_ASSERT_EQ(res_stream->tokens[1].type, test_stream.tokens[1].type);
    TEST_ASSERT_EQ(strcmp(res_stream->tokens[1].value, test_stream.tokens[1].value), 0);

    TEST_ASSERT_EQ(res_stream->tokens[2].type, test_stream.tokens[2].type);
    TEST_ASSERT_EQ(strcmp(res_stream->tokens[2].value, test_stream.tokens[2].value), 0);

    return 0;
}
