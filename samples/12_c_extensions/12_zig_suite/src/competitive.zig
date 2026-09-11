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

// This is genuinely the exceptional path. The cold hint asks LLVM to favor
// compact code instead of unrolling the callback-heavy scalar loop on ARM.
noinline fn scalarBlock(x: [*]f32, y: [*]f32, speed: [*]const f32, count: usize, random: Random, context: ?*anyopaque) void {
    @branchHint(.cold);
    @setFloatMode(.strict);
    var i: usize = 0;
    while (i < count) : (i += 1) {
        x[i] += speed[i];
        if (x[i] > 1280) x[i] = random(context) * -1280;
        y[i] += speed[i];
        if (y[i] > 720) y[i] = random(context) * -720;
    }
}

// When a whole vector has both axes wrapping, continue scalar only while that
// condition remains true. The all-wrap workload becomes one dense run instead
// of paying vector detection plus an outlined call for every eight stars.
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
        const wraps = wrap_x | wrap_y;
        if (!@reduce(.Or, wraps)) {
            x[i..][0..8].* = nx;
            y[i..][0..8].* = ny;
            i += 8;
            continue;
        }
        if (@reduce(.And, wrap_x & wrap_y)) {
            const consumed = denseBothWrapRun(x.ptr + i, y.ptr + i, speed.ptr + i, x.len - i, random, context);
            // The first eight lanes were proven dense, so progress is guaranteed.
            i += consumed;
            continue;
        }
        // Preserve star order and x-before-y RNG consumption for mixed blocks.
        scalarBlock(x.ptr + i, y.ptr + i, speed.ptr + i, 8, random, context);
        i += 8;
    }
    if (i < x.len) scalarBlock(x.ptr + i, y.ptr + i, speed.ptr + i, x.len - i, random, context);
}

export fn drbz_count_dual(bytes: [*c]const u8, length: usize) usize {
    return if (length == 0) 0 else countDual(bytes[0..length]);
}
export fn drbz_stars_block(x: [*c]f32, y: [*c]f32, speed: [*c]const f32, count: usize, random: Random, context: ?*anyopaque) void {
    if (count == 0) return;
    starsBlock(x[0..count], y[0..count], speed[0..count], random, context);
}
