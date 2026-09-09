#include "native.h"
#include "re.h"
#include "guard.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = UINT32_C(0x91e10da5);
static uint64_t comparisons, guarded_comparisons, binary_comparisons;
static uint32_t next(void) {
    rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
    return rng;
}
static void compare(const char *source, const unsigned char *text, size_t n, int guarded) {
    // The C oracle requires a terminator; the candidate receives exactly n
    // readable bytes. Never grant the candidate the oracle's extra byte.
    char *copy = malloc(n + 1);
    assert(copy);
    memcpy(copy, text, n);
    copy[n] = 0;
    re_t original = re_compile(source);
    assert(original);
    int expected_length = -99;
    int expected = re_matchp(original, copy, &expected_length);
    drbz_regex pattern;
    assert(drbz_regex_compile(&pattern, (const unsigned char *)source, strlen(source)) == 0);
    for (int optimized = 0; optimized <= 1; ++optimized) {
        int length = -99;
        int found = drbz_regex_search(&pattern, text, n, &length, optimized, 10000000);
        if (found != expected || length != expected_length) {
            fprintf(stderr, "REGEX_LONG_MISMATCH pattern=%s size=%zu optimized=%d C=%d/%d Zig=%d/%d\n",
                    source, n, optimized, expected, expected_length, found, length);
            free(copy);
            exit(43);
        }
        ++comparisons;
        guarded_comparisons += guarded != 0;
    }
    free(copy);
}
static void deterministic(void) {
    unsigned char storage[4160];
    const size_t sizes[] = {0, 1, 14, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 1024, 4096};
    const char *patterns[] = {"a", "needle", "a.b", "ab$", "a?b", "a+b", "a*b", "[a-z]+\\d?", "[^a]", "\\w+", "\\s", "^a+$"};
    for (size_t offset = 0; offset < 32; ++offset) {
        for (size_t s = 0; s < sizeof sizes / sizeof *sizes; ++s) {
            size_t n = sizes[s];
            unsigned char *text = storage + offset;
            for (size_t i = 0; i < n; ++i) text[i] = i % 3 == 0 ? 'a' : 'x';
            for (size_t p = 0; p < sizeof patterns / sizeof *patterns; ++p) compare(patterns[p], text, n, 0);
            if (n >= 6) {
                const size_t positions[] = {0, 1, 14, 15, 16, 17, 30, 31, 32, n / 2, n - 6};
                for (size_t p = 0; p < sizeof positions / sizeof *positions; ++p) {
                    size_t at = positions[p];
                    if (at > n - 6) continue;
                    memset(text, 'x', n);
                    memcpy(text + at, "needle", 6);
                    compare("needle", text, n, 0);
                    // A false first-byte candidate and partial suffix exercise
                    // failed matches before/after SIMD candidate skipping.
                    if (at > 0) text[0] = 'n';
                    text[n - 1] = 'n';
                    compare("needle", text, n, 0);
                }
            }
        }
    }
}
static void generated(void) {
    unsigned char storage[1088];
    const char *patterns[] = {"abc", "[a-z]+", "[^a-z]", "[a-zA-Z0-9_]", "[-a]", "[a-]", "[\\d_]", "\\d+", "\\D", "\\W", "\\S", "\\s+", "a.b", "^a", "b$", ".+"};
    for (size_t fixture = 0; fixture < 4096; ++fixture) {
        size_t n = next() % 1025;
        size_t offset = fixture % 32;
        for (size_t i = 0; i < n; ++i) storage[offset + i] = (unsigned char)(1 + next() % 127);
        compare(patterns[fixture % (sizeof patterns / sizeof *patterns)], storage + offset, n, 0);
    }
}
static void protected_inputs(void) {
    struct guarded_page page = guard_create();
    memset(page.data, 'x', page.size);
    assert(page.size >= 64);
    memcpy(page.data + page.size - 6, "needle", 6);
    guard_readonly(page);
    // Every short tail, including exactly one and two SIMD blocks, ends at
    // inaccessible memory. Test starts as well as ends without write access.
    for (size_t n = 0; n <= page.size; ++n) {
        compare("needle", page.data + page.size - n, n, 1);
        compare("absent", page.data, n, 1);
    }
    guard_destroy(page);
}
static void binary_extension(void) {
    unsigned char storage[288];
    const char *sources[] = {"needle", ".+", "[^a]", "\\W"};
    for (size_t offset = 0; offset < 32; ++offset) {
        for (size_t n = 0; n <= 256; ++n) {
            for (size_t i = 0; i < n; ++i) storage[offset + i] = (unsigned char)next();
            if (n >= 6) memcpy(storage + offset + n - 6, "needle", 6);
            for (size_t p = 0; p < sizeof sources / sizeof *sources; ++p) {
                drbz_regex pattern;
                assert(drbz_regex_compile(&pattern, (const unsigned char *)sources[p], strlen(sources[p])) == 0);
                int slow_length, fast_length;
                int slow = drbz_regex_search(&pattern, storage + offset, n, &slow_length, 0, 10000000);
                int fast = drbz_regex_search(&pattern, storage + offset, n, &fast_length, 1, 10000000);
                if (slow != fast || slow_length != fast_length) {
                    fprintf(stderr, "REGEX_BINARY_MISMATCH size=%zu offset=%zu\n", n, offset);
                    exit(43);
                }
                ++binary_comparisons;
            }
        }
    }
}
int main(void) {
    deterministic();
    generated();
    protected_inputs();
    binary_extension();
    printf("REGEX_LONG_EVIDENCE {\"c_comparisons\":%" PRIu64 ",\"guarded_c_comparisons\":%" PRIu64 ",\"binary_baseline_comparisons\":%" PRIu64 ",\"seed\":2447445413,\"maximum_fixture_bytes\":4096,\"formal_proof\":false}\n", comparisons, guarded_comparisons, binary_comparisons);
    return 0;
}
