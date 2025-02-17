//
// Created by Chris Kramer on 2/13/25.
//

#include <cJSON.h>
#include <stdio.h>
#include <sys/errno.h>


#include "json_tests.h"

CompactTestCase all_files[] = {
    {false, "Basic", "tests/basic_json.json"},
    {false, "Basic With Large Number", "tests/basic_json_with_large_num.json"},
    {false, "Broad Tree", "tests/broad_tree.json"},
    {false, "Deep Tree", "tests/deep_tree.json"},
    {false, "Full JSON Schema", "tests/full_json_schema.json"},
    {false, "Hello World", "tests/hello_world.json"},
    {false, "Lots of Numbers", "tests/lots_of_numbers.json"},
    {false, "Lots of Strings", "tests/lots_of_strings.json"},
    {false, "Project Lock", "tests/project_lock.json"},
    {false, "400 Bytes", "tests/400B.json"},
    {false, "4 Kilobytes", "tests/4KB.json"},
    {false, "40 Kilobytes", "tests/40KB.json"},
    {false, "400 Kilobytes", "tests/400KB.json"},
};

CompactTestCase small_files[] = {
    {false, "Basic", "tests/basic_json.json"},
    {false, "Basic With Large Number", "tests/basic_json_with_large_num.json"},
    {false, "Full JSON Schema", "tests/full_json_schema.json"},
    {false, "Hello World", "tests/hello_world.json"},
    {false, "400 Bytes", "tests/400B.json"},
};

CompactTestCase large_files[] = {
    {false, "Broad Tree", "tests/broad_tree.json"},
    {false, "Deep Tree", "tests/deep_tree.json"},
    {false, "Lots of Numbers", "tests/lots_of_numbers.json"},
    {false, "Lots of Strings", "tests/lots_of_strings.json"},
    {false, "Project Lock", "tests/project_lock.json"},
    {false, "4 Kilobytes", "tests/4KB.json"},
    {false, "40 Kilobytes", "tests/40KB.json"},
    {false, "400 Kilobytes", "tests/400KB.json"},
};

char* read_json_file(const char* filename) {
    FILE* file = NULL;
    char* buffer = NULL;
    file = fopen(filename, "r");
    if (!file) {
        goto error;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < 0) {
        goto error;
    }

    buffer = malloc(size + 1);
    if (!buffer) {
        goto error;
    }
    buffer[size] = '\0';

    rewind(file);

    size_t remaining_size = size;

    size_t result = fread(buffer, remaining_size, 1, file);
    if (result == 0 && !feof(file)) {
        goto error;
    }

    fclose(file);
    return buffer;

error:
    if (file) {
        fclose(file);
    }

    if (buffer) {
        free(buffer);
    }

    return NULL;
}

char* compact_json_file(const char* filename) {
    char* json_string = NULL;
    char* compact_json = NULL;
    cJSON* json = NULL;

    json_string = read_json_file(filename);
    if (!json_string) {
        goto error;
    }

    json = cJSON_Parse(json_string);
    if (!json) {
        goto error;
    }

    compact_json = cJSON_PrintUnformatted(json);
    if (!compact_json) {
        goto error;
    }

    free(json_string);
    cJSON_Delete(json);

    return compact_json;

error:
    if (json_string) {
        free(json_string);
    }
    if (compact_json) {
        free(compact_json);
    }
    if (json) {
        cJSON_Delete(json);
    }
    return NULL;
}
