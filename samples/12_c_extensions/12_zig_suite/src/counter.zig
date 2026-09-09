const std = @import("std");

// Thirty-two counters share one reduction. At most 255 loads contribute to
// a chunk, so each lane is in 0..255. Modular addition is exact under this
// bound. Keep chunk arithmetic explicitly usize: inferred @min results can
// be narrower than the original buffer length.
pub fn count(bytes: []const u8) usize {
    const V = @Vector(32, u8);
    const newline: V = @splat('\n');
    const one: V = @splat(1);
    const zero: V = @splat(0);
    var remaining = bytes;
    var total: usize = 0;
    while (remaining.len >= 32) {
        const blocks: usize = @min(remaining.len / 32, 255);
        const chunk = remaining[0 .. blocks * 32];
        var counts: V = @splat(0);
        var offset: usize = 0;
        while (offset < chunk.len) : (offset += 32) {
            const value: V = chunk[offset..][0..32].*;
            counts +%= @select(u8, value == newline, one, zero);
        }
        const wide: @Vector(32, u16) = @intCast(counts);
        total += @reduce(.Add, wide);
        remaining = remaining[chunk.len..];
    }
    for (remaining) |byte| total += @intFromBool(byte == '\n');
    return total;
}

test "every chunk boundary retains 255 hits per lane" {
    var bytes: [3 * 8160 + 65]u8 = undefined;
    @memset(&bytes, '\n');
    for (0..65) |offset| {
        for ([_]usize{ 0, 1, 31, 32, 33, 8128, 8159, 8160, 8161, 16320, 24480 }) |length| {
            try std.testing.expectEqual(length, count(bytes[offset..][0..length]));
        }
    }
}
