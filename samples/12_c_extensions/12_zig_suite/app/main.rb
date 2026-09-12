# Build bridge/ against the matching SDK, then put its library in the game's
# native/<platform>/ directory before running this game. Public CI tests Ruby
# calls with real mruby, but does not validate the proprietary engine renderer.
$gtk.ffi_misc.gtk_dlopen 'zig_suite'

class ZigNativeStarfield
  def initialize(path)
    @path = path
  end

  # DragonRuby invokes one draw_override for the whole field. Native code updates
  # all coordinates first, then uses the same supported draw_sprite boundary as
  # the original sample without N Ruby Star data objects or N Ruby callbacks.
  def draw_override ffi_draw
    FFI::Zig.starfield_draw ffi_draw, @path
  end
end

def tick args
  unless args.state.zig_starfield
    FFI::Zig.starfield_reset 16_384
    args.state.zig_starfield = ZigNativeStarfield.new 'sprites/tiny-star.png'
    args.outputs.static_sprites << args.state.zig_starfield
  end

  FFI::Zig.update_scanner_texture
  args.outputs.background_color = [18, 20, 28]
  args.outputs.sprites << { x: 80, y: 200, w: 400, h: 400, path: 'zig_suite_scanner' }
  args.outputs.labels << [80, 660, 'One Ruby starfield object. Native Zig motion. Measured boundaries.']
  args.outputs.labels << [540, 580, "Square: #{FFI::Zig.square(17)}"]
  args.outputs.labels << [540, 530, "Binary LF count: #{FFI::Zig.count_newlines("\0\n\0\n")}"]
  args.outputs.labels << [540, 480, "Nested sum: #{FFI::Zig.sum(1, [2, [3]], 4)}"]
  args.outputs.labels << [540, 430, "Regex index: #{FFI::Zig.regex_index('[a-z]+', '123abc')}"]
  args.outputs.labels << [540, 380, FFI::Zig.hello('DragonRuby')]
end
