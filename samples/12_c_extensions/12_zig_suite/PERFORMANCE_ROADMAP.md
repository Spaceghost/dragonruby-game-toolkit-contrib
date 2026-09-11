# Benchmark-driven C / Zig / Odin migration roadmap

PR #2 stays draft while platform and real DragonRuby renderer boundaries remain incomplete.
Every optimization keeps a readable reference path, measures equal work, retains losses,
and is promoted only when correctness, timing, allocation behavior and code size support it.
Shared-runner measurements are observations, not universal performance claims.

## Implemented and measured

### Persistent starfield

C, Zig and Odin share the same observable star-motion contract. Zig and Odin also have
persistent caller-owned starfields with the same storage layout, deterministic RNG state,
coordinate/full packing, and borrowed batch-sink contract. Their owned starfields specialize
the known RNG path without changing the generic callback ABI.

Evidence separates update, full pack, coordinate-only pack and sink consumption at
64 / 1,024 / 16,384 / 100,000 stars. Packing strategy remains an explicit candidate because
coordinate-only packing wins on ARM but not consistently on x86. The public DragonRuby sample
uses one native starfield object rather than one Ruby object per star; real proprietary GPU
renderer validation remains a release gate.

### Cached query output

C, Zig and Odin implement the same cached SQLite statement and packed first-column result
contract. Warmed cached variants have matching allocator behavior and are benchmarked at
1 / 64 / 1,024 rows. A fixed four-entry cache is also compared with a single-entry cache at
working sets of 1 / 4 / 16 SQL texts.

The four-entry cache is promoted only for fitting working sets: at four alternating statements
it removes the measured prepare/allocation traffic and is several times faster, while at 1 or
16 statements it adds no useful architectural win. This is a cache-policy result, not a
language-only speedup.

### Nested Ruby values

The host adapter compares direct C traversal, Zig one-value reader, Zig 16-value batched reader,
and Ruby traversal while retaining strict depth-first floating-point order, fixed stack storage,
cycle/depth rejection and transactional output. Odin implements the same single/batched C ABI
and runs the same C traversal/greeting/worker tests through symbol remapping.

### Newline kernels

Short inputs are swept exhaustively over lengths 0..512 at offsets 0/1/15 before any dispatcher
threshold is installed. Larger C, Zig and Odin paths remain separate readable SIMD expressions.
Odin's medium path accumulates up to 255 32-byte masks in u8 lanes, widens once to a 32-lane
u16 vector and reduces once, avoiding both per-block horizontal reduction and target intrinsics.
4 / 8 / 16 / 64 KiB and 1 MiB cases keep the crossover honest.

### Compiler/toolchain evidence

C compiler experiments remain distinct from language comparisons: identical C sources are built
with host Clang and zig cc across multiple optimization profiles and hosted Linux, Windows and
macOS targets. Odin is pinned separately and has its own minimal / size / speed / aggressive
profile experiment so compiler-profile choices cannot masquerade as language results.

## Current implementation rules

- No handwritten assembly or per-ISA intrinsic forests unless ordinary language/vector source hits
  a demonstrated wall worth the maintenance cost.
- No fast-math reassociation for results whose ordering is part of the contract.
- `restrict` / `noalias` are used only where the public ABI already requires disjoint storage.
- Generic callback ABIs remain available; owned objects may specialize known callbacks internally.
- Timing and allocation instrumentation stay separate.
- C, Zig and Odin get the same algorithmic opportunities before language comparisons are claimed.
- Source mutation tests must compile and fail for the intended reason; a mutation that preserves
  behavior is replaced rather than counted as evidence.

## Remaining application/platform work

1. Validate the one-object starfield against a matching proprietary DragonRuby SDK and real renderer.
2. Keep Ruby `query_json` allocation/result-construction evidence alongside native cache evidence;
   explore result-capacity hints only if the real SDK exposes the needed mruby API.
3. Extend SQLite cache/output experiments only where real working sets justify more policy.
4. Keep the batched nested-value reader and short-LF dispatcher candidates benchmark-driven.
5. Replace polling worker sleep with an interruptible SDL wait/wake adapter.
6. Execute the new suite directly on Windows and macOS, not merely through compiler-only matrices.
7. Validate Apple main-thread/framework, Steamworks and Android JNI boundaries in their real targets.
8. Run whole-application allocation/render profiling with the matching DragonRuby SDK.

The PR does not leave draft until the platform and renderer claims are backed by the actual
relevant environments rather than inferred from portable test hosts.
