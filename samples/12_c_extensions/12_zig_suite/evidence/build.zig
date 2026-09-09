const std = @import("std");
const builtin = @import("builtin");

pub fn build(b: *std.Build) void {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) @panic("Use Zig 0.16.0");
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const library = b.addLibrary(.{
        .name = "evidence_kernels",
        .linkage = .static,
        .root_module = b.createModule(.{
            .root_source_file = .{ .cwd_relative = b.pathFromRoot("../src/native.zig") },
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    const regex = b.addExecutable(.{
        .name = "drbz-regex-long",
        .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }),
    });
    regex.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../src") });
    regex.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../../02_intermediate/app") });
    regex.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../../11_zig_native_pixel_arrays/tests/support") });
    regex.root_module.addCSourceFile(.{
        .file = b.path("regex_long.c"),
        .flags = &.{ "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-UNDEBUG" },
    });
    regex.root_module.addCSourceFile(.{
        .file = .{ .cwd_relative = b.pathFromRoot("../../02_intermediate/app/re.c") },
        .flags = &.{ "-std=c11" },
    });
    regex.root_module.linkLibrary(library);
    b.installArtifact(regex);

    // GNU-style wrapping observes references in linked objects, not arbitrary
    // allocations inside shared libraries or direct mmap/syscall allocation.
    // This executable is a Linux native-only probe, not a timing benchmark.
    if (target.result.os.tag == .linux) {
        const link = b.addSystemCommand(&.{
            b.graph.zig_exe, "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
            "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-fno-builtin", "-O2",
            "-Wl,--wrap=malloc", "-Wl,--wrap=calloc", "-Wl,--wrap=realloc",
            "-Wl,--wrap=aligned_alloc", "-Wl,--wrap=posix_memalign", "-Wl,--wrap=free",
        });
        link.addArgs(&.{ "-I", b.pathFromRoot("../src") });
        link.addFileArg(b.path("allocations.c"));
        link.addArtifactArg(library);
        link.addArg("-o");
        const binary = link.addOutputFileArg("drbz-allocations");
        const install = b.addInstallFile(binary, "bin/drbz-allocations");
        b.getInstallStep().dependOn(&install.step);
    }
}
