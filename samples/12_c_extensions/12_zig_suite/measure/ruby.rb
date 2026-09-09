# Parsed once before measurements. Inputs persist; result construction and normal
# automatic GC remain part of each Ruby batch. No proprietary renderer is used.
module Measure
  TEXT = ("a" * 15 + "\n") * 8
  NUMBERS = (1..64).to_a
  NAME = "x" * 128
  HAYSTACK = "x" * 4090 + "needle"
  PATTERN = "needle"
  MASK = 0x3fffffff

  def self.lf_ruby(n)
    i = 0; checksum = 0
    while i < n
      j = 0; found = 0
      while j < TEXT.length
        found += 1 if TEXT.getbyte(j) == 10
        j += 1
      end
      checksum = (checksum + found) & MASK
      i += 1
    end
    checksum
  end

  def self.lf_zig(n)
    i = 0; checksum = 0
    while i < n
      checksum = (checksum + FFI::Zig.count_newlines(TEXT)) & MASK
      i += 1
    end
    checksum
  end

  def self.sum_ruby(n)
    i = 0; checksum = 0
    while i < n
      j = 0; result = 0.0
      while j < NUMBERS.length
        result += NUMBERS[j]
        j += 1
      end
      checksum = (checksum + result.to_i) & MASK
      i += 1
    end
    checksum
  end

  def self.sum_zig(n)
    i = 0; checksum = 0
    while i < n
      checksum = (checksum + FFI::Zig.sum(NUMBERS).to_i) & MASK
      i += 1
    end
    checksum
  end

  def self.hello_ruby(n)
    i = 0; checksum = 0
    while i < n
      result = "Hello " + NAME + "!"
      checksum = (checksum + result.length + result.getbyte(6)) & MASK
      i += 1
    end
    checksum
  end

  def self.hello_zig(n)
    i = 0; checksum = 0
    while i < n
      result = FFI::Zig.hello(NAME)
      checksum = (checksum + result.length + result.getbyte(6)) & MASK
      i += 1
    end
    checksum
  end

  def self.literal_ruby(n)
    i = 0; checksum = 0
    while i < n
      checksum = (checksum + HAYSTACK.index(PATTERN)) & MASK
      i += 1
    end
    checksum
  end

  def self.literal_zig(n)
    i = 0; checksum = 0
    while i < n
      checksum = (checksum + FFI::Zig.regex_index(PATTERN, HAYSTACK)) & MASK
      i += 1
    end
    checksum
  end

  def self.scanner(n)
    i = 0
    while i < n
      FFI::Zig.update_scanner_texture
      i += 1
    end
    i
  end

  def self.envelope(n)
    i = 0
    while i < n
      i += 1
    end
    i
  end
end
raise 'greeting mismatch' unless FFI::Zig.hello(Measure::NAME) == "Hello " + Measure::NAME + "!"
raise 'sum mismatch' unless FFI::Zig.sum(Measure::NUMBERS) == 2080.0
raise 'LF mismatch' unless FFI::Zig.count_newlines(Measure::TEXT) == 8
raise 'literal mismatch' unless FFI::Zig.regex_index(Measure::PATTERN, Measure::HAYSTACK) == 4090
