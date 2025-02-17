//
// Created by Chris Kramer on 2/7/25.
//

#include "json_stream.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "bit_stack.h"

#include <string.h>

typedef enum {
    JSON_CONSUME_NUMBER_SUCCESS,
    JSON_CONSUME_NUMBER_ERROR,
    JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE,
    JSON_CONSUME_NUMBER_NEED_MORE_DATA
} JsonConsumeNumberResult;

typedef enum {
    JSON_CONSUME_TOKEN_SUCCESS,
    JSON_CONSUME_TOKEN_ERROR,
    JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE,
    JSON_CONSUME_TOKEN_INCOMPLETE_NO_ROLLBACK_NECESSARY,
} JsonConsumeTokenResult;

typedef struct JsonRollbackState {
    size_t prev_consumed;
    size_t prev_position;
    size_t prev_line;
    size_t prev_token_start;
    size_t prev_token_size;
    JsonType prev_token_type;
    JsonType prev_prev_token_type;
    bool prev_trailing_comma;
} JsonRollbackState;

#define JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_size, position) \
    ((buffer_size != 0 && ((position) >= buffer_size)) || (buffer[position] == '\0'))

#define JSON_STREAM_OUT_OF_BOUNDS(stream, position) \
    JSON_BUFFER_OUT_OF_BOUNDS(stream->buffer, stream->buffer_size, position)


static bool json_read_single_segment(JsonStream* stream);

static bool json_skip_helper(JsonStream* stream);

static bool json_try_skip_partial(JsonStream* stream, size_t target_depth);

static bool json_unescape_and_compare(JsonStream* stream, const char* text, size_t length);

static bool json_consume_object_start(JsonStream* stream);

static bool json_consume_object_end(JsonStream* stream);

static bool json_consume_array_start(JsonStream* stream);

static bool json_consume_array_end(JsonStream* stream);

static void json_update_bit_stack_on_end_token(JsonStream* stream);

static bool json_has_more_data(JsonStream* stream);

static bool json_has_more_data_specific_error(JsonStream* stream, JsonErrorType type);

static bool json_read_first_token(JsonStream* stream, char first);

static void json_skip_whitespace(JsonStream* stream);

static bool json_consume_next_token_or_rollback(JsonStream* stream, char token);

static JsonConsumeTokenResult json_consume_next_token(JsonStream* stream, char token);

static JsonConsumeTokenResult json_consume_next_token_from_last_non_comment_token(JsonStream* stream);

static bool json_consume_property_name(JsonStream* stream);

static bool json_consume_value(JsonStream* stream, char first);

static bool json_helper_is_digit(char c);

static bool json_helper_is_hex_digit(char c);

static bool json_helper_is_token_type_primitive(JsonType token_type);

static bool json_helper_count_new_lines(
    const char* buffer,
    size_t buffer_size,
    size_t* out_count,
    size_t* out_new_line_index
);

static bool json_consume_string(JsonStream* stream);

static bool json_consume_string_and_validate(JsonStream* stream, const char* buffer, size_t buffer_size, size_t index);

static bool json_validate_hex_digits(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_size,
    size_t index,
    bool* threw
);

static bool json_try_get_number(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_bytes_consumed
);

static bool json_consume_number(JsonStream* stream);

static JsonConsumeNumberResult json_consume_negative_sign(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
);

static JsonConsumeNumberResult json_consume_zero(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
);

static JsonConsumeNumberResult json_consume_integer_digits(
    const JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
);

static JsonConsumeNumberResult json_consume_decimal_digits(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
);

static JsonConsumeNumberResult json_consume_sign(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
);

static bool json_consume_literal(JsonStream* stream, const char* literal, size_t length, JsonType literal_type);

static void json_generate_literal_error(JsonStream* stream, const char* literal, size_t length, JsonType literal_type);

static bool json_consume_comment(JsonStream* stream);

static bool json_skip_comment(JsonStream* stream);

static bool json_skip_all_comments(JsonStream* stream, char* token);

static bool json_skip_all_comments_specific_error(JsonStream* stream, char* token, JsonErrorType type);

static JsonConsumeTokenResult json_consume_next_token_until_all_comments_are_skipped(JsonStream* stream, char token);

static bool json_skip_single_line_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_index
);

static bool json_find_line_separator(JsonStream* stream, const char* buffer, size_t buffer_length, size_t* out_index);

static bool json_skip_multiline_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_index
);

static bool json_consume_single_line_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t previous_consumed
);

static bool json_consume_multiline_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t previous_consumed
);

static void json_rollback_init(const JsonStream* stream, JsonRollbackState* state);

static void json_rollback(JsonStream* stream, const JsonRollbackState* state);

static bool json_unescape(
    JsonStream* stream,
    char* destination,
    size_t destination_length,
    size_t* out_written,
    bool* out_full
);

static inline bool json_is_token_type_string(const JsonStream* stream) {
    return stream->token_type == JSON_TYPE_STRING || stream->token_type == JSON_TYPE_PROPERTY;
}

static void json_throw(JsonStream* stream, JsonErrorType type) {
    stream->error.type = type;
    stream->error.column = stream->byte_position_in_line;
    stream->error.line = stream->line_number;

    if (stream->error_handler) {
        stream->error_handler(stream, &stream->error, stream->error_context);
    }
}

static void json_throw_char(JsonStream* stream, JsonErrorType type, char c) {
    stream->error.character = c;
    json_throw(stream, type);
}

static void json_throw_string(JsonStream* stream, JsonErrorType type, const char* string) {
    stream->error.string = string;
    json_throw(stream, type);
}

static void json_throw_number(JsonStream* stream, JsonErrorType type, int64_t number) {
    stream->error.number = number;
    json_throw(stream, type);
}

static void json_throw_slice(JsonStream* stream, JsonErrorType type, const char* string, int slice_length) {
    stream->error.string = string;
    stream->error.slice_length = slice_length;
    json_throw(stream, type);
}

void json_stream_init(JsonStream* stream, const char* buffer, size_t buffer_size, bool is_final_block, JsonStreamOptions options) {
    stream->buffer = buffer;
    stream->buffer_size = buffer_size;
    stream->error = (JsonError){0};
    stream->error_handler = options.error_handler;
    stream->error_context = options.error_context;
    json_z_bits_init(&stream->bits);
    stream->is_final_block = is_final_block;
    stream->line_number = 0;
    stream->byte_position_in_line = 0;
    stream->consumed = 0;
    stream->in_object = false;
    stream->is_not_primitive = true;
    stream->token_type = JSON_TYPE_UNKNOWN;
    stream->previous_token_type = JSON_TYPE_UNKNOWN;
    stream->max_depth = options.max_depth;
    stream->allow_multiple_values = options.allow_multiple_values;
    stream->allow_trailing_commas = options.allow_trailing_commas;
    stream->comment_handling = options.comment_handling;
    stream->total_consumed = 0;
    stream->trailing_comma = false;
    stream->token_start = 0;
    stream->token_size = 0;
    stream->value_is_escaped = false;
}

void json_stream_continue(JsonStream* stream, JsonStream* old, const char* buffer, size_t buffer_size, bool is_final_block) {
    stream->buffer = buffer;
    stream->buffer_size = buffer_size;
    stream->is_final_block = is_final_block;

    stream->line_number = old->line_number;
    stream->byte_position_in_line = old->byte_position_in_line;
    stream->in_object = old->in_object;
    stream->is_not_primitive = old->is_not_primitive;
    stream->value_is_escaped = old->value_is_escaped;
    stream->trailing_comma = old->trailing_comma;
    stream->token_type = old->token_type;
    stream->previous_token_type = old->previous_token_type;
    stream->allow_trailing_commas = old->allow_trailing_commas;
    stream->comment_handling = old->comment_handling;
    stream->allow_multiple_values = old->allow_multiple_values;
    stream->max_depth = old->max_depth;
    stream->error_handler = old->error_handler;
    stream->error_context = old->error_context;
    stream->bits = old->bits;
    stream->total_consumed = old->total_consumed + old->consumed;
    stream->consumed = 0;
    stream->token_start = 0;
    stream->token_size = 0;
    stream->value_is_escaped = false;
}

JsonStreamOptions json_stream_options_default() {
    JsonStreamOptions options = (JsonStreamOptions){0};

    options.max_depth = 64;

    return options;
}

void json_stream_free_resources(JsonStream* stream) {
    json_z_bits_clear(&stream->bits);
}

bool json_read(JsonStream* stream) {
    bool result = json_read_single_segment(stream);
    if (!result) {
        if (stream->is_final_block && stream->token_type == JSON_TYPE_UNKNOWN && !stream->allow_multiple_values) {
            json_throw(stream, JSON_ERROR_EXPECTED_JSON_TOKENS);
        }
    }

    return result;
}

bool json_skip(JsonStream* stream) {
    if (!stream->is_final_block) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_CANNOT_SKIP_ON_PARTIAL);
        return false;
    }

    return json_skip_helper(stream);
}

bool json_skip_helper(JsonStream* stream) {
    assert(stream->is_final_block);

    if (stream->token_type == JSON_TYPE_PROPERTY) {
        // Since is_final_block == true here, and the JSON token is not a primitive value or comment.
        // Read() is guaranteed to return true OR throw for invalid/incomplete data.
        bool result = json_read(stream);
        if (!result)
            return false;
    }

    if (stream->token_type == JSON_TYPE_ARRAY_START || stream->token_type == JSON_TYPE_OBJECT_START) {
        size_t depth = json_current_depth(stream);
        do {
            bool result = json_read(stream);
            if (!result)
                return false;
        } while (depth < json_current_depth(stream));
    }

    return true;
}

bool json_try_skip(JsonStream* stream) {
    if (stream->is_final_block) {
        return json_skip_helper(stream);
    }

    JsonStream copy = *stream;
    bool result = json_try_skip_partial(stream, json_current_depth(stream));
    if (!result) {
        *stream = copy;
    }

    return result;
}

bool json_try_skip_partial(JsonStream* stream, size_t target_depth) {
    assert(target_depth <= json_current_depth(stream));

    if (target_depth == json_current_depth(stream)) {
        // This is the first call to TrySkipHelper.
        if (stream->token_type == JSON_TYPE_PROPERTY) {
            // Skip any property name tokens preceding the value.
            if (!json_read(stream)) {
                return false;
            }
        }

        if (stream->token_type != JSON_TYPE_ARRAY_START && stream->token_type != JSON_TYPE_OBJECT_START) {
            // The next value is not an object or array, so there is nothing to skip.
            return true;
        }
    }

    do {
        if (!json_read(stream)) {
            return false;
        }
    } while (target_depth < json_current_depth(stream));

    assert(target_depth == json_current_depth(stream));
    return true;
}

