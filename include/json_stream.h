#ifndef JSON_STREAM_H
#define JSON_STREAM_H

#include <stdint.h>
#include <stdlib.h>

#include "bit_stack.h"

typedef enum {
    JSON_TYPE_UNKNOWN,
    JSON_TYPE_OBJECT_START,
    JSON_TYPE_OBJECT_END,
    JSON_TYPE_ARRAY_START,
    JSON_TYPE_ARRAY_END,
    JSON_TYPE_PROPERTY,
    JSON_TYPE_STRING,
    JSON_TYPE_NUMBER,
    JSON_TYPE_BOOLEAN,
    JSON_TYPE_NULL,
    JSON_TYPE_COMMENT,
} JsonType;

typedef enum {
    JSON_COMMENT_DISALLOW,
    JSON_COMMENT_SKIP,
    JSON_COMMENT_ALLOW,
} JsonCommentHandling;

typedef enum {
    JSON_ERROR_NONE,
    JSON_ERROR_NOT_IMPLEMENTED,
    JSON_ERROR_OUT_OF_MEMORY,
    JSON_ERROR_ARRAY_DEPTH_TOO_LARGE,
    JSON_ERROR_MISMATCHED_OBJECT_ARRAY,
    JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END,
    JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END,
    JSON_ERROR_END_OF_STRING_NOT_FOUND,
    JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_SIGN,
    JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_DECIMAL,
    JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA,
    JSON_ERROR_EXPECTED_END_AFTER_SINGLE_JSON,
    JSON_ERROR_EXPECTED_END_OF_DIGIT_NOT_FOUND,
    JSON_ERROR_EXPECTED_NEXT_DIGIT_E_VALUE_NOT_FOUND,
    JSON_ERROR_EXPECTED_SEPARATOR_AFTER_PROPERTY_NAME_NOT_FOUND,
    JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND,
    JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND,
    JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_AFTER_COMMENT,
    JSON_ERROR_EXPECTED_START_OF_VALUE_NOT_FOUND,
    JSON_ERROR_EXPECTED_VALUE_AFTER_PROPERTY_NAME_NOT_FOUND,
    JSON_ERROR_FOUND_INVALID_CHARACTER,
    JSON_ERROR_INVALID_END_OF_JSON_NON_PRIMITIVE,
    JSON_ERROR_OBJECT_DEPTH_TOO_LARGE,
    JSON_ERROR_EXPECTED_FALSE,
    JSON_ERROR_EXPECTED_NULL,
    JSON_ERROR_EXPECTED_TRUE,
    JSON_ERROR_INVALID_CHARACTER_WITHIN_STRING,
    JSON_ERROR_INVALID_CHARACTER_AFTER_ESCAPE_WITHIN_STRING,
    JSON_ERROR_INVALID_HEX_CHARACTER_WITHIN_STRING,
    JSON_ERROR_END_OF_COMMENT_NOT_FOUND,
    JSON_ERROR_ZERO_DEPTH_AT_END,
    JSON_ERROR_EXPECTED_JSON_TOKENS,
    JSON_ERROR_NOT_ENOUGH_DATA,
    JSON_ERROR_EXPECTED_ONE_COMPLETE_TOKEN,
    JSON_ERROR_INVALID_CHARACTER_AT_START_OF_COMMENT,
    JSON_ERROR_UNEXPECTED_END_OF_DATA_WHILE_READING_COMMENT,
    JSON_ERROR_UNEXPECTED_END_OF_LINE_SEPARATOR,
    JSON_ERROR_INVALID_LEADING_ZERO_IN_NUMBER,
    JSON_ERROR_INVALID_OPERATION_CANNOT_SKIP_ON_PARTIAL,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING_COMPARISON,
    JSON_ERROR_STRING_PARSE_FAILED,

    JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_COMMENT,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_BOOL,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_U8,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_I8,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_U16,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_I16,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_U32,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_I32,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_U64,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_I64,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_FLOAT,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_DOUBLE,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_START,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_END,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_START,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_END,
    JSON_ERROR_INVALID_OPERATION_EXPECTED_PROPERTY,
} JsonErrorType;

#define JSON_CONSTANT_SPACE ' '
#define JSON_CONSTANT_SLASH '/'
#define JSON_CONSTANT_BACKSLASH '\\'
#define JSON_CONSTANT_ASTERISK '*'
#define JSON_CONSTANT_LINE_FEED '\n'
#define JSON_CONSTANT_CARRIAGE_RETURN '\r'
#define JSON_CONSTANT_TAB '\t'
#define JSON_CONSTANT_BACKSPACE '\b'
#define JSON_CONSTANT_FORM_FEED '\f'
#define JSON_CONSTANT_STARTING_BYTE_OF_NON_STANDARD_LINE_SEPARATOR '\xE2'
#define JSON_CONSTANT_BRACE_OPEN '{'
#define JSON_CONSTANT_BRACE_CLOSE '}'
#define JSON_CONSTANT_BRACKET_OPEN '['
#define JSON_CONSTANT_BRACKET_CLOSE ']'
#define JSON_CONSTANT_QUOTE '"'
#define JSON_CONSTANT_NEGATIVE '-'
#define JSON_CONSTANT_LIST_SEPARATOR ','
#define JSON_CONSTANT_DELIMITERS ",}] \n\r\t/"
#define JSON_CONSTANT_ESCAPE_CHARS "nrt/ubf\""
#define JSON_CONSTANT_KEY_VALUE_SEPARATOR ':'
#define JSON_CONSTANT_CONTROL_BACKSLASH_QUOTE \
    "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C" \
    "\x1D\x1E\x1F\\\""

