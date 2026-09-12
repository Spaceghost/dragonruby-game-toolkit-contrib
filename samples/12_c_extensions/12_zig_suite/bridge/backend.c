/* Backend-neutral DragonRuby/mruby adapter. Ruby parsing/result construction is
 * intentionally shared; C, Zig and Odin compete behind one C ABI vtable. */
#include <dragonruby.h>
#include <mruby/array.h>
#include "../src/backend.h"
#include "../conformance/conformance.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DRBZ_SQLITE_QUERY
#include <sqlite3.h>
#include "../sqlite/query.h"
#endif

extern void drbz_register_legacy_extensions(mrb_state *, drb_api_t *);
static drb_api_t *api;
static const drbn_backend_ops *backend;
static drbz_scanner scanner;
static mrb_sym draw_sprite_sym;
static void *star_storage;
static drbz_starfield starfield;
static int starfield_ready;

static void arg_error(mrb_state *mrb,const char *s){api->mrb_raise(mrb,api->mrb_class_get(mrb,"ArgumentError"),s);}
static void run_error(mrb_state *mrb,const char *s){api->mrb_raise(mrb,api->mrb_class_get(mrb,"RuntimeError"),s);}
static void view_from_value(mrb_value value, drbz_view *view) {
    *view=(drbz_view){0};
    if(mrb_fixnum_p(value)){view->kind=1;view->number=(double)mrb_fixnum(value);}
    else if(mrb_float_p(value)){view->kind=1;view->number=(double)mrb_float(value);}
    else if(mrb_array_p(value)){view->kind=2;view->children=RARRAY_PTR(value);view->length=(size_t)RARRAY_LEN(value);}
}
static size_t read_values(void *ctx,const void *source,size_t index,drbz_view *views,size_t cap){(void)ctx;const mrb_value *v=(const mrb_value *)source+index;for(size_t i=0;i<cap;++i)view_from_value(v[i],&views[i]);return cap;}