bool json_text_equals(JsonStream* stream, const char* text, size_t length) {
    if (!json_is_token_type_string(stream)) {
        json_throw_string(
            stream,
            JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING_COMPARISON,
            json_token_type_name(stream->token_type)
        );
        return false;
    }

    if (stream->value_is_escaped) {
        return json_unescape_and_compare(stream, text, length);
    }

    return strncmp(stream->buffer + stream->token_start, text, length) == 0;
}

static bool json_unescape_and_compare(JsonStream* stream, const char* text, size_t length) {
    json_throw_string(stream, JSON_ERROR_NOT_IMPLEMENTED, __func__);
    return false;
}

static bool json_consume_object_start(JsonStream* stream) {
    if (json_z_bits_count(&stream->bits) >= stream->max_depth) {
        json_throw_number(stream, JSON_ERROR_OBJECT_DEPTH_TOO_LARGE, (int64_t)stream->max_depth);
        return false;
    }

    if (!json_z_bits_push(&stream->bits, true)) {
        json_throw(stream, JSON_ERROR_OUT_OF_MEMORY);
        return false;
    }

    stream->token_start = stream->consumed;
    stream->token_size = 1;
    stream->consumed++;
    stream->byte_position_in_line++;
    stream->token_type = JSON_TYPE_OBJECT_START;
    stream->in_object = true;

    return true;
}

static bool json_consume_object_end(JsonStream* stream) {
    if (!stream->in_object || json_z_bits_count(&stream->bits) <= 0) {
        json_throw_char(stream, JSON_ERROR_MISMATCHED_OBJECT_ARRAY, '}');
        return false;
    }

    if (stream->trailing_comma) {
        if (!stream->allow_trailing_commas) {
            json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END);
            return false;
        }
        stream->trailing_comma = false;
    }

    stream->token_type = JSON_TYPE_OBJECT_END;
    stream->token_start = stream->consumed;
    stream->token_size = 1;

    json_update_bit_stack_on_end_token(stream);
    return true;
}

static bool json_consume_array_start(JsonStream* stream) {
    if (json_z_bits_count(&stream->bits) >= stream->max_depth) {
        json_throw_number(stream, JSON_ERROR_ARRAY_DEPTH_TOO_LARGE, (int64_t)stream->max_depth);
    }

    if (!json_z_bits_push(&stream->bits, false)) {
        json_throw(stream, JSON_ERROR_OUT_OF_MEMORY);
    }

    stream->token_start = stream->consumed;
    stream->token_size = 1;
    stream->consumed++;
    stream->byte_position_in_line++;
    stream->token_type = JSON_TYPE_ARRAY_START;
    stream->in_object = false;

    return true;
}

static bool json_consume_array_end(JsonStream* stream) {
    if (stream->in_object || json_z_bits_count(&stream->bits) <= 0) {
        json_throw_char(stream, JSON_ERROR_MISMATCHED_OBJECT_ARRAY, ']');
        return false;
    }

    if (stream->trailing_comma) {
        if (!stream->allow_trailing_commas) {
            json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END);
            return false;
        }
        stream->trailing_comma = false;
    }
    stream->token_type = JSON_TYPE_ARRAY_END;
    stream->token_start = stream->consumed;
    stream->token_size = 1;
    json_update_bit_stack_on_end_token(stream);

    return true;
}

static void json_update_bit_stack_on_end_token(JsonStream* stream) {
    stream->consumed++;
    stream->byte_position_in_line++;
    stream->in_object = json_z_bits_pop(&stream->bits);
}

static bool json_read_single_segment(JsonStream* stream) {
    bool result = false;
    char first = '\0';
    stream->token_start = 0;
    stream->token_size = 0;
    stream->value_is_escaped = false;

    if (!json_has_more_data(stream)) {
        goto done;
    }

    first = stream->buffer[stream->consumed];

    if (first <= JSON_CONSTANT_SPACE) {
        json_skip_whitespace(stream);
        if (!json_has_more_data(stream)) {
            goto done;
        }
        first = stream->buffer[stream->consumed];
    }

    stream->token_start = stream->consumed;
    if (stream->token_type == JSON_TYPE_UNKNOWN) {
        result = json_read_first_token(stream, first);
        goto done;
    }

    if (first == JSON_CONSTANT_SLASH) {
        result = json_consume_next_token_or_rollback(stream, first);
        goto done;
    }

    if (stream->token_type == JSON_TYPE_OBJECT_START) {
        if (first == JSON_CONSTANT_BRACE_CLOSE) {
            result = json_consume_object_end(stream);
        } else {
            if (first != JSON_CONSTANT_QUOTE) {
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
                goto done;
            }

            size_t prev_consumed = stream->consumed;
            size_t prev_position = stream->byte_position_in_line;
            size_t prev_line = stream->line_number;
            result = json_consume_property_name(stream);
            if (!result) {
                stream->consumed = prev_consumed;
                stream->token_type = JSON_TYPE_OBJECT_START;
                stream->byte_position_in_line = prev_position;
                stream->line_number = prev_line;
            }
        }
    } else if (stream->token_type == JSON_TYPE_ARRAY_START) {
        if (first == JSON_CONSTANT_BRACKET_CLOSE) {
            result = json_consume_array_end(stream);
        } else {
            result = json_consume_value(stream, first);
        }
    } else if (stream->token_type == JSON_TYPE_PROPERTY) {
        result = json_consume_value(stream, first);
    } else {
        result = json_consume_next_token_or_rollback(stream, first);
    }

done:
    return result;
}

static bool json_has_more_data(JsonStream* stream) {
    if ((stream->buffer_size != 0 && stream->consumed >= stream->buffer_size)
        || stream->buffer[stream->consumed] == '\0')
    {
        if (stream->is_not_primitive && json_is_last_span(stream)) {
            if (json_current_depth(stream) != 0) {
                json_throw(stream, JSON_ERROR_ZERO_DEPTH_AT_END);
                return false;
            }

            if (stream->comment_handling == JSON_COMMENT_ALLOW && stream->token_type == JSON_TYPE_COMMENT) {
                return false;
            }

            if (stream->token_type != JSON_TYPE_ARRAY_END && stream->token_type != JSON_TYPE_OBJECT_END) {
                json_throw_string(
                    stream,
                    JSON_ERROR_INVALID_END_OF_JSON_NON_PRIMITIVE,
                    json_token_type_name(stream->token_type)
                );
            }
        }
        return false;
    }

    return true;
}

static bool json_has_more_data_specific_error(JsonStream* stream, JsonErrorType error_type) {
    if ((stream->buffer_size != 0 && stream->consumed >= stream->buffer_size)
        || stream->buffer[stream->consumed] == '\0')
    {
        if (json_is_last_span(stream)) {
            json_throw(stream, error_type);
        }
        return false;
    }

    return true;
}

static bool json_read_first_token(JsonStream* stream, char first) {
    bool result = true;

    if (first == JSON_CONSTANT_BRACE_OPEN) {
        // TODO: SetFirstBit optimization
        result = json_consume_object_start(stream);
        stream->is_not_primitive = true;
    } else if (first == JSON_CONSTANT_BRACKET_OPEN) {
        result = json_consume_array_start(stream);
        stream->is_not_primitive = true;
    } else {
        if (json_helper_is_digit(first) || first == JSON_CONSTANT_NEGATIVE) {
            size_t bytes_consumed;
            result = json_try_get_number(
                stream,
                stream->buffer + stream->consumed,
                stream->buffer_size == 0 ? 0 : stream->buffer_size - stream->consumed,
                &bytes_consumed
            );
            if (!result) {
                return false;
            }
            stream->token_type = JSON_TYPE_NUMBER;
            stream->consumed += bytes_consumed;
            stream->byte_position_in_line += bytes_consumed;
        } else if (!json_consume_value(stream, first)) {
            return false;
        }

        stream->is_not_primitive =
            stream->token_type == JSON_TYPE_OBJECT_START || stream->token_type == JSON_TYPE_ARRAY_START;
    }

    return result;
}

static void json_skip_whitespace(JsonStream* stream) {
    while (!JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
        switch (stream->buffer[stream->consumed]) {
            case '\n':
                stream->consumed++;
                stream->line_number++;
                stream->byte_position_in_line = 0;
                break;
            case ' ':
            case '\r':
            case '\t':
                stream->consumed++;
                stream->byte_position_in_line++;
                break;
            default:
                return;
        }
    }
}

static bool json_consume_value(JsonStream* stream, char first) {
    while (true) {
        assert((stream->trailing_comma && stream->comment_handling == JSON_COMMENT_ALLOW) || !stream->trailing_comma);
        assert((stream->trailing_comma && first != JSON_CONSTANT_SLASH) || !stream->trailing_comma);
        stream->trailing_comma = false;

        switch (first) {
            case JSON_CONSTANT_QUOTE:
                return json_consume_string(stream);
            case JSON_CONSTANT_BRACE_OPEN:
                return json_consume_object_start(stream);
            case JSON_CONSTANT_BRACKET_OPEN:
                return json_consume_array_start(stream);
            case 'f':
                return json_consume_literal(stream, "false", 5, JSON_TYPE_BOOLEAN);
            case 't':
                return json_consume_literal(stream, "true", 4, JSON_TYPE_BOOLEAN);
            case 'n':
                return json_consume_literal(stream, "null", 4, JSON_TYPE_NULL);
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
                return json_consume_number(stream);
            case '/':
                switch (stream->comment_handling) {
                    case JSON_COMMENT_DISALLOW:
                        break;
                    case JSON_COMMENT_ALLOW:
                        if (first == JSON_CONSTANT_SLASH) {
                            return json_consume_comment(stream);
                        }
                        break;
                    default:
                        assert(stream->comment_handling == JSON_COMMENT_SKIP);
                        if (first == JSON_CONSTANT_SLASH) {
                            if (json_consume_comment(stream)) {
                                if (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
                                    if (stream->is_not_primitive && json_is_last_span(stream)
                                        && stream->token_type != JSON_TYPE_ARRAY_END
                                        && stream->token_type != JSON_TYPE_OBJECT_END)
                                    {
                                        json_throw_string(
                                            stream,
                                            JSON_ERROR_INVALID_END_OF_JSON_NON_PRIMITIVE,
                                            json_token_type_name(stream->token_type)
                                        );
                                    }
                                    return false;
                                }
                                first = stream->buffer[stream->consumed];

                                if (first <= JSON_CONSTANT_SPACE) {
                                    json_skip_whitespace(stream);
                                    if (!json_has_more_data(stream)) {
                                        return false;
                                    }
                                    first = stream->buffer[stream->consumed];
                                }

                                stream->token_start = stream->consumed;
                                continue;
                            }
                            return false;
                        }
                        break;
                }
            default:
                break;
        }
        json_throw_char(stream, JSON_ERROR_EXPECTED_START_OF_VALUE_NOT_FOUND, first);
        return false;
    }
}

