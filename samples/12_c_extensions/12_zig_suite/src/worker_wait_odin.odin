package rivals
import "base:intrinsics"
Wait_Entry :: proc "c" (ctx: rawptr) -> i32
Wait_Create :: proc "c" (ctx: rawptr, entry: Wait_Entry, argument: rawptr) -> rawptr
Wait_Join :: proc "c" (ctx: rawptr, thread: rawptr)
Wait_Log :: proc "c" (ctx: rawptr)
Wait_Wait :: proc "c" (ctx: rawptr, milliseconds: u32)
Wait_Wake :: proc "c" (ctx: rawptr)
Wait_Host :: struct { create: Wait_Create, join: Wait_Join, log: Wait_Log, wait: Wait_Wait, wake: Wait_Wake }
Wait_Worker :: struct { host: Wait_Host, ctx: rawptr, thread: rawptr, running: i32 }
@(export) drbo_wait_worker_init :: proc "c" (worker: ^Wait_Worker, host: ^Wait_Host, ctx: rawptr) { worker.host=host^; worker.ctx=ctx; worker.thread=nil; worker.running=0 }
@(export) drbo_wait_worker_running :: proc "c" (worker: ^Wait_Worker) -> i32 { return intrinsics.atomic_load_explicit(&worker.running, .Acquire) }
wait_worker_main :: proc "c" (raw: rawptr) -> i32 {
    worker:=cast(^Wait_Worker)raw
    for drbo_wait_worker_running(worker)!=0 { worker.host.log(worker.ctx); if drbo_wait_worker_running(worker)==0 { break }; worker.host.wait(worker.ctx,1000) }
    return 0
}
@(export) drbo_wait_worker_start :: proc "c" (worker: ^Wait_Worker) -> i32 {
    if worker.thread!=nil { if drbo_wait_worker_running(worker)!=0 { return 0 }; worker.host.join(worker.ctx,worker.thread); worker.thread=nil }
    intrinsics.atomic_store_explicit(&worker.running,i32(1),.Release)
    thread:=worker.host.create(worker.ctx,wait_worker_main,worker)
    if thread==nil { intrinsics.atomic_store_explicit(&worker.running,i32(0),.Release); return 1 }
    worker.thread=thread; return 0
}
@(export) drbo_wait_worker_stop :: proc "c" (worker: ^Wait_Worker) { intrinsics.atomic_store_explicit(&worker.running,i32(0),.Release); if worker.thread!=nil { worker.host.wake(worker.ctx); worker.host.join(worker.ctx,worker.thread); worker.thread=nil } }
