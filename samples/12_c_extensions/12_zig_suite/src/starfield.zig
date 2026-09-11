const std = @import("std");
const rivals = @import("competitive.zig");

pub const Sprite = extern struct {
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    path_id: usize,
};

pub const Starfield = extern struct {
    x: [*c]f32,
    y: [*c]f32,
    speed: [*c]f32,
    sprites: [*c]Sprite,
    len: usize,
    rng_state: u64,
};

pub const BatchSink = *const fn (?*anyopaque, [*c]const Sprite, usize) callconv(.c) void;

fn mul(a: usize, b: usize) ?usize {
    const r = @mulWithOverflow(a, b);
    return if (r[1] == 0) r[0] else null;
}

pub fn storageBytes(count: usize) ?usize {
    const floats = mul(count, 3 * @sizeOf(f32)) orelse return null;
    const sprite_start = std.mem.alignForward(usize, floats, @alignOf(Sprite));
    const sprite_bytes = mul(count, @sizeOf(Sprite)) orelse return null;
    const total = @addWithOverflow(sprite_start, sprite_bytes);
    return if (total[1] == 0) total[0] else null;
}

export fn drbz_starfield_storage_bytes(count: usize) usize {
    return storageBytes(count) orelse 0;
}

fn nextRandom(field: *Starfield) f32 {
    // xorshift64*: deterministic test/application state, no libc rand() lock/state.
    var x = field.rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    field.rng_state = x;
    const mixed = x *% 0x2545F4914F6CDD1D;
    const top: u24 = @truncate(mixed >> 40);
    return @as(f32, @floatFromInt(top)) / 16777215.0;
}

fn randomCallback(context: ?*anyopaque) callconv(.c) f32 {
    const field: *Starfield = @ptrCast(@alignCast(context.?));
    return nextRandom(field);
}

export fn drbz_starfield_init(storage: ?*anyopaque, storage_bytes: usize, count: usize, seed: u64, out: *Starfield) c_int {
    const needed = storageBytes(count) orelse return 1;
    if (needed > storage_bytes) return 1;
    if (count == 0) {
        out.* = .{ .x = null, .y = null, .speed = null, .sprites = null, .len = 0, .rng_state = if (seed == 0) 1 else seed };
        return 0;
    }
    const raw = storage orelse return 1;
    if (@intFromPtr(raw) % @alignOf(Sprite) != 0) return 2;
    const base: [*]u8 = @ptrCast(raw);
    const one = count * @sizeOf(f32);
    const sprite_start = std.mem.alignForward(usize, one * 3, @alignOf(Sprite));
    const x: [*]f32 = @ptrCast(@alignCast(base));
    const y: [*]f32 = @ptrCast(@alignCast(base + one));
    const speed: [*]f32 = @ptrCast(@alignCast(base + one * 2));
    const sprites: [*]Sprite = @ptrCast(@alignCast(base + sprite_start));
    out.* = .{ .x = x, .y = y, .speed = speed, .sprites = sprites, .len = count, .rng_state = if (seed == 0) 1 else seed };
    for (0..count) |i| {
        x[i] = nextRandom(out) * -1280.0;
        y[i] = nextRandom(out) * -720.0;
        speed[i] = 1.0 + nextRandom(out) * 4.0;
    }
    // Width, height and asset never vary per frame. Establish them once; the
    // hot frame pack can thereafter write only the two coordinates.
    drbz_starfield_pack(out);
    return 0;
}

export fn drbz_starfield_update(field: *Starfield) void {
    if (field.len == 0) return;
    rivals.starsBlock(field.x[0..field.len], field.y[0..field.len], field.speed[0..field.len], randomCallback, field);
}

// Full public pack re-establishes the complete canonical sprite representation.
export fn drbz_starfield_pack(field: *Starfield) void {
    for (0..field.len) |i| {
        field.sprites[i] = .{ .x = field.x[i], .y = field.y[i], .w = 4.0, .h = 4.0, .path_id = 1 };
    }
}

