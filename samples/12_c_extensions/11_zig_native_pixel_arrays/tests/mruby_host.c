/* Real mruby and two real shared libraries: untouched C sample versus Zig.
   Only the DragonRuby host table/upload sink is a test contract, not the SDK. */
#include <dragonruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static uint32_t captured[100];
static unsigned uploads, compared_frames, compared_pixels, checked_frames, vm_lifetimes;
static unsigned observed_rows[22];
static uint32_t first_frame[100];

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

static void call_scanner(mrb_state *mrb, const char *source) {
    unsigned before = uploads;
    evaluate(mrb, source);
    assert(uploads == before + 1);
}

static void check_frame(mrb_state *mrb, int row) {
    call_scanner(mrb, "raise 'scanner result' unless FFI::CExt.update_scanner_texture.nil?");
    for (int i = 0; i < 100; ++i)
        assert(captured[i] == (i / 10 == row ? 0xff00ff00u : 0xff000000u));
    ++checked_frames;
}

/* Both methods are called by Ruby. No copied C algorithm supplies this oracle. */
static int compare_original(mrb_state *mrb, int inject_mismatch) {
    uint32_t reference[100];
    evaluate(mrb, "FFI::CExt.reset_scanner");
    for (unsigned frame = 0; frame < 10000; ++frame) {
        int arena = mrb_gc_arena_save(mrb);
        call_scanner(mrb, "raise 'C result' unless FFI::CExt.original_c_scanner.nil?");
        memcpy(reference, captured, sizeof reference);
        call_scanner(mrb, "raise 'Zig result' unless FFI::CExt.update_scanner_texture.nil?");
        /* Fault injection is in the test sink, never the production library. */
        if (inject_mismatch && frame == 17) captured[7] ^= 1;
        for (unsigned pixel = 0; pixel < 100; ++pixel) {
            if (captured[pixel] != reference[pixel]) {
                fprintf(stderr, "PIXEL_MISMATCH frame=%u pixel=%u C=%08x Zig=%08x\n",
                        frame, pixel, (unsigned)reference[pixel], (unsigned)captured[pixel]);
                mrb_gc_arena_restore(mrb, arena);
                /* Only the specific injected error qualifies as the negative control. */
                return inject_mismatch && frame == 17 && pixel == 7 ? 42 : 1;
            }
            ++compared_pixels;
        }
        ++compared_frames;
        if (frame == 0) memcpy(first_frame, captured, sizeof first_frame);
        if (frame < 22) {
            unsigned pixel = 0;
            while (pixel < 100 && captured[pixel] != 0xff00ff00u) ++pixel;
            assert(pixel < 100);
            observed_rows[frame] = pixel / 10;
        }
        mrb_gc_arena_restore(mrb, arena);
    }
    return 0;
}

int main(int argc, char **argv) {
    int inject_mismatch = argc > 1 && strcmp(argv[argc - 1], "--inject-mismatch") == 0;
    if (inject_mismatch) --argc;
#ifdef DRB_ZIG_PUBLISHED_MRUBY
    assert(argc == 5); /* Zig library, original C library, smoke, patch probe. */
#else
    assert(argc == 4);
#endif
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    void *original = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!original) { fprintf(stderr, "%s\n", dlerror()); dlclose(lib); return 1; }
    typedef void (*register_fn)(mrb_state *, drb_api_t *);
    register_fn register_extension = (register_fn)dlsym(lib, "drb_register_c_extensions_with_api");
    register_fn register_original = (register_fn)dlsym(original, "drb_register_c_extensions_with_api");
    assert(register_extension && register_original && register_extension != register_original);
    drb_api_t api = {mrb_module_get, mrb_define_module_under, mrb_define_module_function,
                     mrb_get_args, upload};

    for (int cycle = 0; cycle < 3; ++cycle) {
        mrb_state *mrb = mrb_open();
        assert(mrb);
#ifdef DRB_ZIG_PUBLISHED_MRUBY
        assert(mrb->sym_default == mrb_intern_lit(mrb, "default"));
        assert(mrb->sym_initialize == mrb_intern_lit(mrb, "initialize"));
#endif
        mrb_define_module(mrb, "FFI");
        if (cycle == 0) {
            register_original(mrb, &api);
            evaluate(mrb, "class << FFI::CExt; alias original_c_scanner update_scanner_texture; end");
        }
        register_extension(mrb, &api);
        for (int file = 3; file < argc; ++file) {
            FILE *script = fopen(argv[file], "rb");
            assert(script);
            mrb_load_file(mrb, script);
            fclose(script);
            if (mrb->exc) { mrb_print_error(mrb); mrb_close(mrb); dlclose(original); dlclose(lib); return 1; }
        }
        if (cycle == 0) {
            int result = compare_original(mrb, inject_mismatch);
            if (result || inject_mismatch) {
                mrb_close(mrb);
                assert(dlclose(original) == 0);
                assert(dlclose(lib) == 0);
                return result ? result : 1; /* A missing injected failure must fail. */
            }
            evaluate(mrb, "puts 'Ruby call: count_newlines(\"one\\ntwo\\n\") = ' + FFI::CExt.count_newlines(\"one\\ntwo\\n\").to_s");
            evaluate(mrb, "puts 'Ruby call: binary LF count = ' + FFI::CExt.count_newlines(\"\\0\\n\\0\\n\").to_s");
        }

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
        check_frame(mrb, 1);
        register_extension(mrb, &api);
        check_frame(mrb, 0);
        mrb_full_gc(mrb);
        evaluate(mrb, "raise 'post-GC result' unless FFI::CExt.count_newlines(\"a\\nb\\n\") == 2");
        mrb_close(mrb);
        ++vm_lifetimes;
    }
    assert(dlclose(original) == 0);
    assert(dlclose(lib) == 0);
    printf("Original C versus Zig: %u frames, %u pixels matched.\n", compared_frames, compared_pixels);
    printf("Observed scanner rows:");
    for (unsigned i = 0; i < 22; ++i) printf(" %u", observed_rows[i]);
    puts("\nFirst uploaded frame (# = green, . = black; NOT an engine screenshot):");
    for (unsigned i = 0; i < 100; ++i) {
        putchar(first_frame[i] == 0xff00ff00u ? '#' : '.');
        if (i % 10 == 9) putchar('\n');
    }
    printf("PROOF {\"compared_frames\":%u,\"compared_pixels\":%u,\"checked_zig_frames\":%u,"
           "\"vm_lifetimes\":%u,\"dragonruby_engine_validated\":false}\n",
           compared_frames, compared_pixels, checked_frames, vm_lifetimes);
    puts("mruby dynamic bridge integration passed (not DragonRuby SDK validation).");
    return 0;
}
