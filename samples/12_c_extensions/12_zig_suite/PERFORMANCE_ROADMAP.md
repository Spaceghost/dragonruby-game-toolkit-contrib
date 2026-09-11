# Benchmark-driven migration roadmap

PR #2 stays draft while these application and platform boundaries are incomplete.
The rule for every optimization is simple: preserve a readable reference path,
measure equal work, retain losses, and promote only when correctness, timing,
allocation behavior and code size support the change on the relevant targets.
Shared-runner measurements are observations, not universal performance claims.

## 1. Persistent starfield to the real renderer boundary

Implemented native shape:

```text
Ruby creates once
  -> persistent caller-owned x[] / y[] / speed[] / packed sprite records
  -> Zig SIMD update
  -> optional pack/render adapter
  -> one Ruby starfield draw dispatch
```

The current test sink deliberately consumes every packed record. It proves the
native lifecycle and locates CPU cost, but it is not the proprietary DragonRuby
renderer. The supported sample API exposes per-sprite `draw_sprite`, so the
portable intermediate adapter uses one Ruby starfield object and performs the
native update before issuing renderer calls. It does not invent an unavailable
native batch-render API.

Current source-level experiments, each independently measured:

- C `restrict` / Zig `noalias` for the already-documented disjoint SoA storage.
- Hoist invariant width/height/path fields to initialization; hot packing writes
  coordinates only.
- Skip packing entirely in the per-sprite renderer adapter and draw from x/y SoA.
- Consider 32/64-byte caller-storage alignment only after aligned/misaligned
  measurements show a repeatable gain.

Required evidence remains 64 / 1,024 / 16,384 / 100,000 stars, separate update,
pack and sink timings, allocation-symbol audit, exact state/checksums, and a
matching proprietary SDK renderer execution before calling this end-to-end.

Promotion gate: one native starfield object per Ruby starfield, no per-star Ruby
state/data-object round trip, and no hidden packing or allocation cost that erases
the measured kernel win.

## 2. Cached Ruby `query_json`

Original sample semantics are the reference: first column only, one Ruby string
per row, SQL NULL represented by the Ruby string `"null"`. The replacement also
preserves embedded NUL result bytes by using length-aware strings instead of the
original C-string truncation.

Current architecture:

```text
Ruby query_json(sql)
  -> cached prepared SQLite statement keyed by exact SQL bytes
  -> step rows directly
  -> reusable native packed row buffer
  -> reset statement
  -> construct Ruby array/strings after SQLite borrowed values expire
```

Evidence includes native 1 / 64 / 1,024-row C-prepare-each / C-cached /
Zig-cached comparisons, separate SQLite allocation profiling, and real pinned
mruby cache-hit timings/allocation records for all six VM flavor/boxing builds.

Next source-level target: if the matching DragonRuby host table exposes
`mrb_ary_new_capa`, benchmark pre-sizing the result array from the already-known
row count. Do not extend the production host ABI merely because upstream mruby
has the function; proprietary SDK validation decides whether it is available.

## 3. SQLite gains above SQLite, across languages

Do not credit Zig with work that optimized C can also avoid. Compare architecture
first, language second.

Next experiments:

1. One-statement cache versus small fixed/LRU caches with working sets of 1, 4
   and 16 SQL texts.
2. Packed-row buffer versus direct row consumer versus direct Ruby construction,
   keeping SQLite borrowed-column lifetime rules explicit.
3. Prepare/reprepare behavior across schema changes.
4. Transaction-batched writes versus individual autocommit operations.
5. C and Zig implementations receive matching cache/output policies.

Report preparation, execution, result construction, Ruby allocation and SQLite
allocator traffic separately. No combined number is allowed to masquerade as a
language-only speedup.

## 4. Batch the nested-value reader

`drbz_sum_tree` intentionally avoids depending on mruby value layout. The new
candidate decodes up to sixteen adjacent values per C/Zig callback while retaining
a fixed traversal stack, strict evaluation order, cycle/depth rejection and
transactional output. A nested descent discards and later re-decodes trailing
siblings instead of retaining Ruby views in a large pending buffer.

The real-mruby benchmark pits four implementations against each other:

- Ruby traversal;
- direct C traversal that depends on mruby's value layout;
- Zig with one-value reader callbacks;
- Zig with sixteen-value reader callbacks.

Flat and nested 64-value workloads are measured separately. The direct-C result
quantifies the cost of keeping Zig ABI-independent rather than pretending that
abstraction is free.

## 5. Short newline dispatch

Keep simple short and long kernels rather than forcing one implementation to win
all lengths. Sweep every length through 512 bytes, representative larger vector
boundaries, and pointer offsets on x86 baseline/native, ARM native, Windows and
both macOS runners. Choose a threshold only if a stable winning region exists.

The preferred final shape is two readable microkernels plus one tiny length
branch, not an ISA-intrinsic thicket.

## 6. Compiler/profile partitioning

The Clang/zig-cc matrix shows there is no universal best `-O` profile. Some
workloads run fastest at `-Oz` or `-O2`, and the winner changes by architecture.
This creates a useful source-level option: compile independent kernel translation
units with the compiler/profile that repeatedly wins on that target, while
retaining one common source implementation.

Promotion requires repeated cross-runner stability, equal CPU target semantics,
and code-size/build-time reporting. A per-kernel profile win is acceptable; a
single cherry-picked shared-runner sample is not.

For broad-distribution binaries, also test baseline/native multiversioning with
one startup dispatch. Keep it only when the runtime gain justifies duplicated
text size.

## 7. C star code-size follow-up

Sparse mixed-wrap repair made both implementations faster, but C machine code
expanded more than Zig on ARM. The current experiment outlines C mixed repair
using pointers to already-computed vector results/masks so baseline x86 does not
change the vector-argument ABI. Keep it only if size falls without a material
mixed-wrap regression.

## 8. Platform adapters

After the application-level work above:

- SDL worker: interruptible wait/wake rather than polling sleep.
- Windows/macOS execution for the new suite.
- Apple main-thread/framework adapter.
- Steamworks C++ boundary.
- Android JNI lifetime/exception cleanup.
- Matching proprietary DragonRuby SDK compilation/loading and real renderer run.

The PR does not leave draft until platform/renderer claims are backed by the
actual relevant environments rather than inferred from portable test hosts.
