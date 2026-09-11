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
    while (bytes.len - offset >= 128) {
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
    while (bytes.len - offset >= 32) {
        const value: V = bytes[offset..][0..32].*;
        // One block has at most 32 hits, so byte reduction is exact.
        total += @reduce(.Add, @select(u8, value == newline, one, zero));
        offset += 32;
    }
    // Two bounded vector fragments leave at most seven scalar tail bytes.
    inline for (.{ 16, 8 }) |width| {
        if (bytes.len - offset >= width) {
            const Tail = @Vector(width, u8);
            const value: Tail = bytes[offset..][0..width].*;
            const hits = value == @as(Tail, @splat('\n'));
            total += @reduce(.Add, @select(u8, hits, @as(Tail, @splat(1)), @as(Tail, @splat(0))));
            offset += width;
        }
    }
    for (bytes[offset..]) |byte| total += @intFromBool(byte == '\n');
    return total;
}

// Externally visible on purpose: keeping this tiny fallback as a real ABI
// function prevents whole-unit argument specialization from turning the common
// count=8 call site into an eight-copy ARM code balloon. It is also useful as a
// direct scalar fallback for embeddings that need identical RNG ordering.
export fn drbz_stars_scalar_fallback(x: [*c]f32, y: [*c]f32, speed: [*c]const f32, count: usize, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    if (count == 0) return;
    var i: usize = 0;
    while (i < count) : (i += 1) {
        x[i] += speed[i];
        if (x[i] > 1280) x[i] = random(context) * -1280;
        y[i] += speed[i];
        if (y[i] > 720) y[i] = random(context) * -720;
    }
}

noinline fn denseBothWrapRun(x: [*]f32, y: [*]f32, speed: [*]const f32, count: usize, random: Random, context: ?*anyopaque) usize {
    @setFloatMode(.strict);
    var i: usize = 0;
    while (i < count) : (i += 1) {
        const nx = x[i] + speed[i];
        const ny = y[i] + speed[i];
        if (!(nx > 1280 and ny > 720)) break;
        x[i] = random(context) * -1280;
        y[i] = random(context) * -720;
    }
    return i;
}

pub fn starsBlock(x: []f32, y: []f32, speed: []const f32, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    const V = @Vector(8, f32);
    const max_x: V = @splat(1280);
    const max_y: V = @splat(720);
    var i: usize = 0;
    while (x.len - i >= 8) {
        const vx: V = x[i..][0..8].*;
        const vy: V = y[i..][0..8].*;
        const vs: V = speed[i..][0..8].*;
        const nx = vx + vs;
        const ny = vy + vs;
        const wrap_x = nx > max_x;
        const wrap_y = ny > max_y;
        if (!@reduce(.Or, wrap_x | wrap_y)) {
            x[i..][0..8].* = nx;
            y[i..][0..8].* = ny;
            i += 8;
            continue;
        }
        if (@reduce(.And, wrap_x & wrap_y)) {
            const consumed = denseBothWrapRun(x.ptr + i, y.ptr + i, speed.ptr + i, x.len - i, random, context);
            i += consumed;
            continue;
        }

        // Mixed blocks already paid for nx/ny. Publish those vector results,
        // then repair only lanes that wrapped. The callback contract requires
        // RNG state to be independent of these arrays, so this retains the
        // observable RNG sequence while avoiding eight repeated scalar adds and
        // an ABI call per exceptional block. ctz visits stars in ascending order;
        // x is still consumed before y for a star that wraps both axes.
        x[i..][0..8].* = nx;
        y[i..][0..8].* = ny;
        const x_bits: u8 = @bitCast(wrap_x);
        const y_bits: u8 = @bitCast(wrap_y);
        var wrapped = x_bits | y_bits;
        while (wrapped != 0) {
            const lane: u3 = @intCast(@ctz(wrapped));
            const bit = @as(u8, 1) << lane;
            const index = i + @as(usize, lane);
            if (x_bits & bit != 0) x[index] = random(context) * -1280;
            if (y_bits & bit != 0) y[index] = random(context) * -720;
            wrapped &= wrapped - 1;
        }
        i += 8;
    }
    if (i < x.len) drbz_stars_scalar_fallback(x.ptr + i, y.ptr + i, speed.ptr + i, x.len - i, random, context);
}

export fn drbz_count_dual(bytes: [*c]const u8, length: usize) usize {
    return if (length == 0) 0 else countDual(bytes[0..length]);
}
export fn drbz_stars_block(x: [*c]f32, y: [*c]f32, speed: [*c]const f32, count: usize, random: Random, context: ?*anyopaque) void {
    if (count == 0) return;
    starsBlock(x[0..count], y[0..count], speed[0..count], random, context);
}
