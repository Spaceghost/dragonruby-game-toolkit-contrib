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
* Star motion uses eight-lane SoA arithmetic when no star wraps. Dense blocks
  where all eight lanes wrap both axes enter one ordered scalar run instead of
  paying a vector check plus one fallback call for every eight stars. Mixed
  blocks use a compact scalar fallback. Star order and x-before-y RNG calls are
  preserved and every fallback cost remains inside the measurement.

The C candidate uses **Clang vector extensions**, not ISO-only C and not a claim
about GCC. Both are compiled by the pinned Zig 0.16.0 distribution (bundled Clang
for C), with the same target and build mode. C autovectorization is enabled;
`c_scalar` describes the source control, not necessarily scalar machine code.
No fast math, inline assembly, target-specific intrinsics or LTO is used.

New candidates are available through `competitive.h` or by importing
`src/competitive.zig` (`countDual` and `starsBlock`). Zig also exposes
`drbz_stars_scalar_fallback`: a real low-level scalar ABI entry. That boundary is
intentional. On ARM, keeping the fallback internal let LLVM specialize the
frequent count=8 call sites into an eight-copy callback-heavy loop. External
visibility prevents that code-size explosion while leaving the source ordinary.
It is also a useful direct fallback for embeddings that require the same RNG
ordering. This is not an automatic replacement for every workload.

Float slices must have equal lengths; distinct arrays must not overlap. The RNG
must use its own state, not mutate the arrays or raise/reenter Ruby. Binary LF
counting supports arbitrary bytes, including NUL. NULL is accepted only for an
empty C range.

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
compile and fail the unchanged harness with the intended diagnostic. These are
finite tests, not formal proof.

## Dense-wrap fix and ARM code-size result

The original tuned path detected wrapping vectorially and then called a scalar
helper for each exceptional eight-star block. In the all-wrap case, every block
therefore paid vector work plus fallback overhead before doing the required RNG
work. The current implementation recognizes an all-eight, both-axes wrap mask
and continues as one ordered dense scalar run until that condition ends.

That removes the former all-wrap regression in the measured ARM workload: the
current tuned Zig result is effectively tied with previous Zig, while tuned C is
about 6% faster than original C in the same run. No-wrap remains a clear win for
the tuned paths. Mixed-wrap remains a visible weakness: current tuned Zig is
roughly 8% slower than previous Zig on the ARM run, so it is not silently promoted
as a universal replacement.

The ARM code-size problem had a separate cause. Disassembly showed Zig's internal
scalar helper at roughly 1 KiB versus about 224 bytes for C because LLVM unrolled
and specialized the callback-heavy loop around its count=8 call sites. Merely
changing slices to pointer+count and marking the helper cold did not stop it.
Making the scalar fallback an ordinary exported ABI function did.

Current ReleaseFast host-native symbol totals for star motion, counting each
unique entry/helper exactly once:

| Target | C | Zig | Zig/C |
| --- | ---: | ---: | ---: |
| x86-64 | 676 B | **664 B** | 0.98x |
| ARM64 | **800 B** | 836 B | 1.05x |

The earlier ARM result was **604 B C versus 1,388 B Zig** before the dense helper
was added and before the specialization fix. The current comparison includes the
new dense helper in both languages, so the more relevant result is 800 B versus
836 B. We did not use assembly, fake volatility, manually duplicated loops or an
ISA-specific implementation to get there.

Representative current ARM timings from the same implementation family:

| Workload | C tuned | Zig tuned | Note |
| --- | ---: | ---: | --- |
| 16,384 no-wrap stars | 6.23 us | **5.98 us** | Zig ~4% faster |
| 16,384 mixed-wrap stars | **14.85 us** | 14.92 us | essentially tied C/Zig; previous Zig remains faster |
| 4,096 all-wrap stars | 17.26 us | 17.30 us | effectively tied; regression removed |

These are shared-runner observations, not portable equivalence claims. Exact
paired ratios, MAD, ranges, every sample and disassembly are retained in the CI
artifacts.

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

The first candidates are retained in Git at `8c2488a`. `f71e7be` added bounded
vector tails and outlined exceptional star work. A later density-aware pass
removed the large all-wrap fallback-call tax. Inspection of the ARM artifact then
showed that the remaining 1,388-byte Zig star body was overwhelmingly one
compiler-unrolled scalar helper. A truthful `@branchHint(.cold)` did not change
its machine code and was removed. The current exported scalar fallback blocks
that internal specialization and reduces the ARM Zig star implementation to
within 36 bytes of C while preserving the same tests and measured behavior.
