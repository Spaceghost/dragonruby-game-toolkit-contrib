/* SDK-specific Ruby adapter. All raising/allocation stays on the C side of
 * the boundary, after native kernels have returned. The test host supplies
 * real mruby but not the proprietary DragonRuby API table or renderer. */
#include <dragonruby.h>
#include <mruby/array.h>
#include "native.h"
#include "apps.h"
#include <limits.h>
#include <stdint.h>

static drb_api_t *api;
static drbz_scanner scanner;

static void argument_error(mrb_state *mrb, const char *message) {
    api->mrb_raise(mrb, api->mrb_class_get(mrb, "ArgumentError"), message);
}
static mrb_value square_value(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int input;
    api->mrb_get_args(mrb, "i", &input);
    int output = 0;
    if (input < INT_MIN || input > INT_MAX || drbz_square((int)input, &output) != 0) {
        argument_error(mrb, "square is outside the supported signed 32-bit range");
        return mrb_nil_value();
    }
    return mrb_fixnum_value(output);
}
static mrb_value newlines(mrb_state *mrb, mrb_value self) {
    (void)self;
    char *text;
    mrb_int length;
    api->mrb_get_args(mrb, "s", &text, &length);
    return mrb_fixnum_value((mrb_int)drbz_count_blocked((const unsigned char *)text, (size_t)length));
}
static mrb_value regex_index(mrb_state *mrb, mrb_value self) {
    (void)self;
    char *pattern, *text;
    mrb_int pattern_length, text_length;
    api->mrb_get_args(mrb, "ss", &pattern, &pattern_length, &text, &text_length);
    drbz_regex compiled;
    if (drbz_regex_compile(&compiled, (const unsigned char *)pattern, (size_t)pattern_length) != 0) {
        argument_error(mrb, "invalid or oversized tiny-regex pattern");
        return mrb_nil_value();
    }
    int matched_length = 0;
    int index = drbz_regex_search(&compiled, (const unsigned char *)text, (size_t)text_length, &matched_length, 1, 10000000);
    if (index == -2) {
        argument_error(mrb, "regex work or input-size limit exceeded");
        return mrb_nil_value();
    }
    return mrb_fixnum_value(index);
}
static void read_value(void *context, const void *source, size_t index, drbz_view *view) {
    (void)context;
    mrb_value value = ((const mrb_value *)source)[index];
    *view = (drbz_view){0, 0, NULL, 0};
    if (mrb_fixnum_p(value)) {
        view->kind = 1;
        view->number = (double)mrb_fixnum(value);
    } else if (mrb_float_p(value)) {
        view->kind = 1;
        view->number = (double)mrb_float(value);
    } else if (mrb_array_p(value)) {
        view->kind = 2;
        view->children = RARRAY_PTR(value);
        view->length = (size_t)RARRAY_LEN(value);
    }
}
static mrb_value sum_values(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_value *values;
    mrb_int length;
    api->mrb_get_args(mrb, "*", &values, &length);
    double result = 0;
    int status = drbz_sum_tree(values, (size_t)length, read_value, NULL, &result);
    if (status != 0) {
        argument_error(mrb, status == 1 ? "unsupported value in nested sum" : "cyclic or excessively deep array");
        return mrb_nil_value();
    }
    return api->drb_float_value(mrb, result);
}
static mrb_value greeting(mrb_state *mrb, int goodbye) {
    char *name;
    mrb_int length;
    api->mrb_get_args(mrb, "s", &name, &length);
    unsigned char text[512];
    size_t written = 0;
    if (drbz_greeting(goodbye, (const unsigned char *)name, (size_t)length, text, sizeof text, &written) != 0) {
        argument_error(mrb, "greeting exceeds the adapter's 511-byte limit");
        return mrb_nil_value();
    }
    return api->mrb_str_new(mrb, (const char *)text, (mrb_int)written);
}
static mrb_value hello(mrb_state *mrb, mrb_value self) { (void)self; return greeting(mrb, 0); }
static mrb_value goodbye(mrb_state *mrb, mrb_value self) { (void)self; return greeting(mrb, 1); }
static mrb_value reset(mrb_state *mrb, mrb_value self) {
    (void)self;
    api->mrb_get_args(mrb, "");
    drbz_scanner_reset(&scanner);
    return mrb_nil_value();
}
static mrb_value frame(mrb_state *mrb, mrb_value self) {
    (void)self;
    api->mrb_get_args(mrb, "");
    const uint32_t *pixels = drbz_scanner_frame(&scanner);
    api->drb_upload_pixel_array("zig_suite_scanner", 10, 10, pixels);
    return mrb_nil_value();
}
DRB_FFI_EXPORT void drb_register_c_extensions(mrb_state *mrb, drb_api_t *host) {
    api = host;
    drbz_scanner_reset(&scanner);
    struct RClass *ffi = api->mrb_module_get(mrb, "FFI");
    struct RClass *zig = api->mrb_define_module_under(mrb, ffi, "Zig");
    api->mrb_define_module_function(mrb, zig, "square", square_value, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "count_newlines", newlines, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "regex_index", regex_index, MRB_ARGS_REQ(2));
    api->mrb_define_module_function(mrb, zig, "sum", sum_values, MRB_ARGS_ANY());
    api->mrb_define_module_function(mrb, zig, "hello", hello, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "goodbye", goodbye, MRB_ARGS_REQ(1));
    api->mrb_define_module_function(mrb, zig, "reset_scanner", reset, MRB_ARGS_NONE());
    api->mrb_define_module_function(mrb, zig, "update_scanner_texture", frame, MRB_ARGS_NONE());
}
