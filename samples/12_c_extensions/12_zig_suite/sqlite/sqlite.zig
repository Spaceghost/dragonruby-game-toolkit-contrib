//! Zig-first SQLite entry point. c.sqlite3_* are actual SQLite declarations;
//! runIntBatch specializes its source and sink without per-operation ABI calls.
//! The legacy exported state machine is opt-in: build/import compat.zig only
//! for existing C consumers or explicitly labeled benchmark controls.
pub const direct = @import("direct.zig");
pub const c = direct.c;
pub const runIntBatch = direct.runIntBatch;
pub const IntSlice = direct.IntSlice;
pub const BatchResult = direct.BatchResult;
