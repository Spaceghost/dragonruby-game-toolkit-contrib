# Review the working C-to-Zig boundary

This PR is a parallel implementation of `03_native_pixel_arrays`, not a rewrite
of DragonRuby. Ruby remains the caller. Zig implements the scanner and SIMD
byte processing; the small C adapter keeps the engine's mruby types out of Zig.

## Start with executable evidence

Open the **Zig mruby integration** check, then a **dragonruby-3.0** job. The job
summary contains the actual captured scanner pixels, observed bounce sequence,
comparison counts and tested commit. Its log shows Ruby calls and the full build
result. No screenshot, recorded transcript or manually authored result file is
used to satisfy this check.

`zig build mruby-test` now builds **two separate shared libraries**:

| Library | Source | How exercised |
| --- | --- | --- |
| Original C scanner | `../03_native_pixel_arrays/app/ext.c`, unchanged | Registered in mruby, retained as a Ruby method alias |
| Zig scanner and newline counter | `app/bridge.c` and `app/*.zig` | Registered in the same mruby VM and called from Ruby |

Both callbacks upload to the same capture sink. Each must upload exactly once
and return Ruby `nil`. For **10,000 consecutive frames**, every one of the 100
32-bit pixels must match. The original C file is compiled directly with Zig's C
toolchain; it is not a handwritten facsimile of the C algorithm. CI checks its
blob against `e8f5458c41034dfdf38f75163a277ef015fae125` before building.

The first 22 reported rows are computed from the captured pixels, not supplied
as a preview animation. The printed first-frame grid is likewise the actual
captured upload. **Neither is a DragonRuby renderer screenshot.**

### Prove the comparison can fail

The same host runs in a separate process with `--inject-mismatch`. At frame 17,
the test sink flips one bit of pixel 7. The build requires **both exit code 42
and the exact mismatch diagnostic**. Unexpected failures, a missed corruption,
or a successful exit cannot satisfy this negative control. No fault injection
is compiled into the production extension.

After positive parity, the existing tests still exercise three fresh VM
lifetimes, 10,000 additional scanner frames per VM, invalid arguments without
state mutation, reset, re-registration, real Ruby exceptions and garbage
collection. The shared Ruby suite checks binary, empty, frozen, mutated and
large strings. Patch-specific probes run inside the same patched VM.

## Reproduce without the proprietary SDK

Prerequisites: Linux, Git, Ruby/Rake, a C compiler for mruby, and **Zig 0.16.0**.
Use the [pinned mruby preparation recipe](tests/DRAGONRUBY_MRUBY.md), then run:

```sh
zig build mruby-test \
  -Dmruby-root=/absolute/path/to/prepared-mruby \
  -Dmruby-boxing=word -Dmruby-dragonruby=true \
  -Doptimize=ReleaseSafe -Dcpu=baseline --summary all
```

The build runs both the positive comparison and the negative control. Keep the
sibling C sample in place and match the boxing choice to the mruby build. CI
repeats this in Debug and ReleaseSafe for all three value layouts, using both
the published DragonRuby-patched 3.0.0 source and the upstream 3.4.0 control.
The separate native workflow executes the Zig kernels, static/dynamic C ABI and
protected-memory tests on the five standard Linux/macOS/Windows runners.

## Read the production code before the harness

Start with `app/main.rb` for the Ruby API, `app/bridge.c` for registration and
validation, `app/native.h` for ownership, `app/kernels.zig` for the SIMD loop,
and `app/scanner.zig` for the animation. Then read `tests/mruby_host.c` and the
`mruby-test` section of `build.zig` for the evidence described above. Everything
under `tests/support` belongs to the test host, never the production SDK path.

## The remaining release gate

Public CI proves execution against real mruby, including the complete published
DragonRuby patch in explicitly selected configurations. It does not reproduce
the proprietary host table, SDK build configuration or GPU renderer. Preallocated
symbols remain disabled in the test VMs, as documented in the pinned recipe.

With the matching SDK available, run the existing engine check:

```sh
python3 tools/sdk_smoke.py --sdk /absolute/path/to/dragonruby
```

That checks actual SDK compilation, extension loading, Ruby assertions and 60
sample ticks. Inspect renderer output separately. Until that gate has run, this
PR is a draft: passing the public-source proof must not be presented as a
validated DragonRuby game. No SIMD speedup is claimed by this demonstration.
