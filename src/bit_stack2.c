#include "bit_stack.h"

#include <assert.h>
#include <stdlib.h>


static bool json_z_bits_push_current(JsonBitStack* bits) {
    size_t index = (bits->count - 63) / 63;
    if ((bits->count - 63) / 63 >= bits->capacity) {
        size_t capacity = bits->capacity == 0 ? 4 : bits->capacity * 2;
        uint64_t* buffer = realloc(bits->array, capacity * sizeof(*buffer));
        if (!buffer) {
            return false;
        }

        bits->array = buffer;
        bits->capacity = capacity;
    }

    bits->array[index] = bits->current;
    bits->current = 1;
    return true;
}

static void json_z_bits_pop_current(JsonBitStack* bits) {
    size_t index = ((bits->count - 63) / 63) - 1;
    bits->current = bits->array[index];
}

void json_z_bits_init(JsonBitStack *bits) {
    assert(bits);

    *bits = (JsonBitStack){0};
    bits->current = 1;
    bits->capacity = 0;
}

void json_z_bits_clear(JsonBitStack *bits) {
    assert(bits);
    free(bits->array);

    *bits = (JsonBitStack){0};
    bits->current = 1;
    bits->capacity = 0;
}

bool json_z_bits_push(JsonBitStack *bits, bool value) {
    if ((bits->current & 0x8000000000000000) != 0) {
        if (!json_z_bits_push_current(bits)) {
            return false;
        }
    }
    bits->current = (bits->current << 1) | (value ? 1 : 0);
    bits->count += 1;
    return true;
}

bool json_z_bits_pop(JsonBitStack *bits) {
    assert(bits);
    assert(bits->count > 0);

    bits->current >>= 1;
    bits->count -= 1;

    if (bits->count == 0) {
        return false;
    }
    if (bits->current == 1) {
        json_z_bits_pop_current(bits);
    }

    return bits->current & 1;
}

size_t json_z_bits_count(const JsonBitStack* bits) {
    return bits->count;
}
