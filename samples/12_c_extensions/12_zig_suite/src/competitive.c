#include "competitive.h"
#include <string.h>

/* One readable algorithm per operation, with Clang's target-independent
 * vectors. memcpy permits unaligned loads without violating C alias rules. */
typedef unsigned char bytes32 __attribute__((ext_vector_type(32)));
typedef uint16_t wide32 __attribute__((ext_vector_type(32)));
typedef float floats8 __attribute__((ext_vector_type(8)));

size_t drbc_count_dual(const unsigned char *bytes, size_t length) {
    const bytes32 newline = (bytes32)'\n';
    size_t total = 0, offset = 0;
    while (length - offset >= 64) {
        size_t pairs = (length - offset) / 64;
        if (pairs > 255) pairs = 255;
        bytes32 even = (bytes32)0, odd = (bytes32)0;
        const size_t end = offset + pairs * 64;
        for (; offset < end; offset += 64) {
            bytes32 a, b;
            memcpy(&a, bytes + offset, sizeof a);
            memcpy(&b, bytes + offset + 32, sizeof b);
            /* Comparison masks are all-ones: subtraction adds one per hit. */
            even -= (bytes32)(a == newline);
            odd -= (bytes32)(b == newline);
        }
        /* Each lane <= 255; the widened pair sum <= 510; total <= 16320. */
        wide32 counts = __builtin_convertvector(even, wide32)
                      + __builtin_convertvector(odd, wide32);
        total += __builtin_reduce_add(counts);
    }
    if (length - offset >= 32) {
        bytes32 value;
        memcpy(&value, bytes + offset, sizeof value);
        /* At most 32 hits here, so a byte reduction cannot overflow. */
        total += __builtin_reduce_add((bytes32)(value == newline) & (bytes32)1);
        offset += 32;
    }
    for (; offset < length; ++offset) total += bytes[offset] == '\n';
    return total;
}

static void scalar_block(float *x, float *y, const float *speed, size_t count,
                         rival_random random, void *context) {
    for (size_t i = 0; i < count; ++i) {
        x[i] += speed[i];
        if (x[i] > 1280.0f) x[i] = random(context) * -1280.0f;
        y[i] += speed[i];
        if (y[i] > 720.0f) y[i] = random(context) * -720.0f;
    }
}

void drbc_stars_block(float *x, float *y, const float *speed, size_t count,
                      rival_random random, void *context) {
    size_t i = 0;
    for (; count - i >= 8; i += 8) {
        floats8 vx, vy, vs;
        memcpy(&vx, x + i, sizeof vx);
        memcpy(&vy, y + i, sizeof vy);
        memcpy(&vs, speed + i, sizeof vs);
        const floats8 nx = vx + vs, ny = vy + vs;
        if (__builtin_reduce_or((nx > 1280.0f) | (ny > 720.0f))) {
            /* Keep the exceptional path small and ordered, not eight copies.
             * No array is updated before the first possible RNG callback. */
            scalar_block(x + i, y + i, speed + i, 8, random, context);
        } else {
            memcpy(x + i, &nx, sizeof nx);
            memcpy(y + i, &ny, sizeof ny);
        }
    }
    if (i < count) scalar_block(x + i, y + i, speed + i, count - i, random, context);
}
