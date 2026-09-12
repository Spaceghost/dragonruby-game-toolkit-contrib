#include "../src/backend.h"
#include "../conformance/conformance.h"
#include "../platform/dynlib.h"
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t fbits(float x){uint32_t u;memcpy(&u,&x,sizeof u);return u;}static void compare(const drbn_backend_ops *a,const drbn_backend_ops *b){_Alignas(max_align_t)unsigned char sa[4096],sb[4096];drbz_starfield fa,fb;size_t na=a->starfield_storage_bytes(64),nb=b->starfield_storage_bytes(64);assert(na==nb&&na&&na<=sizeof sa);assert(a->starfield_init(sa,sizeof sa,64,UINT64_C(0x4d595df4d0f33173),&fa)==0);assert(b->starfield_init(sb,sizeof sb,64,UINT64_C(0x4d595df4d0f33173),&fb)==0);for(unsigned f=0;f<32;++f){a->starfield_update(&fa);b->starfield_update(&fb);}assert(fa.rng_state==fb.rng_state&&fa.len==fb.len);for(size_t i=0;i<fa.len;++i){assert(fbits(fa.x[i])==fbits(fb.x[i]));assert(fbits(fa.y[i])==fbits(fb.y[i]));assert(fbits(fa.speed[i])==fbits(fb.speed[i]));}}
static void dynlib(void){
#ifdef _WIN32
const char *lib="kernel32.dll",*sym="GetCurrentProcessId";
#elif defined(__APPLE__)
const char *lib="/usr/lib/libSystem.B.dylib",*sym="malloc";
#else
const char *lib="libc.so.6",*sym="malloc";
#endif
void *h=drbn_dynlib_open(lib);assert(h);assert(drbn_dynlib_symbol(h,sym));drbn_dynlib_close(h);}
int main(void){assert(sizeof(void *)==8);assert(sizeof(drbz_star)==12&&_Alignof(drbz_star)==4);assert(sizeof(drbz_scanner)==412&&_Alignof(drbz_scanner)==4);assert(sizeof(drbz_view)==32&&_Alignof(drbz_view)==8);assert(sizeof(drbz_packed_sprite)==24&&_Alignof(drbz_packed_sprite)==8);assert(sizeof(drbz_starfield)==48&&_Alignof(drbz_starfield)==8);assert(drbn_backend_count()==3);const drbn_backend_ops *c=drbn_backend_by_name("c",1),*z=drbn_backend_by_name("zig",3),*o=drbn_backend_by_name("odin",4);assert(c&&z&&o);compare(c,z);compare(c,o);uint64_t names=0;for(size_t i=0;i<3;++i){const drbn_backend_ops *ops=drbn_backend_at(i);char json[2048];size_t n=0;assert(drbn_conformance_json(ops,json,sizeof json,&n)==0);assert(n&&strstr(json,"\"abi_version\":1"));printf("PORTABLE_CONFORMANCE %.*s\n",(int)n,json);for(const char *p=ops->name;*p;++p)names=names*131u+(unsigned char)*p;}dynlib();printf("PORTABLE_ABI_PROOF {\"backends\":3,\"pointer_bytes\":%zu,\"star\":%zu,\"scanner\":%zu,\"view\":%zu,\"sprite\":%zu,\"starfield\":%zu,\"dynlib\":true,\"name_checksum\":%" PRIu64 "}\n",sizeof(void *),sizeof(drbz_star),sizeof(drbz_scanner),sizeof(drbz_view),sizeof(drbz_packed_sprite),sizeof(drbz_starfield),names);return 0;}
