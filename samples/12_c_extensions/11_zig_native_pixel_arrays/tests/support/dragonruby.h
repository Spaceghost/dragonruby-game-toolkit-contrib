/* Test-only API table. This is NOT the DragonRuby SDK or its binary layout.
   Real mruby supplies every Ruby type, value, parser, and exception operation.
   Production builds never add this directory to their include paths. */
#ifndef DRB_ZIG_TEST_HOST
#error "This header may only be used by the explicit mruby integration test."
#endif
#ifndef DRB_ZIG_TEST_API_H
#define DRB_ZIG_TEST_API_H
/* C11 preprocessing paths need not provide the C attribute feature query. */
#ifndef __has_c_attribute
#define __has_c_attribute(attribute) 0
#endif
#include <mruby.h>
#include <stdint.h>
#define DRB_FFI_EXPORT __attribute__((visibility("default")))
/* mrb_int is also a function-like conversion macro. A named function type
   avoids putting '(' immediately after the return type in a pointer field. */
typedef mrb_int drb_test_get_args_fn(mrb_state *, const char *, ...);
typedef struct drb_api_t {
    struct RClass *(*mrb_module_get)(mrb_state *, const char *);
    struct RClass *(*mrb_define_module_under)(mrb_state *, struct RClass *, const char *);
    void (*mrb_define_module_function)(mrb_state *, struct RClass *, const char *, mrb_func_t, mrb_aspec);
    drb_test_get_args_fn *mrb_get_args;
    void (*drb_upload_pixel_array)(const char *, int, int, const uint32_t *);
} drb_api_t;
#endif
