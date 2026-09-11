const std = @import("std");
const builtin = @import("builtin");
const flags = [_][]const u8{ "-std=c11", "-D_DEFAULT_SOURCE", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-ffp-contract=off" };
fn path(b: *std.Build, name: []const u8) std.Build.LazyPath {
    return .{ .cwd_relative = b.pathFromRoot(name) };
}
fn object(b: *std.Build, name: []const u8, source: std.Build.LazyPath, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, cflags: []const []const u8) *std.Build.Step.Compile {
    const obj = b.addObject(.{ .name = name, .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    obj.root_module.addIncludePath(path(b, "../src"));
    obj.root_module.addIncludePath(path(b, "../measure"));
    obj.root_module.addIncludePath(path(b, "../../11_zig_native_pixel_arrays/tests/support"));
    obj.root_module.addCSourceFile(.{ .file = source, .flags = cflags });
    return obj;
}
fn library(b: *std.Build, name: []const u8, source: []const u8, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const lib = b.addLibrary(.{ .name = name, .linkage = .static, .root_module = b.createModule(.{ .root_source_file = path(b, source), .target = target, .optimize = optimize, .link_libc = true }) });
    lib.bundle_compiler_rt = true;
    return lib;
}
pub fn build(b: *std.Build) void {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) @panic("Use Zig 0.16.0");
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    if (target.result.os.tag != .linux or target.result.cpu.arch != builtin.cpu.arch) @panic("Competition currently executes on native Linux only");
    const baseline = library(b, "rival_baseline", "../src/native.zig", target, optimize);
    const zig_source = b.option([]const u8, "zig-source", "Isolated source-mutation test override") orelse "../src/competitive.zig";
    const c_source = b.option([]const u8, "c-source", "Isolated source-mutation test override") orelse "../src/competitive.c";
    const odin_object = b.option([]const u8, "odin-object", "Prebuilt pinned Odin C-ABI object") orelse "odin-out/competitive.o";
    const odin = path(b, odin_object);
    const tuned = library(b, "rival_zig", zig_source, target, optimize);
    const c_tuned = object(b, "rival-c", path(b, c_source), target, optimize, &flags);
    const control = object(b, "rival-control", path(b, "../tests/controls.c"), target, optimize, &flags);
    const reference = b.addSystemCommand(&.{ "python3", "../tools/reference.py" });
    reference.addFileArg(path(b, "../../04_handcrafted_extension_advanced/native/ext-bindings.c"));
    reference.addFileArg(path(b, "../../03_native_pixel_arrays/app/ext.c"));
    const original = object(b, "rival-original", reference.addOutputFileArg("original.c"), target, optimize, &flags);
    const test_obj = object(b, "rival-tests", b.path("test.c"), target, optimize, &flags);
    const tests = b.addExecutable(.{ .name = "rivals-check", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    for ([_]*std.Build.Step.Compile{ test_obj, c_tuned, original }) |obj| tests.root_module.addObject(obj);
    tests.root_module.addObjectFile(odin);
    tests.root_module.linkLibrary(baseline);
    tests.root_module.linkLibrary(tuned);
    tests.root_module.linkSystemLibrary("m", .{});
    const run = b.addRunArtifact(tests);
    run.has_side_effects = true;
    b.step("test", "Compare C, Zig and Odin with independent/original oracles and guarded memory").dependOn(&run.step);
    const installed_tests = b.addInstallArtifact(tests, .{});
    b.step("build-tests", "Compile the test executable for source mutation checks").dependOn(&installed_tests.step);

    const sweep_obj = object(b, "lf-sweep-c", b.path("lf_sweep.c"), target, optimize, &flags);
    const sweep = b.addExecutable(.{ .name = "lf-sweep", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    sweep.root_module.addObject(sweep_obj);
    sweep.root_module.addObject(c_tuned);
    sweep.root_module.addObject(control);
    sweep.root_module.addObjectFile(odin);
    sweep.root_module.linkLibrary(baseline);
    sweep.root_module.linkLibrary(tuned);
    b.installArtifact(sweep);

    b.installArtifact(tuned);
    b.getInstallStep().dependOn(&b.addInstallFile(c_tuned.getEmittedBin(), "lib/rival-c.o").step);
    b.getInstallStep().dependOn(&b.addInstallFile(odin, "lib/rival-odin.o").step);
    for ([_]bool{ false, true }) |profile| {
        const name = if (profile) "rivals-alloc" else "rivals-time";
        const prof_flags = flags ++ [_][]const u8{"-DDRBZ_PROFILE"};
        const host = object(b, name, b.path("bench.c"), target, optimize, if (profile) &prof_flags else &flags);
        const link = b.addSystemCommand(&.{ "cc", "-no-pie", "-o" });
        const output = link.addOutputFileArg(name);
        for ([_]*std.Build.Step.Compile{ host, c_tuned, control, original }) |obj| link.addArtifactArg(obj);
        link.addFileArg(odin);
        if (profile) {
            const meter_flags = prof_flags ++ [_][]const u8{"-fno-builtin"};
            const meter = object(b, "rival-meter", path(b, "../measure/libc_meter.c"), target, optimize, &meter_flags);
            link.addArtifactArg(meter);
            for ([_][]const u8{ "malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign" }) |symbol| link.addArg(b.fmt("-Wl,--wrap={s}", .{symbol}));
        }
        link.addArg("-Wl,--start-group");
        link.addArtifactArg(baseline);
        link.addArtifactArg(tuned);
        link.addArgs(&.{ "-Wl,--end-group", "-lm", "-pthread", "-ldl" });
        b.getInstallStep().dependOn(&b.addInstallFile(output, b.fmt("bin/{s}", .{name})).step);
    }
}
