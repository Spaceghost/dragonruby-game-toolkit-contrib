#include "competitive.h"
#include "native.h"
#include "re.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static uint64_t now_ns(void) {
    static LARGE_INTEGER frequency;
    LARGE_INTEGER value;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&value);
    return (uint64_t)((long double)value.QuadPart * 1000000000.0L / (long double)frequency.QuadPart);
}
#elif defined(__APPLE__)
#include <mach/mach_time.h>
static uint64_t now_ns(void) {
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    return (uint64_t)((__uint128_t)mach_absolute_time() * timebase.numer / timebase.denom);
}
#else
#include <time.h>
static uint64_t now_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
#endif

extern double control_c_sum(double, const double *, size_t);
extern size_t control_c_count(const unsigned char *, size_t);
extern void original_c_scanner(int state[2], uint32_t pixels[100]);
extern void original_c_stars(drbz_star *, size_t, drbz_random, void *);

#define BIG (1024u * 1024u)
#define NSTARS 16384u
static unsigned char bytes[BIG + 64];
static double numbers[65536];
static char regex_text[4097];
static re_t compiled_pattern;
static int scanner_state[2];
static uint32_t scanner_pixels[100];
static drbz_star aos[NSTARS];
static float xs[NSTARS], ys[NSTARS], speeds[NSTARS];
static uint64_t rng_state, rng_calls;

static uint32_t prng32(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27; *state = x;
    return (uint32_t)((x * UINT64_C(0x2545f4914f6cdd1d)) >> 32);
}
static float random_value(void *unused) {
    (void)unused; ++rng_calls;
    return (float)(prng32(&rng_state) >> 8) / (float)UINT32_C(0xffffff);
}
static uint32_t fbits(float x) { uint32_t u; memcpy(&u, &x, 4); return u; }
static uint64_t dbits(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * UINT64_C(1099511628211); }

static void prep_common(uint64_t seed) {
    uint64_t s = seed ? seed : 1;
    for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = (unsigned char)(prng32(&s) >> 24);
    for (size_t i = 0; i < 65536; ++i) numbers[i] = ((double)(int32_t)prng32(&s)) / 2147483648.0;
    memset(regex_text, 'x', sizeof regex_text - 1);
    memcpy(regex_text + sizeof regex_text - 1 - 6, "needle", 6);
    regex_text[sizeof regex_text - 1] = 0;
    compiled_pattern = re_compile("needle");
    scanner_state[0] = 0; scanner_state[1] = 1;
    memset(scanner_pixels, 0, sizeof scanner_pixels);
}
static void prep_stars(uint64_t seed, int detail) {
    rng_state = seed ? seed : 1; rng_calls = 0;
    for (size_t i = 0; i < NSTARS; ++i) {
        float speed = detail == 2 ? 4000.0f : detail == 1 ? (float)(i % 31 + 1) : 0.00001f;
        float x = (float)(i % 64), y = (float)(i % 31);
        aos[i] = (drbz_star){x, y, speed}; xs[i] = x; ys[i] = y; speeds[i] = speed;
    }
}
static uint64_t star_checksum_aos(void) {
    uint64_t h = rng_state ^ rng_calls;
    for (size_t i = 0; i < NSTARS; ++i) { h = mix(h, fbits(aos[i].x)); h = mix(h, fbits(aos[i].y)); }
    return h;
}
static uint64_t star_checksum_soa(void) {
    uint64_t h = rng_state ^ rng_calls;
    for (size_t i = 0; i < NSTARS; ++i) { h = mix(h, fbits(xs[i])); h = mix(h, fbits(ys[i])); }
    return h;
}

typedef uint64_t (*run_fn)(size_t);
typedef void (*prep_fn)(uint64_t);
typedef struct { const char *name; prep_fn prep; run_fn run; } workload;

static void prep0(uint64_t s) { prep_common(s); prep_stars(s ^ UINT64_C(0x1111), 0); }
static void prep1(uint64_t s) { prep_common(s); prep_stars(s ^ UINT64_C(0x2222), 1); }
static void prep2(uint64_t s) { prep_common(s); prep_stars(s ^ UINT64_C(0x3333), 2); }

