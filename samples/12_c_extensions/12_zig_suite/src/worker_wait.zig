pub const Entry = *const fn (?*anyopaque) callconv(.c) c_int;
pub const Host = extern struct {
    create: *const fn (?*anyopaque, Entry, ?*anyopaque) callconv(.c) ?*anyopaque,
    join: *const fn (?*anyopaque, *anyopaque) callconv(.c) void,
    log: *const fn (?*anyopaque) callconv(.c) void,
    wait: *const fn (?*anyopaque, u32) callconv(.c) void,
    wake: *const fn (?*anyopaque) callconv(.c) void,
};
pub const Worker = extern struct { host: Host, context: ?*anyopaque, thread: ?*anyopaque, running: c_int };
export fn drbz_wait_worker_init(worker: *Worker, host: *const Host, context: ?*anyopaque) void { worker.* = .{ .host = host.*, .context = context, .thread = null, .running = 0 }; }
export fn drbz_wait_worker_running(worker: *const Worker) c_int { return @atomicLoad(c_int, &worker.running, .acquire); }
fn workerMain(raw: ?*anyopaque) callconv(.c) c_int {
    const worker: *Worker = @ptrCast(@alignCast(raw.?));
    while (drbz_wait_worker_running(worker) != 0) {
        worker.host.log(worker.context);
        if (drbz_wait_worker_running(worker) == 0) break;
        worker.host.wait(worker.context, 1000);
    }
    return 0;
}
export fn drbz_wait_worker_start(worker: *Worker) c_int {
    if (worker.thread) |thread| { if (drbz_wait_worker_running(worker) != 0) return 0; worker.host.join(worker.context, thread); worker.thread = null; }
    @atomicStore(c_int, &worker.running, 1, .release);
    worker.thread = worker.host.create(worker.context, workerMain, worker) orelse { @atomicStore(c_int, &worker.running, 0, .release); return 1; };
    return 0;
}
export fn drbz_wait_worker_stop(worker: *Worker) void {
    @atomicStore(c_int, &worker.running, 0, .release);
    if (worker.thread) |thread| { worker.host.wake(worker.context); worker.host.join(worker.context, thread); worker.thread = null; }
}
