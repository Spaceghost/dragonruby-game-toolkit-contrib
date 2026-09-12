#include "backend.h"
#include "competitive.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#pragma STDC FP_CONTRACT OFF

int drbc_square(int value, int *out) { int64_t product=(int64_t)value*(int64_t)value; if(product>INT_MAX)return 1; *out=(int)product; return 0; }
int drbc_squares(const int32_t *input,int32_t *output,size_t len){for(size_t i=0;i<len;++i)if(input[i]<-46340||input[i]>46340)return 1;for(size_t i=0;i<len;++i)output[i]=input[i]*input[i];return 0;}
double drbc_sum_ordered(double a,const double *v,size_t n){for(size_t i=0;i<n;++i)a+=v[i];return a;}
double drbc_sum_unrolled(double a,const double *v,size_t n){size_t i=0;for(;n-i>=4;i+=4){a+=v[i];a+=v[i+1];a+=v[i+2];a+=v[i+3];}for(;i<n;++i)a+=v[i];return a;}
void drbc_stars_scalar(drbz_star *stars,size_t len,drbz_random random,void *ctx){for(size_t i=0;i<len;++i){stars[i].x+=stars[i].s;if(stars[i].x>1280.0f)stars[i].x=random(ctx)*-1280.0f;stars[i].y+=stars[i].s;if(stars[i].y>720.0f)stars[i].y=random(ctx)*-720.0f;}}
void drbc_stars_soa(float *x,float *y,const float *speed,size_t len,drbz_random random,void *ctx){drbc_stars_block(x,y,speed,len,random,ctx);}
size_t drbc_count_scalar(const unsigned char *bytes,size_t len){size_t total=0;for(size_t i=0;i<len;++i)total+=bytes[i]=='\n';return total;}
void drbc_scanner_reset(drbz_scanner *s){s->position=0;s->increment=1;s->previous=-1;for(size_t i=0;i<100;++i)s->pixels[i]=UINT32_C(0xff000000);}
const uint32_t *drbc_scanner_frame(drbz_scanner *s){if(s->previous!=s->position){if(s->previous>=0){size_t old=(size_t)s->previous*10;for(size_t i=0;i<10;++i)s->pixels[old+i]=UINT32_C(0xff000000);}size_t row=(size_t)s->position*10;for(size_t i=0;i<10;++i)s->pixels[row+i]=UINT32_C(0xff00ff00);s->previous=s->position;}s->position+=s->increment;if(s->increment>0&&s->position>=10){s->increment=-1;s->position=9;}else if(s->increment<0&&s->position<0){s->increment=1;s->position=1;}return s->pixels;}

typedef struct{const void *source;size_t length,next;} c_frame;
static int c_push(c_frame frames[64],size_t *depth,drbz_view view){if(!view.length)return 0;if(*depth==64||!view.children)return 2;for(size_t i=0;i<*depth;++i)if(frames[i].source==view.children)return 2;frames[(*depth)++]=(c_frame){view.children,view.length,0};return 0;}
int drbc_sum_tree(const void *source,size_t length,drbz_reader reader,void *ctx,double *result){c_frame frames[64]={{source,length,0}};size_t depth=1;double sum=0;while(depth){c_frame *frame=&frames[depth-1];if(frame->next==frame->length){--depth;continue;}drbz_view view={0};reader(ctx,frame->source,frame->next++,&view);if(view.kind==1)sum+=view.number;else if(view.kind==2){int rc=c_push(frames,&depth,view);if(rc)return rc;}else return 1;}*result=sum;return 0;}
int drbc_sum_tree_batched(const void *source,size_t length,drbz_reader_batch reader,void *ctx,double *result){c_frame frames[64]={{source,length,0}};size_t depth=1;double sum=0;drbz_view views[16];while(depth){c_frame *frame=&frames[depth-1];if(frame->next==frame->length){--depth;continue;}size_t wanted=frame->length-frame->next;if(wanted>16)wanted=16;size_t got=reader(ctx,frame->source,frame->next,views,wanted);if(!got||got>wanted)return 1;for(size_t i=0;i<got;++i){drbz_view view=views[i];++frame->next;if(view.kind==1)sum+=view.number;else if(view.kind==2){if(!view.length)continue;int rc=c_push(frames,&depth,view);if(rc)return rc;break;}else return 1;}}*result=sum;return 0;}
int drbc_greeting(int goodbye,const unsigned char *name,size_t length,unsigned char *output,size_t capacity,size_t *written){const char *prefix=goodbye?"Bye ":"Hello ";size_t prefix_len=goodbye?4:6;if(capacity<prefix_len+2||length>capacity-prefix_len-2)return 1;memcpy(output,prefix,prefix_len);if(length)memcpy(output+prefix_len,name,length);output[prefix_len+length]='!';output[prefix_len+length+1]=0;*written=prefix_len+length+1;return 0;}

