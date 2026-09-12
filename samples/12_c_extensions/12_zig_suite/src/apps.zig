const std = @import("std");

// A non-raising C adapter exposes numeric/array views. Zig does not depend on
// mruby's value layout. Readers must not allocate, run Ruby, or mutate input.
pub const View = extern struct { kind: c_int, number: f64, children: ?*const anyopaque, length: usize };
pub const Reader = *const fn (?*anyopaque, ?*const anyopaque, usize, *View) callconv(.c) void;
pub const BatchReader = *const fn (?*anyopaque, ?*const anyopaque, usize, [*c]View, usize) callconv(.c) usize;
const Frame = struct { source: ?*const anyopaque, length: usize, next: usize = 0 };

fn pushArray(frames: *[64]Frame, depth: *usize, view: View) c_int {
    if (view.length == 0) return 0;
    if (depth.* == frames.len or view.children == null) return 2;
    for (frames[0..depth.*]) |ancestor| {
        if (ancestor.source == view.children) return 2;
    }
    frames[depth.*] = .{ .source = view.children, .length = view.length };
    depth.* += 1;
    return 0;
}

// Iterative depth-first traversal preserves the original nested Adder's
// evaluation order. Reject invalid/deep/cyclic inputs without publishing a
// partial result. Fixed stack storage replaces unbounded native recursion.
export fn drbz_sum_tree(source: ?*const anyopaque, length: usize, reader: Reader, context: ?*anyopaque, result: *f64) c_int {
    @setFloatMode(.strict);
    var frames: [64]Frame = undefined;
    frames[0] = .{ .source = source, .length = length };
    var depth: usize = 1;
    var sum: f64 = 0;
    while (depth != 0) {
        const frame = &frames[depth - 1];
        if (frame.next == frame.length) { depth -= 1; continue; }
        var view: View = undefined;
        reader(context, frame.source, frame.next, &view);
        frame.next += 1;
        switch (view.kind) {
            1 => sum += view.number,
            2 => {
                const code = pushArray(&frames, &depth, view);
                if (code != 0) return code;
            },
            else => return 1,
        }
    }
    result.* = sum;
    return 0;
}

// Flat numeric arrays are the common hot path. Decode up to sixteen Ruby values
// per boundary crossing, but retain no Ruby views across a nested descent. If a
// decoded batch contains an array, trailing siblings are intentionally decoded
// again after that child completes so depth-first semantics and tiny fixed
// storage win over a large per-frame pending-view stack.
export fn drbz_sum_tree_batched(source: ?*const anyopaque, length: usize, reader: BatchReader, context: ?*anyopaque, result: *f64) c_int {
    @setFloatMode(.strict);
    var frames: [64]Frame = undefined;
    frames[0] = .{ .source = source, .length = length };
    var depth: usize = 1;
    var sum: f64 = 0;
    var views: [16]View = undefined;
    while (depth != 0) {
        const frame = &frames[depth - 1];
        if (frame.next == frame.length) { depth -= 1; continue; }
        const wanted = @min(views.len, frame.length - frame.next);
        const got = reader(context, frame.source, frame.next, &views, wanted);
        if (got == 0 or got > wanted) return 1;
        decode: for (views[0..got]) |view| {
            frame.next += 1;
            switch (view.kind) {
                1 => sum += view.number,
                2 => {
                    if (view.length == 0) continue;
                    const code = pushArray(&frames, &depth, view);
                    if (code != 0) return code;
                    break :decode;
                },
                else => return 1,
            }
        }
    }
    result.* = sum;
    return 0;
}

// Apple greeting payload, independent of Foundation allocation. Framework
// dispatch and Ruby string creation remain in their matching SDK adapters.
// Input/output must not overlap. Output includes a NUL; written excludes it.
export fn drbz_greeting(goodbye: c_int, name: [*c]const u8, length: usize, output: [*c]u8, capacity: usize, written: *usize) c_int {
    const prefix: []const u8 = if (goodbye != 0) "Bye " else "Hello ";
    if (capacity < prefix.len + 2 or length > capacity - prefix.len - 2) return 1;
    @memcpy(output[0..prefix.len], prefix);
    if (length != 0) @memcpy(output[prefix.len..][0..length], name[0..length]);
    const end = prefix.len + length;
    output[end] = '!'; output[end + 1] = 0;
    written.* = end + 1;
    return 0;
}

