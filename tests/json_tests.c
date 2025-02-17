//
// Created by Chris Kramer on 2/12/25.
//

#include "json_tests.h"
#include <check.h>

#include <_stdlib.h>
#include <stdio.h>

bool expect_success(JsonStream* stream) {
    return stream->error.type == JSON_ERROR_NONE;
}
bool expect_error(JsonStream* stream, JsonErrorType error) {
    return stream->error.type == error;
}

int main(void) {
    Suite* core_suite = json_core_suite();
    Suite* buffered_suite = json_buffered_suite();
    Suite* files_suite = json_files_suite();
    SRunner* runner = srunner_create(core_suite);

    srunner_add_suite(runner, buffered_suite);
    srunner_add_suite(runner, files_suite);

    srunner_set_fork_status(runner, CK_NOFORK);

    srunner_run_all(runner, CK_NORMAL);
    int number_failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
