const std = @import("std");
const builtin = @import("builtin");

comptime {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) {
        @compileError("This sample is pinned to Zig 0.16.0; see .zig-version.");
    }
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const sdk = b.option([]const u8, "dragonruby-root", "Matching DragonRuby SDK root") orelse "../../..";
    const default_platform: []const u8 = switch (target.result.os.tag) {
        .linux => if (target.result.cpu.arch == .x86_64) "linux-amd64" else "custom",
        .windows => if (target.result.cpu.arch == .x86_64) "windows-amd64" else "custom",
        .macos => "macos",
        else => "custom",
    };
    const platform = b.option([]const u8, "platform", "DragonRuby native directory name") orelse default_platform;
    const suffix: []const u8 = switch (target.result.os.tag) {
        .windows => "dll",
        .macos => "dylib",
        else => "so",
    };

    const unit = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("app/native.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    const library = b.addLibrary(.{
        .name = "drb_zig_kernels",
        .linkage = .static,
        .root_module = b.createModule(.{
            .root_source_file = b.path("app/native.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    const abi = b.addExecutable(.{
        .name = "zig-c-abi-test",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    abi.root_module.addIncludePath(b.path("app"));
    abi.root_module.addCSourceFile(.{
        .file = b.path("tests/abi.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG" },
    });
    abi.root_module.linkLibrary(library);
    const test_step = b.step("test", "Run Zig unit tests and the C ABI/reference test (no SDK needed)");
    test_step.dependOn(&b.addRunArtifact(unit).step);
    test_step.dependOn(&b.addRunArtifact(abi).step);
    const check = b.step("check", "Compile the tests without running them (for cross-target checks)");
    check.dependOn(&unit.step);
    check.dependOn(&abi.step);

    // Only this artifact requires the proprietary SDK. Tests never use a mock ABI.
    const extension = b.addLibrary(.{
        .name = "ext",
        .linkage = .dynamic,
        .root_module = b.createModule(.{
            .root_source_file = b.path("app/native.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    extension.root_module.addIncludePath(b.path("app"));
    extension.root_module.addIncludePath(.{ .cwd_relative = sdk });
    extension.root_module.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "include" }) });
    extension.root_module.addCSourceFile(.{
        .file = b.path("app/bridge.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra" },
    });
    const install = b.addInstallFile(extension.getEmittedBin(), b.fmt("native/{s}/ext.{s}", .{ platform, suffix }));
    b.getInstallStep().dependOn(&install.step);
    b.step("extension", "Build and install the DragonRuby extension (requires SDK)").dependOn(&install.step);
}
