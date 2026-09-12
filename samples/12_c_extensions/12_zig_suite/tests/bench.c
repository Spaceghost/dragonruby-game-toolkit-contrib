#include "native.h"
#include "re.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

void original_c_scanner(int *, uint32_t *);
void original_c_stars(drbz_star *, size_t, drbz_random, void *);
double control_c_sum(double, const double *, size_t);
size_t control_c_count(const unsigned char *, size_t);
void drb_zig_scanner_reset(void);
void drb_zig_scanner_frame(uint32_t *);
size_t drb_zig_count_newlines(const unsigned char *, size_t);

static volatile uint64_t observable;
static unsigned char bytes[1048576];
static double numbers[65536];
static drbz_star stars[65536];
static float xs[65536], ys[65536], speeds[65536];
static drbz_regex compiled;
static re_t c_compiled;
struct rng { uint32_t state; uint64_t calls; };
static struct rng random_state;

static uint64_t clock_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER value, frequency;
    assert(QueryPerformanceFrequency(&frequency));
    assert(QueryPerformanceCounter(&value));
    uint64_t v = (uint64_t)value.QuadPart, f = (uint64_t)frequency.QuadPart;
    return (v / f) * UINT64_C(1000000000) + (v % f) * UINT64_C(1000000000) / f;
#else
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
#endif
}
static float random_value(void *context) {
    struct rng *r = context;
    r->state = r->state * UINT32_C(1664525) + UINT32_C(1013904223);
    ++r->calls;
    return (float)(r->state >> 8) / (float)UINT32_C(0xffffff);
}
static uint64_t bits(double value) { uint64_t out; memcpy(&out, &value, sizeof out); return out; }
static uint32_t fbits(float value) { uint32_t out; memcpy(&out, &value, sizeof out); return out; }

enum task { COUNT, SUM, SCANNER, STARS, REGEX };
struct scenario { const char *name; enum task task; size_t size, iterations; const char *pattern; int wrap; };

static void prepare(const struct scenario *s) {
    random_state = (struct rng){ 72819, 0 };
    for (size_t i = 0; i < s->size && i < sizeof bytes; ++i) bytes[i] = i % 23 == 0 ? '\n' : (unsigned char)(i * 37);
    if (s->task == REGEX) {
        memset(bytes, 'x', s->size);
        if (strcmp(s->pattern, "needle") == 0) memcpy(bytes + s->size - 6, "needle", 6);
        else bytes[s->size - 1] = '7';
        bytes[s->size] = 0;
    }
    if (s->task == STARS) {
        for (size_t i = 0; i < s->size; ++i) {
            stars[i] = (drbz_star){ (float)(i % 64) * .25f, (float)(i % 31) * .25f, s->wrap ? 4000.0f : .001f };
            xs[i] = stars[i].x; ys[i] = stars[i].y; speeds[i] = stars[i].s;
        }
    }
    if (s->task == SUM) for (size_t i = 0; i < s->size; ++i) numbers[i] = (double)((int)(i % 200) - 100) * .125;
    drb_zig_scanner_reset();
}

// All buffers, pattern compilation and reset work are outside this region.
// Each candidate is separately compiled; no LTO or fast-math is requested.
static uint64_t execute(const struct scenario *s, int implementation) {
    uint64_t checksum = 0;
    if (s->task == COUNT) {
        size_t (*functions[])(const unsigned char *, size_t) = { control_c_count, drb_zig_count_newlines, drbz_count_blocked };
        size_t (*function)(const unsigned char *, size_t) = functions[implementation];
        for (size_t i = 0; i < s->iterations; ++i) checksum += function(bytes, s->size);
    } else if (s->task == SUM) {
        double (*functions[])(double, const double *, size_t) = { control_c_sum, drbz_sum_ordered, drbz_sum_unrolled };
        double (*function)(double, const double *, size_t) = functions[implementation];
        for (size_t i = 0; i < s->iterations; ++i) checksum += bits(function(0, numbers, s->size));
    } else if (s->task == SCANNER) {
        int state[2] = { 0, 1 };
        uint32_t pixels[100];
        drbz_scanner dirty; drbz_scanner_reset(&dirty);
        for (size_t i = 0; i < s->iterations; ++i) {
            const uint32_t *frame = pixels;
            if (implementation == 0) original_c_scanner(state, pixels);
            else if (implementation == 1) drb_zig_scanner_frame(pixels);
            else frame = drbz_scanner_frame(&dirty);
            checksum += frame[(i * 13) % 100];
        }
    } else if (s->task == STARS) {
        for (size_t i = 0; i < s->iterations; ++i) {
            if (implementation == 0) original_c_stars(stars, s->size, random_value, &random_state);
            else if (implementation == 1) drbz_stars_scalar(stars, s->size, random_value, &random_state);
            else drbz_stars_soa(xs, ys, speeds, s->size, random_value, &random_state);
        }
        // Full-state consumption is performed by checksum_after, outside timing.
        checksum = random_state.state + random_state.calls;
    } else {
        for (size_t i = 0; i < s->iterations; ++i) {
            int length = 0;
            int position = implementation == 0 ? re_matchp(c_compiled, (const char *)bytes, &length) : drbz_regex_search(&compiled, bytes, s->size, &length, implementation == 2, 100000000);
            assert(position >= 0);
            checksum += (uint64_t)(position + 1) * UINT64_C(65537) + (unsigned)length;
        }
    }
    return checksum;
}
static uint64_t checksum_after(const struct scenario *s, int implementation, uint64_t checksum) {
    if (s->task == STARS) for (size_t i = 0; i < s->size; ++i) {
        float x = implementation == 2 ? xs[i] : stars[i].x;
        float y = implementation == 2 ? ys[i] : stars[i].y;
        checksum = (checksum ^ fbits(x)) * UINT64_C(1099511628211);
        checksum = (checksum ^ fbits(y)) * UINT64_C(1099511628211);
    }
    observable = checksum;
    return checksum;
}

