raise 'square' unless FFI::Zig.square(-17) == 289
raise 'empty LF' unless FFI::Zig.count_newlines('') == 0
raise 'binary LF' unless FFI::Zig.count_newlines("\0\n\xff\n") == 2
raise 'large LF' unless FFI::Zig.count_newlines("\n" * 70_000) == 70_000
raise 'nested order' unless FFI::Zig.sum(1e16, [1.0, [-1e16]], 3) == 3.0
raise 'empty sum' unless FFI::Zig.sum([], []) == 0.0
raise 'regex literal' unless FFI::Zig.regex_index('needle', 'x' * 100 + 'needle') == 100
raise 'regex class' unless FFI::Zig.regex_index('[a-z]+', '123abc') == 3
raise 'regex missing' unless FFI::Zig.regex_index('z', 'abc') == -1
raise 'hello' unless FFI::Zig.hello('Zig') == 'Hello Zig!'
raise 'goodbye' unless FFI::Zig.goodbye('Ruby') == 'Bye Ruby!'
raise 'binary greeting' unless FFI::Zig.hello("a\0b") == "Hello a\0b!"
cycle = []
cycle << cycle
checks = [
  -> { FFI::Zig.square(46_341) },
  -> { FFI::Zig.sum(cycle) },
  -> { FFI::Zig.sum([1, 'not a number']) },
  -> { FFI::Zig.regex_index('[', 'a') },
  -> { FFI::Zig.hello('x' * 512) }
]
checks.each do |call|
  raised = false
  begin
    call.call
  rescue ArgumentError
    raised = true
  end
  raise 'expected actual Ruby ArgumentError' unless raised
end
raise 'scanner reset' unless FFI::Zig.reset_scanner.nil?
20.times { raise 'scanner return' unless FFI::Zig.update_scanner_texture.nil? }
