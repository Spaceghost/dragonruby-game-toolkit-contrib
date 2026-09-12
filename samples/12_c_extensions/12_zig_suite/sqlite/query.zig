const std = @import("std");
pub const c = @cImport({ @cInclude("sqlite3.h"); });

pub const QueryCache = extern struct {
    db: ?*c.sqlite3,
    stmt: ?*c.sqlite3_stmt,
    key: [*c]u8,
    key_len: usize,
    buffer: [*c]u8,
    capacity: usize,
    output_len: usize,
    rows: usize,
    prepares: u64,
    hits: u64,
    valid: c_int,
};

pub const QueryResult = extern struct {
    code: c_int,
    reset_code: c_int,
    rows: usize,
    cache_hit: c_int,
};

const PrepareResult = struct { code: c_int, hit: bool };
const null_row = std.math.maxInt(u64);

pub fn init(cache: *QueryCache, db: ?*c.sqlite3) void {
    cache.* = .{
        .db = db, .stmt = null, .key = null, .key_len = 0,
        .buffer = null, .capacity = 0, .output_len = 0, .rows = 0,
        .prepares = 0, .hits = 0, .valid = 0,
    };
}

pub fn clear(cache: *QueryCache) c_int {
    var code: c_int = c.SQLITE_OK;
    if (cache.stmt) |stmt| code = c.sqlite3_finalize(stmt);
    if (cache.key != null) c.sqlite3_free(@ptrCast(cache.key));
    if (cache.buffer != null) c.sqlite3_free(@ptrCast(cache.buffer));
    const db = cache.db;
    const prepares = cache.prepares;
    const hits = cache.hits;
    cache.* = .{
        .db = db, .stmt = null, .key = null, .key_len = 0,
        .buffer = null, .capacity = 0, .output_len = 0, .rows = 0,
        .prepares = prepares, .hits = hits, .valid = 0,
    };
    return code;
}

fn sqlSlice(sql: [*c]const u8, len: usize) []const u8 {
    return if (len == 0) "" else sql[0..len];
}

fn sameKey(cache: *const QueryCache, sql: []const u8) bool {
    if (cache.valid == 0 or cache.key == null or cache.key_len != sql.len) return false;
    return std.mem.eql(u8, cache.key[0..sql.len], sql);
}

fn prepareCached(cache: *QueryCache, sql: []const u8) PrepareResult {
    const db = cache.db orelse return .{ .code = c.SQLITE_MISUSE, .hit = false };
    if (sql.len > std.math.maxInt(c_int)) return .{ .code = c.SQLITE_TOOBIG, .hit = false };
    if (std.mem.indexOfScalar(u8, sql, 0) != null) return .{ .code = c.SQLITE_MISUSE, .hit = false };
    if (cache.stmt) |stmt| if (c.sqlite3_stmt_busy(stmt) != 0) return .{ .code = c.SQLITE_MISUSE, .hit = false };
    if (sameKey(cache, sql)) {
        cache.hits +%= 1;
        return .{ .code = c.SQLITE_OK, .hit = true };
    }

    var new_stmt: ?*c.sqlite3_stmt = null;
    const prep = c.sqlite3_prepare_v3(db, sql.ptr, @intCast(sql.len), c.SQLITE_PREPARE_PERSISTENT, &new_stmt, null);
    if (prep != c.SQLITE_OK) return .{ .code = prep, .hit = false };

    const allocation = if (sql.len == 0) 1 else sql.len;
    const raw_key = c.sqlite3_malloc64(@intCast(allocation)) orelse {
        if (new_stmt) |stmt| _ = c.sqlite3_finalize(stmt);
        return .{ .code = c.SQLITE_NOMEM, .hit = false };
    };
    const new_key: [*c]u8 = @ptrCast(raw_key);
    if (sql.len != 0) @memcpy(new_key[0..sql.len], sql);

    const old_stmt = cache.stmt;
    const old_key = cache.key;
    cache.stmt = new_stmt;
    cache.key = new_key;
    cache.key_len = sql.len;
    cache.valid = 1;
    cache.prepares +%= 1;

    var cleanup: c_int = c.SQLITE_OK;
    if (old_stmt) |stmt| cleanup = c.sqlite3_finalize(stmt);
    if (old_key != null) c.sqlite3_free(@ptrCast(old_key));
    return .{ .code = cleanup, .hit = false };
}

