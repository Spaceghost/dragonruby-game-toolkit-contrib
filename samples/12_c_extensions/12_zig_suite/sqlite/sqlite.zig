const std = @import("std");

// Keep SQLite itself as the upstream C library. This module owns the sample's
// connection/statement lifecycle and exposes a non-raising C boundary.
const Sqlite = opaque {};
const Stmt = opaque {};
extern "c" fn sqlite3_open([*:0]const u8, *?*Sqlite) c_int;
extern "c" fn sqlite3_close(?*Sqlite) c_int;
extern "c" fn sqlite3_errmsg(?*Sqlite) [*:0]const u8;
extern "c" fn sqlite3_errstr(c_int) [*:0]const u8;
extern "c" fn sqlite3_exec(*Sqlite, [*:0]const u8, ?*const anyopaque, ?*anyopaque, ?*anyopaque) c_int;
extern "c" fn sqlite3_prepare_v3(*Sqlite, [*:0]const u8, c_int, c_uint, *?*Stmt, ?*anyopaque) c_int;
extern "c" fn sqlite3_step(*Stmt) c_int;
extern "c" fn sqlite3_finalize(?*Stmt) c_int;
extern "c" fn sqlite3_reset(*Stmt) c_int;
extern "c" fn sqlite3_clear_bindings(*Stmt) c_int;
extern "c" fn sqlite3_bind_int64(*Stmt, c_int, i64) c_int;
extern "c" fn sqlite3_column_text(*Stmt, c_int) ?[*:0]const u8;
extern "c" fn sqlite3_column_bytes(*Stmt, c_int) c_int;
extern "c" fn sqlite3_column_type(*Stmt, c_int) c_int;

pub const Database = extern struct { handle: ?*Sqlite, message: [256]u8 };
pub const Statement = extern struct { handle: ?*Stmt, owner: ?*Database, state: c_int };
pub const Text = extern struct { data: ?[*]const u8, length: usize, is_null: c_int };
const ok = 0;
const misuse = 21;
const row = 100;
const done = 101;

fn record(db: *Database, code: c_int) c_int {
    if (code == ok or code == row or code == done) return code;
    const text = if (db.handle != null and code != misuse) sqlite3_errmsg(db.handle) else sqlite3_errstr(code);
    const size = @min(std.mem.len(text), db.message.len - 1);
    @memcpy(db.message[0..size], text[0..size]);
    db.message[size] = 0;
    return code;
}
export fn drbz_sql_init(db: *Database) void {
    db.* = .{ .handle = null, .message = @splat(0) };
}
export fn drbz_sql_open(db: *Database, path: [*:0]const u8) c_int {
    if (db.handle != null) return record(db, misuse);
    const result = sqlite3_open(path, &db.handle);
    if (result != ok) {
        _ = record(db, result);
        _ = sqlite3_close(db.handle);
        db.handle = null;
    } else db.message[0] = 0;
    return result;
}
export fn drbz_sql_close(db: *Database) c_int {
    const result = sqlite3_close(db.handle);
    if (result == ok) db.handle = null else _ = record(db, result);
    return result;
}
export fn drbz_sql_exec(db: *Database, sql: [*:0]const u8) c_int {
    const handle = db.handle orelse return record(db, misuse);
    // A null errmsg output avoids a separately allocated error string. Copy
    // the connection's borrowed diagnostic before subsequent cleanup calls.
    return record(db, sqlite3_exec(handle, sql, null, null, null));
}
export fn drbz_sql_statement_init(statement: *Statement) void {
    statement.* = .{ .handle = null, .owner = null, .state = ok };
}
export fn drbz_sql_prepare(db: *Database, statement: *Statement, sql: [*:0]const u8, persistent: c_int) c_int {
    const handle = db.handle orelse return record(db, misuse);
    if (statement.handle != null) return record(db, misuse);
    var prepared: ?*Stmt = null;
    const result = sqlite3_prepare_v3(handle, sql, -1, if (persistent != 0) 1 else 0, &prepared, null);
    if (result != ok) {
        _ = record(db, result);
        _ = sqlite3_finalize(prepared);
        return result;
    }
    statement.* = .{ .handle = prepared, .owner = db, .state = ok };
    return ok;
}
export fn drbz_sql_bind_int(statement: *Statement, index: c_int, value: i64) c_int {
    const db = statement.owner orelse return misuse;
    const handle = statement.handle orelse return record(db, misuse);
    return record(db, sqlite3_bind_int64(handle, index, value));
}
export fn drbz_sql_step(statement: *Statement) c_int {
    const db = statement.owner orelse return misuse;
    if (statement.state != ok and statement.state != row) return record(db, misuse);
    const handle = statement.handle orelse {
        statement.state = done;
        return done;
    };
    const result = sqlite3_step(handle);
    statement.state = result;
    return record(db, result);
}
export fn drbz_sql_first_text(statement: *Statement, text: *Text) c_int {
    const db = statement.owner orelse return misuse;
    const handle = statement.handle orelse return record(db, misuse);
    if (statement.state != row) return record(db, misuse);
    const is_null = sqlite3_column_type(handle, 0) == 5;
    const data = sqlite3_column_text(handle, 0);
    if (data == null and !is_null) return record(db, 7); // SQLITE_NOMEM
    const bytes = sqlite3_column_bytes(handle, 0);
    text.* = .{ .data = data, .length = @intCast(bytes), .is_null = @intFromBool(is_null) };
    return ok;
}
export fn drbz_sql_reset(statement: *Statement, clear: c_int) c_int {
    const db = statement.owner orelse return misuse;
    const handle = statement.handle orelse { statement.state = ok; return ok; };
    const result = sqlite3_reset(handle);
    statement.state = ok;
    _ = record(db, result);
    const bindings = if (clear != 0) sqlite3_clear_bindings(handle) else ok;
    return if (result != ok) result else record(db, bindings);
}
export fn drbz_sql_finalize(statement: *Statement) c_int {
    const db = statement.owner;
    const result = sqlite3_finalize(statement.handle);
    statement.* = .{ .handle = null, .owner = null, .state = ok };
    return if (db) |owner| record(owner, result) else result;
}
