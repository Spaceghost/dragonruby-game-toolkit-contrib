const std = @import("std");
const regex = @import("regex.zig");

pub const Star = extern struct { x: f32, y: f32, speed: f32 };
pub const Random = *const fn (?*anyopaque) callconv(.c) f32;

// The original C square has undefined behavior on signed overflow. Report
// that case without modifying the output rather than silently wrapping.
export fn drbz_square(value: c_int, out: *c_int) c_int {
    const result = @mulWithOverflow(value, value);
    if (result[1] != 0) return 1;
    out.* = result[0];
    return 0;
}

// Identical input/output is supported; partial overlap is not. Validate all
// inputs before publishing any output, including on the vectorized path.
export fn drbz_squares(input: [*c]const i32, output: [*c]i32, len: usize) c_int {
    if (len == 0) return 0;
    for (input[0..len]) |value| {
        if (value < -46340 or value > 46340) return 1;
    }
    const V = @Vector(8, i32);
    var i: usize = 0;
    while (len - i >= 8) : (i += 8) {
        const values: V = input[i..][0..8].*;
        output[i..][0..8].* = values * values;
    }
    while (i < len) : (i += 1) output[i] = input[i] * input[i];
    return 0;
}

// Preserve the C adder's left-to-right IEEE-754 evaluation. Reassociation
// would change answers for cancellation and is not an implicit optimization.
export fn drbz_sum_ordered(accumulator: f64, values: [*c]const f64, len: usize) f64 {
    @setFloatMode(.strict);
    if (len == 0) return accumulator;
    var sum = accumulator;
    for (values[0..len]) |value| sum += value;
    return sum;
}
export fn drbz_sum_unrolled(accumulator: f64, values: [*c]const f64, len: usize) f64 {
    @setFloatMode(.strict);
    if (len == 0) return accumulator;
    var sum = accumulator;
    var i: usize = 0;
    while (len - i >= 4) : (i += 4) {
        sum += values[i]; sum += values[i + 1];
        sum += values[i + 2]; sum += values[i + 3];
    }
    for (values[i..len]) |value| sum += value;
    return sum;
}

export fn drbz_stars_scalar(stars: [*c]Star, len: usize, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    if (len == 0) return;
    for (stars[0..len]) |*star| {
        star.x += star.speed;
        if (star.x > 1280) star.x = random(context) * -1280;
        star.y += star.speed;
        if (star.y > 720) star.y = random(context) * -720;
    }
}

// Caller-owned SoA storage is reused every tick. Preserve the original RNG
// order (star order, x before y); no-wrap blocks make no callbacks.
export fn drbz_stars_soa(x: [*c]f32, y: [*c]f32, speed: [*c]const f32, len: usize, random: Random, context: ?*anyopaque) void {
    @setFloatMode(.strict);
    if (len == 0) return;
    const V = @Vector(8, f32);
    const max_x: V = @splat(1280);
    const max_y: V = @splat(720);
    var i: usize = 0;
    while (len - i >= 8) : (i += 8) {
        const vx: V = x[i..][0..8].*;
        const vy: V = y[i..][0..8].*;
        const vs: V = speed[i..][0..8].*;
        const nx = vx + vs;
        const ny = vy + vs;
        if (@reduce(.Or, (nx > max_x) | (ny > max_y))) {
            inline for (0..8) |lane| {
                x[i + lane] = if (nx[lane] > 1280) random(context) * -1280 else nx[lane];
                y[i + lane] = if (ny[lane] > 720) random(context) * -720 else ny[lane];
            }
        } else {
            x[i..][0..8].* = nx;
            y[i..][0..8].* = ny;
        }
    }
    while (i < len) : (i += 1) {
        x[i] += speed[i];
        if (x[i] > 1280) x[i] = random(context) * -1280;
        y[i] += speed[i];
        if (y[i] > 720) y[i] = random(context) * -720;
    }
}

