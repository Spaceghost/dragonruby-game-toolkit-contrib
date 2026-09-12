# Parallel native samples with measured C, Zig and Odin alternatives

This suite extends `11_zig_native_pixel_arrays`. Original examples stay unchanged as references.
Portable native work now has readable C, Zig and Odin implementations where comparison is useful,
with differential tests before timing and explicit retention of losing candidates.

**Scope:** compute kernels, traversal/worker primitives, persistent starfield state and cached SQLite
output are portable. Platform SDK adapters remain platform code. [COVERAGE.json](COVERAGE.json)
accounts for the tracked native sources; [EVIDENCE.md](EVIDENCE.md) defines proof limits; and
[PERFORMANCE_ROADMAP.md](PERFORMANCE_ROADMAP.md) records the remaining application/platform gates.
Finite differential/mutation tests are not formal proof.

## Current language competition

The matched rivals workflow uses pinned Zig 0.16.0 and pinned Odin `dev-2026-09` plus optimized C.
It runs on hosted Linux x86-64 baseline/native and ARM64 native and retains raw timing/allocation
records, source/object hashes, symbol sizes and disassembly.

Odin is not a transliteration-only exhibit. It uses Odin SIMD vectors, multipointers, `#no_alias`,
contextless hot helpers and the language's unaligned SIMD load/store intrinsics. C uses Clang's
target-independent vector extensions; Zig uses native vector types and compile-time specialization.
No implementation uses handwritten assembly or fast-math to manufacture a win.

The shared correctness corpus now covers C/Zig/Odin newline and star competitors, including protected
memory, alignments, exact float/RNG state, all eight-star wrap masks, and compiled source mutants for
all three languages. Odin also exposes parallel portable ABIs for checked square/batches, strict
ordered sums, scalar/SoA stars, scanner state, nested traversal, greeting and worker lifecycle.
The same C app tests are compiled against Zig and Odin symbols for traversal, greeting and pthread
lifecycle behavior.

## Persistent starfield

Zig and Odin implement the same caller-owned persistent starfield layout and deterministic xorshift64*
state. Both provide update, full pack, coordinate-only pack and one borrowed batch-sink frame call.
Owned starfields specialize their known RNG path while the generic exported star ABI keeps an arbitrary
callback. The paired starfield benchmark runs both languages in the same process with identical seeds,
iterations and sink semantics at 64 / 1,024 / 16,384 / 100,000 stars.

The public sample uses one native starfield object rather than thousands of Ruby `Star` objects. It
still uses the supported DragonRuby `draw_sprite` mechanism internally because no public native batch
renderer ABI is assumed. Actual matching proprietary SDK loading and GPU renderer validation remain
release gates.

## SQLite and `query_json`

SQLite remains the upstream C engine. C, Zig and Odin implement matching cached prepared-statement
and packed first-column output policies. Tests cover NULL, empty/binary data, errors, reset/finalize,
failed prepare preserving a usable cache and allocator lifecycle. Warmed cached variants are compared
at 1 / 64 / 1,024 rows; a separate working-set experiment compares single-entry and fixed-four caches
with 1 / 4 / 16 SQL strings.

The Ruby adapter constructs real mruby arrays/strings only after the native packed result is complete;
Ruby allocation/result-construction time is measured separately from SQLite timing. Architectural
cache savings are credited to the cache policy, not to whichever language happens to implement it.

## Nested values and workers

`drbz_sum_tree` deliberately keeps mruby value layout out of Zig. The batched variant requests up to
16 decoded views per adapter call while preserving strict depth-first evaluation order. The benchmark
keeps direct C, single-reader Zig, batched Zig and Ruby traversal visible so ABI independence has a
measured cost instead of a marketing adjective. Odin implements the same single/batched host-reader ABI.

Workers use explicit owner-thread lifecycle and acquire/release atomics. Native thread callbacks never
enter Ruby. The remaining SDL adapter should replace polling delay with interruptible wait/wake before
this migration is considered complete.

## Newline kernels and dispatch

No single newline kernel is forced to win every size. The competition records scalar/previous/tuned
C/Zig/Odin implementations, while an exhaustive sweep measures every length 0..512 at offsets 0/1/15.
Odin's medium path accumulates up to 255 32-byte hit masks per u8 vector, widens to 32 u16 lanes once,
and reduces once. Larger 4 / 8 / 16 / 64 KiB and 1 MiB cases keep crossover behavior visible.
A dispatcher threshold is installed only when runner evidence shows a stable winning region.

## Compiler/profile experiments

C compiler comparison is separate from language comparison. Identical C sources are built with host
Clang and `zig cc` across optimization profiles and hosted Linux, Windows and macOS targets. Odin has a
separate pinned profile experiment for `minimal`, `size`, `speed` and `aggressive`, recording compiler
and link time, object/text/symbol size and runtime through the unchanged rivals harness.

## Ruby adapter

`bridge/bridge.c` remains a shared host adapter. That is intentional: duplicating mruby argument parsing,
Ruby string/array construction and SDK registration three times would compare boilerplate rather than
native languages. Native C/Zig/Odin cores are compared below that common boundary; Ruby and renderer
costs are measured as their own stages.

Build/test recipes continue to use the pinned workflow definitions under `.github/workflows/`. For a
matching proprietary DragonRuby SDK, build the bridge with its real headers and stage the shared library
under the game platform's `native/<platform>/` directory. Public mruby test hosts do not establish
proprietary host-table or GPU-renderer compatibility.

## What remains before PR #2 can leave draft

- Matching proprietary DragonRuby SDK compilation, extension loading and real renderer execution.
- Whole-application allocation/render measurements beyond scoped native/SQLite/mruby hosts.
- Interruptible SDL worker adapter and direct new-suite Windows/macOS execution.
- Apple framework/main-thread, Steamworks and Android JNI validation on real target environments.
- Any dispatcher/cache/profile promotion still must survive the retained runner evidence rather than
  being inferred from a single hosted-machine win.
