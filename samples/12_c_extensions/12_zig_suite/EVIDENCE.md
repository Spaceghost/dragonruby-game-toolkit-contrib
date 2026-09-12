# Executable evidence and its limits

Tests establish observed behavior for their inputs and builds. They are not
formal verification, a proof over all possible inputs, or proprietary
DragonRuby engine validation. The original C examples remain unchanged.

## Claims and witnesses

| Claim | Executed witness | Limit |
| --- | --- | --- |
| Shipped tiny-regex behavior matches C on the test domain | `tests/parity.c` compiles the original C directly; `evidence/regex_long.c` adds long inputs and compares both match position and match length | The short exhaustive corpus is only four ASCII symbols through length six. Neither corpus covers every possible pattern/input. |
| SIMD literal search handles block/tail boundaries | Long tests place matches and false candidates around 16-byte boundaries at 32 offsets, plus read-only guarded-page tests | Finite fixtures, not a universal memory-safety proof. The C oracle receives a separate terminated copy; Zig receives exactly the advertised byte length. |
| Binary-input extension is consistent | Long tests compare scalar and optimized Zig on arbitrary bytes, including NUL | The original NUL-terminated C API is not a binary-data oracle. These comparisons are labeled separately. |
| Tested kernels make no calls to the six observed allocation APIs | `evidence/allocations.c` counts malloc, calloc, realloc, aligned_alloc, posix_memalign and free references while executing native kernels and regex compilation/search | GNU link-reference wrapping only. Not shared-library internals, direct mappings/syscalls, every allocator API, the Ruby VM, SQLite, worker creation, or rendering. |
| Allocation instrumentation actually detects allocations | Positive calibration calls every observed allocator and frees four allocations; a compiled source mutant adds an allocation/free to the newline function | Calibration is mandatory. The instrumented C probe is non-timed; performance timings do not include it. |
| Output assertions detect incorrect implementations | `tools/mutations.py` changes first-match SIMD selection, the counted newline byte and star addition in isolated candidate copies | Four selected mutants, not an exhaustive mutation score. Builds must succeed and executables must return the intended diagnostic/exit code. A compiler error, timeout or unrelated crash fails the evidence job. |
| Reported measurements belong to a complete recorded run | `tools/measure.py` records source hashes and environment at execution; `tools/report.py` validates the digest, exit status, complete case/variant/trial sets and equal checksums | Hashes identify recorded material, not trusted signatures or protection against a maliciously forged entire log. Checksums alone are not equivalence proofs; differential tests run separately before native timing. |
| SQLite reuse comparisons separate lifecycle from wrapper overhead | C and Zig each have prepare-per-query and reuse variants; matched prepare_v3 flags, reset/clear-bindings policy and checked results | Same in-memory query and SQLite library, not disk latency, Ruby calls, database engine translation or a general speed guarantee. |

## Reproduce

From the suite directory, use Zig 0.16.0 and Python 3.10+:

```sh
zig build test -Doptimize=ReleaseSafe -Dcpu=baseline --summary all
python tools/test_report.py -v
python tools/measure.py kernels --cpu native --output bench.log
python tools/report.py bench.log
python tools/measure.py sqlite --cpu native --output sqlite.log
python tools/report.py sqlite.log
```

SQLite requires the host's development library. The allocation and mutation
probe additionally needs Linux, native system `cc` and a GNU-compatible linker:

```sh
python tools/mutations.py --cpu baseline
```

It runs the positive long-regex and allocation executables in Debug,
ReleaseSafe and ReleaseFast, then compiles and executes four ReleaseFast source
mutants. Temporary copies are deleted, original files are checked for changes,
and source/executable SHA-256 digests are printed with execution results.
`--skip-mutations` explicitly limits the run to positive evidence only.

The dedicated workflow uses standard Linux x86-64 baseline/native and ARM64
native hosted runners. The candidate library is always compiled by pinned
Zig. Only the non-timed allocator probe uses system cc for GNU `--wrap`, which
Zig 0.16's cc driver rejects. Compiler optimization can otherwise hide private
probe state because wrapping happens at link time: counters and the armed
flag are volatile, and calibration verifies the expected 1/1/1/1/1/4 calls.
The probe is deliberately single-threaded; volatile is not a concurrency fix.

## Reading results

`REGEX_LONG_EVIDENCE` prints counters incremented by completed comparisons;
protected comparisons are a subset of the C comparison count. On a 4096-byte
page the current fixtures produce 62,724 C comparisons, of which 16,388 use
protected inputs, plus 32,896 binary scalar/optimized comparisons. Page size
can change guarded counts. The original 480,568 short comparisons remain.

`ALLOCATION_EVIDENCE` reports observed calls during 1,000 repetitions of the
specified kernel exercise. `ALLOCATOR_CALIBRATION` prints observations before
checking them. The injected allocation must trigger `ALLOCATION_MISMATCH`,
not silently survive optimization or fail at compile time.

For the default 11 trials, `BENCH_COMPLETENESS` must report 396 native-kernel
records (12 cases, 36 variants) and 44 SQLite records (one case, four variants).
The reporter rejects missing entire variants/cases, duplicate/absent trials,
invalid times/counts, cross-variant output differences, modified output,
failed execution, changed sources and missing capture provenance. Unit tests
feed it deliberately invalid evidence and require rejection.

SQLite's `zig_reuse` is compared to `c_reuse`, not to C reparsing each query.
The C prepare-per-query control now uses prepare_v3 and type/error checks to
match the wrapper. Historical pre-change benchmark tables must remain labeled
with their original commit and workload. New ratios must come from one named
run; do not combine different runner CPUs or manufacture a universal claim.

Timing records still have `allocation_count: null`: no allocator instrumentation
runs inside those timing regions. Recorded metadata travels with the saved log,
so reporting it on another computer does not relabel the measurement CPU.
No artifact upload, private credentials or proprietary SDK is required.

## Primary API references

- [GNU ld --wrap](https://sourceware.org/binutils/docs/ld.html): wrapping applies
  to undefined references, not all internal calls or allocation mechanisms.
- [SQLite reset](https://www.sqlite.org/c3ref/reset.html): reset does not clear
  bindings, and its return code must be checked.
- [SQLite finalize](https://www.sqlite.org/c3ref/finalize.html): finalization
  destroys statements and can report the preceding execution error.
- [Zig 0.16 language reference](https://ziglang.org/documentation/0.16.0/):
  optimization modes have different safety behavior. ReleaseFast success alone
  is not evidence that runtime safety checks executed.

Real SDK library loading, the renderer, batched Ruby star drawing, SDL/Ruby
worker ownership, Ruby SQLite query_json, mobile/Steam/Apple integration and
new-suite Windows/macOS execution remain separate work. Nothing in the native
proof markers substitutes for those checks.
