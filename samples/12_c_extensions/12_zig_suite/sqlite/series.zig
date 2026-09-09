//! Benchmark-only C entry point. The production API is direct.zig; only this
//! outer batch crosses the C ABI. All SQLite operations inside are direct.
const direct = @import("direct.zig");
const c = direct.c;
const Sequence = struct {
    remaining: usize,
    value: u32,
    pub fn next(self: *Sequence) ?i64 {
        if (self.remaining == 0) return null;
        self.remaining -= 1;
        const value: i64 = self.value & 1023;
        self.value +%= 1;
        return value;
    }
};
const TextSum = struct {
    checksum: u64 = 0,
    pub fn row(self: *TextSum, stmt: *c.sqlite3_stmt) c_int {
        if (c.sqlite3_column_type(stmt, 0) == c.SQLITE_NULL) return c.SQLITE_MISMATCH;
        const bytes = c.sqlite3_column_text(stmt, 0);
        if (bytes == null) return c.SQLITE_NOMEM;
        const count = c.sqlite3_column_bytes(stmt, 0);
        for (bytes[0..@intCast(count)]) |byte| self.checksum +%= byte;
        return c.SQLITE_OK;
    }
};
fn series(db: *c.sqlite3, sql: [*:0]const u8, count: usize, seed: u32, comptime reuse: bool, comptime clear: bool, output: *u64) c_int {
    var sum: TextSum = .{};
    var left = count;
    var value = seed;
    while (left > 0) {
        var prepared: ?*c.sqlite3_stmt = null;
        const prepare_code = c.sqlite3_prepare_v3(db, sql, -1, if (reuse) c.SQLITE_PREPARE_PERSISTENT else 0, &prepared, null);
        if (prepare_code != c.SQLITE_OK) { _ = c.sqlite3_finalize(prepared); return prepare_code; }
        const stmt = prepared orelse return c.SQLITE_MISUSE;
        const n = if (reuse) left else 1;
        var source: Sequence = .{ .remaining = n, .value = value };
        const result = direct.runIntBatch(stmt, &source, &sum, clear);
        const finalize_code = c.sqlite3_finalize(stmt);
        if (result.code != c.SQLITE_OK) return result.code;
        if (finalize_code != c.SQLITE_OK) return finalize_code;
        left -= n;
        value = source.value;
    }
    output.* = sum.checksum;
    return c.SQLITE_OK;
}
export fn drbz_direct_sql_series(db: ?*c.sqlite3, sql: [*:0]const u8, count: usize, seed: u32, reuse: c_int, clear: c_int, output: *u64) c_int {
    const handle = db orelse return c.SQLITE_MISUSE;
    if (reuse != 0) {
        return if (clear != 0) series(handle, sql, count, seed, true, true, output) else series(handle, sql, count, seed, true, false, output);
    }
    return if (clear != 0) series(handle, sql, count, seed, false, true, output) else series(handle, sql, count, seed, false, false, output);
}
