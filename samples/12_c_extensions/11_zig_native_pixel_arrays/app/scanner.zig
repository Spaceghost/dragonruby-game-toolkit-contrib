const std = @import("std");
const kernels = @import("kernels.zig");

pub const dimension = 10;
pub const pixel_count = dimension * dimension;
pub const background: u32 = 0xff000000;
pub const foreground: u32 = 0xff00ff00;

/// Keep instances on the calling thread; no allocator or Ruby values are needed.
pub const Scanner = struct {
    position: i32 = 0,
    increment: i32 = 1,

    pub fn nextFrame(self: *Scanner, pixels: *[pixel_count]u32) void {
        kernels.fillPixels(pixels, background);
        const start = @as(usize, @intCast(self.position)) * dimension;
        kernels.fillPixels(pixels[start..][0..dimension], foreground);

        // Match 03_native_pixel_arrays exactly, including its repeated last row.
        self.position += self.increment;
        if (self.increment > 0 and self.position >= dimension) {
            self.increment = -1;
            self.position = dimension - 1;
        } else if (self.increment < 0 and self.position < 0) {
            self.increment = 1;
            self.position = 1;
        }
    }
};

test "scanner preserves the C sample's bounce sequence and pixel values" {
    var scanner: Scanner = .{};
    var pixels: [pixel_count]u32 = undefined;
    const rows = [_]usize{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2 };
    for (rows) |row| {
        scanner.nextFrame(&pixels);
        for (pixels, 0..) |pixel, i| {
            try std.testing.expectEqual(if (i / dimension == row) foreground else background, pixel);
        }
    }
}

test "scanner instances are independent" {
    var a: Scanner = .{};
    var b: Scanner = .{};
    var pixels: [pixel_count]u32 = undefined;
    a.nextFrame(&pixels);
    a.nextFrame(&pixels);
    try std.testing.expectEqual(@as(i32, 0), b.position);
    b.nextFrame(&pixels);
    try std.testing.expectEqual(@as(i32, 1), b.position);
    try std.testing.expectEqual(@as(i32, 2), a.position);
}
