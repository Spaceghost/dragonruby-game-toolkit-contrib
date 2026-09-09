#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "native.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "support/guard.h"

#ifdef DRB_ZIG_DYNAMIC
#include "support/dynlib.h"
static void (*loaded_reset)(void);
static void (*loaded_frame)(uint32_t *);
static size_t (*loaded_count)(const unsigned char *, size_t);
#define drb_zig_scanner_reset loaded_reset
#define drb_zig_scanner_frame loaded_frame
#define drb_zig_count_newlines loaded_count
#endif

/* Scalar reference from 03_native_pixel_arrays/app/ext.c (MIT, DragonRuby). */
static void reference_frame(uint32_t *pixels, int *position, int *increment) {
    for (int i = 0; i < 100; ++i) pixels[i] = 0xff000000;
    for (int i = 0; i < 10; ++i) pixels[*position * 10 + i] = 0xff00ff00;
    *position += *increment;
    if (*increment > 0 && *position >= 10) {
        *increment = -1;
        *position = 9;
    } else if (*increment < 0 && *position < 0) {
        *increment = 1;
        *position = 1;
    }
}

static size_t reference_count(const unsigned char *data, size_t len) {
    size_t count = 0;
    for (size_t i = 0; i < len; ++i) count += data[i] == '\n';
    return count;
}

static void test_guard_pages(void) {
    struct guarded_page input = guard_create();
    assert(input.size >= 512);
    for (size_t i = 0; i < input.size; ++i)
        input.data[i] = i % 3 == 0 ? '\n' : (unsigned char)i;
    guard_readonly(input);
    for (size_t len = 0; len <= 257; ++len) {
        const unsigned char *tail = input.data + input.size - len;
        assert(drb_zig_count_newlines(tail, len) == reference_count(tail, len));
        assert(drb_zig_count_newlines(input.data, len) == reference_count(input.data, len));
    }
    /* Even a protected, non-NULL pointer must not be accessed for zero length. */
    assert(drb_zig_count_newlines(input.base, 0) == 0);
    guard_destroy(input);

    struct guarded_page output = guard_create();
    uint32_t *pixels = (uint32_t *)(output.data + output.size - 100 * sizeof(uint32_t));
    drb_zig_scanner_reset();
    drb_zig_scanner_frame(pixels);
    for (int i = 0; i < 100; ++i)
        assert(pixels[i] == (i < 10 ? 0xff00ff00u : 0xff000000u));
    guard_destroy(output);
}

int main(int argc, char **argv) {
#ifdef DRB_ZIG_DYNAMIC
    assert(argc == 2);
    test_library lib = library_open(argv[1]);
    loaded_reset = (void (*)(void))library_symbol(lib, "drb_zig_scanner_reset");
    loaded_frame = (void (*)(uint32_t *))library_symbol(lib, "drb_zig_scanner_frame");
    loaded_count = (size_t (*)(const unsigned char *, size_t))library_symbol(lib, "drb_zig_count_newlines");
    assert(loaded_reset && loaded_frame && loaded_count);
#else
    (void)argv;
    assert(argc == 1);
#endif
    assert(DRB_ZIG_DIMENSION == 10 && DRB_ZIG_PIXEL_COUNT == 100);
    uint32_t guarded[DRB_ZIG_PIXEL_COUNT + 2];
    uint32_t expected[DRB_ZIG_PIXEL_COUNT];
    guarded[0] = guarded[DRB_ZIG_PIXEL_COUNT + 1] = 0xdeadbeef;
    int position = 0, increment = 1;
    drb_zig_scanner_reset();
    for (int frame = 0; frame < 10000; ++frame) {
        drb_zig_scanner_frame(guarded + 1);
        reference_frame(expected, &position, &increment);
        assert(memcmp(guarded + 1, expected, sizeof expected) == 0);
        assert(guarded[0] == 0xdeadbeef);
        assert(guarded[DRB_ZIG_PIXEL_COUNT + 1] == 0xdeadbeef);
    }
    drb_zig_scanner_reset();
    position = 0;
    increment = 1;
    reference_frame(expected, &position, &increment);
    drb_zig_scanner_frame(guarded + 1);
    assert(memcmp(guarded + 1, expected, sizeof expected) == 0);

    assert(drb_zig_count_newlines(NULL, 0) == 0);
    unsigned char data[288];
    for (size_t i = 0; i < sizeof data; ++i)
        data[i] = i % 7 == 0 ? '\n' : (unsigned char)(i * 31);
    for (size_t offset = 0; offset < 32; ++offset) {
        for (size_t len = 0; len <= 257; ++len) {
            assert(drb_zig_count_newlines(data + offset, len) ==
                   reference_count(data + offset, len));
        }
    }
    memset(data, '\n', sizeof data);
    for (size_t offset = 0; offset < 32; ++offset) {
        for (size_t len = 0; len <= 257; ++len)
            assert(drb_zig_count_newlines(data + offset, len) == len);
    }
    test_guard_pages();
#ifdef DRB_ZIG_DYNAMIC
    library_close(lib);
#endif
    puts("C ABI, scalar references and protected-page tests passed.");
    return 0;
}