export fn drbz_count_scalar(bytes: [*c]const u8, len: usize) usize {
    if (len == 0) return 0;
    var total: usize = 0;
    for (bytes[0..len]) |byte| total += @intFromBool(byte == '\n');
    return total;
}

// Amortize reductions across up to 255 vector loads. Widen before reducing:
// each u8 lane can hold 255 hits, but their sum needs more than eight bits.
export fn drbz_count_blocked(bytes: [*c]const u8, len: usize) usize {
    if (len == 0) return 0;
    const V = @Vector(32, u8);
    const newline: V = @splat('\n');
    const one: V = @splat(1);
    const zero: V = @splat(0);
    var total: usize = 0;
    var i: usize = 0;
    while (len - i >= 32) {
        const blocks = @min((len - i) / 32, 255);
        var counts: V = @splat(0);
        for (0..blocks) |_| {
            const value: V = bytes[i..][0..32].*;
            counts += @select(u8, value == newline, one, zero);
            i += 32;
        }
        const wide: @Vector(32, u16) = @intCast(counts);
        total += @reduce(.Add, wide);
    }
    for (bytes[i..len]) |byte| total += @intFromBool(byte == '\n');
    return total;
}

pub const Scanner = extern struct { position: i32, increment: i32, previous: i32, pixels: [100]u32 };

// This scanner owns its persistent pixels. Callers may read them until the
// next frame but must not modify them between frames.
export fn drbz_scanner_reset(state: *Scanner) void {
    state.* = .{ .position = 0, .increment = 1, .previous = -1, .pixels = @splat(0xff000000) };
}
export fn drbz_scanner_frame(state: *Scanner) [*]const u32 {
    if (state.previous != state.position) {
        if (state.previous >= 0) {
            const old: usize = @intCast(state.previous * 10);
            @memset(state.pixels[old..][0..10], 0xff000000);
        }
        const row: usize = @intCast(state.position * 10);
        @memset(state.pixels[row..][0..10], 0xff00ff00);
        state.previous = state.position;
    }
    state.position += state.increment;
    if (state.increment > 0 and state.position >= 10) {
        state.increment = -1; state.position = 9;
    } else if (state.increment < 0 and state.position < 0) {
        state.increment = 1; state.position = 1;
    }
    return &state.pixels;
}
comptime { _ = regex; }

test "checked square and zero-length C buffers" {
    var result: c_int = 123;
    try std.testing.expectEqual(@as(c_int, 1), drbz_square(46341, &result));
    try std.testing.expectEqual(@as(c_int, 123), result);
    try std.testing.expectEqual(@as(c_int, 0), drbz_squares(null, null, 0));
    try std.testing.expectEqual(@as(usize, 0), drbz_count_blocked(null, 0));
    try std.testing.expectEqual(@as(f64, 7), drbz_sum_ordered(7, null, 0));
}
test "ordered sums retain cancellation" {
    const values = [_]f64{ 1e16, 1, -1e16, 3, -0.0, 1e-300, -1e-300 };
    const a = drbz_sum_ordered(0, &values, values.len);
    const b = drbz_sum_unrolled(0, &values, values.len);
    try std.testing.expectEqual(@as(u64, @bitCast(a)), @as(u64, @bitCast(b)));
}
test "blocked counters do not overflow lanes or read tails" {
    var buffer: [65570]u8 = undefined;
    @memset(&buffer, '\n');
    for (0..33) |offset| {
        for ([_]usize{ 0, 1, 15, 16, 31, 32, 33, 8159, 8160, 8161, 16320, 65536 }) |len| {
            try std.testing.expectEqual(len, drbz_count_blocked(buffer[offset..].ptr, len));
        }
    }
    for (&buffer, 0..) |*byte, i| byte.* = @truncate(i * 17);
    for (0..33) |offset| {
        for (0..260) |len| {
            try std.testing.expectEqual(drbz_count_scalar(buffer[offset..].ptr, len), drbz_count_blocked(buffer[offset..].ptr, len));
        }
    }
}
