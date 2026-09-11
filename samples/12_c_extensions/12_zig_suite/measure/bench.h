#ifndef DRBZ_MEASURE_BENCH_H
#define DRBZ_MEASURE_BENCH_H
#include "meter.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_MAX_VARIANTS 6

typedef struct bench_case bench_case;
struct bench_case {
    const char *name;
    const char *variants[BENCH_MAX_VARIANTS];
    unsigned variant_count;
    size_t size;
    int task, detail;
    void (*reset)(const bench_case *, uint32_t);
    uint64_t (*batch)(const bench_case *, unsigned, size_t);
    uint64_t (*finish)(const bench_case *, unsigned, uint64_t);
};
static volatile uint64_t bench_observable;
static inline uint64_t bench_clock(void) {
    struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static uint64_t bench_number(const char *s, uint64_t min, uint64_t max) {
    char *end; errno = 0; unsigned long long n = strtoull(s, &end, 10);
    if (errno || !*s || *s == '-' || *end || n < min || n > max) {
        fprintf(stderr, "invalid benchmark argument: %s\n", s); exit(2);
    }
    return n;
}
static inline uint32_t bench_random(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223); return *state;
}
static uint64_t bench_finish(const bench_case *c, unsigned v, uint64_t value) {
    if (c->finish) value = c->finish(c, v, value);
    bench_observable = value; return value;
}
static inline void bench_alloc_record(const char *scope, const char *name, const char *variant,
                                const char *phase, size_t n, uint64_t checksum, meter_stats s) {
    printf("{\"event\":\"allocation\",\"scope\":\"%s\",\"case\":\"%s\",\"variant\":\"%s\",\"phase\":\"%s\",\"operations\":%zu,\"checksum\":%" PRIu64
           ",\"malloc_calls\":%" PRIu64 ",\"calloc_calls\":%" PRIu64 ",\"realloc_calls\":%" PRIu64 ",\"free_calls\":%" PRIu64
           ",\"aligned_calls\":%" PRIu64 ",\"failed_calls\":%" PRIu64 ",\"requested_bytes\":%" PRIu64 ",\"overflow_requests\":%" PRIu64 ",\"live_before\":",
           scope, name, variant, phase, n, checksum, s.malloc_calls, s.calloc_calls, s.realloc_calls, s.free_calls,
           s.aligned_calls, s.failed_calls, s.requested_bytes, s.overflow_requests);
    if (s.live_supported) printf("%" PRIu64 ",\"live_after\":%" PRIu64 ",\"peak_live\":%" PRIu64, s.live_before, s.live_after, s.peak_live);
    else printf("null,\"live_after\":null,\"peak_live\":null");
    puts("}");
}
/* One batch size for all variants. Reset and full-state checks are outside timing.
 * Calibration seeks the minimum duration for the fastest variant, not just C.
 * The ceiling bounds work; undersized samples remain visible, never discarded. */
