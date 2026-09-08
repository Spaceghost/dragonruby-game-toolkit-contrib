#include "native.h"
#include <assert.h>
#include <string.h>

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

int main(void) {
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
    return 0;
}