fn packPositions(field: *Starfield) void {
    for (0..field.len) |i| {
        field.sprites[i].x = field.x[i];
        field.sprites[i].y = field.y[i];
    }
}

export fn drbz_starfield_update_pack(field: *Starfield) void {
    drbz_starfield_update(field);
    packPositions(field);
}

// One native update, coordinate-only packing pass, one batch boundary. The sink
// must consume borrowed records before returning and must not mutate/re-enter.
export fn drbz_starfield_frame(field: *Starfield, sink: BatchSink, context: ?*anyopaque) void {
    drbz_starfield_update_pack(field);
    sink(context, field.sprites, field.len);
}

fn testSink(raw: ?*anyopaque, sprites: [*c]const Sprite, count: usize) callconv(.c) void {
    const seen: *usize = @ptrCast(@alignCast(raw.?));
    for (0..count) |i| {
        std.debug.assert(sprites[i].w == 4 and sprites[i].h == 4 and sprites[i].path_id == 1);
        seen.* +%= @as(usize, @as(u32, @bitCast(sprites[i].x)));
        seen.* +%= @as(usize, @as(u32, @bitCast(sprites[i].y)));
    }
}

test "persistent starfield packs one borrowed batch without allocation" {
    const count = 17;
    const needed = storageBytes(count).?;
    var storage: [2048]u8 align(@alignOf(Sprite)) = undefined;
    try std.testing.expect(needed <= storage.len);
    var field: Starfield = undefined;
    try std.testing.expectEqual(@as(c_int, 0), drbz_starfield_init(&storage, storage.len, count, 0x12345678, &field));
    field.x[0] = 1280.0;
    field.y[0] = 720.0;
    field.speed[0] = 1.0;
    const before = field.rng_state;
    var seen: usize = 0;
    drbz_starfield_frame(&field, testSink, &seen);
    try std.testing.expect(field.rng_state != before);
    try std.testing.expect(seen != 0);
    for (0..count) |i| {
        try std.testing.expectEqual(@as(u32, @bitCast(field.x[i])), @as(u32, @bitCast(field.sprites[i].x)));
        try std.testing.expectEqual(@as(u32, @bitCast(field.y[i])), @as(u32, @bitCast(field.sprites[i].y)));
        try std.testing.expectEqual(@as(f32, 4), field.sprites[i].w);
        try std.testing.expectEqual(@as(f32, 4), field.sprites[i].h);
        try std.testing.expectEqual(@as(usize, 1), field.sprites[i].path_id);
    }
}

test "full pack restores invariant render fields while hot pack need not rewrite them" {
    var storage: [512]u8 align(@alignOf(Sprite)) = undefined;
    var field: Starfield = undefined;
    try std.testing.expectEqual(@as(c_int, 0), drbz_starfield_init(&storage, storage.len, 4, 7, &field));
    field.sprites[2].w = 99;
    field.sprites[2].h = 98;
    field.sprites[2].path_id = 97;
    drbz_starfield_pack(&field);
    try std.testing.expectEqual(@as(f32, 4), field.sprites[2].w);
    try std.testing.expectEqual(@as(f32, 4), field.sprites[2].h);
    try std.testing.expectEqual(@as(usize, 1), field.sprites[2].path_id);
}

test "storage contract handles zero, short and misaligned buffers" {
    var field: Starfield = undefined;
    try std.testing.expectEqual(@as(c_int, 0), drbz_starfield_init(null, 0, 0, 0, &field));
    try std.testing.expectEqual(@as(usize, 0), field.len);
    var storage: [128]u8 align(@alignOf(Sprite)) = undefined;
    const needed = storageBytes(4).?;
    try std.testing.expectEqual(@as(c_int, 1), drbz_starfield_init(&storage, needed - 1, 4, 1, &field));
    try std.testing.expectEqual(@as(c_int, 2), drbz_starfield_init(@ptrCast(&storage[1]), storage.len - 1, 1, 1, &field));
}
