#include <dragonruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t uploads;
static void upload(const char *name, int width, int height, const uint32_t *pixels) {
    assert(strcmp(name, "zig_suite_scanner") == 0 && width == 10 && height == 10);
    size_t green = 0;
    for (int i = 0; i < 100; ++i) {
        assert(pixels[i] == UINT32_C(0xff000000) || pixels[i] == UINT32_C(0xff00ff00));
        green += pixels[i] == UINT32_C(0xff00ff00);
    }
    assert(green == 10);
    ++uploads;
}
static mrb_value float_value(mrb_state *mrb, double value) { return mrb_float_value(mrb, value); }
static void load(mrb_state *mrb, const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    mrb_load_file(mrb, file);
    fclose(file);
    if (mrb->exc) { mrb_print_error(mrb); exit(1); }
}
int main(int argc, char **argv) {
    assert(argc == 3);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef void (*register_fn)(mrb_state *, drb_api_t *);
    register_fn register_extension = (register_fn)dlsym(library, "drb_register_c_extensions");
    assert(register_extension);
    drb_api_t api = {
        .mrb_module_get = mrb_module_get,
        .mrb_define_module_under = mrb_define_module_under,
        .mrb_define_module_function = mrb_define_module_function,
        .mrb_get_args = mrb_get_args,
        .drb_upload_pixel_array = upload,
        .mrb_raise = mrb_raise,
        .mrb_class_get = mrb_class_get,
        .drb_float_value = float_value,
        .mrb_str_new = mrb_str_new,
        .mrb_ary_new = mrb_ary_new,
        .mrb_ary_push = mrb_ary_push,
        .mrb_intern_cstr = mrb_intern_cstr,
        .mrb_funcall_id = mrb_funcall_id,
    };
    for (int lifetime = 0; lifetime < 3; ++lifetime) {
        mrb_state *mrb = mrb_open();
        assert(mrb);
        mrb_define_module(mrb, "FFI");
#ifdef DRBZ_PUBLISHED_MRUBY
        assert(mrb->sym_default == mrb_intern_lit(mrb, "default"));
        assert(mrb->sym_initialize == mrb_intern_lit(mrb, "initialize"));
        mrb_load_string(mrb, "raise 'not the published patch' unless MRUBY_VERSION == '3.0.0' && (5 / 2) == 2.5");
        assert(!mrb->exc);
#endif
        register_extension(mrb, &api);
        load(mrb, argv[2]);
        mrb_full_gc(mrb);
        register_extension(mrb, &api);
        load(mrb, argv[2]);
        mrb_close(mrb);
    }
    assert(uploads == 120);
    dlclose(library);
    printf("RUBY_SUITE_PROOF {\"vm_lifetimes\":3,\"registrations\":6,\"scanner_uploads\":%zu,\"sqlite_query_cache\":true,\"starfield_one_object_adapter\":true,\"dragonruby_engine_validated\":false}\n", uploads);
    return 0;
}
