const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const @"98fmplayer" = lib98fmplayer(b, target, optimize);
    const portaudio = b.dependency("portaudio", .{
        .target = target,
        .optimize = optimize,
    }).artifact("portaudio");

    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.linkLibrary(@"98fmplayer");
    mod.linkLibrary(portaudio);
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{
            "fmplay.c",
        },
        .flags = &.{
            "-Wall",
            "-Wextra",
            "-pedantic",
            "-std=c23",
        },
    });

    const exe = b.addExecutable(.{
        .name = "fmplay",
        .root_module = mod,
    });
    b.installArtifact(exe);
}

fn lib98fmplayer(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) *std.Build.Step.Compile {
    const dep = b.dependency("98fmplayer", .{});

    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addIncludePath(dep.path("."));
    mod.addCSourceFiles(.{
        .root = dep.path("."),
        .files = &.{
            "libopna/opnaadpcm.c",
            "libopna/opnadrum.c",
            "libopna/opnafm.c",
            "libopna/opnassg.c",
            "libopna/opnassg-sinc-c.c",
            "libopna/opnatimer.c",
            "libopna/opna.c",
            "fmdriver/fmdriver_fmp.c",
            "fmdriver/fmdriver_pmd.c",
            "fmdriver/fmdriver_common.c",
            "fmdriver/ppz8.c",
            "common/fmplayer_file.c",
            "common/fmplayer_file_unix.c",
            "common/fmplayer_work_opna.c",
            "common/fmplayer_drumrom_unix.c",
        },
        .flags = &.{
            "-Wall",
            "-Wextra",
            "-pedantic",
            "-std=c99",
            "-fno-sanitize=shift",
        },
    });

    const lib = b.addLibrary(.{
        .name = "98fmplayer",
        .root_module = mod,
    });
    lib.installHeadersDirectory(dep.path("."), "", .{});
    return lib;
}
