raise 'square' unless FFI::Zig.square(-17) == 289
raise 'empty LF' unless FFI::Zig.count_newlines('') == 0
raise 'binary LF' unless FFI::Zig.count_newlines("\0\n\xff\n") == 2
raise 'large LF' unless FFI::Zig.count_newlines("\n" * 70_000) == 70_000
nested_sum = [1e16, [1.0, [-1e16]], 3]
raise 'nested order' unless FFI::Zig.sum(*nested_sum) == 3.0
raise 'nested single-reader order' unless FFI::Zig.sum_single_reader(*nested_sum) == 3.0
raise 'nested direct-C order' unless FFI::Zig.sum_c_direct(*nested_sum) == 3.0
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
  -> { FFI::Zig.sum_single_reader(cycle) },
  -> { FFI::Zig.sum_c_direct(cycle) },
  -> { FFI::Zig.sum([1, 'not a number']) },
  -> { FFI::Zig.sum_single_reader([1, 'not a number']) },
  -> { FFI::Zig.sum_c_direct([1, 'not a number']) },
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

class ZigSuiteDrawSink
  attr_reader :calls, :checksum
  def initialize
    @calls = 0
    @checksum = 0
  end
  def draw_sprite(x, y, w, h, path)
    raise 'star size' unless w == 4.0 && h == 4.0
    raise 'star path' unless path == 'sprites/tiny-star.png'
    raise 'star coordinate' unless x.is_a?(Numeric) && y.is_a?(Numeric)
    @calls += 1
    @checksum = (@checksum + x.to_i + y.to_i) & 0x3fffffff
    nil
  end
end
begin
  FFI::Zig.starfield_draw(ZigSuiteDrawSink.new, 'sprites/tiny-star.png')
  raise 'expected uninitialized starfield error'
rescue RuntimeError
end
FFI::Zig.starfield_reset(64)
draw_sink = ZigSuiteDrawSink.new
2.times { FFI::Zig.starfield_draw(draw_sink, 'sprites/tiny-star.png') }
raise 'one-object starfield did not draw all stars' unless draw_sink.calls == 128
raise 'starfield draw checksum stayed empty' if draw_sink.checksum == 0
FFI::Zig.starfield_clear
begin
  FFI::Zig.starfield_draw(draw_sink, 'sprites/tiny-star.png')
  raise 'expected cleared starfield error'
rescue RuntimeError
end

FFI::Zig.sqlite_open(':memory:')
FFI::Zig.sqlite_exec("create table q(v text); insert into q values ('{\"id\":1}'), (NULL), ('a'||char(0)||'b')")
sql = 'select v from q order by rowid'
expected = ["{\"id\":1}", 'null', "a\0b"]
raise 'query_json first result' unless FFI::Zig.query_json(sql) == expected
raise 'query should prepare once' unless FFI::Zig.query_prepares == 1 && FFI::Zig.query_hits == 0
raise 'query_json cached result' unless FFI::Zig.query_json(sql) == expected
raise 'query should hit cache' unless FFI::Zig.query_prepares == 1 && FFI::Zig.query_hits == 1
begin
  FFI::Zig.query_json('select from bad_sql')
  raise 'expected query error'
rescue RuntimeError
end
raise 'failed prepare evicted cache' unless FFI::Zig.query_json(sql) == expected
raise 'failed prepare changed counters' unless FFI::Zig.query_prepares == 1 && FFI::Zig.query_hits == 2
FFI::Zig.sqlite_close
