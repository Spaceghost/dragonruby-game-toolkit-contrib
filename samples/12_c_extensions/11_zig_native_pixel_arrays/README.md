# Zig native pixel arrays and SIMD

A parallel Zig implementation of `../03_native_pixel_arrays`, plus a useful
SIMD byte-scanning example. The original C samples remain unchanged.

```ruby
FFI::CExt.update_scanner_texture             # upload the next scanner frame
FFI::CExt.reset_scanner                      # return to the initial frame
FFI::CExt.count_newlines("one\ntwo\n")       # => 2
FFI::CExt.count_newlines("\0\n")            # => 1
```

Zig owns the algorithms and scanner state. A small C adapter uses the matching
DragonRuby SDK's `drb_api_t` table and mruby headers. Do not recreate `mrb_value`
in Zig or link a second mruby into the extension. Nelua and Lua are not required.

## Build and run

Install **Zig 0.16.0**, pinned in `.zig-version` and checked by `build.zig`.
Building the game extension additionally requires a DragonRuby distribution
with native-extension support and matching SDK headers. This public repository
contains neither those proprietary headers nor the DragonRuby executable.

Place the sample under `samples/12_c_extensions` in that distribution:

```sh
./pre.sh
../../../dragonruby .
```

On Windows run `pre.bat`, then `..\..\..\dragonruby.exe .`.
For an SDK elsewhere:

```sh
DRB_ROOT=/absolute/path/to/dragonruby ./pre.sh
/absolute/path/to/dragonruby/dragonruby .
```

Set `ZIG` to the Zig executable path when necessary. Set `DRB_ROOT` before
running `pre.bat` on Windows. Relative SDK paths are relative to this sample.
Both launchers build in ReleaseSafe for a baseline CPU. Zig's C toolchain
compiles the adapter; no separate Clang or `dragonruby-bind` is needed.

The corresponding direct command for x86-64 Linux is:

```sh
zig build extension --prefix . -Doptimize=ReleaseSafe \
  -Dtarget=x86_64-linux-gnu -Dcpu=baseline \
  -Ddragonruby-root=/absolute/path/to/dragonruby
```

The installed file is `native/linux-amd64/ext.so`, `native/macos/ext.dylib`, or
`native/windows-amd64/ext.dll`. Without `--prefix .`, installation is under
`zig-out/`. Match the target architecture, libc, deployment minimum and SDK to
the **DragonRuby executable**, not merely the build host. Linux defaults to
glibc even when building on Alpine. Override `ZIG_TARGET` and `DRB_PLATFORM`
together for cross-builds. Unmapped targets require an explicit platform name;
macOS outputs are architecture-specific, not universal binaries. Mobile and web
extension packaging are outside this sample's scope.

## SIMD, ownership and errors

The newline counter's core is:

```zig
const bytes: @Vector(16, u8) = data[i..][0..16].*;
const flags = @select(u8, bytes == newline, ones, zeros);
count += @reduce(.Add, flags);
```

The three constants are sixteen-lane vectors created with `@splat`. The load
needs only byte alignment. Comparison produces sixteen booleans, selection maps
them to zero or one, and reduction counts matches. The per-block sum is at most
16; the total uses `usize`. Only complete blocks are loaded. A scalar tail
handles the final 0..15 bytes without reading outside the buffer.

This counts LF bytes, not logical lines or Unicode line separators. Embedded
NULs are ordinary input. `fillPixels` uses four-u32 stores and a scalar tail;
for production workloads compare it with `@memset` rather than assuming a
speedup. The 10x10 scanner is a teaching example. No performance gain or specific
instruction sequence is promised. This sample is not a gzip decoder.

The complete native contract is `app/native.h`. Kernels allocate no heap memory,
retain no caller pointers and never call Ruby. Input validation and Ruby
exceptions stay in the C adapter, outside Zig frames. All three methods validate
arity before changing state or entering Zig. Scanner exports share one instance
and are main-thread-only; the newline counter has no mutable global state.
Registration and `reset_scanner` reset the animation. The original repeated last
row at the bounce and stack pixel-buffer upload lifetime are preserved.

## Validation layers

These layers are intentionally separate. A passing upstream mruby harness is
not a claim about the proprietary DragonRuby API table layout or renderer.

### Native tests: no SDK

