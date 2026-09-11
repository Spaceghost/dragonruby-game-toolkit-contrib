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

enum language { LANG_ZIG, LANG_ODIN, LANG_COUNT };
static const char *language_name(enum language language) { return language == LANG_ZIG ? "zig" : "odin"; }

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

typedef struct { void *storage; drbz_starfield field; enum language language; } owned_field;
static size_t storage_bytes(enum language language, size_t count) {
    return language == LANG_ZIG ? drbz_starfield_storage_bytes(count) : drbo_starfield_storage_bytes(count);
}
static owned_field make_field(enum language language, size_t count, uint64_t seed) {
    owned_field result = {.language=language};
    size_t zig_bytes = drbz_starfield_storage_bytes(count), odin_bytes = drbo_starfield_storage_bytes(count);
    assert(zig_bytes == odin_bytes);
    size_t bytes = storage_bytes(language, count);
    assert(bytes != 0 || count == 0);
    result.storage = bytes ? malloc(bytes) : NULL;
    assert(!bytes || result.storage);
    int rc = language == LANG_ZIG
        ? drbz_starfield_init(result.storage, bytes, count, seed, &result.field)
        : drbo_starfield_init(result.storage, bytes, count, seed, &result.field);
    assert(rc == 0);
    return result;
}
static void free_field(owned_field *f) { free(f->storage); f->storage = NULL; }
static void update(owned_field *f) {
    if (f->language == LANG_ZIG) drbz_starfield_update(&f->field); else drbo_starfield_update(&f->field);
}
static void pack(owned_field *f) {
    if (f->language == LANG_ZIG) drbz_starfield_pack(&f->field); else drbo_starfield_pack(&f->field);
}
static void update_pack(owned_field *f) {
    if (f->language == LANG_ZIG) drbz_starfield_update_pack(&f->field); else drbo_starfield_update_pack(&f->field);
}
static void frame(owned_field *f, sink_state *sink) {
    if (f->language == LANG_ZIG) drbz_starfield_frame(&f->field, consume, sink); else drbo_starfield_frame(&f->field, consume, sink);
}
static void same_field(const drbz_starfield *a, const drbz_starfield *b) {
    assert(a->len == b->len && a->rng_state == b->rng_state);
    for (size_t i = 0; i < a->len; ++i) {
        assert(fbits(a->x[i]) == fbits(b->x[i]));
        assert(fbits(a->y[i]) == fbits(b->y[i]));
        assert(fbits(a->speed[i]) == fbits(b->speed[i]));
    }
}
static void same_sprites(const drbz_starfield *a, const drbz_starfield *b) {
    assert(a->len == b->len);
    for (size_t i = 0; i < a->len; ++i) {
        assert(fbits(a->sprites[i].x) == fbits(b->sprites[i].x));
        assert(fbits(a->sprites[i].y) == fbits(b->sprites[i].y));
        assert(fbits(a->sprites[i].w) == fbits(b->sprites[i].w));
        assert(fbits(a->sprites[i].h) == fbits(b->sprites[i].h));
        assert(a->sprites[i].path_id == b->sprites[i].path_id);
    }
}
static void check_size(size_t count) {
    const uint64_t seed = UINT64_C(0x123456789abcdef);
    owned_field zig = make_field(LANG_ZIG, count, seed), odin = make_field(LANG_ODIN, count, seed);
    same_field(&zig.field, &odin.field); same_sprites(&zig.field, &odin.field);
    free_field(&zig); free_field(&odin);

    for (int stage = 0; stage < 4; ++stage) {
        zig = make_field(LANG_ZIG, count, seed); odin = make_field(LANG_ODIN, count, seed);
        sink_state zs={0}, os={0};
        if (stage == 0) { update(&zig); update(&odin); }
        else if (stage == 1) { update(&zig); pack(&zig); update(&odin); pack(&odin); }
        else if (stage == 2) { update_pack(&zig); update_pack(&odin); }
        else { frame(&zig,&zs); frame(&odin,&os); assert(zs.calls==1&&os.calls==1&&zs.checksum==os.checksum); }
        same_field(&zig.field, &odin.field);
        if (stage != 0) same_sprites(&zig.field, &odin.field);
        free_field(&zig); free_field(&odin);
    }

    drbz_starfield field;
    unsigned char tiny[256];
    size_t needed=drbo_starfield_storage_bytes(4);
    assert(drbo_starfield_init(tiny, needed-1, 4, 1, &field)==1);
    assert(drbo_starfield_init(tiny+1, sizeof tiny-1, 1, 1, &field)==2);
    assert(drbo_starfield_init(NULL,0,0,0,&field)==0&&field.len==0&&field.rng_state==1);
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
        if (stage == UPDATE) update(f);
        else if (stage == UPDATE_FULL_PACK) { update(f); pack(f); }
        else if (stage == UPDATE_PACK) update_pack(f);
        else frame(f, sink);
    }
    uint64_t h = f->field.rng_state;
    if (f->field.len) {
        h = mix(h, fbits(f->field.x[0])); h = mix(h, fbits(f->field.y[0]));
        h = mix(h, fbits(f->field.x[f->field.len - 1])); h = mix(h, fbits(f->field.y[f->field.len - 1]));
    }
    return h ^ sink->checksum ^ sink->calls;
}
static size_t calibrate(enum language language, size_t count, enum stage stage, uint64_t min_ns) {
    size_t iterations = 1;
    while (iterations < (UINT64_C(1) << 22)) {
        owned_field f = make_field(language, count, UINT64_C(0x1111222233334444));
        sink_state sink = {0};
        uint64_t start = ns_now();
        volatile uint64_t checksum = execute(&f, stage, iterations, &sink);
        uint64_t elapsed = ns_now() - start;
        (void)checksum; free_field(&f);
        if (elapsed >= min_ns) break;
        iterations *= 2;
    }
    return iterations;
}

