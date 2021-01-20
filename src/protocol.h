#ifndef DPATCH_PROTOCOL_H
#define DPATCH_PROTOCOL_H

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

typedef enum {
    PROTOCOL_MSG_NONE,
    PROTOCOL_MSG_PING,
    PROTOCOL_MSG_ACK,
    PROTOCOL_MSG_WORKSPACE_GET,
    PROTOCOL_MSG_WORKSPACE_USE,
    /* PROTOCOL_MSG_WORKSPACE_LOAD, */
    /* PROTOCOL_MSG_WORKSPACE_REMOVE, */
    PROTOCOL_MSG_TASK_GET,
    /* PROTOCOL_MSG_TASK_CREATE, */
    PROTOCOL_MSG_TASK_INVOKE,
    /* PROTOCOL_MSG_TASK_REMOVE, */
    PROTOCOL_MSG_TASK_COMPLETE,
    PROTOCOL_MSG_PRINT_OUT,
    PROTOCOL_MSG_PRINT_ERR,
    __PROTOCOL_MSG_COUNT
} ProtocolMsgType;

typedef enum {
    PROTOCOL_TOKEN_NONE,
    PROTOCOL_TOKEN_ARG,
    PROTOCOL_TOKEN_VAR
} ProtocolTokenType;

typedef struct ProtocolToken_st {
    unsigned char type;
    char* value;
} ProtocolToken;

typedef struct ProtocolTokenStream_st {
    ProtocolMsgType type;
    int length;
    ProtocolToken* tokens;
} ProtocolTokenStream;

/// Allocate a new token stream object.
ProtocolTokenStream* protocol_tokenstream_alloc(int token_length);
void protocol_tokenstream_reset(ProtocolTokenStream* token_stream, int token_length);
int protocol_tokenstream_add_token(ProtocolTokenStream* token_stream, ProtocolTokenType type, char* value);
/// Deserialize a byte buffer into a token stream.
int protocol_buf_to_tokenstream(char* in_buf, int in_buf_len, int in_buf_loc, ProtocolTokenStream* token_stream);
/// Serialize token stream into a byte buffer.
int protocol_tokenstream_to_buf(ProtocolTokenStream* token_stream, char* out_buf, int out_buf_len, int out_buf_loc);
/// Output token stream into comprised parts.
int protocol_parse_token_stream(ProtocolTokenStream* token_stream, unsigned char* type, char** args, char** vars);

#ifdef PROTOCOL_IMPL

ProtocolTokenStream*
protocol_tokenstream_alloc(int token_length) {
    ProtocolTokenStream* token_stream = (ProtocolTokenStream*)MMALLOC(
            sizeof(ProtocolTokenStream) + (sizeof(ProtocolToken) * token_length));
    if (!token_stream) return NULL;

    token_stream->length = 0;
    token_stream->tokens = (ProtocolToken*)((char*)token_stream + sizeof(ProtocolTokenStream));
    memset(token_stream->tokens, 0, sizeof(ProtocolToken) * token_length);

    return token_stream;
}

void
protocol_tokenstream_reset(ProtocolTokenStream* token_stream, int token_length) {
    token_stream->length = 0;
    memset(token_stream->tokens, 0, sizeof(ProtocolToken) * token_length);
}

int
protocol_tokenstream_add_token(ProtocolTokenStream* token_stream, ProtocolTokenType type, char* value) {
    token_stream->tokens[token_stream->length] = (ProtocolToken){
        .type = type,
        .value = value,
    };
    token_stream->length++;
}

int
protocol_buf_to_tokenstream(char* in_buf,
                            int in_buf_len,
                            int start_loc,
                            ProtocolTokenStream* token_stream)
{
    if (!in_buf || !token_stream) return -1;

    int stream_type = *(int*)(in_buf + start_loc);
    int token_len = *(int*)(in_buf + start_loc + sizeof(int));
    if (token_len < 1) return -1;

    token_stream->type = stream_type;
    token_stream->length = token_len;
    memset(token_stream->tokens, 0, sizeof(ProtocolToken) * token_len);

    int cur = start_loc + (sizeof(int) * 2);

    for (int i = 0; i < token_stream->length; i++) {
        token_stream->tokens[i].type = *(unsigned char*)(in_buf + cur);
        cur += sizeof(unsigned char);

        int c = 0;
        while (in_buf[cur + c] != '\0') {
            c++;
        }
        token_stream->tokens[i].value = in_buf + cur;

        cur += c + 1;
    }

    return 0;
}

int
protocol_tokenstream_to_buf(ProtocolTokenStream* token_stream,
                            char* out_buf,
                            int out_buf_len,
                            int start_loc)
{
    if (!out_buf || !token_stream) return -1;

    *(int*)(out_buf + start_loc) = token_stream->type;
    *(int*)(out_buf + start_loc + sizeof(int)) = token_stream->length;
    int cur = start_loc + (sizeof(int) * 2);

    for (int i = 0; i < token_stream->length; i++) {
        ProtocolToken token = token_stream->tokens[i];

        *(unsigned char*)(out_buf + cur) = token.type;
        cur += sizeof(unsigned char);

        int c = 0;
        while (token.value[c] != '\0') {
            out_buf[cur + c] = token.value[c];
            c++;
        }
        out_buf[cur + c] = '\0';

        cur += c + 1;
    }

    return cur;
}

int protocol_parse_token_stream(ProtocolTokenStream* token_stream, unsigned char* type, char** args, char** vars) {
    int argc = 0;
    int varc = 0;
    *type = token_stream->type;

    for (int i = 0; i < token_stream->length; i++) {
        ProtocolToken token = token_stream->tokens[i];
        if (token.type == PROTOCOL_TOKEN_ARG) {
            args[argc] = token.value;
            argc++;
        }
        else if (token.type == PROTOCOL_TOKEN_VAR) {
            vars[varc] = token.value;
            varc++;
        }
    }
    args[argc] = NULL;
    vars[varc] = NULL;
    return 0;
}

#endif

#endif
