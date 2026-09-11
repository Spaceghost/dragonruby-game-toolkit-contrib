package rivals

Packed_Sprite :: struct {
	x:       f32,
	y:       f32,
	w:       f32,
	h:       f32,
	path_id: uintptr,
}

Starfield :: struct {
	x:         [^]f32,
	y:         [^]f32,
	speed:     [^]f32,
	sprites:   [^]Packed_Sprite,
	len:       uintptr,
	rng_state: u64,
}

Batch_Sink :: proc "c" (ctx: rawptr, sprites: [^]Packed_Sprite, count: uintptr)

when size_of(rawptr) == 8 {
	#assert(size_of(Packed_Sprite) == 24)
	#assert(align_of(Packed_Sprite) == 8)
	#assert(size_of(Starfield) == 48)
}

storage_bytes :: proc "contextless" (count: uintptr) -> (uintptr, bool) {
	if count > max(uintptr) / 12 { return 0, false }
	floats := count * 12
	if floats > max(uintptr) - 7 { return 0, false }
	sprite_start := (floats + 7) & ~uintptr(7)
	if count > (max(uintptr) - sprite_start) / 24 { return 0, false }
	return sprite_start + count * 24, true
}

@(export)
drbo_starfield_storage_bytes :: proc "c" (count: uintptr) -> uintptr {
	bytes, ok := storage_bytes(count)
	return bytes if ok else 0
}

next_random :: #force_inline proc "contextless" (field: ^Starfield) -> f32 {
	x := field.rng_state
	x ~= x >> 12
	x ~= x << 25
	x ~= x >> 27
	field.rng_state = x
	mixed := x * u64(0x2545F4914F6CDD1D)
	top := u32(mixed >> 40)
	return f32(top) / 16777215.0
}

owned_random :: #force_inline proc "c" (raw: rawptr) -> f32 {
	return next_random(cast(^Starfield)raw)
}

@(export)
drbo_starfield_init :: proc "c" (storage: rawptr, available: uintptr, count: uintptr, seed: u64, out: ^Starfield) -> i32 #no_bounds_check {
	needed, ok := storage_bytes(count)
	if !ok || needed > available { return 1 }
	initial_seed := seed
	if initial_seed == 0 { initial_seed = 1 }
	if count == 0 {
		out^ = Starfield{rng_state=initial_seed}
		return 0
	}
	if storage == nil { return 1 }
	if uintptr(storage) % uintptr(align_of(Packed_Sprite)) != 0 { return 2 }

	base := cast([^]u8)storage
	one := count * 4
	floats := one * 3
	sprite_start := (floats + 7) & ~uintptr(7)
	x := cast([^]f32)storage
	y := cast([^]f32)&base[one]
	speed := cast([^]f32)&base[one * 2]
	sprites := cast([^]Packed_Sprite)&base[sprite_start]
	out^ = Starfield{x=x, y=y, speed=speed, sprites=sprites, len=count, rng_state=initial_seed}
	for i: uintptr = 0; i < count; i += 1 {
		x[i] = next_random(out) * -1280.0
		y[i] = next_random(out) * -720.0
		speed[i] = 1.0 + next_random(out) * 4.0
	}
	drbo_starfield_pack(out)
	return 0
}

@(export)
drbo_starfield_update :: proc "c" (field: ^Starfield) {
	if field.len == 0 { return }
	// Same stars_core as the exported generic path, but the force-inlined core
	// sees this owned RNG as a compile-time-known procedure and can devirtualize it.
	stars_core(field.x, field.y, field.speed, field.len, owned_random, field)
}

@(export)
drbo_starfield_pack :: proc "c" (field: ^Starfield) #no_bounds_check {
	for i: uintptr = 0; i < field.len; i += 1 {
		field.sprites[i] = Packed_Sprite{x=field.x[i], y=field.y[i], w=4.0, h=4.0, path_id=1}
	}
}

pack_positions :: proc "contextless" (field: ^Starfield) #no_bounds_check {
	for i: uintptr = 0; i < field.len; i += 1 {
		field.sprites[i].x = field.x[i]
		field.sprites[i].y = field.y[i]
	}
}

@(export)
drbo_starfield_update_pack :: proc "c" (field: ^Starfield) {
	drbo_starfield_update(field)
	pack_positions(field)
}

@(export)
drbo_starfield_frame :: proc "c" (field: ^Starfield, sink: Batch_Sink, ctx: rawptr) {
	drbo_starfield_update_pack(field)
	sink(ctx, field.sprites, field.len)
}
