#ifndef DRBZ_SUITE_TEST_HOST
#error "Test-only API table: never include this directory in a production SDK build."
#endif
#ifndef DRBZ_SUITE_TEST_API_H
#define DRBZ_SUITE_TEST_API_H
#ifndef __has_c_attribute
#define __has_c_attribute(attribute) 0
#endif
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/string.h>
#include <stdint.h>
#define DRB_FFI_EXPORT __attribute__((visibility("default")))
typedef mrb_int drbz_get_args_fn(mrb_state *, const char *, ...);
typedef __typeof__(mrb_str_new) drbz_str_new_fn;
typedef __typeof__(mrb_ary_new) drbz_ary_new_fn;
typedef __typeof__(mrb_ary_push) drbz_ary_push_fn;
typedef __typeof__(mrb_funcall_id) drbz_funcall_id_fn;
/* This is a host contract, NOT the proprietary DragonRuby SDK ABI. Fields are
 * limited to APIs already exercised by shipped C-extension samples. */
typedef struct drb_api_t {
    struct RClass *(*mrb_module_get)(mrb_state *, const char *);
    struct RClass *(*mrb_define_module_under)(mrb_state *, struct RClass *, const char *);
    void (*mrb_define_module_function)(mrb_state *, struct RClass *, const char *, mrb_func_t, mrb_aspec);
    drbz_get_args_fn *mrb_get_args;
    void (*drb_upload_pixel_array)(const char *, int, int, const uint32_t *);
    void (*mrb_raise)(mrb_state *, struct RClass *, const char *);
    struct RClass *(*mrb_class_get)(mrb_state *, const char *);
    mrb_value (*drb_float_value)(mrb_state *, double);
    drbz_str_new_fn *mrb_str_new;
    drbz_ary_new_fn *mrb_ary_new;
    drbz_ary_push_fn *mrb_ary_push;
    mrb_sym (*mrb_intern_cstr)(mrb_state *, const char *);
    drbz_funcall_id_fn *mrb_funcall_id;
} drb_api_t;
#endif