static bool json_consume_literal(JsonStream* stream, const char* literal, size_t length, JsonType literal_type) {
    if ((stream->buffer_size != 0 && stream->buffer_size - stream->consumed < length)
        || strncmp(stream->buffer + stream->consumed, literal, length) != 0)
    {
        json_generate_literal_error(stream, literal, length, literal_type);
        return false;
    }

    stream->token_start = stream->consumed;
    stream->token_size = length;
    stream->consumed += length;
    stream->token_type = literal_type;
    stream->byte_position_in_line += length;
    return true;
}

static void json_generate_literal_error(JsonStream* stream, const char* literal, size_t length, JsonType literal_type) {
    // TODO: Move stream->consumed to first mismatched character.
    switch (literal[0]) {
        case 'f':
            json_throw_slice(stream, JSON_ERROR_EXPECTED_FALSE, stream->buffer + stream->consumed, 5);
            break;
        case 't':
            json_throw_slice(stream, JSON_ERROR_EXPECTED_TRUE, stream->buffer + stream->consumed, 4);
            break;
        case 'n':
            json_throw_slice(stream, JSON_ERROR_EXPECTED_NULL, stream->buffer + stream->consumed, 4);
            break;
        default:
            break;
    }
}

static bool json_consume_number(JsonStream* stream) {
    size_t bytes_consumed;
    if (!json_try_get_number(
            stream,
            stream->buffer + stream->consumed,
            stream->buffer_size == 0 ? 0 : stream->buffer_size - stream->consumed,
            &bytes_consumed
        ))
    {
        return false;
    }

    stream->token_type = JSON_TYPE_NUMBER;
    stream->consumed += bytes_consumed;
    stream->byte_position_in_line += bytes_consumed;

    if (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
        assert(json_is_last_span(stream));

        if (stream->is_not_primitive) {
            json_throw_char(stream, JSON_ERROR_EXPECTED_END_OF_DIGIT_NOT_FOUND, stream->buffer[stream->consumed]);
            return false;
        }
    }

    bool check = (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed) && !stream->is_not_primitive
                  && strchr(JSON_CONSTANT_DELIMITERS, stream->buffer[stream->consumed]) != NULL)
        || (stream->is_not_primitive ^ (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)));

    assert(check);

    return true;
}

static bool json_consume_property_name(JsonStream* stream) {
    stream->trailing_comma = false;

    if (!json_consume_string(stream)) {
        return false;
    }

    if (!json_has_more_data_specific_error(stream, JSON_ERROR_EXPECTED_VALUE_AFTER_PROPERTY_NAME_NOT_FOUND)) {
        return false;
    }

    char first = stream->buffer[stream->consumed];

    if (first <= JSON_CONSTANT_SPACE) {
        json_skip_whitespace(stream);
        if (!json_has_more_data_specific_error(stream, JSON_ERROR_EXPECTED_VALUE_AFTER_PROPERTY_NAME_NOT_FOUND)) {
            return false;
        }

        first = stream->buffer[stream->consumed];
    }

    if (first != JSON_CONSTANT_KEY_VALUE_SEPARATOR) {
        json_throw_char(stream, JSON_ERROR_EXPECTED_SEPARATOR_AFTER_PROPERTY_NAME_NOT_FOUND, first);
        return false;
    }

    stream->consumed++;
    stream->byte_position_in_line++;
    stream->token_type = JSON_TYPE_PROPERTY;
    return true;
}

static bool json_consume_string(JsonStream* stream) {
    assert(stream->buffer[stream->consumed] == JSON_CONSTANT_QUOTE);
    const char* buffer = stream->buffer + stream->consumed + 1;
    size_t buffer_size = stream->buffer_size == 0 ? 0 : stream->buffer_size - stream->consumed - 1;
    // TODO: Implement search function to make unrolling/vectorization possible.
    char* first = strpbrk(buffer, JSON_CONSTANT_CONTROL_BACKSLASH_QUOTE);
    if (first) {
        size_t index = first - buffer;
        if (buffer[index] == JSON_CONSTANT_QUOTE) {
            stream->byte_position_in_line += index + 2;
            stream->token_start = stream->consumed + 1;
            stream->token_size = index;
            stream->value_is_escaped = false;
            stream->token_type = JSON_TYPE_STRING;
            stream->consumed += index + 2;
            return true;
        } else {
            return json_consume_string_and_validate(stream, buffer, buffer_size, index);
        }
    } else {
        if (json_is_last_span(stream)) {
            stream->byte_position_in_line += buffer_size == 0 ? strlen(buffer) : buffer_size;
            json_throw(stream, JSON_ERROR_END_OF_STRING_NOT_FOUND);
        }
        return false;
    }
}

static bool json_consume_string_and_validate(JsonStream* stream, const char* buffer, size_t buffer_size, size_t index) {
    assert(buffer_size == 0 || index < buffer_size);
    assert(buffer[index] != JSON_CONSTANT_QUOTE);
    assert(buffer[index] == JSON_CONSTANT_BACKSLASH || buffer[index] < JSON_CONSTANT_SPACE);

    size_t prev_position = stream->byte_position_in_line;
    size_t prev_line = stream->line_number;

    stream->byte_position_in_line += index + 1;

    bool next_char_escaped = false;
    for (; !JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_size, index); index++) {
        char current_byte = buffer[index];
        if (current_byte == JSON_CONSTANT_QUOTE) {
            if (!next_char_escaped) {
                goto done;
            }
            next_char_escaped = false;
        } else if (current_byte == JSON_CONSTANT_BACKSLASH) {
            next_char_escaped = !next_char_escaped;
        } else if (next_char_escaped) {
            char* escape = strchr(JSON_CONSTANT_ESCAPE_CHARS, current_byte);
            if (!escape) {
                json_throw_char(stream, JSON_ERROR_INVALID_CHARACTER_AFTER_ESCAPE_WITHIN_STRING, current_byte);
                goto error;
            }

            if (current_byte == 'u') {
                stream->byte_position_in_line++;
                bool threw;
                if (json_validate_hex_digits(stream, buffer, buffer_size, index + 1, &threw)) {
                    index += 4;
                } else if (threw) {
                    goto error;
                } else {
                    break;
                }
            }
            next_char_escaped = false;
        } else if (current_byte < JSON_CONSTANT_SPACE) {
            json_throw_char(stream, JSON_ERROR_INVALID_CHARACTER_WITHIN_STRING, current_byte);
            goto error;
        }
    }

    if (json_is_last_span(stream)) {
        json_throw(stream, JSON_ERROR_END_OF_STRING_NOT_FOUND);
        goto error;
    }

    stream->byte_position_in_line = prev_position;
    stream->line_number = prev_line;
    return false;

error:
    stream->byte_position_in_line = prev_position;
    stream->line_number = prev_line;
    return false;
done:
    stream->byte_position_in_line++;
    stream->token_start = stream->consumed + 1;
    stream->token_size = index;
    stream->token_type = JSON_TYPE_STRING;
    stream->value_is_escaped = true;
    stream->consumed += index + 2;
    return true;
}

bool json_validate_hex_digits(JsonStream* stream, const char* buffer, size_t buffer_size, size_t index, bool* threw) {
    *threw = false;
    for (size_t j = index; !JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_size, j); j++) {
        char next_byte = buffer[j];
        if (!json_helper_is_hex_digit(next_byte)) {
            json_throw_char(stream, JSON_ERROR_INVALID_HEX_CHARACTER_WITHIN_STRING, next_byte);
            *threw = true;
            return false;
        }
        if (j - index >= 3) {
            return true;
        }
        stream->byte_position_in_line++;
    }

    return false;
}

static bool json_try_get_number(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_bytes_consumed
) {
    *out_bytes_consumed = 0;
    size_t index = 0;

    JsonConsumeNumberResult sign_result = json_consume_negative_sign(stream, buffer, buffer_length, &index);
    if (sign_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
        return false;
    }

    assert(sign_result == JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE);

    char next = buffer[index];

    assert(next >= '0' && next <= '9');

    if (next == '0') {
        JsonConsumeNumberResult zero_result = json_consume_zero(stream, buffer, buffer_length, &index);
        if (zero_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
            return false;
        }
        if (zero_result == JSON_CONSUME_NUMBER_SUCCESS) {
            goto done;
        }
        next = buffer[index];
    } else {
        index++;
        JsonConsumeNumberResult number_result = json_consume_integer_digits(stream, buffer, buffer_length, &index);
        if (number_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
            return false;
        }
        if (number_result == JSON_CONSUME_NUMBER_SUCCESS) {
            goto done;
        }

        next = buffer[index];
        if (next != '.' && next != 'e' && next != 'E') {
            stream->byte_position_in_line += index;
            json_throw_char(stream, JSON_ERROR_EXPECTED_END_OF_DIGIT_NOT_FOUND, next);
            return false;
        }
    }

    assert(next == '.' || next == 'e' || next == 'E');

    if (next == '.') {
        index++;
        JsonConsumeNumberResult decimal_result = json_consume_decimal_digits(stream, buffer, buffer_length, &index);
        if (decimal_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
            return false;
        }
        if (decimal_result == JSON_CONSUME_NUMBER_SUCCESS) {
            goto done;
        }

        next = buffer[index];
        if (next != 'e' && next != 'E') {
            stream->byte_position_in_line += index;
            json_throw_char(stream, JSON_ERROR_EXPECTED_NEXT_DIGIT_E_VALUE_NOT_FOUND, next);
            return false;
        }
    }

    assert(next == 'e' || next == 'E');
    index++;

    sign_result = json_consume_sign(stream, buffer, buffer_length, &index);
    if (sign_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
        return false;
    }

    assert(sign_result == JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE);

    index++;
    JsonConsumeNumberResult exponent_result = json_consume_integer_digits(stream, buffer, buffer_length, &index);
    if (exponent_result == JSON_CONSUME_NUMBER_NEED_MORE_DATA) {
        return false;
    }
    if (exponent_result == JSON_CONSUME_NUMBER_SUCCESS) {
        goto done;
    }

    stream->byte_position_in_line += index;
    json_throw_char(stream, JSON_ERROR_EXPECTED_END_OF_DIGIT_NOT_FOUND, buffer[index]);
    return false;

done:
    stream->token_size = index;
    *out_bytes_consumed = index;
    return true;
}