fn reserve(cache: *QueryCache, needed: usize) c_int {
    if (needed <= cache.capacity) return c.SQLITE_OK;
    var next: usize = if (cache.capacity == 0) 256 else cache.capacity;
    while (next < needed) {
        const doubled = @mulWithOverflow(next, 2);
        if (doubled[1] != 0) { next = needed; break; }
        next = doubled[0];
    }
    const old: ?*anyopaque = if (cache.buffer == null) null else @ptrCast(cache.buffer);
    const raw = c.sqlite3_realloc64(old, @intCast(next)) orelse return c.SQLITE_NOMEM;
    cache.buffer = @ptrCast(raw);
    cache.capacity = next;
    return c.SQLITE_OK;
}

fn appendFirstColumn(cache: *QueryCache, stmt: *c.sqlite3_stmt) c_int {
    const kind = c.sqlite3_column_type(stmt, 0);
    var row_len: usize = 0;
    var bytes: [*c]const u8 = null;
    var header: u64 = null_row;
    if (kind != c.SQLITE_NULL) {
        bytes = c.sqlite3_column_text(stmt, 0);
        if (bytes == null) return c.SQLITE_NOMEM;
        const n = c.sqlite3_column_bytes(stmt, 0);
        if (n < 0) return c.SQLITE_TOOBIG;
        row_len = @intCast(n);
        header = @intCast(row_len);
    }
    const with_header = @addWithOverflow(cache.output_len, @sizeOf(u64));
    if (with_header[1] != 0) return c.SQLITE_TOOBIG;
    const total = @addWithOverflow(with_header[0], row_len);
    if (total[1] != 0) return c.SQLITE_TOOBIG;
    const rc = reserve(cache, total[0]);
    if (rc != c.SQLITE_OK) return rc;
    @memcpy(cache.buffer[cache.output_len..][0..@sizeOf(u64)], std.mem.asBytes(&header));
    cache.output_len = with_header[0];
    if (row_len != 0) {
        @memcpy(cache.buffer[cache.output_len..][0..row_len], bytes[0..row_len]);
        cache.output_len += row_len;
    }
    cache.rows += 1;
    return c.SQLITE_OK;
}

/// Preserve the original sample's contract: compile the first SQL statement and
/// return column zero of each row as text, with SQL NULL distinct from empty text.
/// Prepared statement, SQL key and output buffer persist in the cache. No Ruby or
/// user callback executes while SQLite is stepped; the packed bytes are consumed
/// only after reset. Embedded NUL SQL is rejected; embedded NUL result text is kept.
pub fn query(cache: *QueryCache, sql: []const u8) QueryResult {
    cache.output_len = 0;
    cache.rows = 0;
    const prepared = prepareCached(cache, sql);
    if (prepared.code != c.SQLITE_OK) return .{ .code = prepared.code, .reset_code = c.SQLITE_OK, .rows = 0, .cache_hit = @intFromBool(prepared.hit) };
    const stmt = cache.stmt orelse return .{ .code = c.SQLITE_OK, .reset_code = c.SQLITE_OK, .rows = 0, .cache_hit = @intFromBool(prepared.hit) };

    var code: c_int = c.SQLITE_OK;
    while (true) {
        const step = c.sqlite3_step(stmt);
        if (step == c.SQLITE_DONE) break;
        if (step != c.SQLITE_ROW) { code = step; break; }
        code = appendFirstColumn(cache, stmt);
        if (code != c.SQLITE_OK) break;
    }
    const completed_rows = cache.rows;
    const reset_code = c.sqlite3_reset(stmt);
    if (code == c.SQLITE_OK and reset_code != c.SQLITE_OK) code = reset_code;
    if (code != c.SQLITE_OK) {
        cache.output_len = 0;
        cache.rows = 0;
    }
    return .{ .code = code, .reset_code = reset_code, .rows = completed_rows, .cache_hit = @intFromBool(prepared.hit) };
}

export fn drbz_query_cache_init(cache: *QueryCache, db: ?*c.sqlite3) void { init(cache, db); }
export fn drbz_query_cache_clear(cache: *QueryCache) c_int { return clear(cache); }
export fn drbz_query_pack(cache: *QueryCache, sql: [*c]const u8, len: usize) QueryResult { return query(cache, sqlSlice(sql, len)); }
export fn drbz_query_output(cache: *const QueryCache, data: *[*c]const u8, len: *usize, rows: *usize) void {
    data.* = cache.buffer;
    len.* = cache.output_len;
    rows.* = cache.rows;
}

