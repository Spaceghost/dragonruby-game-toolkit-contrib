//! Prefer c.sqlite3_* or runIntBatch from Zig. The exported drbz_sql_* state
//! machine is retained solely for C ABI compatibility and regression controls;
//! it is not an intermediate layer for the Zig-native path.
pub const direct = @import("direct.zig");
pub const c = direct.c;
pub const runIntBatch = direct.runIntBatch;
pub const IntSlice = direct.IntSlice;
pub const BatchResult = direct.BatchResult;
const compatibility = @import("compat.zig");
pub const Database = compatibility.Database;
pub const Statement = compatibility.Statement;
pub const Text = compatibility.Text;
comptime { _ = compatibility; }