static JsonConsumeNumberResult json_consume_negative_sign(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
) {
    char next = buffer[*index];

    if (next == '-') {
        (*index)++;
        if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
            if (json_is_last_span(stream)) {
                stream->byte_position_in_line += *index;
                json_throw(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA);
                return JSON_CONSUME_NUMBER_ERROR;
            }
            return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
        }

        next = buffer[*index];
        if (!json_helper_is_digit(next)) {
            stream->byte_position_in_line += *index;
            json_throw_char(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_SIGN, next);
            return JSON_CONSUME_NUMBER_ERROR;
        }
    }
    return JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE;
}

static JsonConsumeNumberResult json_consume_zero(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
) {
    assert(buffer[*index] == '0');
    (*index)++;
    char next;

    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
        if (json_is_last_span(stream)) {
            return JSON_CONSUME_NUMBER_SUCCESS;
        } else {
            return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
        }
    } else {
        next = buffer[*index];
        if (strchr(JSON_CONSTANT_DELIMITERS, next) != NULL) {
            return JSON_CONSUME_NUMBER_SUCCESS;
        }
    }
    if (next != '.' && next != 'e' && next != 'E') {
        stream->byte_position_in_line += *index;
        json_throw_slice(stream, JSON_ERROR_INVALID_LEADING_ZERO_IN_NUMBER, buffer, (int)*index);
    }

    return JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE;
}

static JsonConsumeNumberResult json_consume_integer_digits(
    const JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
) {
    char next = 0;
    for (; !JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index); (*index)++) {
        next = buffer[*index];
        if (!json_helper_is_digit(next)) {
            break;
        }
    }

    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
        if (json_is_last_span(stream)) {
            return JSON_CONSUME_NUMBER_SUCCESS;
        } else {
            return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
        }
    }

    if (strchr(JSON_CONSTANT_DELIMITERS, next) != NULL) {
        return JSON_CONSUME_NUMBER_SUCCESS;
    }

    return JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE;
}

static JsonConsumeNumberResult json_consume_decimal_digits(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
) {
    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
        if (json_is_last_span(stream)) {
            stream->byte_position_in_line += *index;
            json_throw(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA);
            return JSON_CONSUME_NUMBER_ERROR;
        }
        return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
    }
    char next = buffer[*index];
    if (!json_helper_is_digit(next)) {
        stream->byte_position_in_line += *index;
        json_throw_char(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_DECIMAL, next);
        return JSON_CONSUME_NUMBER_ERROR;
    }

    (*index)++;
    return json_consume_integer_digits(stream, buffer, buffer_length, index);
}

static JsonConsumeNumberResult json_consume_sign(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* index
) {
    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
        if (json_is_last_span(stream)) {
            stream->byte_position_in_line += *index;
            json_throw(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA);
            return JSON_CONSUME_NUMBER_ERROR;
        }
        return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
    }

    char next = buffer[*index];

    if (next == '+' || next == '-') {
        (*index)++;
        if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, *index)) {
            if (json_is_last_span(stream)) {
                stream->byte_position_in_line += *index;
                json_throw(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA);
                return JSON_CONSUME_NUMBER_ERROR;
            }
            return JSON_CONSUME_NUMBER_NEED_MORE_DATA;
        }
        next = buffer[*index];
    }

    if (!json_helper_is_digit(next)) {
        stream->byte_position_in_line += *index;
        json_throw_char(stream, JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_SIGN, next);
        return JSON_CONSUME_NUMBER_ERROR;
    }

    return JSON_CONSUME_NUMBER_OPERATION_INCOMPLETE;
}

static bool json_consume_next_token_or_rollback(JsonStream* stream, char token) {
    size_t prev_consumed = stream->consumed;
    size_t prev_position = stream->byte_position_in_line;
    size_t prev_line = stream->line_number;
    JsonType prev_token_type = stream->token_type;
    bool prev_trailing_comma = stream->trailing_comma;

    JsonConsumeTokenResult result = json_consume_next_token(stream, token);

    if (result == JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE) {
        stream->consumed = prev_consumed;
        stream->byte_position_in_line = prev_position;
        stream->line_number = prev_line;
        stream->token_type = prev_token_type;
        stream->trailing_comma = prev_trailing_comma;
    }

    return result == JSON_CONSUME_TOKEN_SUCCESS;
}

