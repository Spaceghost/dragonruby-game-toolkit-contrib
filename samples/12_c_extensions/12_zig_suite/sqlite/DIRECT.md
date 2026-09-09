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

The measurement executables additionally compare the direct C and Zig batch
entry points over 144 combinations of length, seed and lifecycle flags, including
32-bit parameter-generator wrap, plus six failing SQL/arity/result cases.
They check that failures preserve output and leave no outstanding statements.

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
