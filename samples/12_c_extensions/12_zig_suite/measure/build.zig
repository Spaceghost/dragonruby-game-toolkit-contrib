const std = @import("std");
const builtin = @import("builtin");
const common = [_][]const u8{ "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-ffp-contract=off" };
const prof = common ++ [_][]const u8{"-DDRBZ_PROFILE"};
const meter_flags = prof ++ [_][]const u8{"-fno-builtin"};

fn path(b: *std.Build, name: []const u8) std.Build.LazyPath {
    return .{ .cwd_relative = b.pathFromRoot(name) };
}
fn library(b: *std.Build, name: []const u8, source: []const u8, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const lib = b.addLibrary(.{ .name = name, .linkage = .static, .root_module = b.createModule(.{
        .root_source_file = path(b, source), .target = target, .optimize = optimize, .link_libc = true,
    }) });
    lib.bundle_compiler_rt = true;
    return lib;
}
fn object(b: *std.Build, name: []const u8, source: std.Build.LazyPath, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, flags: []const []const u8, mruby: ?[]const u8) *std.Build.Step.Compile {
    const obj = b.addObject(.{ .name = name, .root_module = b.createModule(.{ .target = target, .optimize = optimize, .link_libc = true }) });
    for ([_][]const u8{ ".", "../src", "../sqlite", "../bridge/support", "../../02_intermediate/app" }) |include| obj.root_module.addIncludePath(path(b, include));
    if (mruby) |root| obj.root_module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ root, "include" }) });
    obj.root_module.addCSourceFile(.{ .file = source, .flags = flags });
    return obj;
}
fn link(b: *std.Build, name: []const u8, objects: []const *std.Build.Step.Compile, libraries: []const *std.Build.Step.Compile, wrapped: bool, mruby: ?[]const u8, sqlite: bool) void {
    // Compilation is exclusively Zig/Clang with one resolved CPU and mode.
    // Host cc only links the objects; timing does not use GNU --wrap or LTO.
    const cmd = b.addSystemCommand(&.{ "cc", "-no-pie", "-o" });
    const output = cmd.addOutputFileArg(name);
    for (objects) |obj| cmd.addArtifactArg(obj);
    cmd.addArg("-Wl,--start-group");
    for (libraries) |lib| cmd.addArtifactArg(lib);
    if (mruby) |root| cmd.addFileArg(.{ .cwd_relative = b.pathJoin(&.{ root, "build/host/lib/libmruby.a" }) });
    cmd.addArg("-Wl,--end-group");
    if (wrapped) for ([_][]const u8{ "malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign" }) |symbol| cmd.addArg(b.fmt("-Wl,--wrap={s}", .{symbol}));
    if (sqlite) cmd.addArg("-lsqlite3");
    cmd.addArgs(&.{ "-lm", "-pthread", "-ldl" });
    const install = b.addInstallFile(output, b.fmt("bin/{s}", .{name}));
    b.getInstallStep().dependOn(&install.step);
}
pub fn build(b: *std.Build) void {
    if (!std.mem.eql(u8, builtin.zig_version_string, "0.16.0")) @panic("Use Zig 0.16.0");
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    if (target.result.os.tag != .linux or target.result.cpu.arch != builtin.cpu.arch) @panic("Measurement runners currently require native Linux, not cross execution");
    const metadata = b.addWriteFiles().add("build.json", b.fmt(
        "{{\"zig\":\"{s}\",\"optimize\":\"{s}\",\"arch\":\"{s}\",\"os\":\"{s}\",\"cpu_model\":\"{s}\",\"c_compiler\":\"Zig bundled Clang\",\"lto\":false,\"fast_math\":false,\"linker\":\"host cc -no-pie\"}}\n",
        .{ builtin.zig_version_string, @tagName(optimize), @tagName(target.result.cpu.arch), @tagName(target.result.os.tag), target.result.cpu.model.name },
    ));
    b.getInstallStep().dependOn(&b.addInstallFile(metadata, "build.json").step);
    const kernels = library(b, "measure_kernels", "../src/root.zig", target, optimize);
    const mruby = b.option([]const u8, "mruby-root", "Built, pinned test VM; never the proprietary SDK");
    if (mruby) |root| {
        const boxing = b.option(enum { word, nan, none }, "boxing", "Matching VM boxing") orelse .word;
        const published = b.option(bool, "published", "Matching DragonRuby-patched VM") orelse false;
        const extras = [_][]const u8{
            "-DDRBZ_SUITE_TEST_HOST", "-DMRB_NO_PRESYM",
            if (published) "-DDRBZ_PUBLISHED_MRUBY" else "-DDRBZ_UPSTREAM_MRUBY",
            switch (boxing) { .word => "-DMRB_WORD_BOXING", .nan => "-DMRB_NAN_BOXING", .none => "-DMRB_NO_BOXING" },
            if (boxing == .nan) "-DMRB_INT32" else "-DMRB_INT64",
            switch (boxing) { .word => "-DDRBZ_BOXING_NAME=\"word\"", .nan => "-DDRBZ_BOXING_NAME=\"nan\"", .none => "-DDRBZ_BOXING_NAME=\"none\"" },
        };
        for ([_]bool{ false, true }) |profile| {
            const name = if (profile) "ruby-alloc" else "ruby-time";
            const flags = std.mem.concat(b.allocator, []const u8, &.{ if (profile) &prof else &common, &extras }) catch @panic("OOM");
            const host = object(b, name, path(b, "ruby.c"), target, optimize, flags, root);
            const bridge = object(b, b.fmt("{s}-bridge", .{name}), path(b, "../bridge/bridge.c"), target, optimize, flags, root);
            link(b, name, &.{ host, bridge }, &.{kernels}, false, root, false);
        }
    } else {
        const legacy = library(b, "measure_legacy", "../../11_zig_native_pixel_arrays/app/native.zig", target, optimize);
        // Legacy C consumers opt in; importing sqlite.zig no longer emits this ABI.
        const sql = library(b, "measure_sqlite", "../sqlite/compat.zig", target, optimize);
        sql.root_module.linkSystemLibrary("sqlite3", .{});
        const direct = library(b, "measure_direct_sqlite", "../sqlite/series.zig", target, optimize);
        direct.root_module.linkSystemLibrary("sqlite3", .{});
        b.installArtifact(direct);
        const audit = b.addSystemCommand(&.{ "python3", "../tools/check_direct_calls.py" });
        audit.addArtifactArg(direct);
        const audit_output = audit.addOutputFileArg("direct-call-audit.json");
        b.getInstallStep().dependOn(&b.addInstallFile(audit_output, "direct-call-audit.json").step);
        const direct_c = object(b, "measure-direct-c", path(b, "sqlite_direct.c"), target, optimize, &common, null);
        const reference = b.addSystemCommand(&.{ "python3", "../tools/reference.py" });
        reference.addFileArg(path(b, "../../04_handcrafted_extension_advanced/native/ext-bindings.c"));
        reference.addFileArg(path(b, "../../03_native_pixel_arrays/app/ext.c"));
        const generated = reference.addOutputFileArg("original_kernels.c");
        const original = object(b, "measure-original", generated, target, optimize, &common, null);
        const controls = object(b, "measure-controls", path(b, "../tests/controls.c"), target, optimize, &common, null);
        const regex = object(b, "measure-regex", path(b, "../../02_intermediate/app/re.c"), target, optimize, &.{ "-std=c11", "-UNDEBUG", "-ffp-contract=off" }, null);
        const noop = object(b, "measure-noop", path(b, "noop.c"), target, optimize, &common, null);
        for ([_]bool{ false, true }) |profile| {
            const name = if (profile) "native-alloc" else "native-time";
            const host = object(b, name, path(b, "native.c"), target, optimize, if (profile) &prof else &common, null);
            if (profile) {
                const meter = object(b, "measure-libc-meter", path(b, "libc_meter.c"), target, optimize, &meter_flags, null);
                link(b, name, &.{ host, original, controls, regex, noop, meter }, &.{ kernels, legacy }, true, null, false);
            } else link(b, name, &.{ host, original, controls, regex, noop }, &.{ kernels, legacy }, false, null, false);
            const sql_name = if (profile) "sqlite-alloc" else "sqlite-time";
            const sql_host = object(b, sql_name, path(b, "sqlite.c"), target, optimize, if (profile) &prof else &common, null);
            link(b, sql_name, &.{ sql_host, direct_c }, &.{ sql, direct }, false, null, true);
        }
    }
}
