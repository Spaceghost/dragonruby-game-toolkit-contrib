//! Zig-native SQLite access. Import `c` for the complete header API.
//! No drbz_sql_* calls, runtime callback table, owned text-view wrapper, or
//! per-operation owner/state bookkeeping is used by this module.
const std = @import("std");
pub const c = @cImport({ @cInclude("sqlite3.h"); });

pub const BatchResult = struct {
    code: c_int = c.SQLITE_OK,
    reset_code: c_int = c.SQLITE_OK,
    clear_code: c_int = c.SQLITE_OK,
    completed: usize = 0,
};

pub const IntSlice = struct {
    values: []const i64,
    index: usize = 0,
    pub fn next(self: *IntSlice) ?i64 {
        if (self.index == self.values.len) return null;
        const value = self.values[self.index];
        self.index += 1;
        return value;
    }
};

/// Execute a prepared statement repeatedly, binding its sole parameter (?1).
/// The caller owns a live statement/connection and serializes their access.
/// source.next() -> ?i64 and sink.row(stmt) -> SQLite result code are resolved
/// at compile time and inlined, not called through C function pointers.
/// Consume borrowed columns inside row(); never retain them across step/reset.
/// Neither callback may raise, re-enter SQLite on this statement, or finalize it.
/// clear_bindings=false is valid because the one parameter is rebound each time.
/// Every execution still checks bind/step/reset; successful rows are not proof
/// that execution or reset succeeded. Partial DB changes are NOT rolled back:
/// use a transaction for atomic batches. completed counts fully finished runs.
/// Error codes survive cleanup, but SQLite's borrowed diagnostic text may not.
/// This function allocates no storage; SQLite and user callbacks may allocate.
pub fn runIntBatch(stmt: *c.sqlite3_stmt, source: anytype, sink: anytype, comptime clear_bindings: bool) BatchResult {
    return runWith(c, stmt, source, sink, clear_bindings);
}

fn runWith(comptime api: type, stmt: *c.sqlite3_stmt, source: anytype, sink: anytype, comptime clear_bindings: bool) BatchResult {
    if (api.sqlite3_stmt_busy(stmt) != 0) return .{ .code = c.SQLITE_MISUSE };
    if (api.sqlite3_bind_parameter_count(stmt) != 1) return .{ .code = c.SQLITE_RANGE };
    var result: BatchResult = .{};
    while (@call(.always_inline, @TypeOf(source.*).next, .{source})) |value| {
        var code = api.sqlite3_bind_int64(stmt, 1, value);
        if (code == c.SQLITE_OK) {
            while (true) {
                code = api.sqlite3_step(stmt);
                if (code == c.SQLITE_DONE) { code = c.SQLITE_OK; break; }
                if (code != c.SQLITE_ROW) break;
                code = @call(.always_inline, @TypeOf(sink.*).row, .{ sink, stmt });
                if (code != c.SQLITE_OK) break;
            }
        }
        result.reset_code = api.sqlite3_reset(stmt);
        result.clear_code = if (clear_bindings) api.sqlite3_clear_bindings(stmt) else c.SQLITE_OK;
        result.code = if (code != c.SQLITE_OK) code else if (result.reset_code != c.SQLITE_OK) result.reset_code else result.clear_code;
        if (result.code != c.SQLITE_OK) return result;
        result.completed += 1;
    }
    return result;
}

const t = std.testing;
fn openTest() !*c.sqlite3 {
    var db: ?*c.sqlite3 = null;
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_open(":memory:", &db));
    return db orelse error.NoDatabase;
}
fn prepareTest(db: *c.sqlite3, sql: [*:0]const u8) !*c.sqlite3_stmt {
    var stmt: ?*c.sqlite3_stmt = null;
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_prepare_v3(db, sql, -1, 0, &stmt, null));
    return stmt orelse error.NoStatement;
}
const SumSink = struct {
    total: i64 = 0,
    rows: usize = 0,
    pub fn row(self: *SumSink, stmt: *c.sqlite3_stmt) c_int {
        self.total += c.sqlite3_column_int64(stmt, 0);
        self.rows += 1;
        return c.SQLITE_OK;
    }
};

