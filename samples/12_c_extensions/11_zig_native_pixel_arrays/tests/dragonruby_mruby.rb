# Additional checks for docs/dragonruby-mruby.patch, not the proprietary engine.
raise "expected the published mruby 3.0.0 base" unless MRUBY_VERSION == "3.0.0"

# A stock 3.0.0 VM produces Integer 2 here. This must execute in the same
# patched interpreter that loads bridge.c and runs tests/smoke.rb.
quotient = 5 / 2
raise "published division patch is not active" unless quotient.is_a?(Float) && quotient == 2.5

# The language-level arithmetic change must not change the extension's result
# type or weaken its argument validation.
count = FFI::CExt.count_newlines("one\ntwo\n")
raise "native result is not an Integer" unless count.is_a?(Integer) && count == 2
ZigSmoke.reject(TypeError) { FFI::CExt.count_newlines(quotient) }

# Exercise initialization and hash-default dispatch with native results.
# The C host separately checks the patch's cached symbols in every fresh VM.
class DragonRubyPatchValue
  attr_reader :value
  def initialize value
    @value = value
  end
end
value = DragonRubyPatchValue.new(count)
raise "patched initialization failed" unless value.value == 2
fallback = Hash.new(value)
raise "patched hash default failed" unless fallback[:missing].equal?(value)

class DragonRubyPatchHash < Hash
  def default key
    FFI::CExt.count_newlines(key)
  end
end
raise "patched default dispatch failed" unless DragonRubyPatchHash.new["\n\0\n"] == 2
puts "Published DragonRuby mruby patch verified inside the C/Zig integration host."
