# Parallel Zig native samples, with measured alternatives

This suite extends `11_zig_native_pixel_arrays`. The original C examples stay
unchanged. Small native kernels have readable implementations and explicitly
named performance candidates; the tests compare outputs before timing them.

**Scope:** portable sample logic is ported, not every platform adapter or test
harness. [COVERAGE.json](COVERAGE.json) accounts for every tracked C-family
source and header, including the Rust samples. The audit rejects unclassified
files, missing implementations and changes to pinned original sources. It does
not turn a retained SDK adapter into a completed Zig port by relabeling it.

## What is implemented

| Original example | Zig implementation | Alternative and validation |
| --- | --- | --- |
| `01_basics` square | Checked scalar square | Transactional eight-lane batches; exhaustive comparison over all 92,681 defined signed inputs |
| `02_intermediate` tiny-regex | Caller-owned compiled patterns and bounded matcher | Character-class bitmaps and SIMD leading-literal search; 480,568 comparisons with the actual C library |
| `03_native_pixel_arrays` scanner | Existing faithful Zig sample remains the baseline | Persistent pixels update only changed rows; 10,000 frames compared against extracted original C and previous Zig |
| `04_handcrafted_extension` Adder | Ordered nested traversal | Fixed 64-frame stack, cycle/depth rejection and unchanged output on error |
| Advanced starfield | Scalar star motion | Eight-lane structure-of-arrays updates; 3,016,000 star updates compared bit-for-bit, including RNG state and call counts |
| macOS greetings | Caller-buffer UTF-8 byte formatting | No Foundation temporary strings in the kernel; exact-capacity and short-buffer tests |
| `09_handcrafted_threads` Worker | Atomic lifecycle with explicit owner-thread controls | Failed creation, idempotent start/stop, restart and 100 real pthread lifetimes |
| `10_sqlite3` | Connection and statement lifecycle | Checked errors, borrowed column views and prepared-statement reuse measured against real SQLite |
| Previous SIMD LF example | Previous 16-byte implementation remains a benchmark baseline | 32-byte blocked accumulation reduces horizontal-reduction frequency, with guarded-page and chunk-boundary tests |

The square batch and nested traversal are not claimed as measured speedups.
Strictly ordered sum unrolling is deliberately retained as an experimental
candidate, including workloads where it loses. No fast-math reassociation is
used to manufacture a faster but different answer.

## Run the native proof and measurements

Use **Zig 0.16.0**, Python 3 and a C-capable host. From this directory:

```sh
zig build test -Doptimize=Debug -Dcpu=baseline --summary all
zig build test -Doptimize=ReleaseSafe -Dcpu=baseline --summary all
zig build bench -Doptimize=ReleaseFast -Dcpu=native --summary all > bench.log 2>&1
ZIG_CPU=native ZIG_OPTIMIZE=ReleaseFast python tools/report.py bench.log
```

`bench` depends on the correctness suite. It warms each implementation, runs
11 interleaved trials in shuffled order, validates observable checksums, and
prints every raw `BENCH` record. The report includes median, minimum, maximum,
median absolute deviation, CPU/OS/toolchain metadata and ratios to the C
control. A noisy result is labeled, not converted into a brittle CI speed gate.
C and Zig receive the same optimization mode and CPU target; the C optimizer
is not artificially disabled.

The deliberately corrupted scanner run must return **42** and exactly identify
frame 17 / pixel 7. An unrelated crash does not count as successful detection.
Debug and ReleaseSafe test the same vectorized paths used by ReleaseFast.

The suite's CI executes on standard hosted Linux x86-64 and ARM64 runners.
The x86 matrix includes baseline and host-native CPU targets. This is not a
claim that the new suite has inherited the old sample's Windows/macOS results.

For the separate real-SQLite tests and repeated-query measurements, install
the host SQLite development library, then run:

```sh
cd sqlite
zig build test -Doptimize=ReleaseSafe -Dcpu=native --summary all
zig build test -Doptimize=ReleaseFast -Dcpu=native --summary all > sqlite.log 2>&1
ZIG_CPU=native ZIG_OPTIMIZE=ReleaseFast python ../tools/report.py sqlite.log
```

