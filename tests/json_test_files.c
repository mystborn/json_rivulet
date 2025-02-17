//
// Created by Chris Kramer on 2/12/25.
//

#include <cJSON.h>
#include <stdio.h>

#include "json_tests.h"

static inline char* load_file(const char* fname) {
    char* file = read_json_file(fname);
    ck_assert_msg(file != NULL, "Failed to load %s", fname);
    return file;
}

static inline char* load_compact_file(const char* fname) {
    char* file = compact_json_file(fname);
    ck_assert_msg(file != NULL, "Failed to load %s", fname);
    return file;
}

START_TEST(json_full_file_no_compact_hello_world) {
   char* file = load_file("hello_world.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_basic_json) {
   char* file = load_file("basic_json.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_basic_json_with_large_num) {
   char* file = load_file("basic_json_with_large_num.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_full_json_schema) {
   char* file = load_file("full_json_schema.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_400B) {
   char* file = load_file("400B.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_broad_tree) {
   char* file = load_file("broad_tree.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_deep_tree) {
   char* file = load_file("deep_tree.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_lots_of_numbers) {
   char* file = load_file("lots_of_numbers.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_lots_of_strings) {
   char* file = load_file("lots_of_strings.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_project_lock) {
   char* file = load_file("project_lock.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_4KB) {
   char* file = load_file("4KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_40KB) {
   char* file = load_file("40KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_no_compact_400KB) {
   char* file = load_file("400KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_hello_world) {
   char* file = load_compact_file("hello_world.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_basic_json) {
   char* file = load_compact_file("basic_json.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_basic_json_with_large_num) {
   char* file = load_compact_file("basic_json_with_large_num.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_full_json_schema) {
   char* file = load_compact_file("full_json_schema.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_400B) {
   char* file = load_compact_file("400B.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_broad_tree) {
   char* file = load_compact_file("broad_tree.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_deep_tree) {
   char* file = load_compact_file("deep_tree.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_lots_of_numbers) {
   char* file = load_compact_file("lots_of_numbers.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_lots_of_strings) {
   char* file = load_compact_file("lots_of_strings.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_project_lock) {
   char* file = load_compact_file("project_lock.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_4KB) {
   char* file = load_compact_file("4KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_40KB) {
   char* file = load_compact_file("40KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

START_TEST(json_full_file_compact_400KB) {
   char* file = load_compact_file("400KB.json");
    ck_assert(compare_full_buffer_to_cjson(file, json_stream_options_default()));
    free(file);
}
END_TEST

Suite* json_files_suite(void) {
    Suite* suite = suite_create("files");

    TCase* tc_small_files_no_compact = tcase_create("small_files_no_compact");
    tcase_add_test(tc_small_files_no_compact, json_full_file_no_compact_hello_world);
    tcase_add_test(tc_small_files_no_compact, json_full_file_no_compact_basic_json);
    tcase_add_test(tc_small_files_no_compact, json_full_file_no_compact_basic_json_with_large_num);
    tcase_add_test(tc_small_files_no_compact, json_full_file_no_compact_full_json_schema);
    tcase_add_test(tc_small_files_no_compact, json_full_file_no_compact_400B);
    tcase_set_tags(tc_small_files_no_compact, "small no_compact");

    TCase* tc_small_files_compact = tcase_create("small_files_compact");
    tcase_add_test(tc_small_files_compact, json_full_file_compact_hello_world);
    tcase_add_test(tc_small_files_compact, json_full_file_compact_basic_json);
    tcase_add_test(tc_small_files_compact, json_full_file_compact_basic_json_with_large_num);
    tcase_add_test(tc_small_files_compact, json_full_file_compact_full_json_schema);
    tcase_add_test(tc_small_files_compact, json_full_file_compact_400B);
    tcase_set_tags(tc_small_files_no_compact, "small compact");


    TCase* tc_large_files_no_compact = tcase_create("large_files_no_compact");
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_broad_tree);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_deep_tree);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_lots_of_numbers);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_lots_of_strings);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_project_lock);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_4KB);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_40KB);
    tcase_add_test(tc_large_files_no_compact, json_full_file_no_compact_400KB);
    tcase_set_tags(tc_small_files_no_compact, "large no_compact");

    TCase* tc_large_files_compact = tcase_create("large_files_compact");
    tcase_add_test(tc_large_files_compact, json_full_file_compact_broad_tree);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_deep_tree);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_lots_of_numbers);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_lots_of_strings);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_project_lock);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_4KB);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_40KB);
    tcase_add_test(tc_large_files_compact, json_full_file_compact_400KB);
    tcase_set_tags(tc_small_files_no_compact, "large compact");

    suite_add_tcase(suite, tc_small_files_no_compact);
    suite_add_tcase(suite, tc_small_files_compact);
    suite_add_tcase(suite, tc_large_files_no_compact);
    suite_add_tcase(suite, tc_large_files_compact);

    return suite;
}
