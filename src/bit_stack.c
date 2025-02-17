//
// Created by Chris Kramer on 2/7/25.
//

#include "bit_stack.h"

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>


void json_z_bits_init(JsonBitStack *bits) {
    assert(bits);

    *bits = (JsonBitStack){0};
    bits->capacity = 64;
}

void json_z_bits_clear(JsonBitStack *bits) {
    assert(bits);

    if (bits->allocated) {
        free(bits->bits.array);
    }

    *bits = (JsonBitStack){0};
    bits->capacity = 64;
}

bool json_z_bits_push(JsonBitStack *bits, bool value) {
    assert(bits);

    if (bits->count == bits->capacity) {
        uint64_t *buffer = bits->allocated ? bits->bits.array : NULL;
        size_t capacity = bits->capacity * 2;
        buffer = realloc(buffer, sizeof(uint64_t) * (capacity / 64));
        if (!buffer) {
            return false;
        }
        if (!bits->allocated) {
            *buffer = bits->bits.single;
            bits->allocated = true;
        }
        bits->capacity = capacity;
        bits->bits.array = buffer;
    }
    if (bits->allocated) {
        size_t index = (size_t) floor(bits->count / 64.f);
        size_t shift = bits->count % bits->capacity;
        bits->bits.array[index] |= ((uint64_t) (value ? 1 : 0) << shift);
    } else {
        bits->bits.single |= ((uint64_t) (value ? 1 : 0) << bits->count);
    }
    bits->count++;
    return true;
}

bool json_z_bits_pop(JsonBitStack *bits) {
    assert(bits);
    assert(bits->count > 0);

    bool result = false;
    bits->count--;

    if (bits->count == 0) {
        return false;
    }

    if (bits->allocated) {
        size_t index = (size_t) floor(bits->count / 64.f);
        size_t shift = bits->count % 64;

        result = (bits->bits.array[index] >> shift) & 1;
    } else {
        result = (bits->bits.single >> (bits->count - 1)) & 1;
    }

    return result;
}

size_t json_z_bits_count(const JsonBitStack* bits) {
    return bits->count;
}
