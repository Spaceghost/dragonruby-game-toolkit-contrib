//! Matched, readable competitors to competitive.c. Prior implementations remain
//! separate baselines. No fast math, allocation, ISA intrinsics or dispatch table.
const Random = *const fn (?*anyopaque) callconv(.c) f32;

pub fn countDual(bytes: []const u8) usize {
    const V = @Vector(32, u8);
    const newline: V = @splat('\n');
    const one: V = @splat(1);
    const zero: V = @splat(0);
    var offset: usize = 0;
    var total: usize = 0;
    while (bytes.len - offset >= 64) {
        const pairs: usize = @min((bytes.len - offset) / 64, 255);
        const end = offset + pairs * 64;
        var even: V = zero;
        var odd: V = zero;
        while (offset < end) : (offset += 64) {
            const a: V = bytes[offset..][0..32].*;
            const b: V = bytes[offset + 32 ..][0..32].*;
            even +%= @select(u8, a == newline, one, zero);
            odd +%= @select(u8, b == newline, one, zero);
        }
        // Each lane <= 255; widened pair <= 510; the reduction <= 16320.
        const wide_even: @Vector(32, u16) = @intCast(even);
        const wide_odd: @Vector(32, u16) = @intCast(odd);
        total += @reduce(.Add, wide_even + wide_odd);
    }
    if (bytes.len - offset >= 32) {
        const value: V = bytes[offset..][0..32].*;
        // One block has at most 32 hits, so byte reduction is exact.
        total += @reduce(.Add, @select(u8, value == newline, one, zero));
        offset += 32;
    }
    for (bytes[offset..]) |byte| total += @intFromBool(byte == '\n');
    return total;
}

fn scalarBlock(x: []f32, y: []f32, speed: []const f32, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    for (x, y, speed) |*px, *py, delta| {
        px.* += delta;
        if (px.* > 1280) px.* = random(context) * -1280;
        py.* += delta;
        if (py.* > 720) py.* = random(context) * -720;
    }
}

pub fn starsBlock(x: []f32, y: []f32, speed: []const f32, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    const V = @Vector(8, f32);
    var i: usize = 0;
    while (x.len - i >= 8) : (i += 8) {
        const vx: V = x[i..][0..8].*;
        const vy: V = y[i..][0..8].*;
        const vs: V = speed[i..][0..8].*;
        const nx = vx + vs;
        const ny = vy + vs;
        if (@reduce(.Or, (nx > @as(V, @splat(1280))) | (ny > @as(V, @splat(720))))) {
            // Ordered exceptional loop, not eight unrolled copies. Preserve
            // the array state visible before the first possible RNG callback.
            scalarBlock(x[i..][0..8], y[i..][0..8], speed[i..][0..8], random, context);
        } else {
            x[i..][0..8].* = nx;
            y[i..][0..8].* = ny;
        }
    }
    scalarBlock(x[i..], y[i..], speed[i..], random, context);
}

export fn drbz_count_dual(bytes: [*c]const u8, length: usize) usize {
    return if (length == 0) 0 else countDual(bytes[0..length]);
}
export fn drbz_stars_block(x: [*c]f32, y: [*c]f32, speed: [*c]const f32, count: usize, random: Random, context: ?*anyopaque) void {
    if (count == 0) return;
    starsBlock(x[0..count], y[0..count], speed[0..count], random, context);
}