int main(int argc, char **argv) {
    int trials = 11;
    if (argc == 2) { trials = atoi(argv[1]); if (trials < 3 || trials > 101) return 2; }
    const struct scenario cases[] = {
        { "lf/tiny", COUNT, 32, 262144, NULL, 0 },
        { "lf/page", COUNT, 4096, 16384, NULL, 0 },
        { "lf/large", COUNT, 1048576, 128, NULL, 0 },
        { "sum/tiny", SUM, 8, 262144, NULL, 0 },
        { "sum/large", SUM, 65536, 256, NULL, 0 },
        { "scanner/frame", SCANNER, 100, 500000, NULL, 0 },
        { "stars/small", STARS, 64, 32768, NULL, 0 },
        { "stars/large", STARS, 16384, 1024, NULL, 0 },
        { "stars/all-wrap", STARS, 4096, 512, NULL, 1 },
        { "regex/literal-small", REGEX, 128, 8192, "needle", 0 },
        { "regex/literal-large", REGEX, 65536, 128, "needle", 0 },
        { "regex/class", REGEX, 4096, 512, "[a-z]+\\d", 0 },
    };
    const char *names[5][3] = {
        { "c_control", "zig_previous16", "zig_blocked32" },
        { "c_control", "zig_ordered", "zig_unrolled" },
        { "c_original_kernel", "zig_previous", "zig_dirty_rows" },
        { "c_original_kernel", "zig_scalar", "zig_soa8" },
        { "c_original", "zig_baseline", "zig_bitmap_simd" },
    };
    uint32_t order_seed = 192873;
    for (size_t c = 0; c < sizeof cases / sizeof *cases; ++c) {
        const struct scenario *s = &cases[c];
        if (s->task == REGEX) {
            c_compiled = re_compile(s->pattern); assert(c_compiled);
            assert(drbz_regex_compile(&compiled, (const unsigned char *)s->pattern, strlen(s->pattern)) == 0);
        }
        uint64_t expected_checksum = 0;
        for (int implementation = 0; implementation < 3; ++implementation) {
            prepare(s);
            uint64_t result = checksum_after(s, implementation, execute(s, implementation));
            if (implementation == 0) expected_checksum = result;
            else assert(result == expected_checksum);
        }
        for (int trial = 0; trial < trials; ++trial) {
            int order[] = { 0, 1, 2 };
            for (int k = 2; k > 0; --k) {
                order_seed = order_seed * UINT32_C(1664525) + UINT32_C(1013904223);
                int j = (int)(order_seed % (unsigned)(k + 1));
                int tmp = order[k]; order[k] = order[j]; order[j] = tmp;
            }
            for (int k = 0; k < 3; ++k) {
                int implementation = order[k];
                prepare(s);
                uint64_t start = clock_ns();
                uint64_t result = execute(s, implementation);
                uint64_t elapsed = clock_ns() - start;
                result = checksum_after(s, implementation, result);
                assert(result == expected_checksum);
                printf("BENCH {\"case\":\"%s\",\"variant\":\"%s\",\"size\":%zu,\"iterations\":%zu,\"trial\":%d,\"nanoseconds\":%" PRIu64 ",\"checksum\":%" PRIu64 ",\"allocation_count\":null}\n", s->name, names[s->task][implementation], s->size, s->iterations, trial, elapsed, result);
            }
        }
    }
    return 0;
}
