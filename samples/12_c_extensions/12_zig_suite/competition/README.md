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
  blocks keep the already-computed vector `nx/ny`, publish those results once,
  and repair only coordinates that actually wrapped. Zig turns the two vector
  masks into compact 8-bit masks and visits wrapped stars in ascending order;
  C receives the same sparse-repair idea with an ordinary ordered lane loop.
  Star order and x-before-y RNG calls are preserved.

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
visibility prevents that particular code-size explosion while leaving the source
ordinary. It is also a useful direct fallback for embeddings that require the
same RNG ordering.

Float slices must have equal lengths; distinct arrays must not overlap. The RNG
must use its own state, not mutate the arrays or raise/reenter Ruby. That callback
contract is what makes it valid for mixed blocks to publish clean vector results
before repairing wrapped coordinates. Binary LF counting supports arbitrary
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
compile and fail the unchanged harness with the intended diagnostic. These are
finite tests, not formal proof.

## Wrap-density fixes and machine-code results

The first tuned star path detected wrapping vectorially and then called a scalar
helper for each exceptional eight-star block. That created two separate problems:

1. all-wrap paid vector work plus one fallback call for every eight stars;
2. mixed-wrap threw away already-computed vector results and repeated scalar
   additions/comparisons inside the fallback.

The dense-wrap path now recognizes an all-eight, both-axes wrap mask and continues
as one ordered scalar run until that condition ends. The mixed path now stores
its vector results once and performs RNG repair only for coordinates that wrapped.
This removes both regressions without changing the tested RNG sequence.

### Current star timings

Latest matched run: source `51b7d207e6fe16687411d26564d479f0d1c6e049`,
tested merge `5d39f669f62f0597f33d96752a6a3aa674c9ef42`, Zig 0.16.0
ReleaseFast, host-native target, 3 fresh processes x 11 interleaved trials.
Absolute values are shared-runner observations; compare variants within a row/run.

| Target / workload | C tuned | Zig previous | Zig tuned | Tuned result |
| --- | ---: | ---: | ---: | --- |
| ARM64 / 16,384 no-wrap | **6.010 us** | 7.071 us | 6.048 us | C/Zig within ~1% |
| ARM64 / 16,384 mixed-wrap | 12.063 us | 13.797 us | **10.986 us** | Zig tuned ~9% faster than C, ~20% faster than previous Zig |
| ARM64 / 4,096 all-wrap | **16.925 us** | 17.245 us | 17.096 us | all three close; old tuned regression gone |
| x86-64 / 16,384 no-wrap | 3.573 us | 3.848 us | **3.480 us** | Zig tuned ~3% faster than C |
| x86-64 / 16,384 mixed-wrap | 15.848 us | 14.277 us | **10.482 us** | Zig tuned ~34% faster than C, ~27% faster than previous Zig |
| x86-64 / 4,096 all-wrap | 26.341 us | **26.299 us** | 26.337 us | effectively tied |

The former statement that mixed-wrap was an ~8% Zig regression is therefore
historical, not current. The sparse repair reversed it on both measured native
hosts. C also improved materially on ARM mixed-wrap after receiving the same
algorithmic idea: from roughly 14.8 us in the preceding run to 12.1 us here.
Different shared-runner hosts can change absolute timings, so causal claims use
current within-run controls and the direction is additionally checked on x86.

### Current star code size

Sparse repair costs code, so size remains part of the competition instead of
being ignored after the timing win. Current unique ReleaseFast star-motion code,
counting the entry and each helper exactly once:

| Target | C | Zig | Difference |
| --- | ---: | ---: | ---: |
| x86-64 | 863 B | **858 B** | Zig 5 B smaller |
| ARM64 | 1,420 B | **1,064 B** | Zig 356 B smaller |

Before sparse repair, the external-fallback change had reduced ARM Zig from the
pathological 1,388 B result to 836 B. Sparse mixed repair deliberately spends
another 228 B to remove the mixed-wrap call/recomputation tax, while still ending
well below the earlier 1,388 B result. C's straightforward ordered repair loop is
more aggressively expanded by Clang on this ARM target, so C currently pays a
larger size cost. That is recorded rather than massaged away.

We did not use assembly, fake volatility, manually duplicated eight-lane source,
ISA-specific intrinsics, fast math or LTO to obtain these results.

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
vector tails and outlined exceptional star work. A density-aware pass removed
the large all-wrap fallback-call tax. Inspection of the ARM artifact then showed
that the remaining 1,388-byte Zig star body was overwhelmingly one compiler-
unrolled scalar helper. A truthful `@branchHint(.cold)` did not change its
machine code and was removed. Exporting the scalar fallback stopped that internal
count=8 specialization. Finally, `3595932` replaced mixed-block fallback calls
with Zig's mask-driven sparse repair, and `51b7d20` gave C the same sparse-repair
algorithmic opportunity. The current measurements above are from that matched
C/Zig revision.