const std = @import("std");
const kernels = @import("kernels.zig");
const scanner = @import("scanner.zig");

var current_scanner: scanner.Scanner = .{};

// Only C-compatible pointers and integers cross this boundary. See native.h.
export fn drb_zig_scanner_reset() void {
    current_scanner = .{};
}

export fn drb_zig_scanner_frame(pixels: [*]u32) void {
    current_scanner.nextFrame(pixels[0..scanner.pixel_count]);
}

export fn drb_zig_count_newlines(data: [*c]const u8, len: usize) usize {
    if (len == 0) return 0; // Permit NULL for an empty input.
    return kernels.countNewlines(data[0..len]);
}

test "C entry points accept empty input and reset scanner state" {
    try std.testing.expectEqual(@as(usize, 0), drb_zig_count_newlines(null, 0));
    try std.testing.expectEqual(@as(usize, 2), drb_zig_count_newlines("a\nb\n", 4));
    var first: [scanner.pixel_count]u32 = undefined;
    var later: [scanner.pixel_count]u32 = undefined;
    drb_zig_scanner_reset();
    drb_zig_scanner_frame(&first);
    drb_zig_scanner_frame(&later);
    drb_zig_scanner_reset();
    drb_zig_scanner_frame(&later);
    try std.testing.expectEqualSlices(u32, &first, &later);
}
