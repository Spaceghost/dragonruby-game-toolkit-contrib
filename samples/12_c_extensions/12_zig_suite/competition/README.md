# A matched C/Zig competition

Both languages get the same algorithms, inputs, compiler target, optimization
mode and correctness contract. The original C and previous Zig implementations
remain unchanged. The purpose is to improve both, not pick a preferred winner.

## The candidates

`src/competitive.c` and `src/competitive.zig` are independent, compact
implementations of two shared ideas:

* Newline counting uses two independent 32-byte accumulators. Each sees at most
  255 blocks before widening; pair lanes are at most 510 and the reduction is
  at most 16320. Inputs shorter than 128 bytes skip that accumulation setup.
  Residual 32/16/8-byte vectors are counted with bounded loads and byte
  reductions; at most seven bytes remain for the scalar tail.
* Star motion uses eight-lane SoA arithmetic when no star wraps. A short,
  ordinary scalar loop handles exceptional blocks, rather than eight manually
  duplicated fallback paths. The helper is deliberately not inlined in either
  language, keeping RNG calls out of the fast loop. It preserves star order
  and x-before-y RNG calls; the call cost is included for wrapping blocks.

The C candidate uses **Clang vector extensions**, not ISO-only C and not a claim
about GCC. Both are compiled by the pinned Zig 0.16.0 distribution (bundled Clang
for C), with the same target and build mode. C autovectorization is enabled;
`c_scalar` describes the source control, not necessarily scalar machine code.
No fast math, inline assembly, target-specific intrinsics or LTO is used.

New candidates are available through `competitive.h` or by importing
`src/competitive.zig` (`countDual` and `starsBlock`). They are measured candidates,
not automatic replacements for every workload. Float slices must have equal
lengths; distinct arrays must not overlap. The RNG must use its own state, not
mutate the arrays or raise/reenter Ruby. Binary LF counting supports arbitrary
bytes, including NUL. NULL is accepted only for an empty C range.

## Fair comparisons

The 15 workloads retain four participants each: the C source control/original,
the previous optimized Zig implementation, tuned C, and tuned Zig. Reports show
C's improvement over its own baseline, Zig's improvement over its own baseline,
and tuned C versus tuned Zig separately. For stars, the baseline uses original
AoS storage while both tuned candidates use persistent SoA storage: that is a
layout/algorithm comparison, not a language-only speedup. Conversion, allocation,
Ruby drawing and renderer upload are outside these kernel timings.

Inputs include vector boundaries, unaligned starts, random/all/absent LF data,
warm and rotating buffers, and no-wrap/mixed/all-wrap star distributions. A 16 MiB
rotating set is NOT claimed to exceed every host's last-level cache. Allocation
instrumentation uses a separate executable and the existing calibrated linked
libc meter, with a required injected-allocation failure. It is not RSS or proof
that arbitrary callbacks never allocate.

Tests compare LF counts to an independent scalar specification and star motion
to arithmetic extracted from the pinned original C. They cover all 65536 paired
8-star x/y wrap masks, exact float bits and RNG consumption, generated finite
inputs, signed zero/subnormal/boundary values, every 16-byte LF mask, chunk/tail
boundaries, sentinel stores, read-only inputs and guarded pages. Counters report
completed comparisons, not an estimate. Six source mutants (wrong SIMD LF byte,
byte-lane accumulation overflow and wrong star direction, in each language) must
compile and fail the unchanged harness with the intended diagnostic. These are finite tests, not formal proof.

## Reproduce

From this directory, with Zig 0.16.0 and native Linux development tools:

```sh
zig build test -Doptimize=ReleaseSafe -Dcpu=baseline
python3 mutations.py --cpu baseline
python3 run.py --cpu native --output /tmp/rivals
```

`run.py` rebuilds and rechecks correctness before timing. It uses the existing
report validator, equal-work adaptive batches, 3 fresh processes and 11 shuffled
trials, pinned process affinity where permitted, binary/source hashes, captured
machine data, full C/Zig disassembly and symbol sizes. No outliers or losing
variants are dropped. Undersized samples remain labeled. Ratios pair trials
within processes; process-median ranges are not confidence intervals and p95 is
of batch means, not individual-operation tail latency. No shared-runner timing
threshold gates CI. The workflow executes x86-64 baseline/native and ARM64 native.

Changes are promoted only with their workload/CPU limits documented. Equal
algorithms can still compile differently; readable losing experiments should be
reported or removed explicitly, never silently relabeled as wins.

Primary language references:
https://clang.llvm.org/docs/LanguageExtensions.html#vectors-and-extended-vectors
https://ziglang.org/documentation/0.16.0/#Vectors

## Optimization history

The first candidates are retained in Git at `8c2488a`. On that run's x86 native
host, the new 4 KiB LF implementation beat the previous Zig baseline, but the
short-input scalar tails regressed. Disassembly also showed that a short scalar
star helper was still inlined/unrolled by the compiler, especially in Zig.
The next experiment adds bounded vector tails and explicitly outlines that
exceptional helper in both languages. Fresh results, not source length or the
word `noinline`, decide whether those changes help each workload.
