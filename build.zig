const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "MediaKeys",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });

    exe.addCSourceFiles(.{
        .files = &.{ "src/main.c", "src/cJSON.c" },
        .flags = &.{ "-DUNICODE", "-D_UNICODE" },
    });

    exe.linkLibC();
    exe.linkSystemLibrary("user32");
    exe.linkSystemLibrary("shell32");
    exe.linkSystemLibrary("ole32");
    exe.linkSystemLibrary("gdi32");

    exe.subsystem = .Windows;
    exe.mingw_unicode_entry_point = true;

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run MediaKeys");
    run_step.dependOn(&run_cmd.step);
}
