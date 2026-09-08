# Shared verbatim by the real mruby host and the DragonRuby SDK smoke runner.
module ZigSmoke
  def self.check condition, message
    raise message unless condition
  end

  def self.reject error_class
    begin
      yield
    rescue error_class
      return
    end
    raise "expected #{error_class}"
  end

  def self.run
    samples = [["", 0], ["unterminated", 0], ["one\ntwo\n", 2],
               ["\0\n\0\n", 2], ["\r\n", 1], ["\xff\n", 1],
               ["\n" * 70000, 70000], ["x" * 70000, 0]]
    samples.each do |input, expected|
      before = input.dup
      actual = FFI::CExt.count_newlines input
      check(actual.is_a?(Integer) && actual == expected, "wrong newline count")
      check(input == before, "input was modified")
    end
    65.times do |length|
      input = ("x\n\0" * 64)[0, length]
      check(FFI::CExt.count_newlines(input) == input.count("\n"), "tail mismatch")
    end
    [nil, false, true, 123, 1.5, [], {}, :text, Object.new].each do |input|
      reject(TypeError) { FFI::CExt.count_newlines input }
    end
    reject(ArgumentError) { FFI::CExt.count_newlines }
    reject(ArgumentError) { FFI::CExt.count_newlines("", "extra") }
    reject(ArgumentError) { FFI::CExt.update_scanner_texture(1) }
    reject(ArgumentError) { FFI::CExt.reset_scanner(1) }
    check(FFI::CExt.count_newlines("\n".freeze) == 1, "frozen string failed")
    mutable = "x\0x"
    check(FFI::CExt.count_newlines(mutable) == 0, "mutable string failed")
    mutable.replace("\n\0\n")
    check(FFI::CExt.count_newlines(mutable) == 2, "stale input pointer")
    check(FFI::CExt.reset_scanner.nil?, "reset result")
    check(FFI::CExt.update_scanner_texture.nil?, "scanner result")
    FFI::CExt.reset_scanner
    true
  end
end
ZigSmoke.run
puts "Zig native extension smoke tests passed."
