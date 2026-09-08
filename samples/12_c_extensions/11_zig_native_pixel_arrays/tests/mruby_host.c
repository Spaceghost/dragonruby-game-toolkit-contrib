/* Real mruby + the real bridge.c + the real Zig kernels, dynamically loaded.
   Only the DragonRuby host table/upload sink is replaced by a test contract. */
#include <dragonruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static uint32_t captured[100];
static unsigned uploads;
static void upload(const char *name, int width, int height, const uint32_t *pixels) {
    assert(strcmp(name, "scanner") == 0);
    assert(width == 10 && height == 10 && pixels != NULL);
    memcpy(captured, pixels, sizeof captured);
    ++uploads;
}

static void evaluate(mrb_state *mrb, const char *source) {
    mrb_load_string(mrb, source);
    if (mrb->exc) {
        mrb_print_error(mrb);
        assert(!"Ruby integration assertion failed");
    }
}

static void check_frame(mrb_state *mrb, int row) {
    unsigned before = uploads;
    evaluate(mrb, "raise 'scanner result' unless FFI::CExt.update_scanner_texture.nil?");
    assert(uploads == before + 1);
    for (int i = 0; i < 100; ++i)
        assert(captured[i] == (i / 10 == row ? 0xff00ff00u : 0xff000000u));
}

int main(int argc, char **argv) {
    assert(argc == 3);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef void (*register_fn)(mrb_state *, drb_api_t *);
    register_fn register_extension = (register_fn)dlsym(lib, "drb_register_c_extensions_with_api");
    assert(register_extension);
    drb_api_t api = {mrb_module_get, mrb_define_module_under, mrb_define_module_function,
                     mrb_get_args, upload};

    /* Repeat with a fresh VM to expose stale Ruby pointers or registration state. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        mrb_state *mrb = mrb_open();
        assert(mrb);
        mrb_define_module(mrb, "FFI");
        register_extension(mrb, &api);
        FILE *script = fopen(argv[2], "rb");
        assert(script);
        mrb_load_file(mrb, script);
        fclose(script);
        if (mrb->exc) { mrb_print_error(mrb); return 1; }

        evaluate(mrb, "FFI::CExt.reset_scanner");
        int row = 0, increment = 1;
        for (int frame = 0; frame < 10000; ++frame) {
            int arena = mrb_gc_arena_save(mrb);
            check_frame(mrb, row);
            row += increment;
            if (increment > 0 && row >= 10) { row = 9; increment = -1; }
            else if (increment < 0 && row < 0) { row = 1; increment = 1; }
            mrb_gc_arena_restore(mrb, arena);
        }
        evaluate(mrb, "FFI::CExt.reset_scanner");
        check_frame(mrb, 0);
        unsigned before = uploads;
        evaluate(mrb, "begin; FFI::CExt.update_scanner_texture(123); rescue ArgumentError; end");
        evaluate(mrb, "begin; FFI::CExt.reset_scanner(123); rescue ArgumentError; end");
        assert(uploads == before);
        check_frame(mrb, 1); /* Rejected arguments must not advance OR reset. */
        register_extension(mrb, &api);
        check_frame(mrb, 0);
        mrb_full_gc(mrb);
        evaluate(mrb, "raise 'post-GC result' unless FFI::CExt.count_newlines(\"a\\nb\\n\") == 2");
        mrb_close(mrb); /* No VM retains callbacks when the library is unloaded. */
    }
    assert(dlclose(lib) == 0);
    puts("mruby dynamic bridge integration passed (not DragonRuby SDK validation).");
    return 0;
}