typedef struct JsonError {
    JsonErrorType type;
    size_t line;
    size_t column;
    const char* string;
    unsigned char character;
    int64_t number;
    int slice_length;
} JsonError;

typedef struct JsonStream {
    const char* buffer;
    size_t buffer_size;
    JsonError error;

    void (*error_handler)(struct JsonStream* stream, JsonError* error, void* error_context);

    void* error_context;
    JsonBitStack bits;
    bool is_final_block;
    size_t line_number;
    size_t byte_position_in_line;
    size_t consumed;
    bool in_object;
    bool is_not_primitive;
    JsonType token_type;
    JsonType previous_token_type;
    size_t max_depth;
    bool allow_trailing_commas;
    JsonCommentHandling comment_handling;
    size_t total_consumed;
    bool trailing_comma;

    size_t token_start;
    size_t token_size;

    bool value_is_escaped;
    bool allow_multiple_values;
} JsonStream;

typedef struct JsonStreamOptions {
    bool allow_trailing_commas;
    bool allow_multiple_values;
    JsonCommentHandling comment_handling;
    size_t max_depth;
    void (*error_handler)(struct JsonStream* stream, JsonError* error, void* error_context);
    void* error_context;
} JsonStreamOptions;

void json_stream_init(JsonStream* stream, const char* buffer, size_t buffer_size, bool is_final_block, JsonStreamOptions options);

void json_stream_continue(JsonStream* stream, JsonStream* old, const char* buffer, size_t buffer_size, bool is_final_block);

void json_stream_free_resources(JsonStream* stream);

JsonStreamOptions json_stream_options_default();

static inline size_t json_bytes_consumed(const JsonStream* stream);

static inline size_t json_token_start(const JsonStream* stream);

static inline size_t json_token_size(const JsonStream* stream);

static inline JsonType json_token_type(const JsonStream* stream);

static inline void json_token(const JsonStream* stream, const char** out_token, size_t* out_token_size);

static inline size_t json_current_depth(const JsonStream* stream);

static inline bool json_value_is_escaped(const JsonStream* stream);

static inline bool json_is_final_block(const JsonStream* stream);

bool json_read(JsonStream* stream);

bool json_skip(JsonStream* stream);

bool json_try_skip(JsonStream* stream);

bool json_text_equals(JsonStream* stream, const char* text, size_t length);

char* json_get_string_escaped(JsonStream* stream, char* buffer, size_t buffer_length, size_t* out_length);

char* json_read_string_escaped(JsonStream* stream, char* buffer, size_t buffer_length, size_t* out_length);

bool json_try_get_string_escaped(
    JsonStream* stream,
    char* buffer,
    size_t buffer_length,
    char** out_string,
    size_t* out_length
);

bool json_try_read_string_escaped(
    JsonStream* stream,
    char* buffer,
    size_t buffer_length,
    char** out_string,
    size_t* out_length
);

const char* json_get_string(JsonStream* stream, size_t* out_length);

const char* json_read_string(JsonStream* stream, size_t* out_length);

bool json_try_get_string(JsonStream* stream, const char** out_string, size_t* out_length);

bool json_try_read_string(JsonStream* stream, const char** out_string, size_t* out_length);

const char* json_get_property(JsonStream* stream, size_t* out_length);

const char* json_read_property(JsonStream* stream, size_t* out_length);

bool json_try_get_property(JsonStream* stream, const char** out_property, size_t* out_length);

bool json_try_read_property(JsonStream* stream, const char** out_property, size_t* out_length);

const char* json_get_comment(JsonStream* stream, size_t* out_length);

const char* json_read_comment(JsonStream* stream, size_t* out_length);

bool json_try_get_comment(JsonStream* stream, const char** out_comment, size_t* out_length);

bool json_try_read_comment(JsonStream* stream, const char** out_comment, size_t* out_length);

bool json_get_bool(JsonStream* stream);

bool json_read_bool(JsonStream* stream);

bool json_try_get_bool(JsonStream* stream, bool* out_bool);

bool json_try_read_bool(JsonStream* stream, bool* out_bool);

uint8_t json_get_u8(JsonStream* stream);

uint8_t json_read_u8(JsonStream* stream);