test "direct: multiple rows and both binding policies" {
    const db = try openTest(); defer t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_close(db)) catch @panic("close failed");
    const stmt = try prepareTest(db, "SELECT ?1 UNION ALL SELECT ?1+1");
    defer t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_finalize(stmt)) catch @panic("finalize failed");
    inline for (.{ false, true }) |clear| {
        var source: IntSlice = .{ .values = &.{ 1, 2, 3 } };
        var sink: SumSink = .{};
        const result = runIntBatch(stmt, &source, &sink, clear);
        try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
        try t.expectEqual(@as(usize, 3), result.completed);
        try t.expectEqual(@as(usize, 6), sink.rows);
        try t.expectEqual(@as(i64, 15), sink.total);
        try t.expectEqual(@as(c_int, 0), c.sqlite3_stmt_busy(stmt));
        try t.expectEqual(@as(c_int, c.SQLITE_ROW), c.sqlite3_step(stmt));
        try t.expectEqual(@as(c_int, if (clear) c.SQLITE_NULL else c.SQLITE_INTEGER), c.sqlite3_column_type(stmt, 0));
        if (!clear) try t.expectEqual(@as(i64, 3), c.sqlite3_column_int64(stmt, 0));
        try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_reset(stmt));
    }
}

test "direct: constraint failure resets statement and preserves partial progress" {
    const db = try openTest(); defer _ = c.sqlite3_close(db);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_exec(db, "CREATE TABLE t(v UNIQUE)", null, null, null));
    const stmt = try prepareTest(db, "INSERT INTO t VALUES(?1)"); defer _ = c.sqlite3_finalize(stmt);
    var source: IntSlice = .{ .values = &.{ 7, 7, 8 } };
    var sink: SumSink = .{};
    const result = runIntBatch(stmt, &source, &sink, true);
    try t.expectEqual(@as(c_int, c.SQLITE_CONSTRAINT), result.code);
    try t.expectEqual(@as(c_int, c.SQLITE_CONSTRAINT), result.reset_code);
    try t.expectEqual(@as(usize, 1), result.completed);
    try t.expectEqual(@as(usize, 2), source.index);
    try t.expectEqual(@as(c_int, 0), c.sqlite3_stmt_busy(stmt));
    source = .{ .values = &.{8} };
    try t.expectEqual(@as(c_int, c.SQLITE_OK), runIntBatch(stmt, &source, &sink, true).code);
    try t.expectEqual(@as(c_int, 2), c.sqlite3_total_changes(db));
}

test "direct: empty input, invalid arity and busy statements" {
    const db = try openTest(); defer _ = c.sqlite3_close(db);
    const stmt = try prepareTest(db, "SELECT ?1"); defer _ = c.sqlite3_finalize(stmt);
    var source: IntSlice = .{ .values = &.{} }; var sink: SumSink = .{};
    try t.expectEqual(@as(usize, 0), runIntBatch(stmt, &source, &sink, true).completed);
    try t.expectEqual(@as(c_int, c.SQLITE_ROW), c.sqlite3_step(stmt));
    source = .{ .values = &.{1} };
    try t.expectEqual(@as(c_int, c.SQLITE_MISUSE), runIntBatch(stmt, &source, &sink, true).code);
    try t.expectEqual(@as(usize, 0), source.index);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_reset(stmt));
    const wrong = try prepareTest(db, "SELECT ?1, ?2"); defer _ = c.sqlite3_finalize(wrong);
    try t.expectEqual(@as(c_int, c.SQLITE_RANGE), runIntBatch(wrong, &source, &sink, true).code);
    try t.expectEqual(@as(usize, 0), source.index);
}

test "direct: NULL, empty and embedded NUL text consumed before reset" {
    const db = try openTest(); defer _ = c.sqlite3_close(db);
    const stmt = try prepareTest(db, "SELECT NULL WHERE ?1=1 UNION ALL SELECT '' UNION ALL SELECT CAST(x'610062' AS TEXT)");
    defer _ = c.sqlite3_finalize(stmt);
    const Sink = struct {
        rows: usize = 0, nulls: usize = 0, empty: usize = 0, bytes: [3]u8 = @splat(0),
        pub fn row(self: *@This(), s: *c.sqlite3_stmt) c_int {
            self.rows += 1;
            if (c.sqlite3_column_type(s, 0) == c.SQLITE_NULL) { self.nulls += 1; return c.SQLITE_OK; }
            const p = c.sqlite3_column_text(s, 0);
            if (p == null) return c.SQLITE_NOMEM;
            const n = c.sqlite3_column_bytes(s, 0);
            if (n == 0) self.empty += 1 else if (n == 3) @memcpy(&self.bytes, p[0..3]) else return c.SQLITE_MISMATCH;
            return c.SQLITE_OK;
        }
    };
    var source: IntSlice = .{ .values = &.{1} }; var sink: Sink = .{};
    try t.expectEqual(@as(c_int, c.SQLITE_OK), runIntBatch(stmt, &source, &sink, true).code);
    try t.expectEqual(@as(usize, 3), sink.rows); try t.expectEqual(@as(usize, 1), sink.nulls); try t.expectEqual(@as(usize, 1), sink.empty);
    try t.expectEqualSlices(u8, "a\x00b", &sink.bytes);
}

