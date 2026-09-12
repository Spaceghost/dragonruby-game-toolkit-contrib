#include "bench.h"
#include "competitive.h"
#include "native.h"

size_t control_c_count(const unsigned char *, size_t);
void original_c_stars(drbz_star *, size_t, drbz_random, void *);
static unsigned char bytes[16 * 1048576 + 64];
static drbz_star stars[16384];
static float xs[16384], ys[16384], speeds[16384];
static uint32_t random_state;
static uint64_t random_calls;
static float random_value(void *unused) {
    (void)unused; ++random_calls;
    return (float)(bench_random(&random_state) >> 8) / (float)UINT32_C(0xffffff);
}
static void prepare(const bench_case *c, uint32_t seed) {
    random_state = seed; random_calls = 0;
    if (c->task == 0) {
        size_t n = c->detail == 3 ? sizeof bytes : c->size + 64;
        for (size_t i=0; i<n; ++i)
            bytes[i] = c->detail == 1 ? '\n' : c->detail == 2 ? 'x' : (unsigned char)(bench_random(&seed) >> 24);
    } else {
        for (size_t i=0; i<c->size; ++i) {
            float speed = c->detail == 2 ? 4000.0f : c->detail == 1 ? (float)(i%31+1) : .00001f;
            stars[i]=(drbz_star){(float)(i%64), (float)(i%31), speed};
            xs[i]=stars[i].x; ys[i]=stars[i].y; speeds[i]=speed;
        }
    }
}
static uint64_t batch(const bench_case *c, unsigned variant, size_t iterations) {
    uint64_t checksum=0;
    if (c->task == 0) {
        size_t (*const functions[])(const unsigned char *, size_t)={control_c_count,drbz_count_blocked,drbc_count_dual,drbz_count_dual,drbo_count_dual};
        size_t (*function)(const unsigned char *, size_t)=functions[variant];
        for (size_t i=0; i<iterations; ++i) {
            size_t offset=c->detail == 3 ? (i%16)*1048576 : c->detail == 4 ? 1 : 0;
            checksum+=function(bytes+offset,c->size);
        }
    } else {
        switch (variant) {
        case 0: for (size_t i=0; i<iterations; ++i) original_c_stars(stars,c->size,random_value,NULL); break;
        case 1: for (size_t i=0; i<iterations; ++i) drbz_stars_soa(xs,ys,speeds,c->size,random_value,NULL); break;
        case 2: for (size_t i=0; i<iterations; ++i) drbc_stars_block(xs,ys,speeds,c->size,random_value,NULL); break;
        case 3: for (size_t i=0; i<iterations; ++i) drbz_stars_block(xs,ys,speeds,c->size,random_value,NULL); break;
        case 4: for (size_t i=0; i<iterations; ++i) drbo_stars_block(xs,ys,speeds,c->size,random_value,NULL); break;
        default: abort();
        }
        checksum=random_state+random_calls;
    }
    return checksum;
}
static uint64_t finish(const bench_case *c,unsigned variant,uint64_t checksum) {
    if (c->task == 1) for (size_t i=0; i<c->size; ++i) {
        uint32_t x,y;
        memcpy(&x,variant ? &xs[i] : &stars[i].x,sizeof x);
        memcpy(&y,variant ? &ys[i] : &stars[i].y,sizeof y);
        checksum=(checksum^x)*UINT64_C(1099511628211);
        checksum=(checksum^y)*UINT64_C(1099511628211);
    }
    return checksum;
}
int main(int argc,char **argv) {
    const bench_case cases[]={
        {"rival/lf/31", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,31,0,0,prepare,batch,finish},
        {"rival/lf/32", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,32,0,0,prepare,batch,finish},
        {"rival/lf/33-unaligned", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,33,0,4,prepare,batch,finish},
        {"rival/lf/63-unaligned", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,63,0,4,prepare,batch,finish},
        {"rival/lf/64", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,64,0,0,prepare,batch,finish},
        {"rival/lf/65-unaligned", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,65,0,4,prepare,batch,finish},
        {"rival/lf/4k", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,4096,0,0,prepare,batch,finish},
        {"rival/lf/4k-all", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,4096,0,1,prepare,batch,finish},
        {"rival/lf/4k-none", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,4096,0,2,prepare,batch,finish},
        {"rival/lf/8k", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,8192,0,0,prepare,batch,finish},
        {"rival/lf/16k", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,16384,0,0,prepare,batch,finish},
        {"rival/lf/64k", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,65536,0,0,prepare,batch,finish},
        {"rival/lf/1m-warm", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,1048576,0,0,prepare,batch,finish},
        {"rival/lf/1m-rotating16m", {"c_scalar","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,1048576,0,3,prepare,batch,finish},
        {"rival/stars/64-no-wrap", {"c_original","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,64,1,0,prepare,batch,finish},
        {"rival/stars/16384-no-wrap", {"c_original","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,16384,1,0,prepare,batch,finish},
        {"rival/stars/16384-mixed", {"c_original","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,16384,1,1,prepare,batch,finish},
        {"rival/stars/4096-all-wrap", {"c_original","zig_previous","c_tuned","zig_tuned","odin_tuned"},5,4096,1,2,prepare,batch,finish},
    };
    bench_run(cases,sizeof cases/sizeof *cases,"rivals-linked-libc",1,argc,argv);
    return 0;
}
