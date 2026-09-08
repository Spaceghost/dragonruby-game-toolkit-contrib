# Zig native pixel arrays and SIMD

A parallel Zig implementation of `../03_native_pixel_arrays`. The C reference
is unchanged. Ruby still calls `FFI::CExt.update_scanner_texture` and draws the
same rotating scanner sprite. An additional `FFI::CExt.count_newlines(string)`
method demonstrates useful buffer processing with explicit SIMD.

Zig implements the algorithms and scanner state. A small C adapter includes the
matching DragonRuby SDK headers and uses its `drb_api_t` table. Do not link a
second mruby, recreate `mrb_value` in Zig, or use headers from an unrelated Ruby
installation. There is no Nelua or Lua dependency.

## Build and run

Install **Zig 0.16.0**, which is pinned in `.zig-version` and checked by
`build.zig`. A DragonRuby distribution with native-extension support and its
matching headers is required to build or run the extension. This public contrib
repository alone does not contain the SDK or the DragonRuby executable.

Place this sample under `samples/12_c_extensions` in that distribution. From
this sample's directory:

```sh
./pre.sh
../../../dragonruby .
```

On Windows, run `pre.bat`, then `..\..\..\dragonruby.exe .`.

For an SDK elsewhere:

```sh
DRB_ROOT=/absolute/path/to/dragonruby ./pre.sh
/absolute/path/to/dragonruby/dragonruby .
```

Set `ZIG` to the executable path when it is not on PATH. Set `DRB_ROOT` before
running `pre.bat` on Windows. Relative SDK paths are relative to this sample.
The build scripts use ReleaseSafe, a baseline CPU, and install one extension:
`native/linux-amd64/ext.so`, `native/macos/ext.dylib`, or
`native/windows-amd64/ext.dll`.

The build system compiles `bridge.c` with Zig's C toolchain (zig-cc) and links it
with Zig code. A separate Clang installation or `dragonruby-bind` invocation is
not needed for this handwritten adapter.

The equivalent direct command, for an x86-64 glibc Linux runtime, is:

```sh
zig build extension --prefix . -Doptimize=ReleaseSafe \
  -Dtarget=x86_64-linux-gnu -Dcpu=baseline \
  -Ddragonruby-root=/absolute/path/to/dragonruby
```

`--prefix .` matters: without it, `zig build` installs under `zig-out/`.
The shell script selects a glibc Linux target deliberately, even when building
on Alpine. Match the architecture, libc, deployment minimum, and SDK to the
**DragonRuby executable**, not just the build host. For cross-builds, override
`ZIG_TARGET` and `DRB_PLATFORM` together. Mac builds are architecture-specific,
not universal binaries. Other target folder names require `-Dplatform=...`;
unsupported automatic mappings use `custom` rather than claiming compatibility.
Apple platform builds may need the corresponding SDK/toolchain. Mobile and web
extension packaging are outside this sample's scope.

## The SIMD part

The core of `app/kernels.zig` is:

```zig
const bytes: @Vector(16, u8) = data[i..][0..16].*;
const flags = @select(u8, bytes == newline, ones, zeros);
count += @reduce(.Add, flags);
```

`newline`, `ones`, and `zeros` are sixteen-lane vectors filled with `@splat`.
The first line loads sixteen bytes through an ordinary byte-aligned array.
The comparison produces sixteen booleans, `@select` maps them to zero or one,
and `@reduce` sums them. A block contributes at most 16, so the per-block u8 sum
cannot overflow. The total uses `usize`.

Only complete blocks are loaded; a scalar tail handles the final 0..15 bytes.
The pointer is never cast to a more strictly aligned vector pointer. Inputs can
contain NULs: the C adapter passes the Ruby string's byte length, not `strlen`.
This counts LF bytes, not logical lines or Unicode line separators.

`fillPixels` similarly stores four u32 pixels per vector, with a scalar tail.
The scanner's 10x10 image is a teaching example, not a workload needing native
optimization. The compiler may lower vector operations to different instruction
sequences or replace fills with other operations. No speedup or particular
instruction count is claimed; benchmark real buffers and inspect target code.
This does not implement gzip decoding or a DEFLATE walker.

Try from the DragonRuby console:

```ruby
FFI::CExt.count_newlines("one\ntwo\n") # => 2
FFI::CExt.count_newlines("\0\n")      # => 1
```

## Tests without the DragonRuby SDK

From this sample's directory:

```sh
zig build test
zig build test -Doptimize=ReleaseSafe -Dcpu=baseline
zig fmt --check build.zig app
```

These execute the Zig kernels, not a C replacement for them. Tests include all
65,536 possible newline masks in one sixteen-byte block, every input length
0..257 at 32 offsets, binary/NUL data, fill boundaries, the scanner bounce
sequence, independent scanner instances, and resetting the exported scanner.

`tests/abi.c` calls the Zig exports using `app/native.h`, compares 10,000 scanner
frames against a scalar reference derived from the original C sample, and checks
buffer guards and newline counts. The ABI test requires no mruby mock.

Compile-only cross-target checks can use, for example:

```sh
zig build check -Dtarget=aarch64-linux-gnu -Doptimize=ReleaseSafe
zig build check -Dtarget=x86_64-windows-gnu -Doptimize=ReleaseSafe
```

### Free GitHub-hosted CI

The repository workflow pins the checkout action and every Zig archive checksum.
It **executes** the Debug and ReleaseSafe Zig tests and C ABI/reference program
natively on five standard GitHub-hosted runners:

| Runner | Native execution target |
| --- | --- |
| `ubuntu-24.04` | x86-64 Linux / glibc |
| `ubuntu-24.04-arm` | ARM64 Linux / glibc |
| `windows-2022` | x86-64 Windows / GNU ABI |
| `macos-15` | ARM64 macOS |
| `macos-15-intel` | x86-64 macOS |

GitHub makes standard hosted runner compute free for public repositories; see
[GitHub's runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
This workflow uses no larger runners, self-hosted machines, credentials beyond
its read-only checkout token, paid services, uploaded artifacts, or cache
storage. The free-public-repository condition does not apply automatically to
private forks or larger runners.

Pull requests trigger the suite; pushes to `main` test the merged result. This
avoids duplicate push/PR suites, and concurrency cancels obsolete runs. Manual
`workflow_dispatch` is available once this workflow exists on the default branch.
Each job is capped at 15 minutes and records its actual test outcomes and
validation scope in the Actions job summary.

The x86-64 Linux job also performs compile-only cross-target checks. Those do not
replace the native execution jobs. None of these jobs has the DragonRuby SDK:
they do **not** compile `bridge.c`, load the extension into DragonRuby, or verify
the rendered sprite. The SDK/runtime checks below remain a separate requirement.

## Tests with the real SDK/runtime

Build and start the sample, confirm that the sprite renders and bounces, then
load this in its console:

```ruby
require "tests/smoke.rb"
```

This checks Ruby argument validation, empty/binary strings, a multi-block input,
and the scanner call. It does not replace visually checking the rendered sprite.

## Boundary and ownership

`app/native.h` documents the complete C/Zig boundary. The kernels allocate no
heap memory, retain no caller pointers, and never call Ruby. Ruby validation and
exceptions stay in the C adapter, outside Zig stack frames. The scanner exports
share one instance and are main-thread-only; independent Zig `Scanner` instances
are available internally. The newline counter has no mutable global state.

The scanner uses a stack pixel buffer and uploads it immediately, preserving the
original C sample's buffer lifetime and behavior, including its repeated final
row at the bounce. The original C sample's MIT attribution is retained in
`license-for-sample.txt`.
