const std = @import("std");
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const library = b.addLibrary(.{ .name = "drbz_sqlite", .linkage = .static, .root_module = b.createModule(.{ .root_source_file = b.path("sqlite.zig"), .target = target, .optimize = optimize, .link_libc = true }) });
    const test_exe = b.addExecutable(.{ .name = "drbz-sqlite-test", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    test_exe.root_module.addIncludePath(b.path("."));
    test_exe.root_module.addCSourceFile(.{ .file = b.path("test.c"), .flags = &.{ "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-UNDEBUG" } });
    test_exe.root_module.linkLibrary(library);
    test_exe.root_module.linkSystemLibrary("sqlite3", .{});
    const run = b.addRunArtifact(test_exe);
    run.has_side_effects = true;
    const tests = b.step("test", "Check SQLite ownership/error paths and measure prepared-statement reuse");
    tests.dependOn(&run.step);
    b.default_step = tests;
}
