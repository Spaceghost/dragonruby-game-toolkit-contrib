#include "bench.h"
#include <dragonruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/version.h>

void drb_register_c_extensions(mrb_state *, drb_api_t *);
static mrb_state *vm;
static mrb_value receiver;
static uint64_t uploads, pixels_checksum;
#ifdef DRBZ_PROFILE
/* Header size is a multiple of max_align_t. Payload bytes, not allocator usable
 * bytes or Ruby object counts, are tracked. Timing uses mrb_open() unchanged. */
typedef union { max_align_t alignment; size_t size; } allocation_header;
static meter_stats statistics;
static uint64_t live;
static int active, fail_next;
static void *tracked_alloc(mrb_state *mrb, void *pointer, size_t n, void *ud) {
    (void)mrb; (void)ud;
    allocation_header *old = pointer ? (allocation_header *)pointer - 1 : NULL;
    size_t old_size = old ? old->size : 0;
    if (!n) {
        if (old) { assert(live >= old_size); live -= old_size; if (active) ++statistics.free_calls; free(old); }
        return NULL;
    }
    if (active) {
        if (old) ++statistics.realloc_calls; else ++statistics.malloc_calls;
        assert(UINT64_MAX - statistics.requested_bytes >= n); statistics.requested_bytes += n;
    }
    allocation_header *next = NULL;
    if (active && fail_next) fail_next = 0;
    else if (n <= SIZE_MAX - sizeof(*next)) next = realloc(old, sizeof(*next) + n);
    if (!next) { if (active) ++statistics.failed_calls; return NULL; }
    next->size = n; assert(live >= old_size); live = live - old_size + n;
    if (active && live > statistics.peak_live) statistics.peak_live = live;
    return next + 1;
}
void meter_begin(void) {
    assert(!active); memset(&statistics, 0, sizeof statistics);
    statistics.live_before = statistics.peak_live = live; statistics.live_supported = 1; active = 1;
}
meter_stats meter_end(void) { assert(active); active = 0; statistics.live_after = live; return statistics; }
void meter_inject(void) { void *p = tracked_alloc(NULL, NULL, 17, NULL); assert(p); tracked_alloc(NULL, p, 0, NULL); }
void meter_selftest(void) {
    meter_begin();
    unsigned char *p = tracked_alloc(NULL, NULL, 16, NULL); assert(p); memset(p, 7, 16);
    p = tracked_alloc(NULL, p, 32, NULL); assert(p && p[15] == 7);
    fail_next = 1; void *failed = tracked_alloc(NULL, p, 128, NULL); assert(!failed && p[15] == 7);
    tracked_alloc(NULL, p, 0, NULL);
    meter_stats s = meter_end();
    assert(s.malloc_calls == 1 && s.realloc_calls == 2 && s.failed_calls == 1 && s.free_calls == 1);
    assert(s.requested_bytes == 176 && s.live_after == s.live_before && s.peak_live == s.live_before + 32);
}
#endif
static void check_vm(void) { if (vm->exc) { mrb_print_error(vm); exit(1); } }
static void upload(const char *name, int width, int height, const uint32_t *pixels) {
    assert(strcmp(name, "zig_suite_scanner") == 0 && width == 10 && height == 10);
    size_t green = 0;
    for (size_t i = 0; i < 100; ++i) {
        assert(pixels[i] == UINT32_C(0xff000000) || pixels[i] == UINT32_C(0xff00ff00));
        green += pixels[i] == UINT32_C(0xff00ff00);
        pixels_checksum = (pixels_checksum ^ pixels[i]) * UINT64_C(1099511628211);
    }
    assert(green == 10); ++uploads;
}
static mrb_value make_float(mrb_state *mrb, double n) { return mrb_float_value(mrb, n); }
static const char *methods[][4] = {
    {"lf_ruby", "lf_zig", NULL, NULL},
    {"sum_ruby", "sum_c_direct", "sum_zig_single", "sum_zig_batch"},
    {"hello_ruby", "hello_zig", NULL, NULL},
    {"literal_ruby", "literal_zig", NULL, NULL},
    {"scanner", NULL, NULL, NULL},
    {"envelope", NULL, NULL, NULL},
    {"query_1", NULL, NULL, NULL},
    {"query_64", NULL, NULL, NULL},
    {"query_1024", NULL, NULL, NULL},
    {"sum_nested_ruby", "sum_nested_c_direct", "sum_nested_zig_single", "sum_nested_zig_batch"},
};
static const uint64_t expected_per_call[] = {8, 2080, 255, 4090, 1, 1, 17, 80, 1040, 2080};
static void reset(const bench_case *c, uint32_t seed) {
    (void)seed;
    mrb_full_gc(vm); uploads = pixels_checksum = 0;
    mrb_load_string(vm, "FFI::Zig.reset_scanner"); check_vm();
    mrb_gc_arena_restore(vm, 0);
    mrb_full_gc(vm);
    if (c->task >= 6 && c->task <= 8) {
        int arena = mrb_gc_arena_save(vm);
        mrb_value warm = mrb_funcall(vm, receiver, methods[c->task][0], 1, mrb_fixnum_value(1));
        check_vm(); assert(mrb_fixnum_p(warm));
        assert((uint64_t)mrb_fixnum(warm) == expected_per_call[c->task]);
        mrb_gc_arena_restore(vm, arena); mrb_full_gc(vm);
    }
}
static uint64_t batch(const bench_case *c, unsigned variant, size_t n) {
    int arena = mrb_gc_arena_save(vm);
    mrb_value result = mrb_funcall(vm, receiver, methods[c->task][variant], 1, mrb_fixnum_value((mrb_int)n));
    check_vm(); assert(mrb_fixnum_p(result));
    uint64_t checksum = (uint64_t)mrb_fixnum(result);
    assert(checksum == ((n * expected_per_call[c->task]) & UINT64_C(0x3fffffff)));
    mrb_gc_arena_restore(vm, arena);
    return checksum;
}
static uint64_t finish(const bench_case *c, unsigned variant, uint64_t checksum) {
    (void)variant;
    if (c->task == 4) { assert(uploads == checksum); checksum ^= pixels_checksum; }
    return checksum;
}
int main(int argc, char **argv) {
    if (argc != 5) { fputs("usage: ruby-bench SCRIPT TRIALS SEED MIN_NS\n", stderr); return 2; }
#ifdef DRBZ_PROFILE
    meter_begin(); vm = mrb_open_allocf(tracked_alloc, NULL);
#else
    vm = mrb_open();
#endif
    assert(vm); mrb_define_module(vm, "FFI");
    drb_api_t api = {
        .mrb_module_get = mrb_module_get, .mrb_define_module_under = mrb_define_module_under,
        .mrb_define_module_function = mrb_define_module_function, .mrb_get_args = mrb_get_args,
        .drb_upload_pixel_array = upload, .mrb_raise = mrb_raise, .mrb_class_get = mrb_class_get,
        .drb_float_value = make_float, .mrb_str_new = mrb_str_new,
        .mrb_ary_new = mrb_ary_new, .mrb_ary_push = mrb_ary_push,
        .mrb_intern_cstr = mrb_intern_cstr, .mrb_funcall_id = mrb_funcall_id,
    };
    drb_register_c_extensions(vm, &api);
    FILE *script = fopen(argv[1], "rb"); assert(script); mrb_load_file(vm, script); fclose(script); check_vm();
    receiver = mrb_obj_value(mrb_module_get(vm, "Measure"));
    mrb_gc_arena_restore(vm, 0); mrb_full_gc(vm);
#ifdef DRBZ_PROFILE
    bench_alloc_record("mruby-allocf", "ruby/vm", "real_mruby", "open-register-parse-gc", 1, 0, meter_end());
#endif
    printf("{\"event\":\"ruby_environment\",\"version\":\"%s\",\"boxing\":\"%s\",\"gc\":\"enabled\",\"renderer\":false,\"adapter_linkage\":\"static-test-host\",\"sqlite_query_cache\":true,\"live_units\":\"requested-payload-bytes\"}\n", MRUBY_VERSION, DRBZ_BOXING_NAME);
    const bench_case cases[] = {
        {"ruby/lf/128", {"ruby_byte_loop","ffi_zig"}, 2,128,0,0,reset,batch,finish},
        {"ruby/sum/64", {"ruby_loop","ffi_c_direct","ffi_zig_single_reader","ffi_zig_batch_reader"}, 4,64,1,0,reset,batch,finish},
        {"ruby/hello/128", {"ruby_concat","ffi_zig"}, 2,128,2,0,reset,batch,finish},
        {"ruby/literal/4096", {"ruby_string_index","ffi_tinyregex_compile_each"}, 2,4096,3,0,reset,batch,finish},
        {"ruby/scanner-test-sink", {"ffi_zig"}, 1,100,4,0,reset,batch,finish},
        {"ruby/loop-envelope", {"ruby_loop"}, 1,0,5,0,reset,batch,finish},
        {"ruby/query-json-1", {"ffi_zig_cached"}, 1,1,6,0,reset,batch,finish},
        {"ruby/query-json-64", {"ffi_zig_cached"}, 1,64,7,0,reset,batch,finish},
        {"ruby/query-json-1024", {"ffi_zig_cached"}, 1,1024,8,0,reset,batch,finish},
        {"ruby/sum-nested/64", {"ruby_loop","ffi_c_direct","ffi_zig_single_reader","ffi_zig_batch_reader"}, 4,64,9,0,reset,batch,finish},
    };
    bench_run(cases, sizeof cases / sizeof *cases, "mruby-allocf", 0, argc - 1, argv + 1);
#ifdef DRBZ_PROFILE
    meter_begin();
#endif
    mrb_full_gc(vm);
#ifdef DRBZ_PROFILE
    bench_alloc_record("mruby-allocf", "ruby/vm", "real_mruby", "final-gc", 1, 0, meter_end());
    meter_begin();
#endif
#ifdef DRBZ_SQLITE_QUERY
    mrb_load_string(vm, "FFI::Zig.sqlite_close"); check_vm();
#endif
    mrb_close(vm);
#ifdef DRBZ_PROFILE
    meter_stats final = meter_end(); assert(final.live_after == 0);
    bench_alloc_record("mruby-allocf", "ruby/vm", "real_mruby", "close", 1, 0, final);
#endif
    return 0;
}
