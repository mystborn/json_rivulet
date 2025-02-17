//
// Created by Chris Kramer on 2/12/25.
//

#ifndef JSON_STREAM_TESTS_H
#define JSON_STREAM_TESTS_H

#include <check.h>
#include <json_stream.h>
#include <stdbool.h>

Suite* json_core_suite(void);
Suite* json_files_suite(void);
Suite* json_buffered_suite(void);

bool expect_success(JsonStream* stream);
bool expect_error(JsonStream* stream, JsonErrorType error);
bool compare_full_buffer_to_cjson(const char* buffer, JsonStreamOptions options);
char* read_json_file(const char* filename);
char* compact_json_file(const char* filename);

typedef struct CompactTestCase {
  bool compact;
  char* name;
  char* file;
} CompactTestCase;

extern CompactTestCase all_files[13];
extern CompactTestCase small_files[5];
extern CompactTestCase large_files[8];

#endif //JSON_STREAM_TESTS_H