static mrb_value native_backend(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");return api->mrb_str_new(mrb,backend->name,(mrb_int)strlen(backend->name));}

#ifdef DRBZ_SQLITE_QUERY
typedef struct {
    void (*init)(drbz_query_cache *,sqlite3 *);
    int (*clear)(drbz_query_cache *);
    drbz_query_result (*pack)(drbz_query_cache *,const unsigned char *,size_t);
    void (*output)(const drbz_query_cache *,const unsigned char **,size_t *,size_t *);
} query_ops;
static const query_ops QC={drbc_query_cache_init,drbc_query_cache_clear,drbc_query_pack,drbc_query_output};
static const query_ops QZ={drbz_query_cache_init,drbz_query_cache_clear,drbz_query_pack,drbz_query_output};
static const query_ops QO={drbo_query_cache_init,drbo_query_cache_clear,drbo_query_pack,drbo_query_output};
static const query_ops *qops;
static sqlite3 *query_db;
static drbz_query_cache query_cache;
static int query_ready;
static const query_ops *query_for(const char *name){return name[0]=='c'&&!name[1]?&QC:name[0]=='z'?&QZ:&QO;}
#endif

static mrb_value native_use_backend(mrb_state *mrb,mrb_value self){
    (void)self;char *name; mrb_int length; api->mrb_get_args(mrb,"s",&name,&length);
    if(starfield_ready){run_error(mrb,"clear Native starfield before switching backend");return mrb_nil_value();}
#ifdef DRBZ_SQLITE_QUERY
    if(query_db){run_error(mrb,"close Native SQLite database before switching backend");return mrb_nil_value();}
#endif
    const drbn_backend_ops *next=length<0?NULL:drbn_backend_by_name(name,(size_t)length);
    if(!next){arg_error(mrb,"backend must be c, zig, or odin");return mrb_nil_value();}
    backend=next; backend->scanner_reset(&scanner);
#ifdef DRBZ_SQLITE_QUERY
    qops=query_for(backend->name);
#endif
    return native_backend(mrb,self);
}
static mrb_value native_square(mrb_state *mrb,mrb_value self){(void)self;mrb_int in;api->mrb_get_args(mrb,"i",&in);int out=0;if(in<INT_MIN||in>INT_MAX||backend->square((int)in,&out)){arg_error(mrb,"square is outside signed 32-bit range");return mrb_nil_value();}return mrb_fixnum_value(out);}
static mrb_value native_count(mrb_state *mrb,mrb_value self){(void)self;char *s;mrb_int n;api->mrb_get_args(mrb,"s",&s,&n);return mrb_fixnum_value((mrb_int)backend->count_fast((const unsigned char *)s,(size_t)n));}
static mrb_value native_sum(mrb_state *mrb,mrb_value self){(void)self;mrb_value *values;mrb_int n;api->mrb_get_args(mrb,"*",&values,&n);double out=0;int rc=backend->sum_tree_batched(values,(size_t)n,read_values,NULL,&out);if(rc){arg_error(mrb,rc==1?"unsupported value in nested sum":"cyclic or excessively deep array");return mrb_nil_value();}return api->drb_float_value(mrb,out);}
static mrb_value native_greeting(mrb_state *mrb,int goodbye){char *name;mrb_int n;api->mrb_get_args(mrb,"s",&name,&n);unsigned char out[512];size_t written=0;if(backend->greeting(goodbye,(const unsigned char *)name,(size_t)n,out,sizeof out,&written)){arg_error(mrb,"greeting exceeds 511-byte adapter limit");return mrb_nil_value();}return api->mrb_str_new(mrb,(const char *)out,(mrb_int)written);}
static mrb_value native_hello(mrb_state *mrb,mrb_value self){(void)self;return native_greeting(mrb,0);} static mrb_value native_goodbye(mrb_state *mrb,mrb_value self){(void)self;return native_greeting(mrb,1);}
static mrb_value native_scanner_reset(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");backend->scanner_reset(&scanner);return mrb_nil_value();}
static mrb_value native_scanner_frame(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");const uint32_t *pixels=backend->scanner_frame(&scanner);api->drb_upload_pixel_array("native_suite_scanner",10,10,pixels);return mrb_nil_value();}

static void clear_starfield(void){free(star_storage);star_storage=NULL;memset(&starfield,0,sizeof starfield);starfield_ready=0;}
static mrb_value native_star_reset(mrb_state *mrb,mrb_value self){(void)self;mrb_int count;api->mrb_get_args(mrb,"i",&count);if(count<0){arg_error(mrb,"star count must be nonnegative");return mrb_nil_value();}size_t n=(size_t)count,bytes=backend->starfield_storage_bytes(n);if(n&&!bytes){arg_error(mrb,"starfield storage size overflow");return mrb_nil_value();}void *storage=bytes?malloc(bytes):NULL;if(bytes&&!storage){run_error(mrb,"starfield allocation failed");return mrb_nil_value();}drbz_starfield next;if(backend->starfield_init(storage,bytes,n,UINT64_C(0x91e10da5c79e7b1d)^(uint64_t)n,&next)){free(storage);run_error(mrb,"starfield initialization failed");return mrb_nil_value();}clear_starfield();star_storage=storage;starfield=next;starfield_ready=1;return mrb_nil_value();}
static mrb_value native_star_draw(mrb_state *mrb,mrb_value self){(void)self;mrb_value ffi_draw,path;api->mrb_get_args(mrb,"oo",&ffi_draw,&path);if(!starfield_ready){run_error(mrb,"call starfield_reset before starfield_draw");return mrb_nil_value();}backend->starfield_update(&starfield);mrb_value w=api->drb_float_value(mrb,4),h=api->drb_float_value(mrb,4);for(size_t i=0;i<starfield.len;++i){mrb_value x=api->drb_float_value(mrb,starfield.x[i]),y=api->drb_float_value(mrb,starfield.y[i]);(void)api->mrb_funcall_id(mrb,ffi_draw,draw_sprite_sym,5,x,y,w,h,path);}return mrb_nil_value();}
static mrb_value native_star_clear(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");clear_starfield();return mrb_nil_value();}
static mrb_value native_conformance(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");char json[4096];size_t n=0;if(drbn_conformance_json(backend,json,sizeof json,&n)){run_error(mrb,"native conformance failed");return mrb_nil_value();}if(!n||json[n-1]!='}')return mrb_nil_value();const char *suffix=",\"dragonruby_host\":{\"draw_sprite_symbol_interned\":true,\"renderer_batch_api\":false,\"ruby_worker_callbacks\":false";
    if(n+strlen(suffix)+32>=sizeof json){run_error(mrb,"conformance output overflow");return mrb_nil_value();}--n;memcpy(json+n,suffix,strlen(suffix));n+=strlen(suffix);
#ifdef DRBZ_SQLITE_QUERY
    const char *sql=",\"sqlite\":true}}";
#else
    const char *sql=",\"sqlite\":false}}";
#endif
    memcpy(json+n,sql,strlen(sql));n+=strlen(sql);return api->mrb_str_new(mrb,json,(mrb_int)n);}

#ifdef DRBZ_SQLITE_QUERY
static unsigned char *copy_cstr(mrb_state *mrb,const char *text,mrb_int length,const char *what){if(length<0||memchr(text,0,(size_t)length)){char msg[128];snprintf(msg,sizeof msg,"%s contains embedded NUL",what);arg_error(mrb,msg);return NULL;}unsigned char *p=sqlite3_malloc64((sqlite3_uint64)length+1);if(!p){run_error(mrb,"SQLite string allocation failed");return NULL;}if(length)memcpy(p,text,(size_t)length);p[length]=0;return p;}
static mrb_value native_sqlite_open(mrb_state *mrb,mrb_value self){(void)self;char *path;mrb_int n;api->mrb_get_args(mrb,"s",&path,&n);if(query_db){run_error(mrb,"Native SQLite database already open");return mrb_nil_value();}unsigned char *p=copy_cstr(mrb,path,n,"database path");if(!p)return mrb_nil_value();sqlite3 *db=NULL;int rc=sqlite3_open((const char *)p,&db);sqlite3_free(p);if(rc!=SQLITE_OK){if(db)sqlite3_close(db);run_error(mrb,"SQLite open failed");return mrb_nil_value();}query_db=db;qops->init(&query_cache,db);query_ready=1;return mrb_nil_value();}
static mrb_value native_sqlite_exec(mrb_state *mrb,mrb_value self){(void)self;if(!query_db){run_error(mrb,"call sqlite_open before sqlite_exec");return mrb_nil_value();}char *sql;mrb_int n;api->mrb_get_args(mrb,"s",&sql,&n);unsigned char *p=copy_cstr(mrb,sql,n,"SQL");if(!p)return mrb_nil_value();int rc=sqlite3_exec(query_db,(const char *)p,NULL,NULL,NULL);sqlite3_free(p);if(rc!=SQLITE_OK){run_error(mrb,"SQLite exec failed");return mrb_nil_value();}return mrb_nil_value();}
static mrb_value native_query_json(mrb_state *mrb,mrb_value self){(void)self;if(!query_db||!query_ready){run_error(mrb,"call sqlite_open before query_json");return mrb_nil_value();}char *sql;mrb_int n;api->mrb_get_args(mrb,"s",&sql,&n);if(n<0){arg_error(mrb,"negative SQL length");return mrb_nil_value();}drbz_query_result result=qops->pack(&query_cache,(const unsigned char *)sql,(size_t)n);if(result.code!=SQLITE_OK){run_error(mrb,"SQLite query failed");return mrb_nil_value();}const unsigned char *packed=NULL;size_t packed_len=0,rows=0;qops->output(&query_cache,&packed,&packed_len,&rows);mrb_value array=api->mrb_ary_new(mrb);size_t pos=0;for(size_t row=0;row<rows;++row){if(packed_len-pos<8){run_error(mrb,"invalid packed query result");return mrb_nil_value();}uint64_t len;memcpy(&len,packed+pos,8);pos+=8;mrb_value value;if(len==UINT64_MAX)value=api->mrb_str_new(mrb,"null",4);else{if(len>(uint64_t)(packed_len-pos)||len>(uint64_t)MRB_INT_MAX){run_error(mrb,"invalid packed row length");return mrb_nil_value();}value=api->mrb_str_new(mrb,(const char *)(packed+pos),(mrb_int)len);pos+=(size_t)len;}api->mrb_ary_push(mrb,array,value);}if(pos!=packed_len){run_error(mrb,"trailing packed query bytes");return mrb_nil_value();}return array;}
static mrb_value native_sqlite_close(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");if(!query_db)return mrb_nil_value();int a=query_ready?qops->clear(&query_cache):SQLITE_OK,b=sqlite3_close(query_db);if(b==SQLITE_OK){query_db=NULL;query_ready=0;}if(a!=SQLITE_OK||b!=SQLITE_OK){run_error(mrb,"SQLite close failed");return mrb_nil_value();}return mrb_nil_value();}
static mrb_value native_query_prepares(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");return mrb_fixnum_value((mrb_int)query_cache.prepares);} static mrb_value native_query_hits(mrb_state *mrb,mrb_value self){(void)self;api->mrb_get_args(mrb,"");return mrb_fixnum_value((mrb_int)query_cache.hits);}
#endif

DRB_FFI_EXPORT void drb_register_c_extensions(mrb_state *mrb,drb_api_t *host){
    drbz_register_legacy_extensions(mrb,host);api=host;backend=drbn_backend_by_name("zig",3);backend->scanner_reset(&scanner);draw_sprite_sym=api->mrb_intern_cstr(mrb,"draw_sprite");
#ifdef DRBZ_SQLITE_QUERY
    qops=&QZ;
#endif
    struct RClass *ffi=api->mrb_module_get(mrb,"FFI");struct RClass *native=api->mrb_define_module_under(mrb,ffi,"Native");
    api->mrb_define_module_function(mrb,native,"backend",native_backend,MRB_ARGS_NONE());api->mrb_define_module_function(mrb,native,"use_backend",native_use_backend,MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb,native,"square",native_square,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"count_newlines",native_count,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"sum",native_sum,MRB_ARGS_ANY());
    api->mrb_define_module_function(mrb,native,"hello",native_hello,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"goodbye",native_goodbye,MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb,native,"reset_scanner",native_scanner_reset,MRB_ARGS_NONE());api->mrb_define_module_function(mrb,native,"update_scanner_texture",native_scanner_frame,MRB_ARGS_NONE());
    api->mrb_define_module_function(mrb,native,"starfield_reset",native_star_reset,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"starfield_draw",native_star_draw,MRB_ARGS_REQ(2));api->mrb_define_module_function(mrb,native,"starfield_clear",native_star_clear,MRB_ARGS_NONE());api->mrb_define_module_function(mrb,native,"conformance",native_conformance,MRB_ARGS_NONE());
#ifdef DRBZ_SQLITE_QUERY
    api->mrb_define_module_function(mrb,native,"sqlite_open",native_sqlite_open,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"sqlite_exec",native_sqlite_exec,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"query_json",native_query_json,MRB_ARGS_REQ(1));api->mrb_define_module_function(mrb,native,"sqlite_close",native_sqlite_close,MRB_ARGS_NONE());api->mrb_define_module_function(mrb,native,"query_prepares",native_query_prepares,MRB_ARGS_NONE());api->mrb_define_module_function(mrb,native,"query_hits",native_query_hits,MRB_ARGS_NONE());
#endif
}
