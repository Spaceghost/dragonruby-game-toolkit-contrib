/* Test-only API table. This is NOT the DragonRuby SDK or its binary layout.
   Real mruby supplies every Ruby type, value, parser, and exception operation.
   Production builds never add this directory to their include paths. */
#ifndef DRB_ZIG_TEST_HOST
#error "This header may only be used by the explicit mruby integration test."
#endif
#ifndef DRB_ZIG_TEST_API_H
#define DRB_ZIG_TEST_API_H
#include <mruby.h>
#include <stdint.h>
#define DRB_FFI_EXPORT __attribute__((visibility("default")))
typedef struct drb_api_t {
    struct RClass *(*mrb_module_get)(mrb_state *, const char *);
    struct RClass *(*mrb_define_module_under)(mrb_state *, struct RClass *, const char *);
    void (*mrb_define_module_function)(mrb_state *, struct RClass *, const char *, mrb_func_t, mrb_aspec);
    mrb_int (*mrb_get_args)(mrb_state *, const char *, ...);
    void (*drb_upload_pixel_array)(const char *, int, int, const uint32_t *);
} drb_api_t;
#endif
