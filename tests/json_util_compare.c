//
// Created by Chris Kramer on 2/13/25.
//

#include <cJSON.h>
#include <check.h>
#include <stdio.h>

#include "json_tests.h"

static bool compare_token(JsonStream* stream, const cJSON* cjson);

// NOLINTNEXTLINE(*-no-recursion)
static bool compare_array(JsonStream* stream, const cJSON* cjson) {
    int count = 0;
    while (json_read(stream)) {
        if (json_token_type(stream) == JSON_TYPE_ARRAY_END) {
            ck_assert_int_eq(count, cJSON_GetArraySize(cjson));
            // json_read(stream);
            return true;
        }

        cJSON* item = cJSON_GetArrayItem(cjson, count++);
        if (!compare_token(stream, item)) {
            return false;
        }
    }

    return false;
}

// NOLINTNEXTLINE(*-no-recursion)
static bool compare_object(JsonStream* stream, const cJSON* cjson) {
    while (json_read(stream)) {
        if (json_token_type(stream) == JSON_TYPE_OBJECT_END) {
            return true;
        }

        char* property;
        size_t length;

        ck_assert(json_token_type(stream) == JSON_TYPE_PROPERTY);
        ck_assert(json_try_get_string_escaped(stream, NULL, 0, &property, &length));
        ck_assert(json_read(stream));

        cJSON* value = cJSON_GetObjectItemCaseSensitive(cjson, property);
        ck_assert(value != NULL);
        bool result = compare_token(stream, value);
        free(property);
        if (!result) {
            return false;
        }
    }

    return false;
}

// NOLINTNEXTLINE(*-no-recursion)
static bool compare_token(JsonStream* stream, const cJSON* cjson) {
    switch (json_token_type(stream)) {
        case JSON_TYPE_NULL: {
            return cJSON_IsNull(cjson);
        }
        case JSON_TYPE_BOOLEAN: {
            bool value;
            ck_assert(json_try_get_bool(stream, &value));
            ck_assert(value ? cJSON_IsTrue(cjson) : cJSON_IsFalse(cjson));
            return value ? cJSON_IsTrue(cjson) : cJSON_IsFalse(cjson);
        }
        case JSON_TYPE_NUMBER: {
            double value;
            ck_assert(json_try_get_double(stream, &value));
            ck_assert(cJSON_IsNumber(cjson));
            ck_assert_double_eq(value, cJSON_GetNumberValue(cjson));
            return value == cJSON_GetNumberValue(cjson);
        }
        case JSON_TYPE_STRING: {
            char* value;
            size_t length;
            ck_assert(json_try_get_string_escaped(stream, NULL, 0, &value, &length));
            ck_assert(cJSON_IsString(cjson));
            char* cjson_string = cJSON_GetStringValue(cjson);
            bool result = strcmp(cjson_string, value) == 0;
            ck_assert_str_eq(cjson_string, value);
            free(value);
            return result;
        }
        case JSON_TYPE_ARRAY_START: {
            return compare_array(stream, cjson);
        }
        case JSON_TYPE_OBJECT_START: {
            return compare_object(stream, cjson);
        }
        default:
            ck_assert_msg(false, "Unexpected token type");
            return false;
    }
    return true;
}

bool compare_full_buffer_to_cjson(const char* buffer, JsonStreamOptions options) {
    JsonStream stream;
    size_t buffer_length = strlen(buffer);

    json_stream_init(&stream, buffer, buffer_length, true, options);

    cJSON* root = cJSON_ParseWithLength(buffer, buffer_length);
    ck_assert_ptr_nonnull(root);
    ck_assert(json_read(&stream));

    bool result = compare_token(&stream, root);

    json_stream_free_resources(&stream);
    cJSON_Delete(root);

    return result;
}
