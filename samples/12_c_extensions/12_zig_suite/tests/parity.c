#include "native.h"
#include "re.h"
#include "guard.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int square(int);
void original_c_scanner(int *, uint32_t *);
void original_c_stars(drbz_star *, size_t, drbz_random, void *);
double control_c_sum(double, const double *, size_t);
size_t control_c_count(const unsigned char *, size_t);
void drb_zig_scanner_reset(void);
void drb_zig_scanner_frame(uint32_t *);
size_t drb_zig_count_newlines(const unsigned char *, size_t);

struct rng { uint32_t state; size_t calls; };
static float random_value(void *context) {
    struct rng *rng = context;
    rng->state = rng->state * UINT32_C(1664525) + UINT32_C(1013904223);
    ++rng->calls;
    return (float)(rng->state >> 8) / (float)UINT32_C(0xffffff);
}
static uint32_t float_bits(float value) { uint32_t bits; memcpy(&bits, &value, sizeof bits); return bits; }
static uint64_t double_bits(double value) { uint64_t bits; memcpy(&bits, &value, sizeof bits); return bits; }

static void test_square(void) {
    for (int value = -46340; value <= 46340; ++value) {
        int out = -1;
        assert(drbz_square(value, &out) == 0);
        assert(out == square(value));
    }
    int invalid[] = { INT_MIN, INT_MAX, 46341, -46341 };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        int out = 777;
        assert(drbz_square(invalid[i], &out) == 1 && out == 777);
    }
    int32_t in[260], out[262];
    for (size_t n = 0; n <= 260; ++n) {
        for (size_t i = 0; i < 260; ++i) in[i] = (int32_t)i - 130;
        for (size_t i = 0; i < 262; ++i) out[i] = -1;
        assert(drbz_squares(in, out + 1, n) == 0);
        assert(out[0] == -1 && out[n + 1] == -1);
        for (size_t i = 0; i < n; ++i) assert(out[i + 1] == square(in[i]));
        assert(drbz_squares(in, in, n) == 0);
        for (size_t i = 0; i < n; ++i) assert(in[i] == out[i + 1]);
    }
    for (size_t i = 0; i < 260; ++i) { in[i] = 3; out[i] = 777; }
    in[259] = INT_MAX;
    assert(drbz_squares(in, out, 260) == 1);
    for (size_t i = 0; i < 260; ++i) assert(out[i] == 777);
}

static void test_sum(void) {
    const double data[] = { 1e16, 1, -1e16, 3, -0.0, 1e-300, -1e-300, 4, -7, 0.0 };
    for (size_t start = 0; start < 5; ++start) for (size_t n = 0; n <= 10 - start; ++n) {
        double expected = control_c_sum(-0.0, data + start, n);
        assert(double_bits(drbz_sum_ordered(-0.0, data + start, n)) == double_bits(expected));
        assert(double_bits(drbz_sum_unrolled(-0.0, data + start, n)) == double_bits(expected));
    }
}

static size_t test_regex(void) {
    const char *patterns[] = { "", "a", "ab", "needle", "^", "$", "^$", "^a", "a$", "^a$", ".", ".*", ".+", "a*", "a+", "a?", "a*b", "a+b", "a?b", "a?b?", "ab?", "a.*b", "a.+b", "a*a*b", "[ab]", "[^a]", "[a-z]+", "[a-zA-Z0-9_]", "[a-b]*b", "[-a]", "[a-]", "[]", "[^]", "[\\d_]", "[\\s]", "\\d", "\\D", "\\w+", "\\W", "\\s+", "\\S", "\\.", "\\*", "[a-z]+\\d?" };
    const char alphabet[] = { 'a', 'b', '1', '\n' };
    size_t comparisons = 0;
    for (size_t p = 0; p < sizeof patterns / sizeof *patterns; ++p) {
        drbz_regex zig;
        assert(drbz_regex_compile(&zig, (const unsigned char *)patterns[p], strlen(patterns[p])) == 0);
        re_t original = re_compile(patterns[p]);
        assert(original);
        size_t count = 1;
        for (size_t n = 0; n <= 6; ++n, count *= 4) {
            for (size_t code = 0; code < count; ++code) {
                char text[8] = {0};
                size_t value = code;
                for (size_t i = 0; i < n; ++i) { text[i] = alphabet[value % 4]; value /= 4; }
                int expected_length = -99;
                int expected = re_matchp(original, text, &expected_length);
                for (int fast = 0; fast <= 1; ++fast) {
                    int length = -99;
                    int found = drbz_regex_search(&zig, (const unsigned char *)text, n, &length, fast, 1000000);
                    if (found != expected || length != expected_length) {
                        fprintf(stderr, "REGEX_MISMATCH pattern=%s text_code=%zu len=%zu optimized=%d C=%d/%d Zig=%d/%d\n", patterns[p], code, n, fast, expected, expected_length, found, length);
                        exit(1);
                    }
                    ++comparisons;
                }
            }
        }
    }
    drbz_regex binary;
    const unsigned char bytes[] = { 0, 0xff, '\n' };
    int length = 0;
    assert(drbz_regex_compile(&binary, (const unsigned char *)".+", 2) == 0);
    assert(drbz_regex_search(&binary, bytes, sizeof bytes, &length, 1, 1000) == 0 && length == 3);
    return comparisons;
}