static size_t c_storage_bytes(size_t count){if(count>SIZE_MAX/(3*sizeof(float)))return 0;size_t floats=count*3*sizeof(float),align=_Alignof(drbz_packed_sprite);if(floats>SIZE_MAX-(align-1))return 0;size_t sprite_start=(floats+align-1)&~(align-1);if(count>(SIZE_MAX-sprite_start)/sizeof(drbz_packed_sprite))return 0;return sprite_start+count*sizeof(drbz_packed_sprite);}
size_t drbc_starfield_storage_bytes(size_t count){return c_storage_bytes(count);}
static float c_next_random(drbz_starfield *field){uint64_t x=field->rng_state;x^=x>>12;x^=x<<25;x^=x>>27;field->rng_state=x;uint64_t mixed=x*UINT64_C(0x2545F4914F6CDD1D);uint32_t top=(uint32_t)(mixed>>40);return(float)top/16777215.0f;}
static float c_random_callback(void *raw){return c_next_random((drbz_starfield *)raw);}
int drbc_starfield_init(void *storage,size_t available,size_t count,uint64_t seed,drbz_starfield *out){size_t needed=c_storage_bytes(count);if((count&&!needed)||needed>available)return 1;uint64_t initial=seed?seed:1;if(!count){*out=(drbz_starfield){.rng_state=initial};return 0;}if(!storage)return 1;if((uintptr_t)storage%_Alignof(drbz_packed_sprite))return 2;unsigned char *base=storage;size_t one=count*sizeof(float),align=_Alignof(drbz_packed_sprite),sprite_start=(one*3+align-1)&~(align-1);out->x=(float *)base;out->y=(float *)(base+one);out->speed=(float *)(base+one*2);out->sprites=(drbz_packed_sprite *)(base+sprite_start);out->len=count;out->rng_state=initial;for(size_t i=0;i<count;++i){out->x[i]=c_next_random(out)*-1280.0f;out->y[i]=c_next_random(out)*-720.0f;out->speed[i]=1.0f+c_next_random(out)*4.0f;}drbc_starfield_pack(out);return 0;}
void drbc_starfield_update(drbz_starfield *f){if(f->len)drbc_stars_block(f->x,f->y,f->speed,f->len,c_random_callback,f);}
void drbc_starfield_pack(drbz_starfield *f){for(size_t i=0;i<f->len;++i)f->sprites[i]=(drbz_packed_sprite){f->x[i],f->y[i],4.0f,4.0f,1};}
void drbc_starfield_update_pack(drbz_starfield *f){drbc_starfield_update(f);for(size_t i=0;i<f->len;++i){f->sprites[i].x=f->x[i];f->sprites[i].y=f->y[i];}}
void drbc_starfield_frame(drbz_starfield *f,drbz_sprite_batch_sink sink,void *ctx){drbc_starfield_update_pack(f);sink(ctx,f->sprites,f->len);}

static const drbn_backend_ops BACKENDS[]={
{"c",drbc_square,drbc_squares,drbc_sum_ordered,drbc_sum_unrolled,drbc_stars_scalar,drbc_stars_soa,drbc_count_scalar,drbc_count_dual,drbc_scanner_reset,drbc_scanner_frame,drbc_sum_tree,drbc_sum_tree_batched,drbc_greeting,drbc_starfield_storage_bytes,drbc_starfield_init,drbc_starfield_update,drbc_starfield_pack,drbc_starfield_update_pack,drbc_starfield_frame},
{"zig",drbz_square,drbz_squares,drbz_sum_ordered,drbz_sum_unrolled,drbz_stars_scalar,drbz_stars_soa,drbz_count_scalar,drbz_count_dual,drbz_scanner_reset,drbz_scanner_frame,drbz_sum_tree,drbz_sum_tree_batched,drbz_greeting,drbz_starfield_storage_bytes,drbz_starfield_init,drbz_starfield_update,drbz_starfield_pack,drbz_starfield_update_pack,drbz_starfield_frame},
{"odin",drbo_square,drbo_squares,drbo_sum_ordered,drbo_sum_unrolled,drbo_stars_scalar,drbo_stars_soa,drbo_count_scalar,drbo_count_dual,drbo_scanner_reset,drbo_scanner_frame,drbo_sum_tree,drbo_sum_tree_batched,drbo_greeting,drbo_starfield_storage_bytes,drbo_starfield_init,drbo_starfield_update,drbo_starfield_pack,drbo_starfield_update_pack,drbo_starfield_frame}};
size_t drbn_backend_count(void){return sizeof BACKENDS/sizeof BACKENDS[0];}
const drbn_backend_ops *drbn_backend_at(size_t i){return i<drbn_backend_count()?&BACKENDS[i]:NULL;}
const drbn_backend_ops *drbn_backend_by_name(const char *name,size_t length){for(size_t i=0;i<drbn_backend_count();++i){size_t n=strlen(BACKENDS[i].name);if(n==length&&!memcmp(name,BACKENDS[i].name,n))return &BACKENDS[i];}return NULL;}
