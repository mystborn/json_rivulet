//
// Created by Chris Kramer on 2/12/25.
//

#include <cJSON.h>
#include <stdio.h>


#include "json_tests.h"

START_TEST(json_full_file_hello_world) {
    const char* file = read_json_file("")
}
END_TEST

START_TEST(json_small_files_no_compact) {
    int count = sizeof(small_files) / sizeof(small_files[0]);
    for (int i = 0; i < count; i++) {
        if (small_files[i].compact) {
            continue;
        }

        const char* file = read_json_file(small_files[i].file);
        ck_assert_msg(file != NULL, "Failed to load %s", small_files[i].file);
        ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    }
}
END_TEST

START_TEST(json_small_files_compact) {
    int count = sizeof(small_files) / sizeof(small_files[0]);
    for (int i = 0; i < count; i++) {
        if (!small_files[i].compact) {
            continue;
        }
        const char* file = compact_json_file(small_files[i].file);
        ck_assert_ptr_nonnull(file);
        ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    }
}
END_TEST

START_TEST(json_large_files_no_compact) {
    int count = sizeof(large_files) / sizeof(large_files[0]);
    for (int i = 0; i < count; i++) {
        if (large_files[i].compact) {
            continue;
        }
        const char* file = read_json_file(large_files[i].file);
        ck_assert_ptr_nonnull(file);
        ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    }
}
END_TEST

START_TEST(json_large_files_compact) {
    int count = sizeof(large_files) / sizeof(large_files[0]);
    for (int i = 0; i < count; i++) {
        if (!large_files[i].compact) {
            continue;
        }
        const char* file = compact_json_file(large_files[i].file);
        ck_assert_ptr_nonnull(file);
        ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    }
}
END_TEST

Suite* json_files_suite(void) {
    Suite* suite = suite_create("files");

    TCase* tc_small_files_no_compact = tcase_create("small_files_no_compact");
    tcase_add_test(tc_small_files_no_compact, json_small_files_no_compact);
    tcase_set_tags(tc_small_files_no_compact, "small no_compact");

    TCase* tc_small_files_compact = tcase_create("small_files_compact");
    tcase_add_test(tc_small_files_compact, json_small_files_compact);
    tcase_set_tags(tc_small_files_no_compact, "small compact");


    TCase* tc_large_files_no_compact = tcase_create("large_files_no_compact");
    tcase_add_test(tc_large_files_no_compact, json_large_files_no_compact);
    tcase_set_tags(tc_small_files_no_compact, "large no_compact");

    TCase* tc_large_files_compact = tcase_create("large_files_compact");
    tcase_add_test(tc_large_files_compact, json_large_files_compact);
    tcase_set_tags(tc_small_files_no_compact, "large compact");

    suite_add_tcase(suite, tc_small_files_no_compact);
    suite_add_tcase(suite, tc_small_files_compact);
    suite_add_tcase(suite, tc_large_files_no_compact);
    suite_add_tcase(suite, tc_large_files_compact);

    return suite;
}
