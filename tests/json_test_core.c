//
// Created by Chris Kramer on 2/12/25.
//

#include <cJSON.h>
#include <json_stream.h>
#include <stdio.h>

#include "json_tests.h"

char error_buffer[1024];

START_TEST(json_stream_defaults) {
    JsonStream stream;
    json_stream_init(&stream, "1", 0, true, json_stream_options_default());

    ck_assert_uint_eq(json_bytes_consumed(&stream), 0);
    ck_assert_uint_eq(json_token_start(&stream), 0);
    ck_assert_uint_eq(json_current_depth(&stream), 0);
    ck_assert_int_eq(json_token_type(&stream), JSON_TYPE_UNKNOWN);
    ck_assert(!json_value_is_escaped(&stream));
    ck_assert(json_is_final_block(&stream));
    ck_assert(stream.max_depth == 64);
    ck_assert(!stream.allow_trailing_commas);
    ck_assert(!stream.allow_multiple_values);
    ck_assert_int_eq(stream.comment_handling, JSON_COMMENT_DISALLOW);
    ck_assert(json_read(&stream));
    ck_assert(expect_success(&stream));
    ck_assert(!json_read(&stream));
}
END_TEST

START_TEST(json_init_state_recovery) {
    JsonStream stream;
    json_stream_init(&stream, "[1]", 0, false, json_stream_options_default());

    ck_assert_uint_eq(json_bytes_consumed(&stream), 0);
    ck_assert_uint_eq(json_token_start(&stream), 0);
    ck_assert_uint_eq(json_current_depth(&stream), 0);
    ck_assert_int_eq(json_token_type(&stream), JSON_TYPE_UNKNOWN);
    ck_assert(!json_value_is_escaped(&stream));
    ck_assert(!json_is_final_block(&stream));
    ck_assert(stream.max_depth == 64);
    ck_assert(!stream.allow_trailing_commas);
    ck_assert(!stream.allow_multiple_values);
    ck_assert_int_eq(stream.comment_handling, JSON_COMMENT_DISALLOW);
    ck_assert(json_read(&stream));
    ck_assert(json_read(&stream));
    ck_assert(expect_success(&stream));

    ck_assert_uint_eq(2, json_bytes_consumed(&stream));
    ck_assert_uint_eq(1, json_token_start(&stream));
    ck_assert_uint_eq(1, json_token_size(&stream));
    ck_assert_uint_eq(JSON_TYPE_NUMBER, json_token_type(&stream));
    ck_assert(!json_value_is_escaped(&stream));

    json_stream_continue(&stream, &stream, "]", 0, true);

    ck_assert_uint_eq(0, json_bytes_consumed(&stream)); // Not retained
    ck_assert_uint_eq(2, json_total_bytes_consumed(&stream));
    ck_assert_uint_eq(0, json_token_start(&stream)); // Not retained
    ck_assert_uint_eq(JSON_TYPE_NUMBER, json_token_type(&stream));
    ck_assert(stream.max_depth == 64);
    ck_assert(!stream.allow_trailing_commas);
    ck_assert(!stream.allow_multiple_values);
    ck_assert_int_eq(stream.comment_handling, JSON_COMMENT_DISALLOW);

    ck_assert(json_read(&stream));
    ck_assert(expect_success(&stream));
    ck_assert(!json_read(&stream));
}
END_TEST

START_TEST(json_hello_world) {
    const char* json = "{\"hello\":\"world\"}";
    ck_assert(compare_full_buffer_to_cjson(json, json_stream_options_default()));
}
END_TEST

Suite* json_core_suite(void) {
    Suite* suite = suite_create("core");

    TCase* core = tcase_create("core");
    tcase_add_test(core, json_stream_defaults);
    tcase_add_test(core, json_init_state_recovery);
    tcase_add_test(core, json_hello_world);

    suite_add_tcase(suite, core);

    return suite;
}