int main(int argc, char **argv) {
    static const size_t sizes[] = {64, 1024, 16384, 100000};
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) check_size(sizes[i]);
    puts("STARFIELD_CORRECTNESS {\"sizes\":4,\"stages\":4,\"languages\":2,\"sink_calls_per_frame\":1,\"renderer\":false}");
    if (argc == 2 && strcmp(argv[1], "--check") == 0) return 0;
    unsigned trials = 11;
    uint64_t min_ns = UINT64_C(2000000);
    if (argc == 3) { trials = (unsigned)strtoul(argv[1], NULL, 10); min_ns = strtoull(argv[2], NULL, 10); }
    else if (argc != 1) { fputs("usage: starfield-bench [TRIALS MIN_NS] | --check\n", stderr); return 2; }
    uint64_t records = 0, checksum = 0;
    for (size_t si = 0; si < sizeof sizes / sizeof *sizes; ++si) {
        for (enum stage stage = UPDATE; stage <= FRAME_SINK; stage = (enum stage)(stage + 1)) {
            size_t iterations = 0;
            for (enum language language=LANG_ZIG; language<LANG_COUNT; language=(enum language)(language+1)) {
                size_t n=calibrate(language,sizes[si],stage,min_ns); if(n>iterations) iterations=n;
            }
            for (unsigned trial = 0; trial < trials; ++trial) {
                uint64_t expected=0;
                for (unsigned slot=0;slot<LANG_COUNT;++slot) {
                    enum language language=(enum language)((slot+trial)%LANG_COUNT);
                    owned_field f = make_field(language, sizes[si], UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)trial << 32) ^ sizes[si]);
                    sink_state sink = {0};
                    uint64_t start = ns_now();
                    uint64_t value = execute(&f, stage, iterations, &sink);
                    uint64_t elapsed = ns_now() - start;
                    if(slot==0) expected=value; else assert(value==expected);
                    checksum ^= value;
                    printf("{\"event\":\"starfield_timing\",\"size\":%zu,\"stage\":\"%s\",\"language\":\"%s\",\"trial\":%u,\"order\":%u,\"iterations\":%zu,\"elapsed_ns\":%" PRIu64 ",\"ns_per_frame\":%.6f,\"sink_calls\":%" PRIu64 "}\n",
                           sizes[si], stage_name(stage), language_name(language), trial, slot, iterations, elapsed,
                           (double)elapsed / (double)iterations, sink.calls);
                    ++records; free_field(&f);
                }
            }
        }
    }
    printf("STARFIELD_COMPLETE {\"records\":%" PRIu64 ",\"checksum\":%" PRIu64 "}\n", records, checksum);
    return 0;
}
