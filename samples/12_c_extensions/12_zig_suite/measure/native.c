#include "bench.h"
#include "native.h"
#include "apps.h"
#include "re.h"
#include <limits.h>

void original_c_scanner(int *, uint32_t *);
void original_c_stars(drbz_star *, size_t, drbz_random, void *);
double control_c_sum(double, const double *, size_t);
size_t control_c_count(const unsigned char *, size_t);
size_t drb_zig_count_newlines(const unsigned char *, size_t);
void drb_zig_scanner_reset(void);
void drb_zig_scanner_frame(uint32_t *);
uint64_t drbz_measure_noop(uint64_t);

enum { LF, SUM, SCANNER, STARS, REGEX, NOOP };
/* A fixed rotating working set, not an assertion about the host's cache size. */
static unsigned char input[16 * 1024 * 1024 + 64];
static double values[65536];
static drbz_star stars[16384];
static float xs[16384], ys[16384], speeds[16384];
static drbz_regex pattern;
static re_t c_pattern;
static const char *pattern_text = "needle";
static uint32_t rng;
static uint64_t rng_calls;
static int scanner_state[2];
static uint32_t scanner_pixels[100];
static drbz_scanner dirty;
static uint64_t f64bits(double x) { uint64_t u; memcpy(&u, &x, sizeof u); return u; }
static uint32_t f32bits(float x) { uint32_t u; memcpy(&u, &x, sizeof u); return u; }
static float random_float(void *context) {
    (void)context; ++rng_calls;
    return (float)(bench_random(&rng) >> 8) / (float)UINT32_C(0xffffff);
}
static void reset(const bench_case *c, uint32_t seed) {
    rng = seed; rng_calls = 0;
    if (c->task == LF) {
        size_t bytes = c->detail == 2 ? 16 * 1024 * 1024 : c->size + 63;
        for (size_t i = 0; i < bytes; ++i) input[i] = (unsigned char)(bench_random(&seed) >> 24);
    } else if (c->task == SUM) {
        for (size_t i = 0; i < c->size; ++i) values[i] = (double)((int)(bench_random(&seed) % 2048) - 1024) * .125;
    } else if (c->task == STARS) {
        for (size_t i = 0; i < c->size; ++i) {
            stars[i] = (drbz_star){(float)(bench_random(&seed) % 1280), (float)(bench_random(&seed) % 720), c->detail ? 4000.0f : .001f};
            xs[i] = stars[i].x; ys[i] = stars[i].y; speeds[i] = stars[i].s;
        }
    } else if (c->task == REGEX) {
        memset(input, c->detail == 3 ? 'n' : 'x', c->size); input[c->size] = 0;
        if (c->detail == 0) memcpy(input, "needle", 6);
        if (c->detail == 1 || c->detail == 4) memcpy(input + c->size - 6, "needle", 6);
        c_pattern = re_compile(pattern_text); assert(c_pattern);
        assert(drbz_regex_compile(&pattern, (const unsigned char *)pattern_text, strlen(pattern_text)) == 0);
    }
    scanner_state[0] = 0; scanner_state[1] = 1;
    drbz_scanner_reset(&dirty); drb_zig_scanner_reset();
}
static uint64_t batch(const bench_case *c, unsigned variant, size_t n) {
    uint64_t checksum = 0;
    if (c->task == LF) {
        size_t (*f[])(const unsigned char *, size_t) = {control_c_count, drb_zig_count_newlines, drbz_count_blocked};
        for (size_t i = 0; i < n; ++i) {
            size_t offset = c->detail == 2 ? (i % 16) * c->size : (c->detail == 1 ? 63 : 0);
            checksum += f[variant](input + offset, c->size);
        }
    } else if (c->task == SUM) {
        double (*f[])(double, const double *, size_t) = {control_c_sum, drbz_sum_ordered, drbz_sum_unrolled};
        for (size_t i = 0; i < n; ++i) checksum += f64bits(f[variant](0, values, c->size));
    } else if (c->task == SCANNER) {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t *pixels = scanner_pixels;
            if (variant == 0) original_c_scanner(scanner_state, scanner_pixels);
            else if (variant == 1) drb_zig_scanner_frame(scanner_pixels);
            else pixels = drbz_scanner_frame(&dirty);
            checksum += pixels[i % 100];
        }
    } else if (c->task == STARS) {
        for (size_t i = 0; i < n; ++i) {
            if (variant == 0) original_c_stars(stars, c->size, random_float, NULL);
            else if (variant == 1) drbz_stars_scalar(stars, c->size, random_float, NULL);
            else drbz_stars_soa(xs, ys, speeds, c->size, random_float, NULL);
        }
        checksum = rng + rng_calls;
    } else if (c->task == REGEX) {
        for (size_t i = 0; i < n; ++i) {
            if (c->detail == 4) {
                if (variant == 0) { c_pattern = re_compile(pattern_text); assert(c_pattern); }
                else assert(drbz_regex_compile(&pattern, (const unsigned char *)pattern_text, 6) == 0);
            }
            int length = 0;
            int position = variant == 0 ? re_matchp(c_pattern, (const char *)input, &length) : drbz_regex_search(&pattern, input, c->size, &length, variant == 2, 100000000);
            assert(position >= -1);
            /* The no-match length is also checked, matching the original dialect. */
            checksum += (uint64_t)(uint32_t)(position + 2) + ((uint64_t)(uint32_t)length << 32);
        }
    } else {
        for (size_t i = 0; i < n; ++i) checksum += drbz_measure_noop(i);
    }
    return checksum;
}
static uint64_t finish(const bench_case *c, unsigned v, uint64_t checksum) {
    if (c->task == STARS) for (size_t i = 0; i < c->size; ++i) {
        checksum = (checksum ^ f32bits(v == 2 ? xs[i] : stars[i].x)) * UINT64_C(1099511628211);
        checksum = (checksum ^ f32bits(v == 2 ? ys[i] : stars[i].y)) * UINT64_C(1099511628211);
    }
    if (c->task == SCANNER) for (size_t i = 0; i < 100; ++i)
        checksum = (checksum ^ (v == 2 ? dirty.pixels[i] : scanner_pixels[i])) * UINT64_C(1099511628211);
    return checksum;
}
#ifdef DRBZ_PROFILE
static void read_number(void *ctx, const void *items, size_t i, drbz_view *out) {
    (void)ctx; *out = (drbz_view){1, ((const double *)items)[i], NULL, 0};
}
static void profile_other_apis(void) {
    int32_t in[9] = {1,2,3,4,5,6,7,8,9}, out[9];
    unsigned char greeting[64]; size_t written = 0; int square = 123; double sum = 0, vals[] = {1,2,3};
    drbz_regex regex;
    meter_begin();
    assert(drbz_square(7, &square) == 0 && square == 49);
    assert(drbz_square(INT_MAX, &square) != 0 && square == 49);
    assert(drbz_squares(in, out, 9) == 0 && out[8] == 81);
    in[0] = INT_MAX; assert(drbz_squares(in, out, 9) != 0 && out[8] == 81);
    assert(drbz_sum_tree(vals, 3, read_number, NULL, &sum) == 0 && sum == 6);
    assert(drbz_greeting(0, (const unsigned char *)"test", 4, greeting, sizeof greeting, &written) == 0);
    assert(drbz_greeting(1, (const unsigned char *)"test", 4, greeting, 1, &written) != 0);
    assert(drbz_regex_compile(&regex, (const unsigned char *)"[", 1) != 0);
    meter_stats s = meter_end();
    assert(s.malloc_calls + s.calloc_calls + s.realloc_calls + s.aligned_calls + s.free_calls == 0);
    bench_alloc_record("linked-libc", "native/api-success-and-errors", "zig", "api", 8, (uint64_t)square, s);
}
#endif
#define CASE(name, kind, size_, detail_, a, b, c) {name,{a,b,c},3,size_,kind,detail_,reset,batch,finish}
int main(int argc, char **argv) {
#ifdef DRBZ_PROFILE
    profile_other_apis();
#endif
    const bench_case cases[] = {
        CASE("native/lf/31", LF, 31, 0, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/lf/32", LF, 32, 0, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/lf/33-misaligned", LF, 33, 1, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/lf/4k", LF, 4096, 0, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/lf/1m-warm", LF, 1048576, 0, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/lf/1m-rotating16m", LF, 1048576, 2, "c_control","zig_previous16","zig_blocked32"),
        CASE("native/sum/8", SUM, 8, 0, "c_control","zig_ordered","zig_unrolled"),
        CASE("native/sum/65536", SUM, 65536, 0, "c_control","zig_ordered","zig_unrolled"),
        CASE("native/scanner", SCANNER, 100, 0, "c_original_kernel","zig_previous","zig_dirty_rows"),
        CASE("native/stars/16384", STARS, 16384, 0, "c_original_kernel","zig_scalar","zig_soa8"),
        CASE("native/stars/all-wrap", STARS, 4096, 1, "c_original_kernel","zig_scalar","zig_soa8"),
        CASE("native/regex/early", REGEX, 4096, 0, "c_original","zig_baseline","zig_optimized"),
        CASE("native/regex/late", REGEX, 4096, 1, "c_original","zig_baseline","zig_optimized"),
        CASE("native/regex/absent", REGEX, 4096, 2, "c_original","zig_baseline","zig_optimized"),
        CASE("native/regex/dense-prefix", REGEX, 4096, 3, "c_original","zig_baseline","zig_optimized"),
        CASE("native/regex/compile-and-search", REGEX, 128, 4, "c_original","zig_baseline","zig_optimized"),
        {"native/call-envelope", {"c_noop"}, 1, 0, NOOP, 0, reset, batch, finish},
    };
    bench_run(cases, sizeof cases / sizeof *cases, "linked-libc", 1, argc, argv);
    return 0;
}