pub const Entry = *const fn (?*anyopaque) callconv(.c) c_int;
pub const WorkerHost = extern struct {
    create: *const fn (?*anyopaque, Entry, ?*anyopaque) callconv(.c) ?*anyopaque,
    join: *const fn (?*anyopaque, *anyopaque) callconv(.c) void,
    log: *const fn (?*anyopaque) callconv(.c) void,
    delay: *const fn (?*anyopaque, u32) callconv(.c) void,
};
pub const Worker = extern struct { host: WorkerHost, context: ?*anyopaque, thread: ?*anyopaque, running: c_int };

// The host maps these callbacks to SDL, pthreads, or a deterministic test.
// Control methods are serialized by the owner thread. The background thread
// touches only the atomic flag and non-Ruby host callbacks.
export fn drbz_worker_init(worker: *Worker, host: *const WorkerHost, context: ?*anyopaque) void {
    worker.* = .{ .host = host.*, .context = context, .thread = null, .running = 0 };
}
export fn drbz_worker_running(worker: *const Worker) c_int {
    return @atomicLoad(c_int, &worker.running, .acquire);
}
fn workerMain(context: ?*anyopaque) callconv(.c) c_int {
    const worker: *Worker = @ptrCast(@alignCast(context.?));
    while (drbz_worker_running(worker) != 0) {
        worker.host.log(worker.context);
        worker.host.delay(worker.context, 1000);
    }
    return 0;
}
export fn drbz_worker_start(worker: *Worker) c_int {
    if (worker.thread) |thread| {
        if (drbz_worker_running(worker) != 0) return 0;
        worker.host.join(worker.context, thread);
        worker.thread = null;
    }
    @atomicStore(c_int, &worker.running, 1, .release);
    worker.thread = worker.host.create(worker.context, workerMain, worker) orelse {
        @atomicStore(c_int, &worker.running, 0, .release);
        return 1;
    };
    return 0;
}
export fn drbz_worker_stop(worker: *Worker) void {
    @atomicStore(c_int, &worker.running, 0, .release);
    if (worker.thread) |thread| {
        worker.host.join(worker.context, thread);
        worker.thread = null;
    }
}

fn testReader(_: ?*anyopaque, source: ?*const anyopaque, index: usize, view: *View) callconv(.c) void {
    const values: [*]const View = @ptrCast(@alignCast(source.?));
    view.* = values[index];
}
const BatchStats = struct { calls: usize = 0, values: usize = 0 };
fn testBatchReader(raw: ?*anyopaque, source: ?*const anyopaque, index: usize, out: [*c]View, capacity: usize) callconv(.c) usize {
    const values: [*]const View = @ptrCast(@alignCast(source.?));
    if (raw) |p| {
        const stats: *BatchStats = @ptrCast(@alignCast(p));
        stats.calls += 1; stats.values += capacity;
    }
    for (0..capacity) |i| out[i] = values[index + i];
    return capacity;
}
test "nested sum retains order and rejects cycles transactionally" {
    const nested = [_]View{
        .{ .kind = 1, .number = 1, .children = null, .length = 0 },
        .{ .kind = 1, .number = -1e16, .children = null, .length = 0 },
    };
    const root = [_]View{
        .{ .kind = 1, .number = 1e16, .children = null, .length = 0 },
        .{ .kind = 2, .number = 0, .children = &nested, .length = nested.len },
        .{ .kind = 1, .number = 3, .children = null, .length = 0 },
    };
    var result: f64 = 777;
    try std.testing.expectEqual(@as(c_int, 0), drbz_sum_tree(&root, root.len, testReader, null, &result));
    try std.testing.expectEqual(@as(f64, 3), result);
    var batched: f64 = 777;
    var stats: BatchStats = .{};
    try std.testing.expectEqual(@as(c_int, 0), drbz_sum_tree_batched(&root, root.len, testBatchReader, &stats, &batched));
    try std.testing.expectEqual(result, batched);
    var cycle: View = .{ .kind = 2, .number = 0, .children = null, .length = 1 };
    cycle.children = &cycle;
    result = 777; batched = 777;
    try std.testing.expectEqual(@as(c_int, 2), drbz_sum_tree(&cycle, 1, testReader, null, &result));
    try std.testing.expectEqual(@as(c_int, 2), drbz_sum_tree_batched(&cycle, 1, testBatchReader, null, &batched));
    try std.testing.expectEqual(@as(f64, 777), result);
    try std.testing.expectEqual(@as(f64, 777), batched);
    cycle.kind = 0;
    try std.testing.expectEqual(@as(c_int, 1), drbz_sum_tree(&cycle, 1, testReader, null, &result));
    try std.testing.expectEqual(@as(c_int, 1), drbz_sum_tree_batched(&cycle, 1, testBatchReader, null, &batched));
    try std.testing.expectEqual(@as(c_int, 0), drbz_sum_tree(null, 0, testReader, null, &result));
    try std.testing.expectEqual(@as(c_int, 0), drbz_sum_tree_batched(null, 0, testBatchReader, null, &batched));
    try std.testing.expectEqual(@as(f64, 0), result);
    try std.testing.expectEqual(@as(f64, 0), batched);
}
test "batched sum amortizes flat reader crossings" {
    var values: [100]View = undefined;
    for (&values, 0..) |*view, i| view.* = .{ .kind = 1, .number = @floatFromInt(i + 1), .children = null, .length = 0 };
    var stats: BatchStats = .{};
    var result: f64 = 0;
    try std.testing.expectEqual(@as(c_int, 0), drbz_sum_tree_batched(&values, values.len, testBatchReader, &stats, &result));
    try std.testing.expectEqual(@as(f64, 5050), result);
    try std.testing.expectEqual(@as(usize, 7), stats.calls);
    try std.testing.expectEqual(@as(usize, 100), stats.values);
}
test "greetings fit exactly and leave short buffers untouched" {
    var output: [16]u8 = @splat(0xaa);
    var written: usize = 999;
    try std.testing.expectEqual(@as(c_int, 1), drbz_greeting(0, "Zig", 3, &output, 10, &written));
    try std.testing.expectEqual(@as(usize, 999), written);
    for (output) |byte| try std.testing.expectEqual(@as(u8, 0xaa), byte);
    try std.testing.expectEqual(@as(c_int, 0), drbz_greeting(0, "Zig", 3, &output, 11, &written));
    try std.testing.expectEqualStrings("Hello Zig!", output[0..written]);
    try std.testing.expectEqual(@as(u8, 0), output[written]);
    try std.testing.expectEqual(@as(c_int, 0), drbz_greeting(1, null, 0, &output, output.len, &written));
    try std.testing.expectEqualStrings("Bye !", output[0..written]);
}

