DR.ffi_misc.gtk_dlopen("ext")
include FFI::CExt

def tick args
  args.state.rotation ||= 0
  update_scanner_texture

  w = 100
  h = 100
  x = (1280 - w) / 2
  y = (720 - h) / 2
  args.outputs.background_color = [64, 0, 128]
  args.outputs.primitives << [x, y, w, h, :scanner, args.state.rotation].sprite
  args.state.rotation += 1
  args.outputs.primitives << DR.current_framerate_primitives
end
