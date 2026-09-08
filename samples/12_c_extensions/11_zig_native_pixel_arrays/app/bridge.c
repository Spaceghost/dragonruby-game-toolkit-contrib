#include <dragonruby.h>
#include "native.h"

static drb_api_t *drb_api;

static mrb_value update_scanner_texture(mrb_state *state, mrb_value self) {
    (void)state;
    (void)self;
    uint32_t pixels[DRB_ZIG_PIXEL_COUNT];
    drb_zig_scanner_frame(pixels);
    drb_api->drb_upload_pixel_array("scanner", DRB_ZIG_DIMENSION,
                                    DRB_ZIG_DIMENSION, pixels);
    return mrb_nil_value();
}

static mrb_value count_newlines(mrb_state *state, mrb_value self) {
    (void)self;
    char *data = NULL;
    mrb_int len = 0;
    /* "s" validates the Ruby argument and supplies a byte length, not strlen.
       Ruby exceptions stay on the C side, outside all Zig stack frames. */
    drb_api->mrb_get_args(state, "s", &data, &len);
    size_t count = drb_zig_count_newlines((const unsigned char *)data, (size_t)len);
    /* count <= len, which is already representable as an mrb_int. */
    return mrb_fixnum_value((mrb_int)count);
}

DRB_FFI_EXPORT
void drb_register_c_extensions_with_api(mrb_state *state, struct drb_api_t *api) {
    drb_api = api;
    drb_zig_scanner_reset();
    struct RClass *ffi = drb_api->mrb_module_get(state, "FFI");
    struct RClass *module = drb_api->mrb_define_module_under(state, ffi, "CExt");
    drb_api->mrb_define_module_function(state, module, "update_scanner_texture",
                                       update_scanner_texture, MRB_ARGS_REQ(0));
    drb_api->mrb_define_module_function(state, module, "count_newlines",
                                       count_newlines, MRB_ARGS_REQ(1));
}