const Fake = struct {
    fail: bool = false, creates: usize = 0, joins: usize = 0, logs: usize = 0, delays: usize = 0,
    entry: ?Entry = null, argument: ?*anyopaque = null,
    fn from(raw: ?*anyopaque) *Fake { return @ptrCast(@alignCast(raw.?)); }
    fn create(raw: ?*anyopaque, entry: Entry, argument: ?*anyopaque) callconv(.c) ?*anyopaque {
        const self = from(raw); self.creates += 1;
        if (self.fail) return null;
        self.entry = entry; self.argument = argument;
        return self;
    }
    fn join(raw: ?*anyopaque, _: *anyopaque) callconv(.c) void { from(raw).joins += 1; }
    fn log(raw: ?*anyopaque) callconv(.c) void { from(raw).logs += 1; }
    fn delay(raw: ?*anyopaque, ms: u32) callconv(.c) void {
        const self = from(raw); self.delays += ms;
        const worker: *Worker = @ptrCast(@alignCast(self.argument.?));
        @atomicStore(c_int, &worker.running, 0, .release);
    }
};
test "worker creation failure, repeated start/stop and restart" {
    const host: WorkerHost = .{ .create = Fake.create, .join = Fake.join, .log = Fake.log, .delay = Fake.delay };
    var fake: Fake = .{};
    var worker: Worker = undefined;
    drbz_worker_init(&worker, &host, &fake);
    drbz_worker_stop(&worker);
    try std.testing.expectEqual(@as(usize, 0), fake.joins);
    fake.fail = true;
    try std.testing.expectEqual(@as(c_int, 1), drbz_worker_start(&worker));
    try std.testing.expectEqual(@as(c_int, 0), drbz_worker_running(&worker));
    try std.testing.expect(worker.thread == null);
    fake.fail = false;
    try std.testing.expectEqual(@as(c_int, 0), drbz_worker_start(&worker));
    try std.testing.expectEqual(@as(c_int, 0), drbz_worker_start(&worker));
    try std.testing.expectEqual(@as(usize, 2), fake.creates);
    try std.testing.expectEqual(@as(c_int, 0), fake.entry.?(fake.argument));
    try std.testing.expectEqual(@as(usize, 1), fake.logs);
    try std.testing.expectEqual(@as(usize, 1000), fake.delays);
    try std.testing.expectEqual(@as(c_int, 0), drbz_worker_start(&worker));
    try std.testing.expectEqual(@as(usize, 1), fake.joins);
    drbz_worker_stop(&worker); drbz_worker_stop(&worker);
    try std.testing.expectEqual(@as(usize, 2), fake.joins);
    try std.testing.expectEqual(@as(c_int, 0), drbz_worker_running(&worker));
}
