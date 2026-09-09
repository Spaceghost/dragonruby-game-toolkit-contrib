# Direct SQLite operations from Zig

Import `direct.zig` and call `sql.c.sqlite3_*` for the actual SQLite C API.
`@cImport` uses the installed sqlite3.h instead of maintaining duplicate FFI
signatures or routing each operation through an exported `drbz_sql_*` function.
Link the same SQLite library that supplied those headers.

The former per-operation state machine is retained byte-for-byte in
`compat.zig` for existing C callers and honest before/after controls. It is
not called by `direct.zig` or `series.zig`. `sqlite.zig` still exports that C ABI
and also exposes the new Zig-native API.

## Reusable batches, without a callback table

```zig
const sql = @import("direct.zig");
const c = sql.c;

const IgnoreRows = struct {
    pub fn row(_: *@This(), _: *c.sqlite3_stmt) c_int {
        return c.SQLITE_OK;
    }
};

// stmt is already prepared as INSERT INTO scores(score) VALUES (?1).
// Its owner prepares/finalizes it and serializes connection/statement use.
fn insertScores(stmt: *c.sqlite3_stmt, scores: []const i64) sql.BatchResult {
    var source: sql.IntSlice = .{ .values = scores };
    var sink: IgnoreRows = .{};
    return sql.runIntBatch(stmt, &source, &sink, false);
}
```

The input source and row consumer are specialized at compile time; their methods
are inlined. Inside the loop: bind, step, consume rows, reset. There are no
per-operation exported wrappers, repeated owner-pointer lookups, intermediate
text-view objects, or stored wrapper state transitions. Busy-state and parameter
arity are checked once at batch entry. This API deliberately supports one integer
parameter. Use `c.sqlite3_*` directly for other binding shapes.

With `clear_bindings=false`, every execution rebinds the one parameter, and the
last integer binding remains on the statement. With `true`, every execution also
clears bindings. Both policies check bind, step, reset and applicable clear results.
The false policy is not safe to generalize to partially rebound multi-parameter
statements or retained borrowed strings.

Batch results retain the primary error, reset error, clear error and number of
fully completed executions. Row/step failure still resets and clears as requested.
Borrowed SQLite diagnostic text may change during cleanup; codes, not copied
error messages, are the contract here. Batches are not transactions: preceding
writes remain applied on failure unless the caller uses an explicit transaction.
Column pointers must be consumed before the next step/reset; no Ruby exception
may unwind through the native loop. Callback work and SQLite allocations remain
outside any no-allocation claim about the loop itself.

## Checks and measurements

`zig build test` in this directory runs the unchanged C-ABI tests plus six Zig
test groups covering both binding policies, multiple rows, constraint failure and
recovery, partial progress, empty input, busy statements, wrong arity, NULL/empty/
binary columns, consumer rejection, and isolated bind/step/reset/clear failures.
The injected API failures are explicitly separate from actual-engine tests.

The measurement executables additionally make **72 C/Zig comparisons**, each
executing both implementations: 6 lengths x 3 seeds x 2 reuse modes x 2 clearing
policies = **144 successful batch calls**, not 144 distinct comparisons. These
include 32-bit parameter-generator wrap. Six additional failing SQL/arity/result
cases each execute both implementations, check matching errors and unchanged
output, and verify that no statements remain. All of these run before timing.

`measure/sqlite_direct.c` is an independent control, not the original sample.
Both direct implementations use the same SQLite calls, checks, result consumption,
parameter sequence, prepare flags, reset and binding policy. Both have a single
C entry point per batch and are separately compiled with the same target/mode.

New measurements preserve the old controls and add:

- `sqlite/direct-reuse`: direct C, direct Zig, and the old checked Zig adapter,
  all reusing a statement and clearing bindings.
- `sqlite/direct-rebind`: direct C versus direct Zig, both deliberately retaining
  the fully overwritten integer binding.
