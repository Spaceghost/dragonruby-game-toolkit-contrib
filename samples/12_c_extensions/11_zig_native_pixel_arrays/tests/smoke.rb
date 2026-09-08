# Load from the DragonRuby console after the sample starts:
# require "tests/smoke.rb"
[
  ["", 0],
  ["no trailing newline", 0],
  ["one\ntwo\n", 2],
  ["\0\n\0\n", 2],
  ["\r\n", 1],
  ["\n" * 257, 257]
].each do |input, expected|
  actual = FFI::CExt.count_newlines input
  raise "count_newlines: expected #{expected}, got #{actual}" unless actual == expected
end

rejected_type = false
begin
  FFI::CExt.count_newlines 123
rescue TypeError
  rejected_type = true
end
raise "count_newlines accepted an Integer" unless rejected_type

rejected_arity = false
begin
  FFI::CExt.count_newlines
rescue ArgumentError
  rejected_arity = true
end
raise "count_newlines accepted zero arguments" unless rejected_arity
raise "scanner did not return nil" unless FFI::CExt.update_scanner_texture.nil?
puts "Zig native extension smoke tests passed."
