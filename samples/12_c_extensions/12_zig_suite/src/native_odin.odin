package rivals

// Portable Odin peers for the public native kernel ABI. Keep these ordinary
// Odin: no assembly, no ISA intrinsics, no heap allocation, and no hidden
// relaxation of the observable C/Zig semantics.

Star :: struct {
	x: f32,
	y: f32,
	speed: f32,
}

Scanner :: struct {
	position:  i32,
	increment: i32,
	previous:  i32,
	pixels:    [100]u32,
}

#assert(size_of(Star) == 12)
#assert(size_of(Scanner) == 412)

@(export)
drbo_square :: proc "c" (value: i32, out: ^i32) -> i32 {
	if value < -46340 || value > 46340 {
		return 1
	}
	out^ = value * value
	return 0
}

// Validate the whole input before publishing output, including exact in-place
// operation. Partial overlap remains outside the shared ABI contract.
@(export)
drbo_squares :: proc "c" (input: [^]i32, output: [^]i32, count: uintptr) #no_bounds_check -> i32 {
	for i: uintptr = 0; i < count; i += 1 {
		value := input[i]
		if value < -46340 || value > 46340 {
			return 1
		}
	}
	for i: uintptr = 0; i < count; i += 1 {
		output[i] = input[i] * input[i]
	}
	return 0
}

// One accumulator intentionally preserves source-order additions. There is no
// multiply here for a compiler to contract into an FMA.
@(export)
drbo_sum_ordered :: proc "c" (accumulator: f64, values: [^]f64, count: uintptr) #no_bounds_check -> f64 {
	sum := accumulator
	for i: uintptr = 0; i < count; i += 1 {
		sum += values[i]
	}
	return sum
}

// A source-unrolled candidate that still uses the same single accumulator and
// therefore the same left-to-right evaluation order.
@(export)
drbo_sum_unrolled :: proc "c" (accumulator: f64, values: [^]f64, count: uintptr) #no_bounds_check -> f64 {
	sum := accumulator
	i: uintptr = 0
	for count - i >= 4 {
		sum += values[i]
		sum += values[i + 1]
		sum += values[i + 2]
		sum += values[i + 3]
		i += 4
	}
	for i < count {
		sum += values[i]
		i += 1
	}
	return sum
}

@(export)
drbo_stars_scalar :: proc "c" (stars: [^]Star, count: uintptr, random: Random, ctx: rawptr) #no_bounds_check {
	for i: uintptr = 0; i < count; i += 1 {
		stars[i].x += stars[i].speed
		if stars[i].x > 1280.0 {
			stars[i].x = random(ctx) * -1280.0
		}
		stars[i].y += stars[i].speed
		if stars[i].y > 720.0 {
			stars[i].y = random(ctx) * -720.0
		}
	}
}

// The tuned SoA implementation is the already-validated Odin competitor. This
// keeps one implementation of the exceptional/RNG ordering logic.
@(export)
drbo_stars_soa :: proc "c" (#no_alias x, #no_alias y: [^]f32, #no_alias speed: [^]f32, count: uintptr, random: Random, ctx: rawptr) {
	drbo_stars_block(x, y, speed, count, random, ctx)
}

@(export)
drbo_count_scalar :: proc "c" (bytes: [^]u8, count: uintptr) #no_bounds_check -> uintptr {
	total: uintptr = 0
	for i: uintptr = 0; i < count; i += 1 {
		if bytes[i] == u8('\n') {
			total += 1
		}
	}
	return total
}

@(export)
drbo_count_blocked :: proc "c" (bytes: [^]u8, count: uintptr) -> uintptr {
	return drbo_count_dual(bytes, count)
}

@(export)
drbo_scanner_reset :: proc "c" (state: ^Scanner) {
	state.position = 0
	state.increment = 1
	state.previous = -1
	for i in 0..<100 {
		state.pixels[i] = 0xff000000
	}
}

@(export)
drbo_scanner_frame :: proc "c" (state: ^Scanner) -> [^]u32 #no_bounds_check {
	if state.previous != state.position {
		if state.previous >= 0 {
			old := int(state.previous * 10)
			for i in 0..<10 {
				state.pixels[old + i] = 0xff000000
			}
		}
		row := int(state.position * 10)
		for i in 0..<10 {
			state.pixels[row + i] = 0xff00ff00
		}
		state.previous = state.position
	}
	state.position += state.increment
	if state.increment > 0 && state.position >= 10 {
		state.increment = -1
		state.position = 9
	} else if state.increment < 0 && state.position < 0 {
		state.increment = 1
		state.position = 1
	}
	return &state.pixels[0]
}
