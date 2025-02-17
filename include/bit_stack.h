//
// Created by Chris Kramer on 2/7/25.
//

#ifndef BIT_STACK_H
#define BIT_STACK_H

#include <stdint.h>
#include <stddef.h>

typedef struct JsonBitStack {
    uint64_t* array;
    uint64_t current;
    size_t count;
    size_t capacity;
} JsonBitStack;

void json_z_bits_init(JsonBitStack *bits);

void json_z_bits_clear(JsonBitStack *bits);

bool json_z_bits_push(JsonBitStack *bits, bool value);

bool json_z_bits_pop(JsonBitStack *bits);

size_t json_z_bits_count(const JsonBitStack* bits);

#endif //BIT_STACK_H