- `sqlite/direct-prepare-each`: direct C versus direct Zig, both preparing,
  executing, resetting and finalizing each statement.

Timing and allocator profiling remain separate. The existing runner uses equal
work, calibrated batches, shuffled variants, three processes and 11 trials.
All raw data and losses are retained. No fixed speed ratio gates CI.

`tools/check_direct_calls.py` inspects the compiled direct-batch archive, requires
actual sqlite3_* imports, and rejects drbz_sql_*, mruby and allocation-probe wrapper
symbols. Four deliberate bad symbol-list controls verify that the audit rejects
its intended failures. The installed audit includes the archive's SHA-256. This
is structural evidence, not a promise of universal performance parity.

## Recorded result, not a portable speed guarantee

Source: `3640916d9f56aa81c790a2713af5531a14121e5c`; tested merge commit:
`d539dd24788446bdbab0f9f1368135724d63fa24`. Zig 0.16.0 ReleaseFast,
host-native CPU, SQLite 3.45.1, no LTO, no fast math. Each timing has 33 samples
from 3 processes x 11 trials. No outliers were removed. These are batch-mean
native query costs, not Ruby latency or GPU FPS.

| Reuse with binding cleanup | Direct C ns/query | Direct Zig ns/query | Old checked Zig ns/query |
| --- | ---: | ---: | ---: |
| EPYC 9V74 x86-64 | 201.309 | 204.035 | 212.401 |
| Neoverse N2 ARM64 | 257.497 | 256.454 | 267.642 |

The direct path reduces median time relative to the old wrapper by about 4%
on these runs. It is still about 1.35% behind C on x86, and within about 0.4%
on ARM. These differences are observations, not claims of statistical equivalence.
Matched direct rebind medians were 188.349/189.610 ns (C/Zig, x86) and
241.352/240.192 ns (C/Zig, ARM). Skipping binding cleanup is an explicitly
different policy, available to both languages, not a language-only speedup.

In the x86 general-allocator profile, each 64-query reuse batch, including
prepare/finalize, observed 16 allocations, 16 frees and 5,864 requested bytes
for direct C, direct Zig and the checked Zig control. Live engine bytes returned
to the pre-batch level and then zero at shutdown. SQLite lookaside was enabled;
these counters do not count every internal suballocation or process RSS.

[Execution and artifact records](https://github.com/Spaceghost/dragonruby-game-toolkit-contrib/actions/runs/34371374079).
Artifacts: x86-native `10112181775`, ARM-native `10112183026`.
Raw JSONL SHA-256:

```text
x86: 59141736c6201a58fc01b8f7d4be5aa1c0c328b662a344c689ac2aed51b94d02
ARM: 8989f893fe678fa40179f23e254fe829b854dae0a3888391c85872351f75d2ea
```

Reproduce from the suite directory:

```sh
(cd sqlite && zig build test -Doptimize=ReleaseSafe -Dcpu=baseline)
(cd measure && zig build -Doptimize=ReleaseFast -Dcpu=native)
python3 tools/benchmark_evidence.py --kind native --processes 3 --trials 11 \
  --min-ns 2000000 --output /tmp/direct-evidence
```

## Other boundaries

The Ruby/mruby C adapter remains necessary for value-layout handling and for
keeping Ruby allocation/raising outside active Zig frames. Worker host callbacks
remain the SDL/pthread boundary. The SoA RNG callback preserves the documented
sequence. None is removed merely for being named a wrapper. Native counter and
scanner helpers do not acquire another runtime dispatch layer in this change.

Actual DragonRuby SDK loading, GPU rendering and the remaining platform adapters
are not established by these host measurements. PR #2 remains draft.

Primary contracts: https://www.sqlite.org/c3ref/reset.html,
https://www.sqlite.org/c3ref/column_blob.html,
https://www.sqlite.org/c3ref/finalize.html,
https://ziglang.org/documentation/0.16.0/.