static JsonConsumeTokenResult json_consume_next_token(JsonStream* stream, char token) {
    if (stream->comment_handling != JSON_COMMENT_DISALLOW) {
        if (stream->comment_handling == JSON_COMMENT_ALLOW) {
            if (token == JSON_CONSTANT_SLASH) {
                return json_consume_comment(stream) ? JSON_CONSUME_TOKEN_SUCCESS
                                                    : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
            }

            if (stream->token_type == JSON_TYPE_COMMENT) {
                return json_consume_next_token_from_last_non_comment_token(stream);
            }
        } else {
            return json_consume_next_token_until_all_comments_are_skipped(stream, token);
        }
    }

    if (json_z_bits_count(&stream->bits) == 0) {
        if (stream->allow_multiple_values) {
            return json_read_first_token(stream, token) ? JSON_CONSUME_TOKEN_SUCCESS
                                                        : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
        json_throw_char(stream, JSON_ERROR_EXPECTED_END_AFTER_SINGLE_JSON, token);
        return JSON_CONSUME_TOKEN_ERROR;
    }

    if (token == JSON_CONSTANT_LIST_SEPARATOR) {
        stream->consumed++;
        stream->byte_position_in_line++;
        if (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
            if (json_is_last_span(stream)) {
                stream->consumed--;
                stream->byte_position_in_line--;
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            return JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
        char first = stream->buffer[stream->consumed];

        if (first <= JSON_CONSTANT_SPACE) {
            json_skip_whitespace(stream);
            if (!json_has_more_data_specific_error(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND)) {
                return JSON_CONSUME_TOKEN_ERROR;
            }
            first = stream->buffer[stream->consumed];
        }

        stream->token_start = stream->consumed;

        if (stream->comment_handling == JSON_COMMENT_ALLOW && first == JSON_CONSTANT_SLASH) {
            stream->trailing_comma = true;
            return json_consume_comment(stream) ? JSON_CONSUME_TOKEN_SUCCESS
                                                : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }

        if (stream->in_object) {
            if (first != JSON_CONSTANT_QUOTE) {
                if (first == JSON_CONSTANT_BRACE_CLOSE) {
                    if (stream->allow_trailing_commas) {
                        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                    }
                    json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END);
                    return JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw_char(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND, first);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            return json_consume_property_name(stream) ? JSON_CONSUME_TOKEN_SUCCESS
                                                      : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        } else {
            if (first == JSON_CONSTANT_BRACKET_CLOSE) {
                if (stream->allow_trailing_commas) {
                    return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            return json_consume_value(stream, first) ? JSON_CONSUME_TOKEN_SUCCESS
                                                     : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
    } else if (token == JSON_CONSTANT_BRACE_CLOSE) {
        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else if (token == JSON_CONSTANT_BRACKET_CLOSE) {
        return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else {
        json_throw_char(stream, JSON_ERROR_FOUND_INVALID_CHARACTER, token);
        return JSON_CONSUME_TOKEN_ERROR;
    }
}

static JsonConsumeTokenResult json_consume_next_token_from_last_non_comment_token(JsonStream* stream) {
    assert(stream->comment_handling == JSON_COMMENT_ALLOW);
    assert(stream->token_type == JSON_TYPE_COMMENT);

    if (json_helper_is_token_type_primitive(stream->previous_token_type)) {
        stream->token_type = stream->in_object ? JSON_TYPE_OBJECT_START : JSON_TYPE_ARRAY_START;
    } else {
        stream->token_type = stream->previous_token_type;
    }

    assert(stream->token_type != JSON_TYPE_COMMENT);

    if (!json_has_more_data(stream)) {
        goto roll_back;
    }

    char first = stream->buffer[stream->consumed];

    if (first <= JSON_CONSTANT_SPACE) {
        json_skip_whitespace(stream);
        if (!json_has_more_data(stream)) {
            goto roll_back;
        }
        first = stream->buffer[stream->consumed];
    }

    if (json_z_bits_count(&stream->bits) == 0 && stream->token_type != JSON_TYPE_UNKNOWN) {
        if (stream->allow_multiple_values) {
            return json_read_first_token(stream, first) ? JSON_CONSUME_TOKEN_SUCCESS
                                                        : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
        json_throw_char(stream, JSON_ERROR_EXPECTED_END_AFTER_SINGLE_JSON, first);
        return JSON_CONSUME_TOKEN_ERROR;
    }

    assert(first != JSON_CONSTANT_SLASH);
    stream->token_start = stream->consumed;

    if (first == JSON_CONSTANT_LIST_SEPARATOR) {
        if (stream->previous_token_type == JSON_TYPE_UNKNOWN || stream->token_type == JSON_TYPE_OBJECT_START
            || stream->token_type == JSON_TYPE_ARRAY_START || stream->trailing_comma)
        {
            json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND);
            return JSON_CONSUME_TOKEN_ERROR;
        }

        stream->consumed++;
        stream->byte_position_in_line++;

        if (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
            if (json_is_last_span(stream)) {
                stream->consumed--;
                stream->byte_position_in_line--;
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            goto roll_back;
        }

        first = stream->buffer[stream->consumed];

        if (first <= JSON_CONSTANT_SLASH) {
            json_skip_whitespace(stream);
            if (!json_has_more_data_specific_error(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND)) {
                goto roll_back;
            }
            first = stream->buffer[stream->consumed];
        }

        stream->token_start = stream->consumed;

        if (first == JSON_CONSTANT_SLASH) {
            stream->trailing_comma = true;
            if (json_consume_comment(stream)) {
                goto done;
            } else {
                goto roll_back;
            }
        }

        if (stream->in_object) {
            if (first != JSON_CONSTANT_QUOTE) {
                if (first == JSON_CONSTANT_BRACE_CLOSE) {
                    if (stream->allow_trailing_commas) {
                        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                    }
                    json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END);
                    return JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            if (json_consume_property_name(stream)) {
                goto done;
            } else {
                goto roll_back;
            }
        } else {
            if (first == JSON_CONSTANT_BRACKET_CLOSE) {
                if (stream->allow_trailing_commas) {
                    return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END);
                return JSON_CONSUME_TOKEN_ERROR;
            }

            if (json_consume_value(stream, first)) {
                goto done;
            } else {
                goto roll_back;
            }
        }
    } else if (first == JSON_CONSTANT_BRACE_CLOSE) {
        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else if (first == JSON_CONSTANT_BRACKET_CLOSE) {
        return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else if (stream->token_type == JSON_TYPE_UNKNOWN) {
        if (json_read_first_token(stream, first)) {
            goto done;
        } else {
            goto roll_back;
        }
    } else if (stream->token_type == JSON_TYPE_OBJECT_START) {
        assert(first != JSON_CONSTANT_BRACE_CLOSE);
        if (first != JSON_CONSTANT_QUOTE) {
            json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
            return JSON_CONSUME_TOKEN_ERROR;
        }

        size_t prev_consumed = stream->consumed;
        size_t prev_position = stream->byte_position_in_line;
        size_t prev_line = stream->line_number;
        if (!json_consume_property_name(stream)) {
            stream->consumed = prev_consumed;
            stream->byte_position_in_line = prev_position;
            stream->line_number = prev_line;
            stream->token_type = JSON_TYPE_OBJECT_START;
            goto roll_back;
        }
        goto done;
    } else if (stream->token_type == JSON_TYPE_ARRAY_START) {
        assert(first != JSON_CONSTANT_BRACKET_CLOSE);
        if (!json_consume_value(stream, first)) {
            goto roll_back;
        }
        goto done;
    } else if (stream->token_type == JSON_TYPE_PROPERTY) {
        if (!json_consume_value(stream, first)) {
            goto roll_back;
        }
        goto done;
    } else {
        assert(stream->token_type == JSON_TYPE_ARRAY_END || stream->token_type == JSON_TYPE_OBJECT_END);
        if (stream->in_object) {
            assert(first != JSON_CONSTANT_BRACE_CLOSE);

            if (first != JSON_CONSTANT_QUOTE) {
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }

            if (json_consume_property_name(stream)) {
                goto done;
            } else {
                goto roll_back;
                ;
            }
        } else {
            assert(first != JSON_CONSTANT_BRACKET_CLOSE);
            if (json_consume_value(stream, first)) {
                goto done;
            } else {
                goto roll_back;
            }
        }
    }

done:
    return JSON_CONSUME_TOKEN_SUCCESS;

roll_back:
    return JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
}

static bool json_skip_all_comments(JsonStream* stream, char* token) {
    while (*token == JSON_CONSTANT_SLASH) {
        if (json_skip_comment(stream)) {
            if (!json_has_more_data(stream)) {
                goto incomplete_no_rollback;
            }

            *token = stream->buffer[stream->consumed];

            if (*token <= JSON_CONSTANT_SPACE) {
                json_skip_whitespace(stream);
                if (!json_has_more_data(stream)) {
                    goto incomplete_no_rollback;
                }
                *token = stream->buffer[stream->consumed];
            }
        } else {
            goto incomplete_no_rollback;
        }
    }

    return true;

incomplete_no_rollback:
    return false;
}

static bool json_skip_all_comments_specific_error(JsonStream* stream, char* token, JsonErrorType type) {
    while (*token == JSON_CONSTANT_SLASH) {
        if (json_skip_comment(stream)) {
            if (!json_has_more_data_specific_error(stream, type)) {
                goto incomplete_no_rollback;
            }

            *token = stream->buffer[stream->consumed];

            if (*token <= JSON_CONSTANT_SPACE) {
                json_skip_whitespace(stream);
                if (!json_has_more_data_specific_error(stream, type)) {
                    goto incomplete_no_rollback;
                }
                *token = stream->buffer[stream->consumed];
            }
        } else {
            goto incomplete_no_rollback;
        }
    }

    return true;

incomplete_no_rollback:
    return false;
}

static JsonConsumeTokenResult json_consume_next_token_until_all_comments_are_skipped(JsonStream* stream, char token) {
    if (!json_skip_all_comments(stream, &token)) {
        goto incomplete_no_rollback;
    }

    stream->token_start = stream->consumed;

    if (stream->token_start == JSON_TYPE_OBJECT_START) {
        if (token == JSON_CONSTANT_BRACE_CLOSE) {
            return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
        } else {
            if (token == JSON_CONSTANT_QUOTE) {
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
                goto incomplete_no_rollback;
            }

            size_t prev_consumed = stream->consumed;
            size_t prev_position = stream->byte_position_in_line;
            size_t prev_line = stream->line_number;
            if (!json_consume_property_name(stream)) {
                stream->consumed = prev_consumed;
                stream->byte_position_in_line = prev_position;
                stream->line_number = prev_line;
                stream->token_type = JSON_TYPE_OBJECT_START;
                goto incomplete_no_rollback;
                ;
            }
            goto done;
        }
    } else if (stream->token_type == JSON_TYPE_ARRAY_START) {
        if (token == JSON_CONSTANT_BRACKET_CLOSE) {
            return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
        } else {
            if (!json_consume_value(stream, token)) {
                goto incomplete_no_rollback;
            }
            goto done;
        }
    } else if (stream->token_type == JSON_TYPE_PROPERTY) {
        if (!json_consume_value(stream, token)) {
            goto incomplete_no_rollback;
        }
        goto done;
    } else if (json_z_bits_count(&stream->bits) == 0) {
        if (stream->allow_multiple_values) {
            return json_read_first_token(stream, token) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
        json_throw_char(stream, JSON_ERROR_EXPECTED_END_AFTER_SINGLE_JSON, token);
        return JSON_CONSUME_TOKEN_ERROR;
    } else if (token == JSON_CONSTANT_LIST_SEPARATOR) {
        stream->consumed++;
        stream->byte_position_in_line++;

        if (JSON_STREAM_OUT_OF_BOUNDS(stream, stream->consumed)) {
            if (json_is_last_span(stream)) {
                stream->consumed--;
                stream->byte_position_in_line--;
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            return JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }

        token = stream->buffer[stream->consumed];

        if (token <= JSON_CONSTANT_SPACE) {
            json_skip_whitespace(stream);
            if (!json_has_more_data_specific_error(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND)) {
                return JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
            }
            token = stream->buffer[stream->consumed];
        }

        if (!json_skip_all_comments_specific_error(
                stream,
                &token,
                JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND
            ))
        {
            goto incomplete_rollback;
        }

        stream->token_start = stream->consumed;

        if (stream->in_object) {
            if (token != JSON_CONSTANT_QUOTE) {
                if (token == JSON_CONSTANT_BRACE_CLOSE) {
                    if (stream->allow_trailing_commas) {
                        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                    }
                    json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END);
                    return JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw(stream, JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND);
                return JSON_CONSUME_TOKEN_ERROR;
            }
            return json_consume_property_name(stream) ? JSON_CONSUME_TOKEN_SUCCESS
                                                      : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        } else {
            if (token == JSON_CONSTANT_BRACKET_CLOSE) {
                if (stream->allow_trailing_commas) {
                    return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
                }
                json_throw(stream, JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END);
                return JSON_CONSUME_TOKEN_ERROR;
            }

            return json_consume_value(stream, token) ? JSON_CONSUME_TOKEN_SUCCESS
                                                     : JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
        }
    } else if (token == JSON_CONSTANT_BRACE_CLOSE) {
        return json_consume_object_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else if (token == JSON_CONSTANT_BRACKET_CLOSE) {
        return json_consume_array_end(stream) ? JSON_CONSUME_TOKEN_SUCCESS : JSON_CONSUME_TOKEN_ERROR;
    } else {
        json_throw_char(stream, JSON_ERROR_FOUND_INVALID_CHARACTER, token);
        return JSON_CONSUME_TOKEN_ERROR;
    }

done:
    return JSON_CONSUME_TOKEN_SUCCESS;
incomplete_rollback:
    return JSON_CONSUME_TOKEN_NOT_ENOUGH_DATA_ROLLBACK_STATE;
incomplete_no_rollback:
    return JSON_CONSUME_TOKEN_INCOMPLETE_NO_ROLLBACK_NECESSARY;
}

static bool json_skip_comment(JsonStream* stream) {
    const char* buffer = stream->buffer + stream->consumed + 1;
    size_t buffer_length = stream->buffer_size == 0 ? 0 : (stream->buffer_size - stream->consumed - 1);

    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, 0)) {
        json_throw_char(stream, JSON_ERROR_EXPECTED_START_OF_VALUE_NOT_FOUND, buffer[0]);
        return false;
    }

    char token = buffer[0];

    buffer++;
    buffer_length = buffer_length == 0 ? 0 : buffer_length - 1;

    if (token == JSON_CONSTANT_SLASH) {
        return json_skip_single_line_comment(stream, buffer, buffer_length, NULL);
    } else if (token == JSON_CONSTANT_ASTERISK) {
        return json_skip_multiline_comment(stream, buffer, buffer_length, NULL);
    } else {
        json_throw_char(stream, JSON_ERROR_EXPECTED_START_OF_VALUE_NOT_FOUND, token);
        return false;
    }
}

static bool json_skip_single_line_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_index
) {
    size_t index;
    size_t to_consume;

    if (!json_find_line_separator(stream, buffer, buffer_length, &index)) {
        if (json_is_last_span(stream)) {
            // Assume everything on this line is a comment and there is no more data.
            index = buffer_length == 0 ? strlen(buffer) : buffer_length;
            to_consume = index;
            stream->byte_position_in_line += index + 2;
            goto done;
        }

        // Might be a line feed in the next segment
        return false;
    }

    to_consume = index;
    if (buffer[index] == JSON_CONSTANT_LINE_FEED) {
        goto end_of_comment;
    }

    // If we're here, we have definitely found a \r. Check to see if a \n follows.
    assert(buffer[index] == JSON_CONSTANT_CARRIAGE_RETURN);

    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, index + 1)) {
        if (!json_is_last_span(stream)) {
            // LF could be in next segment
            return false;
        }
    } else if (buffer[index + 1] == JSON_CONSTANT_LINE_FEED) {
        to_consume++;
    }

end_of_comment:
    to_consume++;
    stream->byte_position_in_line = 0;
    stream->line_number++;
done:
    stream->consumed += to_consume + 2;
    if (out_index) {
        *out_index = index;
    }
    return true;
}

static bool json_find_line_separator(JsonStream* stream, const char* buffer, size_t buffer_length, size_t* out_index) {
    size_t total_index = 0;
    while (true) {
        const char* char_index = strpbrk(buffer, "\r\n\xE2");
        if (!char_index || (buffer_length != 0 && total_index + (char_index - buffer) >= buffer_length)) {
            return false;
        }

        total_index += char_index - buffer;

        if (*char_index != JSON_CONSTANT_STARTING_BYTE_OF_NON_STANDARD_LINE_SEPARATOR) {
            *out_index = total_index;
            return true;
        }

        total_index++;
        buffer_length = buffer_length == 0 ? 0 : (buffer_length - (char_index - buffer) - 1);
        buffer = char_index + 1;

        if (!JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, 2)) {
            if (buffer[0] == '\x80' && (buffer[1] == '\xA8' || buffer[1] == '\xA9')) {
                json_throw(stream, JSON_ERROR_UNEXPECTED_END_OF_LINE_SEPARATOR);
                return false;
            }
        }
    }
}

static bool json_skip_multiline_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t* out_index
) {
    size_t index = 0;
    while (true) {
        const char* char_index = buffer_length == 0
            ? strchr(buffer + index, JSON_CONSTANT_SLASH)
            : memchr(buffer + index, JSON_CONSTANT_SLASH, buffer_length - index);
        if (!char_index) {
            if (json_is_last_span(stream)) {
                json_throw(stream, JSON_ERROR_END_OF_COMMENT_NOT_FOUND);
            }
            return false;
        }

        size_t found_index = char_index - (buffer + index);

        if (char_index != buffer + index && char_index[-1] == JSON_CONSTANT_ASTERISK) {
            index += found_index - 1;
            break;
        }

        index += found_index + 1;
    }

    stream->consumed += index + 4;
    if (out_index) {
        *out_index = index;
    }

    size_t new_lines;
    size_t new_line_index;
    if (json_helper_count_new_lines(buffer, index, &new_lines, &new_line_index)) {
        stream->byte_position_in_line = index - new_line_index + 1;
    } else {
        stream->byte_position_in_line += index + 4;
    }
    stream->line_number += new_lines;

    return true;
}

static bool json_consume_comment(JsonStream* stream) {
    const char* buffer = stream->buffer + stream->consumed + 1;
    size_t buffer_length = stream->buffer_size == 0 ? 0 : (stream->buffer_size - stream->consumed - 1);

    if (JSON_BUFFER_OUT_OF_BOUNDS(buffer, buffer_length, 0)) {
        json_throw(stream, JSON_ERROR_UNEXPECTED_END_OF_DATA_WHILE_READING_COMMENT);
        return false;
    }

    char token = buffer[0];

    buffer++;
    buffer_length = buffer_length == 0 ? 0 : buffer_length - 1;

    if (token == JSON_CONSTANT_SLASH) {
        return json_consume_single_line_comment(stream, buffer, buffer_length, stream->consumed);
    } else if (token == JSON_CONSTANT_ASTERISK) {
        return json_consume_multiline_comment(stream, buffer, buffer_length, stream->consumed);
    } else {
        json_throw_char(stream, JSON_ERROR_INVALID_CHARACTER_AT_START_OF_COMMENT, token);
        return false;
    }
}

static bool json_consume_single_line_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t previous_consumed
) {
    size_t index = 0;
    if (!json_skip_single_line_comment(stream, buffer, buffer_length, &index)) {
        return false;
    }

    stream->token_start = previous_consumed + 2;
    stream->token_size = index;

    if (stream->token_type != JSON_TYPE_COMMENT) {
        stream->previous_token_type = stream->token_type;
    }

    stream->token_type = JSON_TYPE_COMMENT;
    return true;
}

static bool json_consume_multiline_comment(
    JsonStream* stream,
    const char* buffer,
    size_t buffer_length,
    size_t previous_consumed
) {
    size_t index = 0;
    if (!json_skip_multiline_comment(stream, buffer, buffer_length, &index)) {
        return false;
    }

    stream->token_start = previous_consumed + 2;
    stream->token_size = index;

    if (stream->token_type != JSON_TYPE_COMMENT) {
        stream->previous_token_type = stream->token_type;
    }

    stream->token_type = JSON_TYPE_COMMENT;
    return true;
}

static bool json_helper_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool json_helper_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool json_helper_is_token_type_primitive(JsonType token_type) {
    switch (token_type) {
        case JSON_TYPE_STRING:
        case JSON_TYPE_NUMBER:
        case JSON_TYPE_BOOLEAN:
        case JSON_TYPE_NULL:
            return true;
        default:
            return false;
    }
}

static bool json_helper_count_new_lines(
    const char* buffer,
    size_t buffer_size,
    size_t* out_count,
    size_t* out_new_line_index
) {
    const char* search = buffer;
    bool found_line_feed = false;
    while ((search = memchr(search, JSON_CONSTANT_LINE_FEED, buffer_size - (search - buffer))) != NULL) {
        size_t index = search - buffer;
        if (index >= buffer_size)
            return true;
        (*out_count)++;
        *out_new_line_index = index;
        found_line_feed = true;
        search++;
    }

    return found_line_feed;
}

static void json_rollback_init(const JsonStream* stream, JsonRollbackState* state) {
    state->prev_token_type = stream->token_type;
    state->prev_consumed = stream->consumed;
    state->prev_position = stream->byte_position_in_line;
    state->prev_token_start = stream->token_start;
    state->prev_token_size = stream->token_size;
    state->prev_line = stream->line_number;
    state->prev_trailing_comma = stream->trailing_comma;
    state->prev_prev_token_type = stream->previous_token_type;
}

static void json_rollback(JsonStream* stream, const JsonRollbackState* state) {
    if (stream->token_type == JSON_TYPE_OBJECT_START || stream->token_type == JSON_TYPE_ARRAY_START) {
        json_z_bits_pop(&stream->bits);
    } else if (stream->token_type == JSON_TYPE_ARRAY_END) {
        json_z_bits_push(&stream->bits, false);
    } else if (stream->token_type == JSON_TYPE_OBJECT_END) {
        json_z_bits_push(&stream->bits, true);
    }
    stream->token_type = state->prev_token_type;
    stream->consumed = state->prev_consumed;
    stream->byte_position_in_line = state->prev_position;
    stream->token_start = state->prev_token_start;
    stream->token_size = state->prev_token_size;
    stream->line_number = state->prev_line;
    stream->trailing_comma = state->prev_trailing_comma;
    stream->previous_token_type = state->prev_prev_token_type;
}

char* json_get_string_escaped(JsonStream* stream, char* buffer, size_t buffer_length, size_t* out_length) {
    char* out;
    if (json_try_get_string_escaped(stream, buffer, buffer_length, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING);
    }
    return NULL;
}

char* json_read_string_escaped(JsonStream* stream, char* buffer, size_t buffer_length, size_t* out_length) {
    char* out;
    if (json_try_read_string_escaped(stream, buffer, buffer_length, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING);
    }
    return NULL;
}

bool json_try_get_string_escaped(
    JsonStream* stream,
    char* buffer,
    size_t buffer_length,
    char** out_string,
    size_t* out_length
) {
    if (stream->token_type != JSON_TYPE_NULL && stream->token_type != JSON_TYPE_STRING
        && stream->token_type != JSON_TYPE_PROPERTY)
    {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }

        return false;
    }

    if (stream->token_type == JSON_TYPE_NULL) {
        if (out_string) {
            *out_string = NULL;
        }
        if (out_length) {
            *out_length = 0;
        }
        return true;
    }

    bool allocated = false;

    if (!buffer || buffer_length == 0) {
        buffer = malloc(stream->token_size + 1);
        if (!buffer) {
            json_throw(stream, JSON_ERROR_OUT_OF_MEMORY);
            return false;
        }
        buffer_length = stream->token_size + 1;
        buffer[stream->token_size] = '\0';
        allocated = true;
    }

    if (stream->value_is_escaped) {
        size_t length;
        bool full;
        bool result = json_unescape(stream, buffer, buffer_length - 1, &length, &full);
        if (!result) {
            if (allocated) {
                free(buffer);
            }
            return false;
        }
        buffer[length] = '\0';
        if (out_length) {
            *out_length = length;
        }
    } else {
        int length =
            snprintf(buffer, buffer_length, "%.*s", (int)stream->token_size, stream->buffer + stream->token_start);
        if (length < 0) {
            json_throw(stream, JSON_ERROR_STRING_PARSE_FAILED);
            if (allocated) {
                free(buffer);
            }
            return false;
        }
        if (out_length) {
            *out_length = buffer_length - 1 < (size_t)length ? buffer_length - 1 : (size_t)length;
        }
    }

    *out_string = buffer;
    return true;
}

bool json_try_read_string_escaped(
    JsonStream* stream,
    char* buffer,
    size_t buffer_length,
    char** out_string,
    size_t* out_length
) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_string_escaped(stream, buffer, buffer_length, out_string, out_length)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

static bool json_unescape(
    JsonStream* stream,
    char* destination,
    size_t destination_length,
    size_t* out_written,
    bool* out_full
) {
    const char* source = stream->buffer + stream->token_start;
    size_t source_length = stream->token_size;
    size_t written = 0;
    bool full = true;

    while (source_length > 0) {
        char* backslash_ptr = memchr(source, JSON_CONSTANT_BACKSLASH, source_length);
        if (backslash_ptr) {
            size_t backslash_index = backslash_ptr - source;
            size_t to_write =
                written + backslash_index < destination_length ? backslash_index : destination_length - written;
            memmove(destination + written, source, to_write);
            written += to_write;
            source += to_write + 1;
            source_length -= to_write + 1;
            if (to_write != backslash_index || written == destination_length) {
                full = false;
                break;
            }
            switch (source[0]) {
                case JSON_CONSTANT_QUOTE:
                    destination[written++] = JSON_CONSTANT_QUOTE;
                    break;
                case 'n':
                    destination[written++] = JSON_CONSTANT_LINE_FEED;
                    break;
                case 'r':
                    destination[written++] = JSON_CONSTANT_CARRIAGE_RETURN;
                    break;
                case JSON_CONSTANT_BACKSLASH:
                    destination[written++] = JSON_CONSTANT_BACKSLASH;
                    break;
                case JSON_CONSTANT_SLASH:
                    destination[written++] = JSON_CONSTANT_SLASH;
                    break;
                case 't':
                    destination[written++] = JSON_CONSTANT_TAB;
                    break;
                case 'b':
                    destination[written++] = JSON_CONSTANT_BACKSPACE;
                    break;
                case 'f':
                    destination[written++] = JSON_CONSTANT_FORM_FEED;
                    break;
                case 'u':
                    assert(source_length > 5);
                    char hex[5];
                    memcpy(hex, source + 1, 4);
                    hex[4] = '\0';
                    wchar_t code_point = (wchar_t)strtol(hex, NULL, 16);
                    int bytes = wctomb(hex, code_point);
                    if (bytes == -1) {
                        json_throw_char(stream, JSON_ERROR_INVALID_HEX_CHARACTER_WITHIN_STRING, source[1]);
                        return false;
                    }

                    if (written + bytes > destination_length) {
                        full = false;
                        goto end;
                    }

                    memcpy(destination + written, source + 1, bytes);
                    written += bytes;
                    source += bytes;
                    source_length -= bytes;
                    break;
                default:
                    json_throw_char(stream, JSON_ERROR_INVALID_CHARACTER_AFTER_ESCAPE_WITHIN_STRING, source[0]);
                    return false;
            }
            source++;
            source_length--;
        } else {
            size_t to_write =
                written + source_length < destination_length ? source_length : destination_length - written;
            memmove(destination + written, source, source_length);
            written += to_write;
            full = to_write == source_length;
            break;
        }
    }

end:

    if (out_full) {
        *out_full = full;
    }

    if (out_written) {
        *out_written = written;
    }

    return true;
}

const char* json_get_string(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_get_string(stream, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING);
    }
    return NULL;
}

const char* json_read_string(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_read_string(stream, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING);
    }
    return NULL;
}

bool json_try_get_string(JsonStream* stream, const char** out_string, size_t* out_length) {
    if (stream->token_type != JSON_TYPE_NULL && stream->token_type != JSON_TYPE_STRING
        && stream->token_type != JSON_TYPE_PROPERTY)
    {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }

        return false;
    }

    if (stream->token_type == JSON_TYPE_NULL) {
        *out_string = NULL;
        *out_length = 0;
        return true;
    }

    *out_string = stream->buffer + stream->token_start;
    *out_length = stream->token_size;
    return true;
}

bool json_try_read_string(JsonStream* stream, const char** out_string, size_t* out_length) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_string(stream, out_string, out_length)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

const char* json_get_property(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_get_property(stream, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_PROPERTY);
    }
    return NULL;
}

const char* json_read_property(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_read_property(stream, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_PROPERTY);
    }
    return NULL;
}

bool json_try_get_property(JsonStream* stream, const char** out_property, size_t* out_length) {
    if (stream->token_type != JSON_TYPE_PROPERTY) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }

        return false;
    }

    *out_property = stream->buffer + stream->token_start;
    *out_length = stream->token_size;
    return true;
}

