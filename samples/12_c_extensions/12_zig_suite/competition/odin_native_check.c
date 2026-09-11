#include "native.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void original_c_scanner(int state[2], uint32_t pixels[100]);
void original_c_stars(drbz_star *, size_t, drbz_random, void *);
double control_c_sum(double, const double *, size_t);
size_t control_c_count(const unsigned char *, size_t);

static uint32_t fbits(float v) { uint32_t u; memcpy(&u,&v,sizeof u); return u; }
static uint64_t dbits(double v) { uint64_t u; memcpy(&u,&v,sizeof u); return u; }
struct rng { uint32_t state; uint64_t calls; };
static float random_value(void *raw) {
    struct rng *r=raw; r->state=r->state*UINT32_C(1664525)+UINT32_C(1013904223); ++r->calls;
    return (float)(r->state>>8)/(float)UINT32_C(0xffffff);
}

static void square_checks(void) {
    for (int value=-46340; value<=46340; ++value) {
        int32_t out=-1; assert(drbo_square(value,&out)==0); assert(out==value*value);
    }
    const int32_t invalid[]={INT32_MIN,INT32_MAX,-46341,46341};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;++i){ int32_t out=777; assert(drbo_square(invalid[i],&out)==1&&out==777); }
    int32_t in[260], out[260];
    for(size_t n=0;n<=260;++n){
        for(size_t i=0;i<260;++i){in[i]=(int32_t)i-130;out[i]=-1;}
        assert(drbo_squares(in,out,n)==0);
        for(size_t i=0;i<n;++i) assert(out[i]==in[i]*in[i]);
        assert(drbo_squares(in,in,n)==0);
        for(size_t i=0;i<n;++i) assert(in[i]==out[i]);
    }
    for(size_t i=0;i<260;++i){in[i]=3;out[i]=777;} in[259]=INT32_MAX;
    assert(drbo_squares(in,out,260)==1); for(size_t i=0;i<260;++i) assert(out[i]==777);
}

static void sum_checks(void) {
    const double data[]={1e16,1,-1e16,3,-0.0,1e-300,-1e-300,4,-7,0.0};
    for(size_t start=0;start<5;++start) for(size_t n=0;n<=10-start;++n){
        double expected=control_c_sum(-0.0,data+start,n);
        assert(dbits(drbo_sum_ordered(-0.0,data+start,n))==dbits(expected));
        assert(dbits(drbo_sum_unrolled(-0.0,data+start,n))==dbits(expected));
    }
}

static void byte_checks(void) {
    unsigned char data[65570];
    uint32_t state=7;
    for(size_t i=0;i<sizeof data;++i){state=state*UINT32_C(1664525)+UINT32_C(1013904223);data[i]=(unsigned char)(state>>24);}
    const size_t lengths[]={0,1,7,8,15,16,17,31,32,33,63,64,65,511,4096,65536};
    for(size_t off=0;off<32;++off) for(size_t j=0;j<sizeof lengths/sizeof *lengths;++j){
        size_t n=lengths[j], expected=control_c_count(data+off,n);
        assert(drbo_count_scalar(data+off,n)==expected); assert(drbo_count_blocked(data+off,n)==expected);
    }
}

static void star_checks(void) {
    drbz_star reference[257], scalar[257]; float x[257],y[257],speed[257];
    uint32_t setup=123;
    for(size_t i=0;i<257;++i){
        setup=setup*UINT32_C(1664525)+UINT32_C(1013904223); reference[i].x=(int)(setup%4001)-2000;
        setup=setup*UINT32_C(1664525)+UINT32_C(1013904223); reference[i].y=(int)(setup%2001)-1000;
        setup=setup*UINT32_C(1664525)+UINT32_C(1013904223); reference[i].s=(float)(setup%81)*0.125f;
        if(i%13==0){reference[i].x=1280;reference[i].y=720;}
        scalar[i]=reference[i]; x[i]=reference[i].x;y[i]=reference[i].y;speed[i]=reference[i].s;
    }
    struct rng a={91827,0},b=a,c=a;
    for(size_t tick=0;tick<1000;++tick){
        original_c_stars(reference,257,random_value,&a);
        drbo_stars_scalar(scalar,257,random_value,&b);
        drbo_stars_soa(x,y,speed,257,random_value,&c);
        assert(a.state==b.state&&a.state==c.state&&a.calls==b.calls&&a.calls==c.calls);
        for(size_t i=0;i<257;++i){
            assert(fbits(reference[i].x)==fbits(scalar[i].x)&&fbits(reference[i].y)==fbits(scalar[i].y));
            assert(fbits(reference[i].x)==fbits(x[i])&&fbits(reference[i].y)==fbits(y[i]));
            assert(fbits(reference[i].s)==fbits(speed[i]));
        }
    }
}

static void scanner_checks(void) {
    int state[2]={0,1}; uint32_t expected[100]; drbz_scanner odin; drbo_scanner_reset(&odin);
    for(size_t frame=0;frame<10000;++frame){
        original_c_scanner(state,expected); const uint32_t *actual=drbo_scanner_frame(&odin);
        assert(actual); assert(memcmp(actual,expected,sizeof expected)==0);
    }
}

int main(void){
    square_checks(); sum_checks(); byte_checks(); star_checks(); scanner_checks();
    puts("ODIN_NATIVE_PROOF {\"square_inputs\":92681,\"star_ticks\":257000,\"scanner_frames\":10000,\"allocation_claim\":\"separate-symbol-audit\"}");
    return 0;
}