static void bench_run(const bench_case *cases, size_t count, const char *scope,
                      int require_no_alloc, int argc, char **argv) {
    if (argc != 4 && argc != 5) { fprintf(stderr, "usage: benchmark TRIALS SEED MIN_NS [--inject-allocation]\n"); exit(2); }
    unsigned trials = (unsigned)bench_number(argv[1], 3, 101);
    uint32_t seed = (uint32_t)bench_number(argv[2], 1, UINT32_MAX);
    uint64_t min_ns = bench_number(argv[3], 1000, 100000000);
    int inject = argc == 5;
    if (inject && strcmp(argv[4], "--inject-allocation") != 0) exit(2);
#ifndef DRBZ_PROFILE
    if (inject) exit(2);
#else
    meter_selftest();
    puts("{\"event\":\"meter_selftest\",\"passed\":true}");
#endif
    size_t records = 0;
    for (size_t ci = 0; ci < count; ++ci) {
        const bench_case *c = cases + ci;
        assert(c->variant_count > 0 && c->variant_count <= BENCH_MAX_VARIANTS);
        uint32_t case_seed = seed ^ (uint32_t)(ci * 65537 + 1);
        printf("{\"event\":\"case\",\"scope\":\"%s\",\"case\":\"%s\",\"size\":%zu,\"variants\":[", scope, c->name, c->size);
        for (unsigned v = 0; v < c->variant_count; ++v) printf("%s\"%s\"", v ? "," : "", c->variants[v]);
        puts("]}");
        size_t n = 64;
#ifndef DRBZ_PROFILE
        (void)require_no_alloc;
        for (;;) {
            uint64_t fastest = UINT64_MAX;
            for (unsigned v = 0; v < c->variant_count; ++v) {
                c->reset(c, case_seed);
                uint64_t t = bench_clock(), checksum = c->batch(c, v, n);
                uint64_t elapsed = bench_clock() - t;
                bench_finish(c, v, checksum);
                if (elapsed < fastest) fastest = elapsed;
            }
            if (fastest >= min_ns || n >= (1u << 22)) break;
            n *= 2;
        }
#endif
        uint64_t expected = 0;
        for (unsigned v = 0; v < c->variant_count; ++v) {
            c->reset(c, case_seed);
            uint64_t checksum = bench_finish(c, v, c->batch(c, v, n));
            if (!v) expected = checksum;
            else if (checksum != expected) { fprintf(stderr, "BENCH_PARITY %s variant=%u\n", c->name, v); exit(44); }
        }
#ifdef DRBZ_PROFILE
        for (unsigned v = 0; v < c->variant_count; ++v) {
            c->reset(c, case_seed);
            meter_begin();
            if (inject && ci == 0 && v == 0) meter_inject();
            uint64_t checksum = c->batch(c, v, n);
            meter_stats stats = meter_end();
            checksum = bench_finish(c, v, checksum);
            assert(checksum == expected);
            if (require_no_alloc && (stats.malloc_calls || stats.calloc_calls || stats.realloc_calls || stats.aligned_calls || stats.free_calls)) {
                fprintf(stderr, "ALLOCATION_GUARD %s\n", c->name); exit(43);
            }
            bench_alloc_record(scope, c->name, c->variants[v], "batch", n, checksum, stats);
            ++records;
        }
#else
        uint32_t order_seed = case_seed;
        for (unsigned trial = 0; trial < trials; ++trial) {
            unsigned order[BENCH_MAX_VARIANTS] = {0, 1, 2, 3, 4, 5};
            for (unsigned k = c->variant_count - 1; k > 0; --k) {
                unsigned j = bench_random(&order_seed) % (k + 1), tmp = order[k]; order[k] = order[j]; order[j] = tmp;
            }
            for (unsigned slot = 0; slot < c->variant_count; ++slot) {
                unsigned v = order[slot]; c->reset(c, case_seed);
                uint64_t t = bench_clock(), checksum = c->batch(c, v, n);
                uint64_t elapsed = bench_clock() - t;
                checksum = bench_finish(c, v, checksum);
                if (checksum != expected) { fprintf(stderr, "BENCH_PARITY %s variant=%u\n", c->name, v); exit(44); }
                printf("{\"event\":\"timing\",\"scope\":\"%s\",\"case\":\"%s\",\"variant\":\"%s\",\"trial\":%u,\"order\":%u,\"iterations\":%zu,\"nanoseconds\":%" PRIu64
                       ",\"checksum\":%" PRIu64 ",\"seed\":%" PRIu32 ",\"min_ns\":%" PRIu64 ",\"undersized\":%s,\"instrumented\":false}\n",
                       scope, c->name, c->variants[v], trial, slot, n, elapsed, checksum, case_seed, min_ns, elapsed < min_ns ? "true" : "false");
                ++records;
            }
        }
#endif
    }
#ifdef DRBZ_PROFILE
    (void)trials; (void)min_ns;
#endif
    printf("{\"event\":\"complete\",\"scope\":\"%s\",\"cases\":%zu,\"records\":%zu}\n", scope, count, records);
}
#endif
