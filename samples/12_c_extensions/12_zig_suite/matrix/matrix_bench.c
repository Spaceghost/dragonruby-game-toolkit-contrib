#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/competitive.h"
#include "../src/native.h"

#ifdef _WIN32
#include <windows.h>
static uint64_t now_ns(void){ LARGE_INTEGER f,c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (uint64_t)((long double)c.QuadPart*1000000000.0L/f.QuadPart); }
#elif defined(__APPLE__)
#include <mach/mach_time.h>
static uint64_t now_ns(void){ static mach_timebase_info_data_t tb; if(!tb.denom) mach_timebase_info(&tb); return (uint64_t)((__uint128_t)mach_absolute_time()*tb.numer/tb.denom); }
#else
#include <time.h>
static uint64_t now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec; }
#endif

static uint32_t rng_state=0x12345678u; static uint64_t rng_calls;
static float rng(void *p){ (void)p; rng_state=rng_state*1664525u+1013904223u; ++rng_calls; return (float)(rng_state>>8)/(float)0xffffffu; }
static unsigned char bytes[1<<20];
static float xs[16384],ys[16384],ss[16384];
static volatile uint64_t sink64;

static void prep_bytes(void){ uint32_t s=7; for(size_t i=0;i<sizeof bytes;i++){ s=s*1664525u+1013904223u; bytes[i]=(unsigned char)(s>>24); } }
static void prep_stars(int mixed){ for(size_t i=0;i<16384;i++){ float sp=mixed?(float)(i%31+1):0.00001f; xs[i]=(float)(i%64); ys[i]=(float)(i%31); ss[i]=sp; } rng_state=0x12345678u; rng_calls=0; }

typedef size_t (*count_fn)(const unsigned char*,size_t);
typedef void (*star_fn)(float*,float*,const float*,size_t,rival_random,void*);
static double bench_count(count_fn f,size_t n){ size_t it=1; uint64_t elapsed=0; do{ uint64_t a=now_ns(); uint64_t sum=0; for(size_t i=0;i<it;i++) sum+=f(bytes,n); elapsed=now_ns()-a; sink64^=sum; if(elapsed<10000000ull) it*=2; }while(elapsed<10000000ull && it<(1u<<26)); return (double)elapsed/(double)it; }
static double bench_stars(star_fn f,int mixed){ size_t it=1; uint64_t elapsed=0; do{ prep_stars(mixed); uint64_t a=now_ns(); for(size_t i=0;i<it;i++) f(xs,ys,ss,16384,rng,NULL); elapsed=now_ns()-a; sink64^=(uint64_t)rng_state+rng_calls; if(elapsed<10000000ull) it*=2; }while(elapsed<10000000ull && it<(1u<<20)); return (double)elapsed/(double)it; }

int main(void){
  prep_bytes();
  printf("RESULT lf32_c_ns %.6f\n",bench_count(drbc_count_dual,32));
  printf("RESULT lf32_zig_ns %.6f\n",bench_count(drbz_count_dual,32));
  printf("RESULT lf4k_c_ns %.6f\n",bench_count(drbc_count_dual,4096));
  printf("RESULT lf4k_zig_ns %.6f\n",bench_count(drbz_count_dual,4096));
  printf("RESULT lf1m_c_ns %.6f\n",bench_count(drbc_count_dual,sizeof bytes));
  printf("RESULT lf1m_zig_ns %.6f\n",bench_count(drbz_count_dual,sizeof bytes));
  printf("RESULT stars_nowrap_c_ns %.6f\n",bench_stars(drbc_stars_block,0));
  printf("RESULT stars_nowrap_zig_ns %.6f\n",bench_stars(drbz_stars_block,0));
  printf("RESULT stars_mixed_c_ns %.6f\n",bench_stars(drbc_stars_block,1));
  printf("RESULT stars_mixed_zig_ns %.6f\n",bench_stars(drbz_stars_block,1));
  printf("RESULT checksum %llu\n",(unsigned long long)sink64);
  return 0;
}