static size_t test_stars(void) {
    drbz_star reference[273], scalar[273];
    float x[275], y[275], speed[275];
    size_t comparisons = 0;
    for (size_t offset = 0; offset < 8; ++offset) {
        const size_t sizes[] = { 0, 1, 7, 8, 9, 31, 64, 257 };
        for (size_t c = 0; c < sizeof sizes / sizeof *sizes; ++c) {
            size_t n = sizes[c];
            struct rng setup = { 123, 0 }, a = { 91827, 0 }, b = a, d = a;
            for (size_t i = 0; i < 275; ++i) x[i] = y[i] = speed[i] = -9876;
            for (size_t i = 0; i < n; ++i) {
                reference[i].x = random_value(&setup) * 4000 - 2000;
                reference[i].y = random_value(&setup) * 2000 - 1000;
                reference[i].s = random_value(&setup) * 10;
                if (i % 13 == 0) { reference[i].x = 1280; reference[i].y = 720; }
                scalar[i] = reference[i];
                x[i + offset] = reference[i].x;
                y[i + offset] = reference[i].y;
                speed[i + offset] = reference[i].s;
            }
            for (size_t tick = 0; tick < 1000; ++tick) {
                original_c_stars(reference, n, random_value, &a);
                drbz_stars_scalar(scalar, n, random_value, &b);
                drbz_stars_soa(x + offset, y + offset, speed + offset, n, random_value, &d);
                assert(a.state == b.state && a.state == d.state && a.calls == b.calls && a.calls == d.calls);
                for (size_t i = 0; i < n; ++i) {
                    assert(float_bits(reference[i].x) == float_bits(scalar[i].x));
                    assert(float_bits(reference[i].y) == float_bits(scalar[i].y));
                    assert(float_bits(reference[i].x) == float_bits(x[i + offset]));
                    assert(float_bits(reference[i].y) == float_bits(y[i + offset]));
                    assert(float_bits(reference[i].s) == float_bits(speed[i + offset]));
                    ++comparisons;
                }
            }
            assert(x[offset + n] == -9876 && y[offset + n] == -9876);
            if (offset) assert(x[offset - 1] == -9876 && y[offset - 1] == -9876);
        }
    }
    return comparisons;
}

static void test_bytes(void) {
    unsigned char data[65570];
    for (size_t mask = 0; mask < 65536; ++mask) {
        size_t expected = 0;
        for (size_t i = 0; i < 16; ++i) { data[i] = (mask & ((size_t)1 << i)) ? '\n' : 0xff; expected += data[i] == '\n'; }
        assert(drbz_count_blocked(data, 16) == expected);
    }
    memset(data, '\n', sizeof data);
    const size_t lengths[] = { 0, 1, 15, 16, 17, 31, 32, 33, 8159, 8160, 8161, 16320, 65536 };
    for (size_t offset = 0; offset < 33; ++offset) for (size_t j = 0; j < sizeof lengths / sizeof *lengths; ++j) assert(drbz_count_blocked(data + offset, lengths[j]) == lengths[j]);
    struct guarded_page page = guard_create();
    for (size_t i = 0; i < page.size; ++i) page.data[i] = (i % 7 == 0) ? '\n' : (unsigned char)i;
    guard_readonly(page);
    for (size_t n = 0; n <= page.size; ++n) {
        const unsigned char *tail = page.data + page.size - n;
        assert(drbz_count_blocked(tail, n) == control_c_count(tail, n));
        assert(drbz_count_blocked(page.data, n) == drb_zig_count_newlines(page.data, n));
    }
    guard_destroy(page);
}

static int test_scanner(int inject) {
    int state[2] = { 0, 1 };
    uint32_t reference[100], previous[100];
    drbz_scanner dirty;
    drbz_scanner_reset(&dirty);
    drb_zig_scanner_reset();
    for (size_t frame = 0; frame < 10000; ++frame) {
        original_c_scanner(state, reference);
        drb_zig_scanner_frame(previous);
        const uint32_t *pixels = drbz_scanner_frame(&dirty);
        uint32_t captured[100]; memcpy(captured, pixels, sizeof captured);
        if (inject && frame == 17) captured[7] ^= 1;
        for (size_t pixel = 0; pixel < 100; ++pixel) {
            assert(previous[pixel] == reference[pixel]);
            if (captured[pixel] != reference[pixel]) {
                fprintf(stderr, "PIXEL_MISMATCH frame=%zu pixel=%zu C=%08x Zig=%08x\n", frame, pixel, reference[pixel], captured[pixel]);
                return 42;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int inject = argc == 2 && strcmp(argv[1], "--inject-mismatch") == 0;
    int scanner_result = test_scanner(inject);
    if (scanner_result) return scanner_result;
    if (inject) return 1;
    test_square(); test_sum(); test_bytes();
    size_t regex_count = test_regex();
    size_t star_count = test_stars();
    printf("PROOF {\"square_inputs\":92681,\"scanner_frames\":10000,\"scanner_pixels\":1000000,\"regex_comparisons\":%zu,\"star_updates\":%zu,\"dragonruby_engine_validated\":false}\n", regex_count, star_count);
    return 0;
}