bool json_try_get_u8(JsonStream* stream, uint8_t* out_u8);

bool json_try_read_u8(JsonStream* stream, uint8_t* out_u8);

int8_t json_get_i8(JsonStream* stream);

int8_t json_read_i8(JsonStream* stream);

bool json_try_get_i8(JsonStream* stream, int8_t* out_i8);

bool json_try_read_i8(JsonStream* stream, int8_t* out_i8);

uint16_t json_get_u16(JsonStream* stream);

uint16_t json_read_u16(JsonStream* stream);

bool json_try_get_u16(JsonStream* stream, uint16_t* out_u16);

bool json_try_read_u16(JsonStream* stream, uint16_t* out_u16);

int16_t json_get_i16(JsonStream* stream);

int16_t json_read_i16(JsonStream* stream);

bool json_try_get_i16(JsonStream* stream, int16_t* out_i16);

bool json_try_read_i16(JsonStream* stream, int16_t* out_i16);

uint32_t json_get_u32(JsonStream* stream);

uint32_t json_read_u32(JsonStream* stream);

bool json_try_get_u32(JsonStream* stream, uint32_t* out_u32);

bool json_try_read_u32(JsonStream* stream, uint32_t* out_u32);

int32_t json_get_i32(JsonStream* stream);

int32_t json_read_i32(JsonStream* stream);

bool json_try_get_i32(JsonStream* stream, int32_t* out_i32);

bool json_try_read_i32(JsonStream* stream, int32_t* out_i32);

uint64_t json_get_u64(JsonStream* stream);

uint64_t json_read_u64(JsonStream* stream);

bool json_try_get_u64(JsonStream* stream, uint64_t* out_u64);

bool json_try_read_u64(JsonStream* stream, uint64_t* out_u64);

int64_t json_get_i64(JsonStream* stream);

int64_t json_read_i64(JsonStream* stream);

bool json_try_get_i64(JsonStream* stream, int64_t* out_i64);

bool json_try_read_i64(JsonStream* stream, int64_t* out_i64);

float json_get_float(JsonStream* stream);

float json_read_float(JsonStream* stream);

bool json_try_get_float(JsonStream* stream, float* out_float);

bool json_try_read_float(JsonStream* stream, float* out_float);

double json_get_double(JsonStream* stream);

double json_read_double(JsonStream* stream);

bool json_try_get_double(JsonStream* stream, double* out_double);

bool json_try_read_double(JsonStream* stream, double* out_double);

bool json_read_array_start(JsonStream* stream);

bool json_try_read_array_start(JsonStream* stream);

bool json_read_array_end(JsonStream* stream);

bool json_try_read_array_end(JsonStream* stream);

bool json_read_object_start(JsonStream* stream);

bool json_try_read_object_start(JsonStream* stream);

bool json_read_object_end(JsonStream* stream);

bool json_try_read_object_end(JsonStream* stream);

void json_clear_error(JsonStream* stream);

bool json_error_get_message(const JsonError* error, char* buffer, size_t buffer_length);

static inline bool json_has_error(JsonStream* stream);

static inline void json_get_error(JsonStream* stream, JsonError* out_error);

const char* json_token_type_name(JsonType type);

static inline bool json_is_last_span(const JsonStream* stream) {
    return stream->is_final_block;
}

static inline size_t json_token_size(const JsonStream* stream) {
    return stream->token_size;
}

static inline void json_token(const JsonStream* stream, const char** out_token, size_t* out_token_size) {
    if (stream->token_type != JSON_TYPE_UNKNOWN) {
        *out_token = stream->buffer + stream->token_start;
        *out_token_size = stream->token_size;
    }
}

static inline size_t json_bytes_consumed(const JsonStream* stream) {
    return stream->consumed;
}

static inline size_t json_total_bytes_consumed(const JsonStream* stream) {
    return stream->total_consumed + stream->consumed;
}

static inline size_t json_token_start(const JsonStream* stream) {
    return stream->token_start;
}

static inline JsonType json_token_type(const JsonStream* stream) {
    return stream->token_type;
}

static inline size_t json_current_depth(const JsonStream* stream) {
    size_t depth = json_z_bits_count(&stream->bits);
    if (json_token_type(stream) == JSON_TYPE_ARRAY_START || json_token_type(stream) == JSON_TYPE_OBJECT_START) {
        depth -= 1;
    }

    return depth;
}

static inline bool json_is_in_array(const JsonStream* stream) {
    return !stream->in_object;
}

static inline bool json_value_is_escaped(const JsonStream* stream) {
    return stream->value_is_escaped;
}

static inline bool json_is_final_block(const JsonStream* stream) {
    return stream->is_final_block;
}

static inline bool json_has_error(JsonStream* stream) {
    return stream->error.type != JSON_ERROR_NONE;
}

static inline void json_get_error(JsonStream* stream, JsonError* out_error) {
    *out_error = stream->error;
}

#endif // JSON_STREAM_H
