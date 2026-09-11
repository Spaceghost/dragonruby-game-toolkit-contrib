package rivals

import "base:intrinsics"

View :: struct {
	kind:     i32,
	number:   f64,
	children: rawptr,
	length:   uintptr,
}

Reader :: proc "c" (ctx: rawptr, source: rawptr, index: uintptr, view: ^View)
Batch_Reader :: proc "c" (ctx: rawptr, source: rawptr, index: uintptr, views: [^]View, capacity: uintptr) -> uintptr
Frame :: struct { source: rawptr, length: uintptr, next: uintptr }

#assert(size_of(View) == 32)

push_array :: proc "contextless" (frames: ^[64]Frame, depth: ^uintptr, view: View) -> i32 #no_bounds_check {
	if view.length == 0 {
		return 0
	}
	if depth^ == 64 || view.children == nil {
		return 2
	}
	for i: uintptr = 0; i < depth^; i += 1 {
		if frames[i].source == view.children {
			return 2
		}
	}
	frames[depth^] = Frame{source=view.children, length=view.length}
	depth^ += 1
	return 0
}

@(export)
drbo_sum_tree :: proc "c" (source: rawptr, length: uintptr, reader: Reader, ctx: rawptr, result: ^f64) -> i32 #no_bounds_check {
	frames: [64]Frame
	frames[0] = Frame{source=source, length=length}
	depth: uintptr = 1
	sum: f64 = 0
	for depth != 0 {
		frame := &frames[depth - 1]
		if frame.next == frame.length {
			depth -= 1
			continue
		}
		view: View
		reader(ctx, frame.source, frame.next, &view)
		frame.next += 1
		switch view.kind {
		case 1:
			sum += view.number
		case 2:
			code := push_array(&frames, &depth, view)
			if code != 0 { return code }
		case:
			return 1
		}
	}
	result^ = sum
	return 0
}

@(export)
drbo_sum_tree_batched :: proc "c" (source: rawptr, length: uintptr, reader: Batch_Reader, ctx: rawptr, result: ^f64) -> i32 #no_bounds_check {
	frames: [64]Frame
	frames[0] = Frame{source=source, length=length}
	depth: uintptr = 1
	sum: f64 = 0
	views: [16]View
	for depth != 0 {
		frame := &frames[depth - 1]
		if frame.next == frame.length {
			depth -= 1
			continue
		}
		wanted := frame.length - frame.next
		if wanted > 16 { wanted = 16 }
		got := reader(ctx, frame.source, frame.next, &views[0], wanted)
		if got == 0 || got > wanted { return 1 }
		for j: uintptr = 0; j < got; j += 1 {
			view := views[j]
			frame.next += 1
			switch view.kind {
			case 1:
				sum += view.number
			case 2:
				if view.length == 0 { continue }
				code := push_array(&frames, &depth, view)
				if code != 0 { return code }
				break
			case:
				return 1
			}
		}
	}
	result^ = sum
	return 0
}

@(export)
drbo_greeting :: proc "c" (goodbye: i32, name: [^]u8, length: uintptr, output: [^]u8, capacity: uintptr, written: ^uintptr) -> i32 #no_bounds_check {
	prefix := "Hello "
	if goodbye != 0 { prefix = "Bye " }
	prefix_len := uintptr(len(prefix))
	if capacity < prefix_len + 2 || length > capacity - prefix_len - 2 {
		return 1
	}
	for i: uintptr = 0; i < prefix_len; i += 1 { output[i] = prefix[i] }
	for i: uintptr = 0; i < length; i += 1 { output[prefix_len + i] = name[i] }
	end := prefix_len + length
	output[end] = u8('!')
	output[end + 1] = 0
	written^ = end + 1
	return 0
}

Entry :: proc "c" (ctx: rawptr) -> i32
Create :: proc "c" (ctx: rawptr, entry: Entry, argument: rawptr) -> rawptr
Join :: proc "c" (ctx: rawptr, thread: rawptr)
Log :: proc "c" (ctx: rawptr)
Delay :: proc "c" (ctx: rawptr, milliseconds: u32)

Worker_Host :: struct {
	create: Create,
	join:   Join,
	log:    Log,
	delay:  Delay,
}

Worker :: struct {
	host:    Worker_Host,
	ctx:     rawptr,
	thread:  rawptr,
	running: i32,
}

@(export)
drbo_worker_init :: proc "c" (worker: ^Worker, host: ^Worker_Host, ctx: rawptr) {
	worker.host = host^
	worker.ctx = ctx
	worker.thread = nil
	worker.running = 0
}

@(export)
drbo_worker_running :: proc "c" (worker: ^Worker) -> i32 {
	return intrinsics.atomic_load_explicit(&worker.running, .Acquire)
}

worker_main :: proc "c" (raw: rawptr) -> i32 {
	worker := cast(^Worker)raw
	for drbo_worker_running(worker) != 0 {
		worker.host.log(worker.ctx)
		worker.host.delay(worker.ctx, 1000)
	}
	return 0
}

@(export)
drbo_worker_start :: proc "c" (worker: ^Worker) -> i32 {
	if worker.thread != nil {
		if drbo_worker_running(worker) != 0 { return 0 }
		worker.host.join(worker.ctx, worker.thread)
		worker.thread = nil
	}
	intrinsics.atomic_store_explicit(&worker.running, i32(1), .Release)
	thread := worker.host.create(worker.ctx, worker_main, worker)
	if thread == nil {
		intrinsics.atomic_store_explicit(&worker.running, i32(0), .Release)
		return 1
	}
	worker.thread = thread
	return 0
}

@(export)
drbo_worker_stop :: proc "c" (worker: ^Worker) {
	intrinsics.atomic_store_explicit(&worker.running, i32(0), .Release)
	if worker.thread != nil {
		worker.host.join(worker.ctx, worker.thread)
		worker.thread = nil
	}
}
