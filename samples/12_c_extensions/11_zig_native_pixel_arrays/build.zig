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
    for (platform) |ch| {
        if (!std.ascii.isAlphanumeric(ch) and ch != '-' and ch != '_') {
            std.debug.panic("-Dplatform must be a single native directory name", .{});
        }
    }
    if (platform.len == 0) std.debug.panic("-Dplatform cannot be empty", .{});
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

    // Load the native exports from an actual shared library on each test OS.
    const shared = b.addLibrary(.{
        .name = "drb_zig_dynamic",
        .linkage = .dynamic,
        .root_module = b.createModule(.{
            .root_source_file = b.path("app/native.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    const dynamic_abi = b.addExecutable(.{
        .name = "zig-dynamic-abi-test",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    dynamic_abi.root_module.addIncludePath(b.path("app"));
    dynamic_abi.root_module.addCSourceFile(.{
        .file = b.path("tests/abi.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-DDRB_ZIG_DYNAMIC" },
    });
    if (target.result.os.tag == .linux) dynamic_abi.root_module.linkSystemLibrary("dl", .{});
    const run_dynamic = b.addRunArtifact(dynamic_abi);
    run_dynamic.addArtifactArg(shared);
    test_step.dependOn(&run_dynamic.step);
    check.dependOn(&dynamic_abi.step);
    check.dependOn(&shared.step);

    // Opt-in real mruby integration. This fixture is NEVER on production paths.
    if (b.option([]const u8, "mruby-root", "Built mruby test checkout")) |mruby| {
        const boxing = b.option(enum { word, nan, none }, "mruby-boxing", "Test VM boxing configuration") orelse .word;
        const published = b.option(bool, "mruby-dragonruby", "Require the published DragonRuby mruby 3.0.0 patch") orelse false;
        const flags: []const []const u8 = &.{
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-UNDEBUG",
            "-DDRB_ZIG_TEST_HOST",
            "-DMRB_NO_PRESYM",
            if (published) "-DDRB_ZIG_PUBLISHED_MRUBY" else "-DDRB_ZIG_UPSTREAM_MRUBY",
            switch (boxing) {
                .word => "-DMRB_WORD_BOXING",
                .nan => "-DMRB_NAN_BOXING",
                .none => "-DMRB_NO_BOXING",
            },
            if (boxing == .nan) "-DMRB_INT32" else "-DMRB_INT64",
        };
        const test_extension = b.addLibrary(.{
            .name = "mruby_test_extension",
            .linkage = .dynamic,
            .root_module = b.createModule(.{
                .root_source_file = b.path("app/native.zig"),
                .target = target,
                .optimize = optimize,
                .link_libc = true,
            }),
        });
        test_extension.root_module.addIncludePath(b.path("app"));
        test_extension.root_module.addIncludePath(b.path("tests/support"));
        test_extension.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ mruby, "include" }) });
        test_extension.root_module.addCSourceFile(.{ .file = b.path("app/bridge.c"), .flags = flags });

        // Compile the actual, unchanged sibling sample, not a copied reference.
        const original = b.addLibrary(.{
            .name = "original_c_scanner",
            .linkage = .dynamic,
            .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }),
        });
        original.root_module.addIncludePath(b.path("tests/support"));
        original.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ mruby, "include" }) });
        const original_flags = b.allocator.alloc([]const u8, flags.len + 2) catch @panic("out of memory");
        @memcpy(original_flags[0..flags.len], flags);
        original_flags[flags.len] = "-DDRB_FFI=";
        // The original example has unused callback arguments. Keep our warnings strict.
        original_flags[flags.len + 1] = "-Wno-unused-parameter";
        original.root_module.addCSourceFile(.{
            .file = .{ .cwd_relative = b.pathFromRoot("../03_native_pixel_arrays/app/ext.c") },
            .flags = original_flags,
        });

        const host = b.addExecutable(.{
            .name = "mruby-bridge-test",
            .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }),
        });
        // Header helpers in the loaded adapter resolve to the existing VM.
        host.rdynamic = true;
        host.root_module.addIncludePath(b.path("tests/support"));
        host.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ mruby, "include" }) });
        host.root_module.addCSourceFile(.{ .file = b.path("tests/mruby_host.c"), .flags = flags });
        host.root_module.addObjectFile(.{ .cwd_relative = b.pathJoin(&.{ mruby, "build/host/lib/libmruby.a" }) });
        host.root_module.linkSystemLibrary("m", .{});
        host.root_module.linkSystemLibrary("dl", .{});
        const integration = b.step("mruby-test", "Compare original C with Zig through Ruby, including a negative control (not the SDK)");
        const run_host = b.addRunArtifact(host);
        run_host.has_side_effects = true;
        run_host.addArtifactArg(test_extension);
        run_host.addArtifactArg(original);
        run_host.addFileArg(b.path("tests/smoke.rb"));
        if (published) run_host.addFileArg(b.path("tests/dragonruby_mruby.rb"));
        integration.dependOn(&run_host.step);

        // A green test requires detecting the injected wrong pixel as well.
        const run_negative = b.addRunArtifact(host);
        run_negative.has_side_effects = true;
        run_negative.addArtifactArg(test_extension);
        run_negative.addArtifactArg(original);
        run_negative.addFileArg(b.path("tests/smoke.rb"));
        if (published) run_negative.addFileArg(b.path("tests/dragonruby_mruby.rb"));
        run_negative.addArg("--inject-mismatch");
        run_negative.expectExitCode(42);
        run_negative.expectStdErrEqual("PIXEL_MISMATCH frame=17 pixel=7 C=ff000000 Zig=ff000001\n");
        integration.dependOn(&run_negative.step);
    }

    // Only this artifact uses the matching proprietary SDK, never test headers.
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
    extension.root_module.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "mruby/include" }) });
    extension.root_module.addCSourceFile(.{
        .file = b.path("app/bridge.c"),
        .flags = &.{ "-std=c11", "-Wall", "-Wextra" },
    });
    const install = b.addInstallFile(extension.getEmittedBin(), b.fmt("native/{s}/ext.{s}", .{ platform, suffix }));
    b.getInstallStep().dependOn(&install.step);
    b.step("extension", "Build and install the DragonRuby extension (requires SDK)").dependOn(&install.step);
}
