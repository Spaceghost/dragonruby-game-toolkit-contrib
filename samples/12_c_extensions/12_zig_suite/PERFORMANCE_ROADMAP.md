# Benchmark-driven migration roadmap

PR #2 stays draft while these application and platform boundaries are incomplete.
The rule for every optimization is simple: preserve a readable reference path,
measure equal work, retain losses, and promote only when correctness, timing,
allocation behavior and code size support the change on the relevant targets.
Shared-runner measurements are observations, not universal performance claims.

## 1. Persistent starfield to one renderer submission

Implemented native shape:

```text
Ruby creates once
  -> persistent caller-owned x[] / y[] / speed[] / packed sprite records
  -> Zig SIMD update
  -> pack render records
  -> exactly one batch-sink call
```

The test sink deliberately consumes every packed record. It proves batching and
locates CPU cost, but it is not the proprietary DragonRuby renderer.

Required evidence:

- 64 / 1,024 / 16,384 / 100,000 stars.
- Separate `update`, `update+pack`, and `update+pack+batch sink` timings.
- Allocation-symbol audit for native implementation.
- Exact state/checksum validation before timing.
- Actual DragonRuby batch-render adapter when a matching SDK is available.

Promotion gate: one native starfield object per Ruby starfield, no per-star Ruby
round trip, one renderer submission per frame, and no regression relative to the
measured kernel path outside the documented packing/render costs.

## 2. Cached Ruby `query_json`

Original sample semantics are the reference: first column only, one Ruby string
per row, SQL NULL represented by the Ruby string `"null"`. The replacement also
preserves embedded NUL result bytes by using length-aware strings instead of the
original C-string truncation.

Current architecture:

```text
Ruby query_json(sql)
  -> one cached prepared SQLite statement keyed by exact SQL bytes
  -> step rows directly
  -> reusable native packed row buffer
  -> reset statement
  -> construct Ruby array/strings after SQLite borrowed values expire
```

Required evidence:

- Native 1 / 64 / 1,024-row matrix:
  `C prepare-each`, `C cached`, `Zig cached`.
- Separate SQLite allocator profile from timing.
- Ruby cache-hit 1 / 64 / 1,024-row timing through real pinned mruby.
- mruby allocator-call/requested-byte/high-water records for Ruby result creation.
- Failed prepare must not evict a usable cached statement.
- NULL, empty strings and embedded NUL bytes remain covered.

Promotion gate: repeated Ruby calls use the cached statement, errors/reset/finalize
remain checked, and the native cache gain survives enough Ruby object construction
to justify replacing the old adapter.

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

## 4. Compiled regex objects

The Ruby convenience method still compiles every call. Add a persistent compiled
regex object/cache and benchmark:

- compile + one search;
- compile once + one search;
- compile once + 100 searches;
- early, late, absent and dense-prefix inputs.

Specialize baseline/optimized matcher strategy at compile time rather than carry
a runtime `optimized` branch through recursive matching. Compile useful prefix
metadata only when evidence shows a win without turning tiny-regex into a much
larger engine.

## 5. Batch the nested-value reader

`drbz_sum_tree` intentionally avoids depending on mruby value layout, but today
it crosses the C/Zig callback boundary once per value. Test a small fixed batch
of decoded views (for example 16) while preserving traversal order, fixed stack,
cycle rejection and transactional output.

Benchmark flat 8/64/1K values plus shallow/deep nested structures. Record callback
count in addition to time so the causal mechanism is explicit.

## 6. Short newline dispatch

Keep simple short and long kernels rather than forcing one implementation to win
all lengths. Sweep every length through 512 bytes, representative larger vector
boundaries, and several pointer offsets on x86 baseline/native and ARM native.
Choose a threshold only if a stable winning region exists across repeated runs.

## 7. Compiler/toolchain matrix

Compile identical C workload sources with host Clang and `zig cc` under O2/O3/Oz
and optional ThinLTO. Run the original five hosted platforms plus Linux baseline:

- Linux x86-64 baseline/native;
- Linux ARM64 native;
- Windows x86-64 native;
- macOS ARM64 native;
- macOS x86-64 native.

The timing harness is precompiled with host Clang on Linux/macOS so candidate
compiler code generation cannot optimize or miscompile the timer itself. Windows
uses QPC in the candidate translation unit because the hosted Clang and zig-cc
object ABIs differ there. Equivalent original/tuned workloads execute identical
iteration counts. Zero/negative timer results are rejected.

Measure runtime, build time, executable/text size and correctness checksum. Zig's
own native implementation belongs in a parallel language comparison, not in a
claim about which C compiler won.

## 8. C star code-size follow-up

Sparse mixed-wrap repair made both implementations faster, but current C machine
code expanded more than Zig on ARM. Test an outlined C repair helper that receives
already-computed results/masks. Keep it only if the size reduction does not cause
a material mixed-wrap performance loss. No assembly or duplicated lane forest.

## 9. Platform adapters

After the application-level work above:

- SDL worker: interruptible wait/wake rather than polling sleep.
- Windows/macOS execution for the new suite.
- Apple main-thread/framework adapter.
- Steamworks C++ boundary.
- Android JNI lifetime/exception cleanup.
- Matching proprietary DragonRuby SDK compilation/loading and real renderer run.

The PR does not leave draft until the platform/renderer claims are backed by the
actual relevant environments rather than inferred from portable test hosts.