bool json_try_read_property(JsonStream* stream, const char** out_property, size_t* out_length) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_property(stream, out_property, out_length)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

const char* json_get_comment(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_get_comment(stream, &out, out_length)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_COMMENT);
    }
    return NULL;
}

const char* json_read_comment(JsonStream* stream, size_t* out_length) {
    const char* out;
    if (json_try_read_comment(stream, &out, out_length)) {
        return out;
    }
    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_COMMENT);
    }
    return NULL;
}

bool json_try_get_comment(JsonStream* stream, const char** out_comment, size_t* out_length) {
    if (stream->token_type != JSON_TYPE_COMMENT) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        *out_comment = NULL;
        return false;
    }

    *out_comment = stream->buffer + stream->token_start;
    *out_length = stream->token_size;
    return true;
}

bool json_try_read_comment(JsonStream* stream, const char** out_comment, size_t* out_length) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);
    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_comment(stream, out_comment, out_length)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_get_bool(JsonStream* stream) {
    bool out;
    if (json_try_get_bool(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_BOOL);
    }
    return false;
}

bool json_read_bool(JsonStream* stream) {
    bool out;
    if (json_try_read_bool(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_BOOL);
    }

    return false;
}

bool json_try_get_bool(JsonStream* stream, bool* out_bool) {
    if (stream->token_type != JSON_TYPE_BOOLEAN) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    *out_bool = stream->token_size == 4;
    return true;
}

