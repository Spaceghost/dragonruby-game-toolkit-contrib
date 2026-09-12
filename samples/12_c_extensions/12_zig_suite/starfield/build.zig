const std = @import("std");
const builtin = @import("builtin");

fn path(b: *std.Build, name: []const u8) std.Build.LazyPath {
    return .{ .cwd_relative = b.pathFromRoot(name) };
}

pub fn build(b: *std.Build) void {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) @panic("Use Zig 0.16.0");
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    if (target.result.os.tag != .linux or target.result.cpu.arch != builtin.cpu.arch) @panic("Starfield evidence executes on native Linux runners");

    const lib = b.addLibrary(.{ .name = "starfield_native", .linkage = .static, .root_module = b.createModule(.{
        .root_source_file = path(b, "../src/starfield.zig"), .target = target, .optimize = optimize, .link_libc = true,
    }) });
    lib.bundle_compiler_rt = true;
    b.installArtifact(lib);
    const odin_name = b.option([]const u8, "odin-object", "Pinned Odin native package object") orelse "../competition/odin-out/competitive.o";
    const odin = path(b, odin_name);

    const flags = &.{ "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-ffp-contract=off" };
    const obj = b.addObject(.{ .name = "starfield-bench-c", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    obj.root_module.addIncludePath(path(b, "../src"));
    obj.root_module.addCSourceFile(.{ .file = b.path("bench.c"), .flags = flags });
    const exe = b.addExecutable(.{ .name = "starfield-bench", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    exe.root_module.addObject(obj);
    exe.root_module.addObjectFile(odin);
    exe.root_module.linkLibrary(lib);
    exe.root_module.linkSystemLibrary("m", .{});
    b.installArtifact(exe);
    b.getInstallStep().dependOn(&b.addInstallFile(odin, "lib/starfield-odin.o").step);

    const check = b.addRunArtifact(exe);
    check.addArg("--check");
    check.has_side_effects = true;
    b.step("check", "Differentially validate Zig/Odin persistent starfield stages").dependOn(&check.step);

    const run = b.addRunArtifact(exe);
    if (b.args) |args| run.addArgs(args);
    run.has_side_effects = true;
    b.step("run", "Benchmark matched Zig/Odin persistent starfield stages").dependOn(&run.step);
}