```sh
zig build test
zig build test -Doptimize=ReleaseSafe -Dcpu=baseline
zig fmt --check build.zig app
python -m unittest discover -s tests -p 'test_*.py' -v
```

Zig tests cover all 65,536 newline masks in a sixteen-byte block, lengths 0..257
at 32 offsets, binary input, vector-fill boundaries, scanner instances and reset.
The C ABI program compares 10,000 frames against the original C algorithm.
It runs both statically linked and through an actual shared-library loader.
OS-protected pages catch overreads/overwrites at allocation boundaries; read-only
input pages catch unintended writes. Windows uses VirtualProtect/LoadLibrary;
Linux and macOS use mprotect/dlopen. Empty input may have a NULL or inaccessible
pointer because zero bytes must be read.

The Python tests verify SDK orchestration only. They test argument boundaries,
paths with spaces, temporary staging, missing inputs, version checks, failed
builds, stale/partial completion markers, premature zero exits and timeouts.
Actual child processes verify cleanup. They are not SDK compatibility tests.

### Real mruby integration: no DragonRuby SDK

`.github/workflows/zig-mruby.yml` builds pinned upstream mruby 3.4.0 in
word-boxed, NaN-boxed and unboxed configurations. The host dynamically loads the
**actual `app/bridge.c` plus Zig** and runs `tests/smoke.rb`, checks 10,000
uploaded frames per VM, rejection without state mutation, re-registration,
garbage collection and three fresh VM lifetimes.

Only the small host API table and pixel upload sink are test substitutes.
`tests/support/dragonruby.h` is explicitly not the SDK. Production build include
paths never reference it, and its guard rejects accidental production use.
The host has one real mruby; the extension must not link another runtime.
To reproduce on Linux with an upstream mruby 3.4.0 checkout:

```sh
MRUBY_BOXING=word \
  MRUBY_CONFIG=/absolute/path/to/this/sample/tests/mruby_config.rb \
  rake -C /absolute/path/to/mruby
zig build mruby-test -Dmruby-root=/absolute/path/to/mruby \
  -Dmruby-boxing=word -Doptimize=ReleaseSafe
```

Match `MRUBY_BOXING` used by rake to `-Dmruby-boxing`; the harness and VM must
share their integer width and value representation. The configuration uses
mruby's `disable_presym` build API so generated bytecode and the compiler agree.
Consult CI results for validation of a particular commit, rather than treating
test source as a pass.

### Matching DragonRuby SDK and runtime

With Python 3.10+ and a licensed matching SDK on a machine that can run the game:

```sh
python tools/sdk_smoke.py --sdk /absolute/path/to/dragonruby
```

The command stages an isolated copy, builds and installs the actual extension,
loads it in DragonRuby, runs the shared Ruby assertions, and calls the original
sample's tick for 60 frames. It requires a fresh per-run completion marker; a
zero exit status, an old marker or a missing library cannot pass. Ruby failures
and timeouts return nonzero with the log tail. The game process is terminated
and the temporary copy removed; the source checkout is not rewritten.

Use `--zig`, `--runtime`, paired `--target`/`--platform`, `--timeout`, and
`--build-timeout` to override defaults. This is a runtime smoke test, not an image
comparison: inspect the rotating scanner visually before release. The command
does not download an SDK or fall back to test headers when it is absent.
The same assertions may be run from the game's console:

```ruby
require "tests/smoke.rb"
```

## Free hosted CI

Native Debug and ReleaseSafe jobs use standard GitHub-hosted runners:
`ubuntu-24.04`, `ubuntu-24.04-arm`, `windows-2022`, `macos-15`, and
`macos-15-intel`. Linux x86-64 also cross-compiles the native tests for the other
four targets. mruby integration uses three standard Ubuntu jobs. Cross-building
is distinct from native execution, and neither replaces the real SDK test.

GitHub's standard runner compute is free for public repositories under its
current billing policy; private forks and larger runners differ. Workflows
require no repository secrets, self-hosted machines, paid services, uploaded
artifacts or cache storage. Checkout commits and compiler checksums are pinned.
PRs trigger tests, main pushes check merged code, and concurrency cancels
superseded runs. The proprietary SDK/runtime is not provisioned in public CI.

The original C example's MIT attribution is retained in `license-for-sample.txt`.