const t = std.testing;
fn openMemory() !*c.sqlite3 {
    var db: ?*c.sqlite3 = null;
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_open(":memory:", &db));
    return db orelse error.NoDatabase;
}
fn readHeader(cache: *const QueryCache, pos: *usize) u64 {
    var value: u64 = 0;
    @memcpy(std.mem.asBytes(&value), cache.buffer[pos.*..][0..@sizeOf(u64)]);
    pos.* += @sizeOf(u64);
    return value;
}

test "query cache hits, replacement and invalid SQL preserve the prior cache" {
    const db = try openMemory();
    var cache: QueryCache = undefined; init(&cache, db);
    defer { _ = clear(&cache); _ = c.sqlite3_close(db); }
    const sql = "SELECT 'one' UNION ALL SELECT 'two'";
    var result = query(&cache, sql);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
    try t.expectEqual(@as(c_int, 0), result.cache_hit);
    try t.expectEqual(@as(u64, 1), cache.prepares);
    try t.expectEqual(@as(usize, 2), cache.rows);
    result = query(&cache, sql);
    try t.expectEqual(@as(c_int, 1), result.cache_hit);
    try t.expectEqual(@as(u64, 1), cache.prepares);
    try t.expectEqual(@as(u64, 1), cache.hits);
    const bad = query(&cache, "SELECT FROM definitely_bad");
    try t.expect(bad.code != c.SQLITE_OK);
    result = query(&cache, sql);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
    try t.expectEqual(@as(c_int, 1), result.cache_hit);
    try t.expectEqual(@as(u64, 1), cache.prepares);
}

test "packed output distinguishes NULL, empty and embedded NUL text" {
    const db = try openMemory();
    var cache: QueryCache = undefined; init(&cache, db);
    defer { _ = clear(&cache); _ = c.sqlite3_close(db); }
    const result = query(&cache, "SELECT NULL UNION ALL SELECT '' UNION ALL SELECT CAST(x'610062' AS TEXT)");
    try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
    try t.expectEqual(@as(usize, 3), cache.rows);
    var pos: usize = 0;
    try t.expectEqual(null_row, readHeader(&cache, &pos));
    try t.expectEqual(@as(u64, 0), readHeader(&cache, &pos));
    try t.expectEqual(@as(u64, 3), readHeader(&cache, &pos));
    try t.expectEqualSlices(u8, "a\x00b", cache.buffer[pos..][0..3]);
    pos += 3;
    try t.expectEqual(cache.output_len, pos);
}

test "errors reset the statement, discard partial output and allow retry" {
    const db = try openMemory();
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_exec(db, "CREATE TABLE t(v UNIQUE); INSERT INTO t VALUES(1);", null, null, null));
    var cache: QueryCache = undefined; init(&cache, db);
    defer { _ = clear(&cache); _ = c.sqlite3_close(db); }
    var result = query(&cache, "INSERT INTO t VALUES(1) RETURNING v");
    try t.expectEqual(@as(c_int, c.SQLITE_CONSTRAINT), result.code);
    try t.expectEqual(@as(usize, 0), cache.output_len);
    try t.expectEqual(@as(usize, 0), cache.rows);
    try t.expectEqual(@as(c_int, 0), c.sqlite3_stmt_busy(cache.stmt.?));
    result = query(&cache, "SELECT v FROM t");
    try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
    try t.expectEqual(@as(usize, 1), cache.rows);
}

test "clear releases statement, key and reusable output allocation" {
    const db = try openMemory();
    var cache: QueryCache = undefined; init(&cache, db);
    const result = query(&cache, "SELECT printf('%01024d', 7)");
    try t.expectEqual(@as(c_int, c.SQLITE_OK), result.code);
    try t.expect(cache.capacity >= 1024);
    try t.expect(c.sqlite3_next_stmt(db, null) != null);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), clear(&cache));
    try t.expect(c.sqlite3_next_stmt(db, null) == null);
    try t.expect(cache.key == null and cache.buffer == null and cache.stmt == null);
    try t.expectEqual(@as(c_int, c.SQLITE_OK), c.sqlite3_close(db));
}

test "embedded NUL SQL is rejected without touching the cache" {
    const db = try openMemory();
    var cache: QueryCache = undefined; init(&cache, db);
    defer { _ = clear(&cache); _ = c.sqlite3_close(db); }
    const bytes = [_]u8{ 'S','E','L','E','C','T',' ', '1', 0, ';' };
    const result = query(&cache, &bytes);
    try t.expectEqual(@as(c_int, c.SQLITE_MISUSE), result.code);
    try t.expectEqual(@as(u64, 0), cache.prepares);
    try t.expectEqual(@as(c_int, 0), cache.valid);
}