bool json_try_read_bool(JsonStream* stream, bool* out_bool) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);
    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_bool(stream, out_bool)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

uint8_t json_get_u8(JsonStream* stream) {
    uint8_t out;
    if (json_try_get_u8(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U8);
    }
    return 0;
}

uint8_t json_read_u8(JsonStream* stream) {
    uint8_t out;
    if (json_try_read_u8(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U8);
    }
    return 0;
}

bool json_try_get_u8(JsonStream* stream, uint8_t* out_u8) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    uint64_t result = strtoull(stream->buffer + stream->token_start, NULL, 10);
    if (result > UINT8_MAX) {
        return false;
    }

    *out_u8 = (uint8_t)result;
    return true;
}

bool json_try_read_u8(JsonStream* stream, uint8_t* out_u8) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);
    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_u8(stream, out_u8)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

int8_t json_get_i8(JsonStream* stream) {
    int8_t out;
    if (json_try_get_i8(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I8);
    }
    return 0;
}

int8_t json_read_i8(JsonStream* stream) {
    int8_t out;
    if (json_try_read_i8(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I8);
    }
    return 0;
}

bool json_try_get_i8(JsonStream* stream, int8_t* out_i8) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    int64_t result = strtoll(stream->buffer + stream->token_start, NULL, 10);
    if (result > INT8_MAX || result < INT8_MIN) {
        return false;
    }

    *out_i8 = (int8_t)result;
    return true;
}

bool json_try_read_i8(JsonStream* stream, int8_t* out_i8) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_i8(stream, out_i8)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

uint16_t json_get_u16(JsonStream* stream) {
    uint16_t out;
    if (json_try_get_u16(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U16);
    }
    return 0;
}

uint16_t json_read_u16(JsonStream* stream) {
    uint16_t out;
    if (json_try_read_u16(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U16);
    }
    return 0;
}

bool json_try_get_u16(JsonStream* stream, uint16_t* out_u16) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    uint64_t result = strtoull(stream->buffer + stream->token_start, NULL, 10);
    if (result > UINT16_MAX) {
        return false;
    }
    *out_u16 = (uint16_t)result;
    return true;
}

bool json_try_read_u16(JsonStream* stream, uint16_t* out_u16) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);
    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_u16(stream, out_u16)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

int16_t json_get_i16(JsonStream* stream) {
    int16_t out;
    if (json_try_get_i16(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I16);
    }
    return 0;
}

int16_t json_read_i16(JsonStream* stream) {
    int16_t out;
    if (json_try_read_i16(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I16);
    }
    return 0;
}

bool json_try_get_i16(JsonStream* stream, int16_t* out_i16) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    int64_t result = strtoll(stream->buffer + stream->token_start, NULL, 10);
    if (result > INT16_MAX || result < INT16_MIN) {
        return false;
    }
    *out_i16 = (int16_t)result;
    return true;
}

bool json_try_read_i16(JsonStream* stream, int16_t* out_i16) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_i16(stream, out_i16)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

uint32_t json_get_u32(JsonStream* stream) {
    uint32_t out;
    if (json_try_get_u32(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U32);
    }
    return 0;
}

uint32_t json_read_u32(JsonStream* stream) {
    uint32_t out;
    if (json_try_read_u32(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U32);
    }
    return 0;
}

bool json_try_get_u32(JsonStream* stream, uint32_t* out_u32) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }
    uint64_t result = strtoull(stream->buffer + stream->token_start, NULL, 10);
    if (result > UINT32_MAX) {
        return false;
    }
    *out_u32 = (uint32_t)result;
    return true;
}

bool json_try_read_u32(JsonStream* stream, uint32_t* out_u32) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);
    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_u32(stream, out_u32)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

int32_t json_get_i32(JsonStream* stream) {
    int32_t out;
    if (json_try_get_i32(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I32);
    }
    return 0;
}

int32_t json_read_i32(JsonStream* stream) {
    int32_t out;
    if (json_try_read_i32(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I32);
    }
    return 0;
}

bool json_try_get_i32(JsonStream* stream, int32_t* out_i32) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    int64_t result = strtoll(stream->buffer + stream->token_start, NULL, 10);
    if (result > INT32_MAX || result < INT32_MIN) {
        return false;
    }

    *out_i32 = (int32_t)result;
    return true;
}

bool json_try_read_i32(JsonStream* stream, int32_t* out_i32) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_i32(stream, out_i32)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

uint64_t json_get_u64(JsonStream* stream) {
    uint64_t out;
    if (json_try_get_u64(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U64);
    }
    return 0;
}

uint64_t json_read_u64(JsonStream* stream) {
    uint64_t out;
    if (json_try_read_u64(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_U64);
    }
    return 0;
}

bool json_try_get_u64(JsonStream* stream, uint64_t* out_u64) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    uint64_t result = strtoull(stream->buffer + stream->token_start, NULL, 10);

    *out_u64 = result;
    return true;
}

bool json_try_read_u64(JsonStream* stream, uint64_t* out_u64) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_u64(stream, out_u64)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

int64_t json_get_i64(JsonStream* stream) {
    int64_t out;
    if (json_try_get_i64(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I64);
    }
    return 0;
}

int64_t json_read_i64(JsonStream* stream) {
    int64_t out;
    if (json_try_read_i64(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_I64);
    }
    return 0;
}

bool json_try_get_i64(JsonStream* stream, int64_t* out_i64) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    int64_t result = strtoll(stream->buffer + stream->token_start, NULL, 10);

    *out_i64 = result;
    return true;
}

bool json_try_read_i64(JsonStream* stream, int64_t* out_i64) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_i64(stream, out_i64)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

float json_get_float(JsonStream* stream) {
    float out;
    if (json_try_get_float(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_FLOAT);
    }
    return 0;
}

float json_read_float(JsonStream* stream) {
    float out;
    if (json_try_read_float(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_FLOAT);
    }
    return 0;
}

bool json_try_get_float(JsonStream* stream, float* out_float) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    float result = strtof(stream->buffer + stream->token_start, NULL);
    if (result == HUGE_VALF) {
        return false;
    }

    *out_float = (float)result;
    return true;
}

bool json_try_read_float(JsonStream* stream, float* out_float) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_float(stream, out_float)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

double json_get_double(JsonStream* stream) {
    double out;
    if (json_try_get_double(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_DOUBLE);
    }
    return 0;
}

double json_read_double(JsonStream* stream) {
    double out;
    if (json_try_read_double(stream, &out)) {
        return out;
    }

    if (stream->error.type == JSON_ERROR_NONE) {
        json_throw(stream, JSON_ERROR_INVALID_OPERATION_EXPECTED_DOUBLE);
    }
    return 0;
}

bool json_try_get_double(JsonStream* stream, double* out_double) {
    if (stream->token_type != JSON_TYPE_NUMBER) {
        if (stream->error.type == JSON_ERROR_NONE) {
            stream->error.string = json_token_type_name(stream->token_type);
        }
        return false;
    }

    double result = atof(stream->buffer + stream->token_start);
    if (result == HUGE_VAL) {
        return false;
    }

    *out_double = (double)result;
    return true;
}