static uint64_t run_count_scalar_4k(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i) h+=control_c_count(bytes,4096); return h; }
static uint64_t run_count_tuned_4k(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i) h+=drbc_count_dual(bytes,4096); return h; }
static uint64_t run_count_scalar_1m(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i) h+=control_c_count(bytes,BIG); return h; }
static uint64_t run_count_tuned_1m(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i) h+=drbc_count_dual(bytes,BIG); return h; }
static uint64_t run_sum(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i) h=mix(h,dbits(control_c_sum(0,numbers,65536))); return h; }
static uint64_t run_regex_compile(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i){int len=0; h=mix(h,(uint32_t)re_match("needle",regex_text,&len)); h=mix(h,(uint32_t)len);} return h; }
static uint64_t run_regex_compiled(size_t n) { uint64_t h=0; for(size_t i=0;i<n;++i){int len=0; h=mix(h,(uint32_t)re_matchp(compiled_pattern,regex_text,&len)); h=mix(h,(uint32_t)len);} return h; }
static uint64_t run_scanner(size_t n) { for(size_t i=0;i<n;++i) original_c_scanner(scanner_state,scanner_pixels); uint64_t h=(uint32_t)scanner_state[0]; for(size_t i=0;i<100;++i) h=mix(h,scanner_pixels[i]); return h; }
static uint64_t run_star_original(size_t n) { for(size_t i=0;i<n;++i) original_c_stars(aos,NSTARS,random_value,NULL); return star_checksum_aos(); }
static uint64_t run_star_tuned(size_t n) { for(size_t i=0;i<n;++i) drbc_stars_block(xs,ys,speeds,NSTARS,random_value,NULL); return star_checksum_soa(); }

static const workload workloads[]={
    {"count/scalar/4k",prep0,run_count_scalar_4k},{"count/tuned/4k",prep0,run_count_tuned_4k},
    {"count/scalar/1m",prep0,run_count_scalar_1m},{"count/tuned/1m",prep0,run_count_tuned_1m},
    {"sum/ordered/65536",prep0,run_sum},{"regex/compile-search/4k",prep0,run_regex_compile},
    {"regex/compiled-search/4k",prep0,run_regex_compiled},{"scanner/original",prep0,run_scanner},
    {"stars/original/no-wrap",prep0,run_star_original},{"stars/tuned/no-wrap",prep0,run_star_tuned},
    {"stars/original/mixed",prep1,run_star_original},{"stars/tuned/mixed",prep1,run_star_tuned},
    {"stars/original/all-wrap",prep2,run_star_original},{"stars/tuned/all-wrap",prep2,run_star_tuned},
};
#define WORKLOAD_COUNT (sizeof workloads / sizeof *workloads)

static size_t calibrate(const workload *w, uint64_t seed, uint64_t min_ns) {
    size_t n=1;
    while (n < (UINT64_C(1)<<22)) {
        w->prep(seed); uint64_t start=now_ns(); volatile uint64_t h=w->run(n); uint64_t elapsed=now_ns()-start; (void)h;
        if (elapsed >= min_ns) break; n*=2;
    }
    return n;
}
static void emit(const workload *w,size_t n,uint64_t seed) {
    w->prep(seed); uint64_t start=now_ns(); uint64_t checksum=w->run(n); uint64_t elapsed=now_ns()-start;
    printf("{\"workload\":\"%s\",\"iterations\":%zu,\"elapsed_ns\":%" PRIu64 ",\"ns_per_op\":%.6f,\"checksum\":%" PRIu64 "}\n",w->name,n,elapsed,(double)elapsed/(double)n,checksum);
}
int main(int argc,char **argv) {
    if(argc==3){
        uint64_t min_ns=strtoull(argv[1],NULL,10), seed=strtoull(argv[2],NULL,10);
        for(size_t i=0;i<WORKLOAD_COUNT;++i) emit(&workloads[i],calibrate(&workloads[i],seed,min_ns),seed);
        return 0;
    }
    if(argc==(int)(3+WORKLOAD_COUNT) && !strcmp(argv[1],"--fixed")){
        uint64_t seed=strtoull(argv[2],NULL,10);
        for(size_t i=0;i<WORKLOAD_COUNT;++i){ size_t n=(size_t)strtoull(argv[3+i],NULL,10); if(!n){fputs("zero fixed batch\n",stderr);return 2;} emit(&workloads[i],n,seed); }
        return 0;
    }
    fputs("usage: compiler-bench MIN_NS SEED | compiler-bench --fixed SEED N...\n",stderr); return 2;
}