SQLite is still the upstream C engine. The Zig wrapper does not allocate its
own error strings, but SQLite itself allocates. Tests force an execution-time
query error, failed open, constraint error and busy close, check NULL/empty/
binary column values, and verify that statements are released. Reuse is an
API/workload optimization that a C caller could also implement, not evidence
that translating the same SQLite call into Zig makes SQLite faster.

## Call the new functions from Ruby

`bridge/bridge.c` registers a parallel `FFI::Zig` module:

```ruby
FFI::Zig.square(-17)                       # 289
FFI::Zig.count_newlines("\0\n\0\n")        # 2
FFI::Zig.regex_index('[a-z]+', '123abc')     # 3
FFI::Zig.sum(1, [2, [3]], 4)                # 10.0
FFI::Zig.hello('DragonRuby')                # "Hello DragonRuby!"
FFI::Zig.reset_scanner
FFI::Zig.update_scanner_texture
```

The Ruby regex convenience call compiles on each invocation. Native benchmarks
measure reuse of compiled patterns instead, so their search timings must not
be presented as timings for that convenience method. The native regex API
allows caller-owned compiled storage. The Ruby greeting adapter has an
explicit 511-byte output limit; the native API accepts any valid capacity.

To run the adapter against a prepared real mruby test VM, follow the existing
[pinned preparation recipe](../11_zig_native_pixel_arrays/tests/DRAGONRUBY_MRUBY.md),
then run from `bridge/`:

```sh
zig build test -Dmruby-root=/absolute/path/to/prepared-mruby \
  -Dboxing=word -Dpublished=true -Doptimize=ReleaseSafe -Dcpu=baseline
```

The Ruby workflow builds upstream 3.4.0 and the complete published DragonRuby
3.0.0 patch with word, NaN and unboxed layouts. It exercises real Ruby strings,
arrays, exceptions, GC, three VM lifetimes and six registrations. Its host
contract is deliberately separate from the proprietary SDK table.

For a matching proprietary SDK, build from `bridge/` with:

```sh
zig build -Dsdk-include=/absolute/path/to/sdk/include \
  -Doptimize=ReleaseSafe -Dcpu=baseline
```

Stage the resulting `zig_suite` shared library under the game's matching
`native/<platform>/` directory. `app/main.rb` is the sample game. Actual SDK
compilation, extension loading and renderer execution remain release gates;
public mruby tests cannot establish proprietary host-table compatibility.

## Ownership and compatibility boundaries

The native kernels accept caller-owned buffers and do not call a heap
allocator. This is a source/API property, **not an instrumented allocation
count**: raw benchmark records use `allocation_count: null`. Ruby allocation,
thread creation, SQLite internals and rendering are outside that claim.
Pointers are borrowed only for the documented operation or view lifetime.
Callbacks must not raise through a Zig frame or execute Ruby from a worker.

Square overflow, malformed/oversized regexes, deep/cyclic arrays and oversized
greetings are reported explicitly rather than reproducing undefined behavior
or unbounded recursion. Regex parity covers the shipped tiny-regex dialect on
the tested ASCII domain, including its dot/newline setting and unusual failed
match-length behavior. It is not the Rust `rure`/Unicode regex engine.

## What remains, rather than being silently called finished

The native SoA starfield, worker and SQLite APIs are usable through their C
headers, but batched Ruby star objects/drawing, the SDL worker adapter and a
replacement Ruby `query_json` adapter are not yet wired into this sample game.
End-to-end Ruby/renderer and allocation-profile measurements remain separate
from the kernel benchmarks.

The original iOS Objective-C main-thread dispatch, macOS framework integration,
Steamworks C++ shim and Android JNI bridge remain platform boundaries. They
are inventoried but not claimed as completed Zig replacements. Steam startup
error handling and JNI reference/exception cleanup still need their own
platform-tested changes. Rust FFI bindings, its C header/examples/tests, and
the existing C ABI/renderer test oracles are deliberately retained.
