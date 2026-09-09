#include "competitive.h"
#include "native.h"
#include "guard.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void original_c_stars(drbz_star *, size_t, drbz_random, void *);
static uint64_t count_checks, star_values, rng_checks, guarded_checks;
static const char *names[] = {"zig_previous", "c_tuned", "zig_tuned"};
static size_t (*const counters[])(const unsigned char *, size_t) = {
    drbz_count_blocked, drbc_count_dual, drbz_count_dual
};
static void (*const motions[])(float *, float *, const float *, size_t, rival_random, void *) = {
    drbz_stars_soa, drbc_stars_block, drbz_stars_block
};
struct rng { uint32_t state; uint64_t calls; };
static uint32_t next(uint32_t *s) { return *s = *s * UINT32_C(1664525) + UINT32_C(1013904223); }
static float random_value(void *context) {
    struct rng *r = context; ++r->calls;
    return (float)(next(&r->state) >> 8) / (float)UINT32_C(0xffffff);
}
static size_t scalar_count(const unsigned char *p, size_t n) {
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) count += p[i] == '\n';
    return count;
}
static void count_case(const unsigned char *p, size_t n, size_t expected) {
    for (size_t v = 0; v < 3; ++v) {
        size_t result = counters[v](p, n);
        if (result != expected) {
            fprintf(stderr, "RIVAL_COUNT_MISMATCH candidate=%s length=%zu expected=%zu got=%zu\n", names[v], n, expected, result);
            exit(45);
        }
        ++count_checks;
    }
}
static void counts(void) {
    static unsigned char bytes[65536 + 128];
    const size_t large[] = {8159,8160,8161,16319,16320,16321,32639,32640,32641,65279,65280,65281,65536};
    count_case(NULL, 0, 0);
    uint32_t seed = 91731;
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        for (size_t i = 0; i < sizeof bytes; ++i)
            bytes[i] = pattern == 0 ? '\n' : pattern == 1 ? 'x' : (unsigned char)(next(&seed) >> 24);
        for (size_t offset = 0; offset < 64; ++offset) {
            for (size_t n = 0; n <= 513; ++n) count_case(bytes + offset, n, scalar_count(bytes + offset, n));
            for (size_t i = 0; i < sizeof large / sizeof *large; ++i)
                count_case(bytes + offset, large[i], scalar_count(bytes + offset, large[i]));
        }
    }
    /* Every 16-byte hit mask, repeated across all four lanes of a pair. */
    for (unsigned mask = 0; mask < 65536; ++mask) {
        for (size_t i = 0; i < 64; ++i) bytes[i] = mask & (1u << (i % 16)) ? '\n' : 'x';
        count_case(bytes, 64, scalar_count(bytes, 64));
    }
    struct guarded_page page = guard_create();
    for (size_t i = 0; i < page.size; ++i) page.data[i] = i % 3 ? '\n' : 'x';
    guard_readonly(page);
    for (size_t n = 0; n <= page.size; ++n) {
        count_case(page.data, n, scalar_count(page.data, n));
        count_case(page.data + page.size - n, n, scalar_count(page.data + page.size - n, n));
        guarded_checks += 6;
    }
    guard_destroy(page);
}
static int same_float(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; }
static void stars_case(const drbz_star *input, size_t n) {
    assert(n <= 1024);
    drbz_star expected[1024];
    memcpy(expected, input, n * sizeof *input);
    struct rng reference = {72819, 0};
    original_c_stars(expected, n, random_value, &reference);
    for (size_t v = 0; v < 3; ++v) {
        float x[1026], y[1026], speed[1026];
        x[0] = y[0] = speed[0] = x[n+1] = y[n+1] = speed[n+1] = 12345.0f;
        for (size_t i = 0; i < n; ++i) { x[i+1] = input[i].x; y[i+1] = input[i].y; speed[i+1] = input[i].s; }
        struct rng observed = {72819, 0};
        motions[v](x+1, y+1, speed+1, n, random_value, &observed);
        for (size_t i = 0; i < n; ++i) {
            if (!same_float(x[i+1], expected[i].x) || !same_float(y[i+1], expected[i].y) || !same_float(speed[i+1], input[i].s)) {
                fprintf(stderr, "RIVAL_STAR_MISMATCH candidate=%s length=%zu index=%zu\n", names[v], n, i); exit(46);
            }
            ++star_values;
        }
        if (observed.state != reference.state || observed.calls != reference.calls) {
            fprintf(stderr, "RIVAL_RNG_MISMATCH candidate=%s\n", names[v]); exit(47);
        }
        ++rng_checks;
        assert(x[0] == 12345 && y[0] == 12345 && speed[0] == 12345);
        assert(x[n+1] == 12345 && y[n+1] == 12345 && speed[n+1] == 12345);
    }
}
static void stars(void) {
    drbz_star input[1024];
    for (size_t v = 0; v < 3; ++v) motions[v](NULL,NULL,NULL,0,random_value,NULL);
    for (unsigned mask = 0; mask < 65536; ++mask) {
        for (size_t i = 0; i < 8; ++i)
            input[i] = (drbz_star){mask & (1u<<i) ? 1280.0f : 0.0f, mask & (1u<<(i+8)) ? 720.0f : 0.0f, 1.0f};
        stars_case(input, 8);
    }
    uint32_t seed = 89123;
    for (size_t trial = 0; trial < 4096; ++trial) {
        size_t n = trial < 130 ? trial : next(&seed) % 1025;
        for (size_t i = 0; i < n; ++i) {
            input[i].x = ((int)(next(&seed) % 24000) - 8000) * .125f;
            input[i].y = ((int)(next(&seed) % 14000) - 4000) * .125f;
            input[i].s = ((int)(next(&seed) % 256) - 64) * .125f;
        }
        stars_case(input, n);
    }
    const float edges[] = {-0.0f, 0.0f, 0x1p-149f, -0x1p-149f, 719.99993896484375f, 720.0f, 720.00006103515625f, 1279.9998779296875f, 1280.0f, 1280.0001220703125f};
    for (size_t a = 0; a < sizeof edges / sizeof *edges; ++a)
        for (size_t b = 0; b < sizeof edges / sizeof *edges; ++b) {
            for (size_t i = 0; i < 17; ++i) input[i] = (drbz_star){edges[a],edges[b],edges[i%4]};
            stars_case(input, 17);
        }
    struct guarded_page gx=guard_create(), gy=guard_create(), gs=guard_create();
    for (size_t i=0; i<gs.size/sizeof(float); ++i) ((float *)gs.data)[i]=1.0f;
    guard_readonly(gs);
    for (size_t n=1; n<=65; ++n) for (size_t v=0; v<3; ++v) {
        float *x=(float *)(gx.data+gx.size)-n, *y=(float *)(gy.data+gy.size)-n;
        const float *s=(const float *)(gs.data+gs.size)-n;
        memset(x,0,n*sizeof *x); memset(y,0,n*sizeof *y);
        struct rng r={72819,0}; motions[v](x,y,s,n,random_value,&r);
        for (size_t i=0; i<n; ++i) assert(x[i]==1.0f && y[i]==1.0f);
        assert(r.calls==0); ++guarded_checks;
    }
    guard_destroy(gx); guard_destroy(gy); guard_destroy(gs);
}
int main(void) {
    counts(); stars();
    printf("RIVAL_CORRECTNESS {\"count_comparisons\":%" PRIu64 ",\"star_updates_compared\":%" PRIu64 ",\"rng_comparisons\":%" PRIu64 ",\"guarded_candidate_calls\":%" PRIu64 ",\"formal_proof\":false}\n", count_checks,star_values,rng_checks,guarded_checks);
    return 0;
}
