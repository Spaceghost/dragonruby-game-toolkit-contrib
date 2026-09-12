const std = @import("std");
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    // Compatibility is explicitly linked by C tests, never by the Zig-first API.
    const library = b.addLibrary(.{ .name = "drbz_sqlite", .linkage = .static, .root_module = b.createModule(.{ .root_source_file = b.path("compat.zig"), .target = target, .optimize = optimize, .link_libc = true }) });
    library.root_module.linkSystemLibrary("sqlite3", .{});
    const test_exe = b.addExecutable(.{ .name = "drbz-sqlite-test", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    test_exe.root_module.addIncludePath(b.path("."));
    test_exe.root_module.addCSourceFile(.{ .file = b.path("test.c"), .flags = &.{ "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-UNDEBUG" } });
    test_exe.root_module.linkLibrary(library);
    test_exe.root_module.linkSystemLibrary("sqlite3", .{});
    const run = b.addRunArtifact(test_exe);
    run.has_side_effects = true;
    const tests = b.step("test", "Check direct SQLite operations, cached query rows and retained C ABI ownership/error paths");
    tests.dependOn(&run.step);
    const direct = b.addTest(.{ .root_module = b.createModule(.{ .root_source_file = b.path("direct.zig"), .target = target, .optimize = optimize, .link_libc = true }) });
    direct.root_module.linkSystemLibrary("sqlite3", .{});
    const run_direct = b.addRunArtifact(direct);
    run_direct.has_side_effects = true;
    tests.dependOn(&run_direct.step);
    b.step("direct-test", "Execute real SQLite batch tests and isolated cleanup-failure controls").dependOn(&run_direct.step);
    const query = b.addTest(.{ .root_module = b.createModule(.{ .root_source_file = b.path("query.zig"), .target = target, .optimize = optimize, .link_libc = true }) });
    query.root_module.addIncludePath(b.path("."));
    query.root_module.linkSystemLibrary("sqlite3", .{});
    const run_query = b.addRunArtifact(query);
    run_query.has_side_effects = true;
    tests.dependOn(&run_query.step);
    b.step("query-test", "Execute cached packed query-row tests against real SQLite").dependOn(&run_query.step);
    b.default_step = tests;
}
