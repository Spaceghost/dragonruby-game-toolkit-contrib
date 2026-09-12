#include "conformance.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#endif
static uint64_t now_ns(void){
#ifdef _WIN32
static LARGE_INTEGER f;LARGE_INTEGER v;if(!f.QuadPart)QueryPerformanceFrequency(&f);QueryPerformanceCounter(&v);return(uint64_t)((long double)v.QuadPart*1000000000.0L/(long double)f.QuadPart);
#elif defined(__APPLE__)
static mach_timebase_info_data_t tb;if(!tb.denom)mach_timebase_info(&tb);return(uint64_t)((__uint128_t)mach_absolute_time()*tb.numer/tb.denom);
#else
struct timespec ts;if(timespec_get(&ts,TIME_UTC)!=TIME_UTC)return 0;return(uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
#endif
}
static uint32_t fbits(float x){uint32_t v;memcpy(&v,&x,sizeof v);return v;}static uint64_t dbits(double x){uint64_t v;memcpy(&v,&x,sizeof v);return v;}static uint64_t mix(uint64_t h,uint64_t v){return(h^v)*UINT64_C(1099511628211);}
static void read_one(void *c,const void *s,size_t i,drbz_view *v){(void)c;*v=((const drbz_view *)s)[i];}static size_t read_batch(void *c,const void *s,size_t i,drbz_view *v,size_t cap){(void)c;memcpy(v,(const drbz_view *)s+i,cap*sizeof *v);return cap;}
int drbn_conformance_json(const drbn_backend_ops *o,char *out,size_t cap,size_t *written){if(!o||!out||!written||!cap)return 1;int square=123;if(o->square(-17,&square)||square!=289)return 2;square=123;if(o->square(46341,&square)!=1||square!=123)return 3;const unsigned char binary[]={0,'\n',0xff,'\n','x'};if(o->count_fast(binary,sizeof binary)!=2||o->count_scalar(binary,sizeof binary)!=2)return 4;const double values[]={1e16,1,-1e16,3,-0.0,1e-300,-1e-300};double ordered=o->sum_ordered(0,values,sizeof values/sizeof values[0]);if(ordered!=3.0||dbits(ordered)!=dbits(o->sum_unrolled(0,values,sizeof values/sizeof values[0])))return 5;drbz_view nested[]={{1,1,NULL,0},{1,-1e16,NULL,0}},tree[]={{1,1e16,NULL,0},{2,0,nested,2},{1,3,NULL,0}};double single=777,batched=777;if(o->sum_tree(tree,3,read_one,NULL,&single)||single!=3.0)return 6;if(o->sum_tree_batched(tree,3,read_batch,NULL,&batched)||dbits(batched)!=dbits(single))return 7;unsigned char greeting[32]={0};size_t glen=0;if(o->greeting(0,(const unsigned char *)"Native",6,greeting,sizeof greeting,&glen)||glen!=13||memcmp(greeting,"Hello Native!",13))return 8;drbz_scanner scanner;o->scanner_reset(&scanner);uint64_t scanner_hash=0;for(unsigned f=0;f<20;++f){const uint32_t *p=o->scanner_frame(&scanner);scanner_hash=mix(scanner_hash,p[(f*7)%100]);}_Alignas(max_align_t) unsigned char storage[4096];drbz_starfield field;size_t need=o->starfield_storage_bytes(32);if(!need||need>sizeof storage||o->starfield_init(storage,sizeof storage,32,UINT64_C(0x123456789abcdef),&field))return 9;uint64_t star_hash=field.rng_state;for(unsigned f=0;f<32;++f)o->starfield_update(&field);for(size_t i=0;i<field.len;++i){star_hash=mix(star_hash,fbits(field.x[i]));star_hash=mix(star_hash,fbits(field.y[i]));}unsigned char bytes[4096];for(size_t i=0;i<sizeof bytes;++i)bytes[i]=(unsigned char)(i*17u+(i>>3));volatile uint64_t checksum=0;uint64_t t0=now_ns();for(unsigned i=0;i<20000;++i)checksum+=o->count_fast(bytes,sizeof bytes);uint64_t count_ns=now_ns()-t0;t0=now_ns();for(unsigned i=0;i<2000;++i){o->starfield_update(&field);checksum^=field.rng_state;}uint64_t stars_ns=now_ns()-t0;int n=snprintf(out,cap,"{\"abi_version\":%d,\"backend\":\"%s\",\"pointer_bytes\":%zu,\"types\":{\"star\":%zu,\"scanner\":%zu,\"view\":%zu,\"sprite\":%zu,\"starfield\":%zu},\"checksums\":{\"scanner\":\"%016" PRIx64 "\",\"starfield\":\"%016" PRIx64 "\",\"timing\":\"%016" PRIx64 "\"},\"timing\":{\"count_4096_x20000_ns\":%" PRIu64 ",\"starfield_32_x2000_ns\":%" PRIu64 "},\"allocation_scope\":\"external_meter_required\",\"shutdown_clean\":true}",DRBN_ABI_VERSION,o->name,sizeof(void *),sizeof(drbz_star),sizeof(drbz_scanner),sizeof(drbz_view),sizeof(drbz_packed_sprite),sizeof(drbz_starfield),scanner_hash,star_hash,(uint64_t)checksum,count_ns,stars_ns);if(n<0||(size_t)n>=cap)return 10;*written=(size_t)n;return 0;}
