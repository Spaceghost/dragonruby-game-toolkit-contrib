const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const extension = b.addLibrary(.{ .name = "zig_suite", .linkage = .dynamic, .root_module = b.createModule(.{
        .root_source_file = .{ .cwd_relative = b.pathFromRoot("../src/root.zig") },
        .target = target, .optimize = optimize, .link_libc = true,
    }) });
    extension.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../src") });
    extension.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../sqlite") });

    // Keep sqlite3 unresolved in the static query archive. The final shared
    // bridge supplies sqlite3 exactly once; embedding a shared object as an
    // archive member makes LLD warn (and -Werror-style CI rightly objects).
    const query = b.addLibrary(.{ .name = "zig_suite_query", .linkage = .static, .root_module = b.createModule(.{
        .root_source_file = .{ .cwd_relative = b.pathFromRoot("../sqlite/query.zig") },
        .target = target, .optimize = optimize, .link_libc = true,
    }) });
    query.root_module.addIncludePath(.{ .cwd_relative = b.pathFromRoot("../sqlite") });

    if (b.option([]const u8, "mruby-root", "Built test VM, not the DragonRuby SDK")) |mruby| {
        const boxing = b.option(enum { word, nan, none }, "boxing", "Matching test VM value layout") orelse .word;
        const published = b.option(bool, "published", "Verify the complete published DragonRuby mruby patch") orelse false;
        const flags: []const []const u8 = &.{
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-DDRBZ_SUITE_TEST_HOST", "-DDRBZ_SQLITE_QUERY", "-DMRB_NO_PRESYM",
            if (published) "-DDRBZ_PUBLISHED_MRUBY" else "-DDRBZ_UPSTREAM_MRUBY",
            switch (boxing) { .word => "-DMRB_WORD_BOXING", .nan => "-DMRB_NAN_BOXING", .none => "-DMRB_NO_BOXING" },
            if (boxing == .nan) "-DMRB_INT32" else "-DMRB_INT64",
        };
        extension.root_module.addIncludePath(b.path("support"));
        extension.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ mruby, "include" }) });
        extension.root_module.addCSourceFile(.{ .file = b.path("bridge.c"), .flags = flags });
        extension.root_module.linkLibrary(query);
        extension.root_module.linkSystemLibrary("sqlite3", .{});
        const host = b.addExecutable(.{ .name = "drbz-ruby-suite", .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
        host.rdynamic = true;
        host.root_module.addIncludePath(b.path("support"));
        host.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ mruby, "include" }) });
        host.root_module.addCSourceFile(.{ .file = b.path("host.c"), .flags = flags });
        host.root_module.addObjectFile(.{ .cwd_relative = b.pathJoin(&.{ mruby, "build/host/lib/libmruby.a" }) });
        host.root_module.linkSystemLibrary("m", .{});
        host.root_module.linkSystemLibrary("dl", .{});
        const run = b.addRunArtifact(host);
        run.has_side_effects = true;
        run.addArtifactArg(extension);
        run.addFileArg(b.path("smoke.rb"));
        const test_step = b.step("test", "Execute Ruby API calls, cached SQLite rows, exceptions, GC and VM re-registration");
        test_step.dependOn(&run.step);
        b.default_step = test_step;
    } else if (b.option([]const u8, "sdk-include", "Directory containing the matching proprietary dragonruby.h and mruby headers")) |sdk| {
        const sqlite_query = b.option(bool, "sqlite-query", "Enable cached query_json using system SQLite") orelse false;
        extension.root_module.addSystemIncludePath(.{ .cwd_relative = sdk });
        if (sqlite_query) {
            extension.root_module.addCSourceFile(.{ .file = b.path("bridge.c"), .flags = &.{ "-std=c11", "-Wall", "-Wextra", "-DDRBZ_SQLITE_QUERY" } });
            extension.root_module.linkLibrary(query);
            extension.root_module.linkSystemLibrary("sqlite3", .{});
        } else {
            extension.root_module.addCSourceFile(.{ .file = b.path("bridge.c"), .flags = &.{ "-std=c11", "-Wall", "-Wextra" } });
        }
        if (target.result.os.tag == .macos) extension.linker_allow_shlib_undefined = true;
        b.installArtifact(extension);
    } else {
        const help = b.addFail("Provide -Dmruby-root=/built/test-vm for tests or -Dsdk-include=/matching/sdk/include for the production adapter.");
        b.default_step.dependOn(&help.step);
    }
}
