#ifndef DPATCH_PROTOCOL_H
#define DPATCH_PROTOCOL_H

#include "net.h"

#ifdef ALLOC_FUNC
#define MMALLOC(size) ALLOC_FUNC(size)
#else
#define MMALLOC(size) malloc(size)
#endif

typedef enum {
    PROTOCOL_MSG_NONE,
    PROTOCOL_MSG_PING,
    PROTOCOL_MSG_TASK_RUN,
    PROTOCOL_MSG_TASK_INFO,
    PROTOCOL_MSG_WORKSPACE_SET,
    PROTOCOL_MSG_WORKSPACE_INFO,
    PROTOCOL_MSG_PROC_INFO,
    PROTOCOL_MSG_SUCCESS,
    PROTOCOL_MSG_ERR,
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
/// Reset a token stream
void protocol_tokenstream_reset(ProtocolTokenStream* token_stream);
/// Add a new token into token stream
void protocol_tokenstream_add_token(ProtocolTokenStream* token_stream, ProtocolTokenType type, char* value);
/// Deserialize a byte buffer into a token stream.
int protocol_buf_to_tokenstream(char* in_buf, int in_buf_len, int in_buf_loc, ProtocolTokenStream* token_stream);
/// Serialize token stream into a byte buffer.
int protocol_tokenstream_to_buf(ProtocolTokenStream* token_stream, char* out_buf, int out_buf_len, int out_buf_loc);
/// Output token stream into comprised parts.
int protocol_parse_token_stream(ProtocolTokenStream* token_stream, unsigned char* type, char** args, char** vars);
/// Send a protocol token stream as a network message, returns number of bytes sent
int protocol_send(int socket, char* data_buf, int buf_len, ProtocolTokenStream* token_stream);
/// Read a network message into a protocol token stream, returns 0 if succesful
int protocol_read(char* data_buf, int buf_len, ProtocolTokenStream* token_stream);

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
protocol_tokenstream_reset(ProtocolTokenStream* token_stream) {
    memset(token_stream->tokens, 0, sizeof(ProtocolToken) * token_stream->length);
    token_stream->length = 0;
}

void
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
        if (token.type == PROTOCOL_TOKEN_NONE || !token.value) continue;

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

int
protocol_send(int socket, char* data_buf, int buf_len, ProtocolTokenStream* token_stream) {
    int length = protocol_tokenstream_to_buf(token_stream, data_buf, buf_len, sizeof(int));
    if (length < 1) {
        fprintf(stderr, "Failed to serialize protocol token stream into a buffer\n");
        return -1;
    }

    length += sizeof(int);
    *((int*)data_buf) = length;

    return socket_send(socket, data_buf, length);
}

int
protocol_read(char* data_buf, int buf_len, ProtocolTokenStream* token_stream) {
    int msg_len = *((int*)data_buf);
    if (protocol_buf_to_tokenstream(data_buf,
                                    msg_len - sizeof(int),
                                    sizeof(int),
                                    token_stream) != 0)
    {
        fprintf(stderr, "Failed to deserialize buffer into a protocol token stream\n");
        return -1;
    }

    return 0;
}

#endif

#endif
