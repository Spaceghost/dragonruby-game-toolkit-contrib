#include "native.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Single-threaded link-reference instrumentation. It deliberately does not
// claim to intercept shared-library internals, mmap or every allocator API.
// The compiler cannot see link-time --wrap rewriting. Volatile prevents it
// from assuming external allocator calls cannot touch these private counters.
static volatile int armed;
static volatile uint64_t counts[6];
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t);
int __real_posix_memalign(void **, size_t, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) { if (armed) ++counts[0]; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { if (armed) ++counts[1]; return __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { if (armed) ++counts[2]; return __real_realloc(p, n); }
void *__wrap_aligned_alloc(size_t a, size_t n) { if (armed) ++counts[3]; return __real_aligned_alloc(a, n); }
int __wrap_posix_memalign(void **p, size_t a, size_t n) { if (armed) ++counts[4]; return __real_posix_memalign(p, a, n); }
void __wrap_free(void *p) { if (armed) ++counts[5]; __real_free(p); }
#define CHECK(label, condition) do { if (!(condition)) { armed = 0; fprintf(stderr, "KERNEL_MISMATCH %s\n", label); exit(43); } } while (0)
static uint32_t next(uint32_t *state) { *state = *state * UINT32_C(1664525) + UINT32_C(1013904223); return *state; }
static float random_value(void *state) { return (float)(next(state) >> 8) / (float)UINT32_C(0xffffff); }
static void calibrate(void) {
    armed = 1;
    void *a = malloc(32), *b = calloc(2, 32), *c = aligned_alloc(64, 64), *d = NULL;
    CHECK("probe allocations", a && b && c);
    a = realloc(a, 96);
    CHECK("probe realloc", a);
    CHECK("probe posix_memalign", posix_memalign(&d, 64, 64) == 0 && d);
    free(a); free(b); free(c); free(d);
    armed = 0;
    printf("ALLOCATOR_CALIBRATION {\"malloc\":%" PRIu64 ",\"calloc\":%" PRIu64 ",\"realloc\":%" PRIu64 ",\"aligned_alloc\":%" PRIu64 ",\"posix_memalign\":%" PRIu64 ",\"free\":%" PRIu64 "}\n", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    for (size_t i = 0; i < 5; ++i) CHECK("probe interception", counts[i] == 1);
    CHECK("probe free interception", counts[5] == 4);
    for (size_t i = 0; i < 6; ++i) counts[i] = 0;
}
int main(void) {
    calibrate();
    unsigned char bytes[16353];
    int32_t values[65], squares[65];
    double terms[65];
    float x[65], y[65], speed[65];
    drbz_star stars[65];
    for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = i % 7 == 0 ? '\n' : 0xff;
    for (size_t i = 0; i < 65; ++i) {
        values[i] = (int32_t)i - 32;
        terms[i] = (double)i - 32;
        stars[i] = (drbz_star){1280.0f, 720.0f, (float)(i % 8)};
        x[i] = stars[i].x; y[i] = stars[i].y; speed[i] = stars[i].s;
    }
    drbz_scanner scanner;
    drbz_regex pattern;
    uint32_t rng_a = 123, rng_b = 123;
    uint64_t repetitions = 0;
    armed = 1;
    drbz_scanner_reset(&scanner);
    for (; repetitions < 1000; ++repetitions) {
        int square;
        CHECK("square", drbz_square(46340, &square) == 0 && square == 2147395600);
        CHECK("squares", drbz_squares(values, squares, 65) == 0);
        for (size_t i = 0; i < 65; ++i) CHECK("square lanes", squares[i] == values[i] * values[i]);
        CHECK("ordered sum", drbz_sum_ordered(3.0, terms, 65) == 3.0);
        CHECK("unrolled sum", drbz_sum_unrolled(3.0, terms, 65) == 3.0);
        CHECK("newline counter", drbz_count_blocked(bytes, sizeof bytes) == drbz_count_scalar(bytes, sizeof bytes));
        drbz_stars_scalar(stars, 65, random_value, &rng_a);
        drbz_stars_soa(x, y, speed, 65, random_value, &rng_b);
        CHECK("star RNG", rng_a == rng_b);
        for (size_t i = 0; i < 65; ++i) {
            CHECK("star coordinates", memcmp(&stars[i].x, &x[i], sizeof(float)) == 0 && memcmp(&stars[i].y, &y[i], sizeof(float)) == 0);
        }
        const uint32_t *pixels = drbz_scanner_frame(&scanner);
        size_t green = 0;
        for (size_t i = 0; i < 100; ++i) green += pixels[i] == UINT32_C(0xff00ff00);
        CHECK("scanner row", green == 10);
        CHECK("regex compile", drbz_regex_compile(&pattern, (const unsigned char *)"needle", 6) == 0);
        int length = -1;
        CHECK("regex search", drbz_regex_search(&pattern, (const unsigned char *)"nnnnnnnnnnnnnnnnxxxxneedle", 26, &length, 1, 10000) == 20 && length == 6);
    }
    armed = 0;
    uint64_t total = 0;
    for (size_t i = 0; i < 6; ++i) total += counts[i];
    if (total != 0) {
        fprintf(stderr, "ALLOCATION_MISMATCH observed=%" PRIu64 "\n", total);
        return 44;
    }
    printf("ALLOCATION_EVIDENCE {\"repetitions\":%" PRIu64 ",\"malloc\":%" PRIu64 ",\"calloc\":%" PRIu64 ",\"realloc\":%" PRIu64 ",\"aligned_alloc\":%" PRIu64 ",\"posix_memalign\":%" PRIu64 ",\"free\":%" PRIu64 ",\"scope\":\"native.zig exports and regex compile/search with a nonallocating RNG callback; linked references only\"}\n", repetitions, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    return 0;
}