bool json_try_read_double(JsonStream* stream, double* out_double) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (!json_try_get_double(stream, out_double)) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_read_array_start(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_ARRAY_START) {
        json_throw_string(
            stream,
            JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_START,
            json_token_type_name(stream->token_type)
        );
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_try_read_array_start(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_ARRAY_START) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_read_array_end(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_ARRAY_END) {
        json_throw_string(
            stream,
            JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_END,
            json_token_type_name(stream->token_type)
        );
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_try_read_array_end(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_ARRAY_END) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_read_object_start(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_OBJECT_START) {
        json_throw_string(
            stream,
            JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_START,
            json_token_type_name(stream->token_type)
        );
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_try_read_object_start(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_OBJECT_START) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_read_object_end(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_OBJECT_END) {
        json_throw_string(
            stream,
            JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_END,
            json_token_type_name(stream->token_type)
        );
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_try_read_object_end(JsonStream* stream) {
    JsonRollbackState state;
    json_rollback_init(stream, &state);

    if (!json_read(stream)) {
        return false;
    }

    if (stream->token_type != JSON_TYPE_OBJECT_END) {
        json_rollback(stream, &state);
        return false;
    }

    return true;
}

bool json_error_get_message(const JsonError* error, char* buffer, size_t buffer_length) {
    int start = snprintf(buffer, buffer_length, "[Line %zu, Column %zu] ", error->line, error->column);
    if (start < 0) {
        return false;
    }

    if ((size_t)start >= buffer_length) {
        return true;
    }

    buffer += start;
    buffer_length -= start;
    int result;

    switch (error->type) {
        case JSON_ERROR_NONE:
            result = snprintf(buffer, buffer_length, "No JSON error");
            break;
        case JSON_ERROR_NOT_IMPLEMENTED:
            result = snprintf(buffer, buffer_length, "%s functionality not implemented", error->string);
            break;
        case JSON_ERROR_OUT_OF_MEMORY:
            result = snprintf(buffer, buffer_length, "Out of memory");
            break;
        case JSON_ERROR_ARRAY_DEPTH_TOO_LARGE:
            result = snprintf(buffer, buffer_length, "The maximum depth of %lld has been exceeded", error->number);
            break;
        case JSON_ERROR_MISMATCHED_OBJECT_ARRAY:
            result = snprintf(buffer, buffer_length, "'%c' is invalid without a matching open", error->character);
            break;
        case JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_ARRAY_END:
            result = snprintf(
                buffer,
                buffer_length,
                "The JSON array contains a trailing comma at the end which is not supported in this mode"
            );
            break;
        case JSON_ERROR_TRAILING_COMMA_NOT_ALLOWED_BEFORE_OBJECT_END:
            result = snprintf(
                buffer,
                buffer_length,
                "The JSON object contains a trailing comma at the end which is not supported in this mode"
            );
            break;
        case JSON_ERROR_END_OF_STRING_NOT_FOUND:
            result = snprintf(buffer, buffer_length, "Expected end of string, but instead reached the end of data");
            break;
        case JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_SIGN:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid within a number, immediately after a sign character ('+' or '-'). Expected a "
                "digit ('0'-'9')",
                error->character
            );
            break;
        case JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_AFTER_DECIMAL:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid within a number, immediately after a decimal point ('.'). Expected a digit "
                "('0'-'9')",
                error->character
            );
            break;
        case JSON_ERROR_REQUIRED_DIGIT_NOT_FOUND_END_OF_DATA:
            result = snprintf(buffer, buffer_length, "Expected a digit ('0'-'9'), but instead reached end of data");
            break;
        case JSON_ERROR_EXPECTED_END_AFTER_SINGLE_JSON:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid after a single JSON value. Expected end of data",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_END_OF_DIGIT_NOT_FOUND:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is an invalid end of number. Expected a delimiter",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_NEXT_DIGIT_E_VALUE_NOT_FOUND:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is an invalid end of number. Expected 'E' or 'e'",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_SEPARATOR_AFTER_PROPERTY_NAME_NOT_FOUND:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid after a property name. Expected a ':'",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_START_OF_PROPERTY_NOT_FOUND:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is an invalid start of a property name. Expected a '\"'",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_NOT_FOUND:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected start of a property name or value, but instead reached end of data"
            );
            break;
        case JSON_ERROR_EXPECTED_START_OF_PROPERTY_OR_VALUE_AFTER_COMMENT:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is an invalid start of a property name or value after a comment",
                error->character
            );
            break;
        case JSON_ERROR_EXPECTED_START_OF_VALUE_NOT_FOUND:
            result = snprintf(buffer, buffer_length, "'%c' is an invalid start of a value", error->character);
            break;
        case JSON_ERROR_EXPECTED_VALUE_AFTER_PROPERTY_NAME_NOT_FOUND:
            result = snprintf(buffer, buffer_length, "Expected a value, but reached end of data");
            break;
        case JSON_ERROR_FOUND_INVALID_CHARACTER:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid after a value. Expected either ',', '}', or ']'",
                error->character
            );
            break;
        case JSON_ERROR_INVALID_END_OF_JSON_NON_PRIMITIVE:
            result = snprintf(
                buffer,
                buffer_length,
                "'%s' is an invalid token type for the end of the JSON payload. Expected either "
                "JSON_TYPE_ARRAY_END or JSON_TYPE_OBJECT_END",
                error->string
            );
            break;
        case JSON_ERROR_OBJECT_DEPTH_TOO_LARGE:
            result = snprintf(
                buffer,
                buffer_length,
                "The maximum configured depth of %lld has been exceeded.",
                error->number
            );
            break;
        case JSON_ERROR_EXPECTED_FALSE:
            result = snprintf(
                buffer,
                buffer_length,
                "'%.*s' is an invalid JSON literal. Expected the literal 'false'",
                error->slice_length,
                error->string
            );
            break;
        case JSON_ERROR_EXPECTED_TRUE:
            result = snprintf(
                buffer,
                buffer_length,
                "'%.*s' is an invalid JSON literal. Expected the literal 'true'",
                error->slice_length,
                error->string
            );
            break;
        case JSON_ERROR_EXPECTED_NULL:
            result = snprintf(
                buffer,
                buffer_length,
                "'%.*s' is an invalid JSON literal. Expected the literal 'null'",
                error->slice_length,
                error->string
            );
            break;
        case JSON_ERROR_INVALID_CHARACTER_WITHIN_STRING:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid within a JSON string. The string should be properly escaped",
                error->character
            );
        case JSON_ERROR_INVALID_CHARACTER_AFTER_ESCAPE_WITHIN_STRING:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is an invalid escapable character within a JSON string. The string should be correctly "
                "escaped.",
                error->character
            );
            break;
        case JSON_ERROR_INVALID_HEX_CHARACTER_WITHIN_STRING:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is not a hex digit following '\\u' within a JSON string. The string should be correctly "
                "escaped",
                error->character
            );
            break;
        case JSON_ERROR_END_OF_COMMENT_NOT_FOUND:
            result = snprintf(buffer, buffer_length, "Expected end of comment, but instead reached end of data");
            break;
        case JSON_ERROR_ZERO_DEPTH_AT_END:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected depth to be zero at the end of the JSON payload. There is an open JSON object or array "
                "that should be closed"
            );
            break;
        case JSON_ERROR_EXPECTED_JSON_TOKENS:
            result = snprintf(
                buffer,
                buffer_length,
                "The input does not contain any JSON tokens. Expected the input to start with a valid JSON token, "
                "when json_is_final_block returns is true"
            );
            break;
        case JSON_ERROR_NOT_ENOUGH_DATA:
            result = snprintf(
                buffer,
                buffer_length,
                "There is not enough data to read through the entire JSON array or object."
            );
            break;
        case JSON_ERROR_EXPECTED_ONE_COMPLETE_TOKEN:
            result = snprintf(
                buffer,
                buffer_length,
                "The input does not contain any complete JSON tokens. Expected the input to have at least one "
                "valid, complete JSON token"
            );
            break;
        case JSON_ERROR_INVALID_CHARACTER_AT_START_OF_COMMENT:
            result = snprintf(
                buffer,
                buffer_length,
                "'%c' is invalid after '/' at the beginning of the comment. Expected either '/' or '*'",
                error->character
            );
            break;
        case JSON_ERROR_UNEXPECTED_END_OF_DATA_WHILE_READING_COMMENT:
            result = snprintf(buffer, buffer_length, "Unexpected end of data while reading a comment");
            break;
        case JSON_ERROR_UNEXPECTED_END_OF_LINE_SEPARATOR:
            result = snprintf(
                buffer,
                buffer_length,
                "Found invalid line or paragraph separator character while reading a comment"
            );
            break;
        case JSON_ERROR_INVALID_LEADING_ZERO_IN_NUMBER:
            result = snprintf(
                buffer,
                buffer_length,
                "Invalid leading zero before '%.*s'",
                error->slice_length,
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_CANNOT_SKIP_ON_PARTIAL:
            result = snprintf(
                buffer,
                buffer_length,
                "Cannot skip tokens on partial JSON. Either get the whole payload and create a JsonStream where "
                "json_is_final_block is true, or call json_try_skip"
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING_COMPARISON:
            result =
                snprintf(buffer, buffer_length, "Cannot compare the value of a token type '%s' to text", error->string);
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_STRING:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_STRING' or 'JSON_TOKEN_NULL', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_PROPERTY:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_PROPERTY', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_I8:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_U8:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_I16:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_U16:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_I32:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_U32:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_I64:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_U64:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_FLOAT:
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_DOUBLE:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_NUMBER', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_BOOL:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_BOOL', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_COMMENT:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_COMMENT', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_START:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_OBJECT_START', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_OBJECT_END:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_OBJECT_END', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_START:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_ARRAY_START', found '%s' instead",
                error->string
            );
            break;
        case JSON_ERROR_INVALID_OPERATION_EXPECTED_ARRAY_END:
            result = snprintf(
                buffer,
                buffer_length,
                "Expected the token to be 'JSON_TOKEN_ARRAY_END', found '%s' instead",
                error->string
            );
            break;
        default:
            result = snprintf(buffer, buffer_length, "Encountered unexpected JSON error %d", error->type);
            break;
    }

    return result >= 0;
}

const char* json_token_type_name(JsonType type) {
    switch (type) {
        case JSON_TYPE_UNKNOWN:
            return "JSON_TYPE_UNKNOWN";
        case JSON_TYPE_OBJECT_START:
            return "JSON_TYPE_OBJECT_START";
        case JSON_TYPE_OBJECT_END:
            return "JSON_TYPE_OBJECT_END";
        case JSON_TYPE_ARRAY_START:
            return "JSON_TYPE_ARRAY_START";
        case JSON_TYPE_ARRAY_END:
            return "JSON_TYPE_ARRAY_END";
        case JSON_TYPE_PROPERTY:
            return "JSON_TYPE_PROPERTY";
        case JSON_TYPE_STRING:
            return "JSON_TYPE_STRING";
        case JSON_TYPE_NUMBER:
            return "JSON_TYPE_NUMBER";
        case JSON_TYPE_BOOLEAN:
            return "JSON_TYPE_BOOLEAN";
        case JSON_TYPE_NULL:
            return "JSON_TYPE_NULL";
        case JSON_TYPE_COMMENT:
            return "JSON_TYPE_COMMENT";
        default:
            return "JSON_TYPE_UNKNOWN";
    }
}

void json_clear_error(JsonStream* stream) {
    stream->error = (JsonError){0};
}
