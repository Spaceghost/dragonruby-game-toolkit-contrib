# Build bridge/ against the matching SDK, then put its library in the game's
# native/<platform>/ directory before running this game. Public CI tests Ruby
# calls with real mruby, but does not validate the proprietary engine ABI.
$gtk.ffi_misc.gtk_dlopen 'zig_suite'

def tick args
  FFI::Zig.update_scanner_texture
  args.outputs.background_color = [18, 20, 28]
  args.outputs.sprites << { x: 80, y: 200, w: 400, h: 400, path: 'zig_suite_scanner' }
  args.outputs.labels << [80, 660, 'Ruby calls. Zig kernels. Measured separately.']
  args.outputs.labels << [540, 580, "Square: #{FFI::Zig.square(17)}"]
  args.outputs.labels << [540, 530, "Binary LF count: #{FFI::Zig.count_newlines("\0\n\0\n")}"]
  args.outputs.labels << [540, 480, "Nested sum: #{FFI::Zig.sum(1, [2, [3]], 4)}"]
  args.outputs.labels << [540, 430, "Regex index: #{FFI::Zig.regex_index('[a-z]+', '123abc')}"]
  args.outputs.labels << [540, 380, FFI::Zig.hello('DragonRuby')]
end
