#include "competitive.h"
#include <string.h>

/* One readable algorithm per operation, with Clang's target-independent
 * vectors. memcpy permits unaligned loads without violating C alias rules. */
typedef unsigned char bytes8 __attribute__((ext_vector_type(8)));
typedef unsigned char bytes16 __attribute__((ext_vector_type(16)));
typedef unsigned char bytes32 __attribute__((ext_vector_type(32)));
typedef uint16_t wide32 __attribute__((ext_vector_type(32)));
typedef float floats8 __attribute__((ext_vector_type(8)));
typedef int masks8 __attribute__((ext_vector_type(8)));

size_t drbc_count_dual(const unsigned char *bytes, size_t length) {
    const bytes32 newline = (bytes32)'\n';
    size_t total = 0, offset = 0;
    while (length - offset >= 128) {
        size_t pairs = (length - offset) / 64;
        if (pairs > 255) pairs = 255;
        bytes32 even = (bytes32)0, odd = (bytes32)0;
        const size_t end = offset + pairs * 64;
        for (; offset < end; offset += 64) {
            bytes32 a, b;
            memcpy(&a, bytes + offset, sizeof a);
            memcpy(&b, bytes + offset + 32, sizeof b);
            even -= (bytes32)(a == newline);
            odd -= (bytes32)(b == newline);
        }
        wide32 counts = __builtin_convertvector(even, wide32)
                      + __builtin_convertvector(odd, wide32);
        total += __builtin_reduce_add(counts);
    }
    while (length - offset >= 32) {
        bytes32 value;
        memcpy(&value, bytes + offset, sizeof value);
        total += __builtin_reduce_add((bytes32)(value == newline) & (bytes32)1);
        offset += 32;
    }
    if (length - offset >= 16) {
        bytes16 value;
        memcpy(&value, bytes + offset, sizeof value);
        total += __builtin_reduce_add((bytes16)(value == (bytes16)'\n') & (bytes16)1);
        offset += 16;
    }
    if (length - offset >= 8) {
        bytes8 value;
        memcpy(&value, bytes + offset, sizeof value);
        total += __builtin_reduce_add((bytes8)(value == (bytes8)'\n') & (bytes8)1);
        offset += 8;
    }
    for (; offset < length; ++offset) total += bytes[offset] == '\n';
    return total;
}

static __attribute__((noinline)) void
scalar_block(float *x, float *y, const float *speed, size_t count,
             rival_random random, void *context) {
    for (size_t i = 0; i < count; ++i) {
        x[i] += speed[i];
        if (x[i] > 1280.0f) x[i] = random(context) * -1280.0f;
        y[i] += speed[i];
        if (y[i] > 720.0f) y[i] = random(context) * -720.0f;
    }
}

static __attribute__((noinline)) size_t
dense_both_wrap_run(float *x, float *y, const float *speed, size_t count,
                    rival_random random, void *context) {
    size_t i = 0;
    for (; i < count; ++i) {
        const float nx = x[i] + speed[i];
        const float ny = y[i] + speed[i];
        if (!(nx > 1280.0f && ny > 720.0f)) break;
        x[i] = random(context) * -1280.0f;
        y[i] = random(context) * -720.0f;
    }
    return i;
}

/* Keep the mixed exceptional work out of the vector-search loop. Passing the
 * already-computed results/masks avoids repeated arithmetic; one outlined copy
 * may also reduce ARM code size. CI keeps this only if the measured tradeoff is
 * worthwhile rather than assuming noinline is magic. */
static __attribute__((noinline)) void
mixed_repair(float *x, float *y, floats8 nx, floats8 ny,
             masks8 wrap_x, masks8 wrap_y, rival_random random, void *context) {
    memcpy(x, &nx, sizeof nx);
    memcpy(y, &ny, sizeof ny);
    for (size_t lane = 0; lane < 8; ++lane) {
        if (wrap_x[lane]) x[lane] = random(context) * -1280.0f;
        if (wrap_y[lane]) y[lane] = random(context) * -720.0f;
    }
}

void drbc_stars_block(float *x, float *y, const float *speed, size_t count,
                      rival_random random, void *context) {
    size_t i = 0;
    while (count - i >= 8) {
        floats8 vx, vy, vs;
        memcpy(&vx, x + i, sizeof vx);
        memcpy(&vy, y + i, sizeof vy);
        memcpy(&vs, speed + i, sizeof vs);
        const floats8 nx = vx + vs, ny = vy + vs;
        const masks8 wrap_x = nx > 1280.0f;
        const masks8 wrap_y = ny > 720.0f;
        const masks8 wraps = wrap_x | wrap_y;
        if (!__builtin_reduce_or(wraps)) {
            memcpy(x + i, &nx, sizeof nx);
            memcpy(y + i, &ny, sizeof ny);
            i += 8;
            continue;
        }
        if (__builtin_reduce_and(wrap_x & wrap_y)) {
            const size_t consumed = dense_both_wrap_run(x + i, y + i, speed + i,
                                                        count - i, random, context);
            i += consumed;
            continue;
        }
        mixed_repair(x + i, y + i, nx, ny, wrap_x, wrap_y, random, context);
        i += 8;
    }
    if (i < count) scalar_block(x + i, y + i, speed + i, count - i, random, context);
}
