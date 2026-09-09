#ifndef DRBZ_SUITE_TEST_HOST
#error "Test-only API table: never include this directory in a production SDK build."
#endif
#ifndef DRBZ_SUITE_TEST_API_H
#define DRBZ_SUITE_TEST_API_H
#ifndef __has_c_attribute
#define __has_c_attribute(attribute) 0
#endif
#include <mruby.h>
#include <stdint.h>
#define DRB_FFI_EXPORT __attribute__((visibility("default")))
typedef mrb_int drbz_get_args_fn(mrb_state *, const char *, ...);
/* This is a host contract, NOT the proprietary DragonRuby SDK ABI. */
typedef struct drb_api_t {
    struct RClass *(*mrb_module_get)(mrb_state *, const char *);
    struct RClass *(*mrb_define_module_under)(mrb_state *, struct RClass *, const char *);
    void (*mrb_define_module_function)(mrb_state *, struct RClass *, const char *, mrb_func_t, mrb_aspec);
    drbz_get_args_fn *mrb_get_args;
    void (*drb_upload_pixel_array)(const char *, int, int, const uint32_t *);
    void (*mrb_raise)(mrb_state *, struct RClass *, const char *);
    struct RClass *(*mrb_class_get)(mrb_state *, const char *);
    mrb_value (*drb_float_value)(mrb_state *, double);
    mrb_value (*mrb_str_new)(mrb_state *, const char *, mrb_int);
} drb_api_t;
#endif
