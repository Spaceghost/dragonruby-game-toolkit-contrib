#include "native.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t ns_now(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static uint64_t mix(uint64_t h, uint32_t v) { return (h ^ v) * UINT64_C(1099511628211); }
static uint32_t fbits(float x) { uint32_t u; memcpy(&u, &x, sizeof u); return u; }

typedef struct { uint64_t calls, checksum; } sink_state;
static void consume(void *raw, const drbz_packed_sprite *sprites, size_t count) {
    sink_state *s = raw;
    ++s->calls;
    for (size_t i = 0; i < count; ++i) {
        assert(sprites[i].w == 4.0f && sprites[i].h == 4.0f && sprites[i].path_id == 1);
        s->checksum = mix(s->checksum, fbits(sprites[i].x));
        s->checksum = mix(s->checksum, fbits(sprites[i].y));
    }
}

typedef struct { void *storage; drbz_starfield field; } owned_field;
static owned_field make_field(size_t count, uint64_t seed) {
    owned_field result = {0};
    size_t bytes = drbz_starfield_storage_bytes(count);
    assert(bytes != 0 || count == 0);
    result.storage = bytes ? malloc(bytes) : NULL;
    assert(!bytes || result.storage);
    assert(drbz_starfield_init(result.storage, bytes, count, seed, &result.field) == 0);
    return result;
}
static void free_field(owned_field *f) { free(f->storage); f->storage = NULL; }
static void same_field(const drbz_starfield *a, const drbz_starfield *b) {
    assert(a->len == b->len && a->rng_state == b->rng_state);
    for (size_t i = 0; i < a->len; ++i) {
        assert(fbits(a->x[i]) == fbits(b->x[i]));
        assert(fbits(a->y[i]) == fbits(b->y[i]));
        assert(fbits(a->speed[i]) == fbits(b->speed[i]));
    }
}
static void check_size(size_t count) {
    owned_field update = make_field(count, UINT64_C(0x123456789abcdef));
    owned_field full = make_field(count, UINT64_C(0x123456789abcdef));
    owned_field packed = make_field(count, UINT64_C(0x123456789abcdef));
    owned_field framed = make_field(count, UINT64_C(0x123456789abcdef));
    sink_state sink = {0};
    drbz_starfield_update(&update.field);
    drbz_starfield_update(&full.field); drbz_starfield_pack(&full.field);
    drbz_starfield_update_pack(&packed.field);
    drbz_starfield_frame(&framed.field, consume, &sink);
    same_field(&update.field, &full.field);
    same_field(&update.field, &packed.field);
    same_field(&update.field, &framed.field);
    assert(sink.calls == 1);
    for (size_t i = 0; i < count; ++i) {
        assert(fbits(full.field.sprites[i].x) == fbits(packed.field.sprites[i].x));
        assert(fbits(full.field.sprites[i].y) == fbits(packed.field.sprites[i].y));
        assert(full.field.sprites[i].w == packed.field.sprites[i].w);
        assert(full.field.sprites[i].h == packed.field.sprites[i].h);
        assert(full.field.sprites[i].path_id == packed.field.sprites[i].path_id);
        assert(fbits(framed.field.sprites[i].x) == fbits(packed.field.sprites[i].x));
        assert(fbits(framed.field.sprites[i].y) == fbits(packed.field.sprites[i].y));
    }
    free_field(&update); free_field(&full); free_field(&packed); free_field(&framed);
}

enum stage { UPDATE, UPDATE_FULL_PACK, UPDATE_PACK, FRAME_SINK };
static const char *stage_name(enum stage s) {
    switch (s) {
        case UPDATE: return "update";
        case UPDATE_FULL_PACK: return "update-full-pack";
        case UPDATE_PACK: return "update-pack";
        case FRAME_SINK: return "frame-sink";
    }
    abort();
}
static uint64_t execute(owned_field *f, enum stage stage, size_t iterations, sink_state *sink) {
    for (size_t i = 0; i < iterations; ++i) {
        if (stage == UPDATE) drbz_starfield_update(&f->field);
        else if (stage == UPDATE_FULL_PACK) { drbz_starfield_update(&f->field); drbz_starfield_pack(&f->field); }
        else if (stage == UPDATE_PACK) drbz_starfield_update_pack(&f->field);
        else drbz_starfield_frame(&f->field, consume, sink);
    }
    uint64_t h = f->field.rng_state;
    if (f->field.len) {
        h = mix(h, fbits(f->field.x[0])); h = mix(h, fbits(f->field.y[0]));
        h = mix(h, fbits(f->field.x[f->field.len - 1])); h = mix(h, fbits(f->field.y[f->field.len - 1]));
    }
    return h ^ sink->checksum ^ sink->calls;
}
static size_t calibrate(size_t count, enum stage stage, uint64_t min_ns) {
    size_t iterations = 1;
    while (iterations < (UINT64_C(1) << 22)) {
        owned_field f = make_field(count, UINT64_C(0x1111222233334444));
        sink_state sink = {0};
        uint64_t start = ns_now();
        volatile uint64_t checksum = execute(&f, stage, iterations, &sink);
        uint64_t elapsed = ns_now() - start;
        (void)checksum;
        free_field(&f);
        if (elapsed >= min_ns) break;
        iterations *= 2;
    }
    return iterations;
}

int main(int argc, char **argv) {
    static const size_t sizes[] = {64, 1024, 16384, 100000};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) check_size(sizes[i]);
    puts("STARFIELD_CORRECTNESS {\"sizes\":4,\"stages\":4,\"sink_calls_per_frame\":1,\"renderer\":false}");
    if (argc == 2 && strcmp(argv[1], "--check") == 0) return 0;
    unsigned trials = 11;
    uint64_t min_ns = UINT64_C(2000000);
    if (argc == 3) { trials = (unsigned)strtoul(argv[1], NULL, 10); min_ns = strtoull(argv[2], NULL, 10); }
    else if (argc != 1) { fputs("usage: starfield-bench [TRIALS MIN_NS] | --check\n", stderr); return 2; }
    uint64_t records = 0, checksum = 0;
    for (size_t si = 0; si < sizeof sizes / sizeof *sizes; ++si) {
        for (enum stage stage = UPDATE; stage <= FRAME_SINK; stage = (enum stage)(stage + 1)) {
            size_t iterations = calibrate(sizes[si], stage, min_ns);
            for (unsigned trial = 0; trial < trials; ++trial) {
                owned_field f = make_field(sizes[si], UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)trial << 32) ^ sizes[si]);
                sink_state sink = {0};
                uint64_t start = ns_now();
                uint64_t value = execute(&f, stage, iterations, &sink);
                uint64_t elapsed = ns_now() - start;
                checksum ^= value;
                printf("{\"event\":\"starfield_timing\",\"size\":%zu,\"stage\":\"%s\",\"trial\":%u,\"iterations\":%zu,\"elapsed_ns\":%" PRIu64 ",\"ns_per_frame\":%.6f,\"sink_calls\":%" PRIu64 "}\n",
                       sizes[si], stage_name(stage), trial, iterations, elapsed, (double)elapsed / (double)iterations, sink.calls);
                ++records;
                free_field(&f);
            }
        }
    }
    printf("STARFIELD_COMPLETE {\"records\":%" PRIu64 ",\"checksum\":%" PRIu64 "}\n", records, checksum);
    return 0;
}
