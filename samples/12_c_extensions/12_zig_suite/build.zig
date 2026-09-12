const std = @import("std");
const builtin = @import("builtin");

comptime {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) @compileError("Use the pinned Zig 0.16.0 toolchain");
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const module = b.createModule(.{ .root_source_file = b.path("src/root.zig"), .target = target, .optimize = optimize, .link_libc = true });
    const library = b.addLibrary(.{ .name = "drbz_suite", .linkage = .static, .root_module = module });
    const legacy = b.addLibrary(.{ .name = "drbz_previous", .linkage = .static, .root_module = b.createModule(.{
        .root_source_file = .{ .cwd_relative = b.pathFromRoot("../11_zig_native_pixel_arrays/app/native.zig") },
        .target = target, .optimize = optimize, .link_libc = true,
    }) });
    const unit = b.addTest(.{ .root_module = b.createModule(.{ .root_source_file = b.path("src/root.zig"), .target = target, .optimize = optimize, .link_libc = true }) });
    const reference = b.addSystemCommand(&.{ "python", "tools/reference.py" });
    reference.addFileArg(.{ .cwd_relative = b.pathFromRoot("../04_handcrafted_extension_advanced/native/ext-bindings.c") });
    reference.addFileArg(.{ .cwd_relative = b.pathFromRoot("../03_native_pixel_arrays/app/ext.c") });
    const generated = reference.addOutputFileArg("original_kernels.c");
    const common = [_][]const u8{ "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-ffp-contract=off" };
    const check = b.step("check", "Compile portable tests without executing them");
    check.dependOn(&unit.step);
    const test_step = b.step("test", "Execute differential, bounds, ABI and negative-control tests");
    test_step.dependOn(&b.addRunArtifact(unit).step);

    // The exhaustive short regex corpus alone never reaches a SIMD load.
    // Require long-input original-C parity before permitting any benchmark.
    const long_regex = b.addExecutable(.{
        .name = "drbz-regex-long",
        .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }),
    });
    long_regex.root_module.addIncludePath(b.path("src"));
    long_regex.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../02_intermediate/app") });
    long_regex.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../11_zig_native_pixel_arrays/tests/support") });
    long_regex.root_module.addCSourceFile(.{ .file = b.path("evidence/regex_long.c"), .flags = &common });
    long_regex.root_module.addCSourceFile(.{
        .file = .{ .cwd_relative = b.pathFromRoot("../02_intermediate/app/re.c") },
        .flags = &.{ "-std=c11", "-ffp-contract=off" },
    });
    long_regex.root_module.linkLibrary(library);
    check.dependOn(&long_regex.step);
    const run_long_regex = b.addRunArtifact(long_regex);
    run_long_regex.has_side_effects = true;
    test_step.dependOn(&run_long_regex.step);

    if (target.result.os.tag != .windows) {
        const apps = b.addExecutable(.{ .name = "drbz-apps", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
        apps.root_module.addIncludePath(b.path("src"));
        apps.root_module.addCSourceFile(.{ .file = b.path("tests/apps.c"), .flags = &common });
        apps.root_module.linkLibrary(library);
        apps.root_module.linkSystemLibrary("pthread", .{});
        check.dependOn(&apps.step);
        const run_apps = b.addRunArtifact(apps);
        run_apps.has_side_effects = true;
        test_step.dependOn(&run_apps.step);
    }
    for ([_]bool{ false, true }) |benchmark| {
        const exe = b.addExecutable(.{ .name = if (benchmark) "drbz-bench" else "drbz-test", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
        exe.root_module.addIncludePath(b.path("src"));
        exe.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../02_intermediate/app") });
        exe.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../11_zig_native_pixel_arrays/tests/support") });
        exe.root_module.addCSourceFile(.{ .file = b.path(if (benchmark) "tests/bench.c" else "tests/parity.c"), .flags = &common });
        exe.root_module.addCSourceFile(.{ .file = generated, .flags = &common });
        exe.root_module.addCSourceFile(.{ .file = b.path("tests/controls.c"), .flags = &common });
        exe.root_module.addCSourceFile(.{ .file = .{ .cwd_relative = b.pathFromRoot("../01_basics/app/ext.c") }, .flags = &common });
        exe.root_module.addCSourceFile(.{ .file = .{ .cwd_relative = b.pathFromRoot("../02_intermediate/app/re.c") }, .flags = &.{ "-std=c11", "-ffp-contract=off" } });
        exe.root_module.linkLibrary(library);
        exe.root_module.linkLibrary(legacy);
        if (target.result.os.tag != .windows) exe.root_module.linkSystemLibrary("m", .{});
        check.dependOn(&exe.step);
        const run = b.addRunArtifact(exe);
        run.has_side_effects = true;
        if (benchmark) {
            run.step.dependOn(test_step);
            if (b.args) |args| run.addArgs(args);
            b.step("bench", "Verify correctness, then emit interleaved raw benchmark samples").dependOn(&run.step);
        } else {
            test_step.dependOn(&run.step);
            const negative = b.addRunArtifact(exe);
            negative.has_side_effects = true;
            negative.addArg("--inject-mismatch");
            negative.expectExitCode(42);
            negative.expectStdErrEqual("PIXEL_MISMATCH frame=17 pixel=7 C=ff000000 Zig=ff000001\n");
            test_step.dependOn(&negative.step);
        }
    }
    b.default_step = test_step;
}
