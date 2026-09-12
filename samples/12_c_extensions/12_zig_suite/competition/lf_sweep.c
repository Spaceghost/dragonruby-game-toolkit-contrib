#include "competitive.h"
#include "native.h"
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern size_t control_c_count(const unsigned char *, size_t);

#define VARIANTS 5
static volatile uint64_t observable;
static unsigned char storage[512 + 64];

static uint64_t now_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static size_t call_variant(unsigned variant, const unsigned char *bytes, size_t length) {
    switch (variant) {
        case 0: return drbz_count_scalar(bytes, length);
        case 1: return drbz_count_blocked(bytes, length);
        case 2: return drbz_count_dual(bytes, length);
        case 3: return drbc_count_dual(bytes, length);
        case 4: return drbo_count_dual(bytes, length);
        default: assert(0); return 0;
    }
}

static uint64_t run_variant(unsigned variant, const unsigned char *bytes, size_t length, size_t iterations) {
    uint64_t sum = 0;
    switch (variant) {
        case 0: for (size_t i = 0; i < iterations; ++i) sum += drbz_count_scalar(bytes, length); break;
        case 1: for (size_t i = 0; i < iterations; ++i) sum += drbz_count_blocked(bytes, length); break;
        case 2: for (size_t i = 0; i < iterations; ++i) sum += drbz_count_dual(bytes, length); break;
        case 3: for (size_t i = 0; i < iterations; ++i) sum += drbc_count_dual(bytes, length); break;
        case 4: for (size_t i = 0; i < iterations; ++i) sum += drbo_count_dual(bytes, length); break;
        default: assert(0);
    }
    observable ^= sum + iterations;
    return sum;
}

static size_t calibrate(unsigned variant, const unsigned char *bytes, size_t length) {
    size_t iterations = 64;
    while (iterations < (UINT64_C(1) << 24)) {
        uint64_t start = now_ns();
        (void)run_variant(variant, bytes, length, iterations);
        uint64_t elapsed = now_ns() - start;
        if (elapsed >= UINT64_C(250000)) return iterations;
        iterations *= 2;
    }
    return iterations;
}

int main(void) {
    static const size_t offsets[] = {0, 1, 15};
    static const char *names[VARIANTS] = {"zig_scalar", "zig_blocked32", "zig_dual", "c_dual", "odin_dual"};
    uint32_t state = UINT32_C(0x5eed1234);
    for (size_t i = 0; i < sizeof storage; ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        storage[i] = (unsigned char)(state >> 24);
        if ((i % 13) == 5 || (i % 47) == 11) storage[i] = '\n';
    }

    uint64_t records = 0;
    for (size_t length = 0; length <= 512; ++length) {
        for (size_t oi = 0; oi < sizeof offsets / sizeof *offsets; ++oi) {
            const size_t offset = offsets[oi];
            const unsigned char *bytes = storage + offset;
            const size_t expected = control_c_count(bytes, length);
            size_t iterations = 0;
            for (unsigned variant = 0; variant < VARIANTS; ++variant) {
                assert(call_variant(variant, bytes, length) == expected);
                const size_t n = calibrate(variant, bytes, length);
                if (n > iterations) iterations = n;
            }
            for (unsigned trial = 0; trial < 5; ++trial) {
                const unsigned rotation = (unsigned)((length + offset + trial) % VARIANTS);
                for (unsigned slot = 0; slot < VARIANTS; ++slot) {
                    const unsigned variant = (slot + rotation) % VARIANTS;
                    const uint64_t start = now_ns();
                    const uint64_t sum = run_variant(variant, bytes, length, iterations);
                    const uint64_t elapsed = now_ns() - start;
                    assert(sum == (uint64_t)expected * iterations);
                    assert(elapsed > 0);
                    printf("{\"event\":\"lf_sweep\",\"length\":%zu,\"offset\":%zu,\"trial\":%u,\"order\":%u,\"variant\":\"%s\",\"iterations\":%zu,\"elapsed_ns\":%" PRIu64 ",\"ns_per_call\":%.9f,\"count\":%zu}\n",
                           length, offset, trial, slot, names[variant], iterations, elapsed,
                           (double)elapsed / (double)iterations, expected);
                    ++records;
                }
            }
        }
    }
    printf("LF_SWEEP_COMPLETE {\"lengths\":513,\"offsets\":3,\"variants\":%d,\"trials\":5,\"records\":%" PRIu64 ",\"observable\":%" PRIu64 "}\n", VARIANTS, records, observable);
    return 0;
}
