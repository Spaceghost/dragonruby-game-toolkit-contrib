package rivals

import "core:simd"

Random :: proc "c" (ctx: rawptr) -> f32

load_u8x32 :: proc "contextless" (p: [^]u8) -> simd.u8x32 {
	return simd.from_array(cast(^[32]u8)(p)^)
}

load_f32x8 :: proc "contextless" (p: [^]f32) -> simd.f32x8 {
	return simd.from_array(cast(^[8]f32)(p)^)
}

store_f32x8 :: proc "contextless" (p: [^]f32, v: simd.f32x8) {
	a := simd.to_array(v)
	for lane in 0..<8 {
		p[lane] = a[lane]
	}
}

// Independent byte accumulators reduce horizontal reductions without allowing
// an 8-bit lane to exceed 255. Loads are through byte-aligned arrays, so caller
// alignment is irrelevant and no overlapping/out-of-range tail is touched.
@(export)
drbo_count_dual :: proc "c" (bytes: [^]u8, length: uintptr) -> uintptr {
	if length == 0 {
		return 0
	}
	newline: simd.u8x32 = u8('\n')
	offset: uintptr = 0
	total: uintptr = 0
	for length - offset >= 128 {
		pairs := (length - offset) / 64
		if pairs > 255 {
			pairs = 255
		}
		even: simd.u8x32 = 0
		odd: simd.u8x32 = 0
		end := offset + pairs * 64
		for offset < end {
			a := load_u8x32(bytes + offset)
			b := load_u8x32(bytes + offset + 32)
			even += simd.lanes_eq(a, newline) & u8(1)
			odd += simd.lanes_eq(b, newline) & u8(1)
			offset += 64
		}
		ea := simd.to_array(even)
		oa := simd.to_array(odd)
		for lane in 0..<32 {
			total += uintptr(ea[lane]) + uintptr(oa[lane])
		}
	}
	for length - offset >= 32 {
		value := load_u8x32(bytes + offset)
		hits := simd.lanes_eq(value, newline) & u8(1)
		total += uintptr(simd.reduce_add_bisect(hits))
		offset += 32
	}
	for offset < length {
		if bytes[offset] == u8('\n') {
			total += 1
		}
		offset += 1
	}
	return total
}

scalar_run :: proc "contextless" (x, y: [^]f32, speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) {
	for i: uintptr = 0; i < count; i += 1 {
		x[i] += speed[i]
		if x[i] > 1280.0 {
			x[i] = random(ctx) * -1280.0
		}
		y[i] += speed[i]
		if y[i] > 720.0 {
			y[i] = random(ctx) * -720.0
		}
	}
}

dense_both_wrap_run :: proc "contextless" (x, y: [^]f32, speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) -> uintptr {
	i: uintptr = 0
	for i < count {
		nx := x[i] + speed[i]
		ny := y[i] + speed[i]
		if !(nx > 1280.0 && ny > 720.0) {
			break
		}
		x[i] = random(ctx) * -1280.0
		y[i] = random(ctx) * -720.0
		i += 1
	}
	return i
}

// Odin keeps its own readable SIMD expression: vector motion and comparisons,
// scalar repair only for exceptional lanes, exact star order and x-before-y RNG.
// The C ABI contract requires x/y/speed to be distinct arrays.
@(export)
drbo_stars_block :: proc "c" (x, y: [^]f32, speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) {
	if count == 0 {
		return
	}
	max_x: simd.f32x8 = 1280.0
	max_y: simd.f32x8 = 720.0
	i: uintptr = 0
	for count - i >= 8 {
		vx := load_f32x8(x + i)
		vy := load_f32x8(y + i)
		vs := load_f32x8(speed + i)
		nx := vx + vs
		ny := vy + vs
		wrap_x := simd.lanes_gt(nx, max_x)
		wrap_y := simd.lanes_gt(ny, max_y)
		wraps := wrap_x | wrap_y
		if simd.reduce_or(wraps) == 0 {
			store_f32x8(x + i, nx)
			store_f32x8(y + i, ny)
			i += 8
			continue
		}
		if simd.reduce_and(wrap_x & wrap_y) != 0 {
			consumed := dense_both_wrap_run(x + i, y + i, speed + i, count - i, random, ctx)
			i += consumed
			continue
		}

		store_f32x8(x + i, nx)
		store_f32x8(y + i, ny)
		xm := simd.to_array(wrap_x)
		ym := simd.to_array(wrap_y)
		for lane in 0..<8 {
			index := i + uintptr(lane)
			if xm[lane] != 0 {
				x[index] = random(ctx) * -1280.0
			}
			if ym[lane] != 0 {
				y[index] = random(ctx) * -720.0
			}
		}
		i += 8
	}
	if i < count {
		scalar_run(x + i, y + i, speed + i, count - i, random, ctx)
	}
}
