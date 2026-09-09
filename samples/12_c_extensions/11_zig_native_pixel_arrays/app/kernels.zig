const std = @import("std");

/// Count LF bytes, not logical lines. NUL bytes are ordinary input.
/// Each vector load stays inside the slice and needs only byte alignment.
pub fn countNewlines(data: []const u8) usize {
    const width = 16;
    const Bytes = @Vector(width, u8);
    const newline: Bytes = @splat('\n');
    const ones: Bytes = @splat(1);
    const zeros: Bytes = @splat(0);
    var i: usize = 0;
    var count: usize = 0;

    while (data.len - i >= width) : (i += width) {
        const bytes: Bytes = data[i..][0..width].*;
        const flags = @select(u8, bytes == newline, ones, zeros);
        count += @reduce(.Add, flags);
    }
    for (data[i..]) |byte| {
        count += @intFromBool(byte == '\n');
    }
    return count;
}

/// Fill four pixels per vector store, then handle the remaining pixels.
/// This is instructional; benchmark against @memset for a real workload.
pub fn fillPixels(pixels: []u32, color: u32) void {
    const width = 4;
    const colors: @Vector(width, u32) = @splat(color);
    var i: usize = 0;
    while (pixels.len - i >= width) : (i += width) {
        pixels[i..][0..width].* = colors;
    }
    for (pixels[i..]) |*pixel| pixel.* = color;
}

fn scalarCount(data: []const u8) usize {
    var count: usize = 0;
    for (data) |byte| {
        if (byte == '\n') count += 1;
    }
    return count;
}

test "empty, text, NUL and binary buffers" {
    try std.testing.expectEqual(@as(usize, 0), countNewlines(""));
    try std.testing.expectEqual(@as(usize, 0), countNewlines("unterminated"));
    try std.testing.expectEqual(@as(usize, 2), countNewlines("one\ntwo\n"));
    try std.testing.expectEqual(@as(usize, 2), countNewlines("\x00\n\xff\r\n"));
}

test "all 65536 masks in a sixteen-byte block" {
    var data: [16]u8 = undefined;
    for (0..65536) |bits| {
        const mask: u16 = @intCast(bits);
        for (&data, 0..) |*byte, i| {
            byte.* = if ((mask & (@as(u16, 1) << @intCast(i))) != 0) '\n' else 0xff;
        }
        try std.testing.expectEqual(@as(usize, @popCount(mask)), countNewlines(&data));
    }
}

test "every alignment and tail length agree with scalar counting" {
    var data: [288]u8 = undefined;
    for (&data, 0..) |*byte, i| {
        byte.* = if (i % 7 == 0) '\n' else @truncate(i * 31);
    }
    for (0..32) |offset| {
        for (0..258) |len| {
            const input = data[offset..][0..len];
            try std.testing.expectEqual(scalarCount(input), countNewlines(input));
        }
    }
    @memset(&data, '\n');
    for (0..32) |offset| {
        for (0..258) |len| {
            try std.testing.expectEqual(len, countNewlines(data[offset..][0..len]));
        }
    }
}

test "vector fills preserve guards at every offset and tail" {
    var data: [48]u32 = undefined;
    for (0..4) |offset| {
        for (0..37) |len| {
            @memset(&data, 0xdeadbeef);
            const start = offset + 1;
            fillPixels(data[start..][0..len], 0xff00ff00);
            for (data, 0..) |pixel, i| {
                const expected: u32 = if (i >= start and i < start + len) 0xff00ff00 else 0xdeadbeef;
                try std.testing.expectEqual(expected, pixel);
            }
        }
    }
}
