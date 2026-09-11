package rivals

import "base:intrinsics"
import "core:simd"

Random :: proc "c" (ctx: rawptr) -> f32

load_u8x32 :: #force_inline proc "contextless" (p: [^]u8) -> simd.u8x32 {
	return intrinsics.unaligned_load(cast(^simd.u8x32)p)
}

load_f32x8 :: #force_inline proc "contextless" (p: [^]f32) -> simd.f32x8 {
	return intrinsics.unaligned_load(cast(^simd.f32x8)p)
}

store_f32x8 :: #force_inline proc "contextless" (p: [^]f32, v: simd.f32x8) {
	intrinsics.unaligned_store(cast(^simd.f32x8)p, v)
}

count_medium :: proc "contextless" (bytes: [^]u8, length: uintptr) -> uintptr #no_bounds_check {
	newline: simd.u8x32 = u8('\n')
	one: simd.u8x32 = u8(1)
	offset: uintptr = 0
	total: uintptr = 0
	for length - offset >= 32 {
		value := load_u8x32(bytes[offset:])
		hits := simd.lanes_eq(value, newline) & one
		total += uintptr(simd.reduce_add_bisect(hits))
		offset += 32
	}
	for offset < length {
		if bytes[offset] == u8('\n') { total += 1 }
		offset += 1
	}
	return total
}

@(export)
drbo_count_dual :: proc "c" (bytes: [^]u8, length: uintptr) -> uintptr #no_bounds_check {
	if length == 0 { return 0 }
	if length <= 16384 { return count_medium(bytes, length) }

	newline: simd.u8x32 = u8('\n')
	one: simd.u8x32 = u8(1)
	offset: uintptr = 0
	total: uintptr = 0
	for length - offset >= 128 {
		pairs := (length - offset) / 64
		if pairs > 255 { pairs = 255 }
		even: simd.u8x32
		odd: simd.u8x32
		end := offset + pairs * 64
		for offset < end {
			a := load_u8x32(bytes[offset:])
			b := load_u8x32(bytes[offset + 32:])
			even += simd.lanes_eq(a, newline) & one
			odd += simd.lanes_eq(b, newline) & one
			offset += 64
		}
		ea := simd.to_array(even)
		oa := simd.to_array(odd)
		for lane in 0..<32 { total += uintptr(ea[lane]) + uintptr(oa[lane]) }
	}
	if offset < length { total += count_medium(bytes[offset:], length - offset) }
	return total
}

scalar_run :: #force_inline proc "contextless" (x, y: [^]f32, speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) #no_bounds_check {
	for i: uintptr = 0; i < count; i += 1 {
		x[i] += speed[i]
		if x[i] > 1280.0 { x[i] = random(ctx) * -1280.0 }
		y[i] += speed[i]
		if y[i] > 720.0 { y[i] = random(ctx) * -720.0 }
	}
}

dense_both_wrap_run :: #force_inline proc "contextless" (x, y: [^]f32, speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) -> uintptr #no_bounds_check {
	i: uintptr = 0
	for i < count {
		nx := x[i] + speed[i]
		ny := y[i] + speed[i]
		if !(nx > 1280.0 && ny > 720.0) { break }
		x[i] = random(ctx) * -1280.0
		y[i] = random(ctx) * -720.0
		i += 1
	}
	return i
}

// A single Odin star algorithm serves both ABIs. The generic exported path
// passes a runtime callback. A persistent starfield can pass its known callback
// into this forced-inline core, allowing LLVM to devirtualize owned RNG calls.
stars_core :: #force_inline proc "contextless" (#no_alias x, #no_alias y: [^]f32, #no_alias speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) #no_bounds_check {
	if count == 0 { return }
	max_x: simd.f32x8 = f32(1280)
	max_y: simd.f32x8 = f32(720)
	i: uintptr = 0
	for count - i >= 8 {
		vx := load_f32x8(x[i:])
		vy := load_f32x8(y[i:])
		vs := load_f32x8(speed[i:])
		nx := vx + vs
		ny := vy + vs
		wrap_x := simd.lanes_gt(nx, max_x)
		wrap_y := simd.lanes_gt(ny, max_y)
		wraps := wrap_x | wrap_y
		if simd.reduce_or(wraps) == 0 {
			store_f32x8(x[i:], nx)
			store_f32x8(y[i:], ny)
			i += 8
			continue
		}
		if simd.reduce_and(wrap_x & wrap_y) != 0 {
			consumed := dense_both_wrap_run(x[i:], y[i:], speed[i:], count - i, random, ctx)
			i += consumed
			continue
		}

		store_f32x8(x[i:], nx)
		store_f32x8(y[i:], ny)
		xm := simd.to_array(wrap_x)
		ym := simd.to_array(wrap_y)
		for lane in 0..<8 {
			index := i + uintptr(lane)
			if xm[lane] != 0 { x[index] = random(ctx) * -1280.0 }
			if ym[lane] != 0 { y[index] = random(ctx) * -720.0 }
		}
		i += 8
	}
	if i < count { scalar_run(x[i:], y[i:], speed[i:], count - i, random, ctx) }
}

@(export)
drbo_stars_block :: proc "c" (#no_alias x, #no_alias y: [^]f32, #no_alias speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) #no_bounds_check {
	stars_core(x, y, speed, count, random, ctx)
}