test "direct: row consumer failure resets and can be retried" {
    const db = try openTest(); defer _ = c.sqlite3_close(db);
    const stmt = try prepareTest(db, "SELECT ?1 UNION ALL SELECT ?1+1"); defer _ = c.sqlite3_finalize(stmt);
    const Reject = struct { pub fn row(_: *@This(), _: *c.sqlite3_stmt) c_int { return c.SQLITE_ABORT; } };
    var source: IntSlice = .{ .values = &.{1} }; var reject: Reject = .{};
    const result = runIntBatch(stmt, &source, &reject, true);
    try t.expectEqual(@as(c_int, c.SQLITE_ABORT), result.code); try t.expectEqual(@as(usize, 0), result.completed);
    try t.expectEqual(@as(c_int, 0), c.sqlite3_stmt_busy(stmt));
    source.index = 0; var sink: SumSink = .{};
    try t.expectEqual(@as(c_int, c.SQLITE_OK), runIntBatch(stmt, &source, &sink, true).code);
    try t.expectEqual(@as(i64, 3), sink.total);
}

// Isolated API failure controls verify cleanup error precedence. They are not
// labeled as real-engine failures; the tests above execute actual SQLite.
const FaultApi = struct {
    var bind_code: c_int = 0; var step_code: c_int = c.SQLITE_DONE;
    var reset_code: c_int = 0; var clear_code: c_int = 0;
    var steps: usize = 0; var resets: usize = 0; var clears: usize = 0;
    fn sqlite3_stmt_busy(_: *c.sqlite3_stmt) c_int { return 0; }
    fn sqlite3_bind_parameter_count(_: *c.sqlite3_stmt) c_int { return 1; }
    fn sqlite3_bind_int64(_: *c.sqlite3_stmt, _: c_int, _: i64) c_int { return bind_code; }
    fn sqlite3_step(_: *c.sqlite3_stmt) c_int { steps += 1; return step_code; }
    fn sqlite3_reset(_: *c.sqlite3_stmt) c_int { resets += 1; return reset_code; }
    fn sqlite3_clear_bindings(_: *c.sqlite3_stmt) c_int { clears += 1; return clear_code; }
};
test "direct: bind, step, reset and clear failures are never swallowed" {
    var dummy: u8 = 0; const stmt: *c.sqlite3_stmt = @ptrCast(&dummy);
    const failures = [_][5]c_int{
        .{ c.SQLITE_NOMEM, c.SQLITE_DONE, c.SQLITE_IOERR, c.SQLITE_ERROR, c.SQLITE_NOMEM },
        .{ 0, c.SQLITE_BUSY, c.SQLITE_BUSY, 0, c.SQLITE_BUSY },
        .{ 0, c.SQLITE_DONE, c.SQLITE_IOERR, c.SQLITE_ERROR, c.SQLITE_IOERR },
        .{ 0, c.SQLITE_DONE, 0, c.SQLITE_ERROR, c.SQLITE_ERROR },
    };
    for (failures) |codes| {
        FaultApi.bind_code = codes[0]; FaultApi.step_code = codes[1]; FaultApi.reset_code = codes[2]; FaultApi.clear_code = codes[3];
        FaultApi.steps = 0; FaultApi.resets = 0; FaultApi.clears = 0;
        var source: IntSlice = .{ .values = &.{ 1, 2 } }; var sink: SumSink = .{};
        const result = runWith(FaultApi, stmt, &source, &sink, true);
        try t.expectEqual(codes[4], result.code); try t.expectEqual(codes[2], result.reset_code); try t.expectEqual(codes[3], result.clear_code);
        try t.expectEqual(@as(usize, 0), result.completed); try t.expectEqual(@as(usize, 1), FaultApi.resets); try t.expectEqual(@as(usize, 1), FaultApi.clears);
        try t.expectEqual(@as(usize, if (codes[0] == 0) 1 else 0), FaultApi.steps);
    }
}
