#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the pinned OBS development SDK on Ubuntu 24.04, without installing a host."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
PREFIX = DEPS / "linux-sdk"
SPEC = json.loads((ROOT / "buildspec.json").read_text(encoding="utf-8-sig"))


def run(*args, cwd=ROOT, env=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, env=env, check=True)


def unpack(url, checksum, directory):
    archive = DEPS / url.rsplit("/", 1)[1]
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + ".partial")
        urllib.request.urlretrieve(url, temporary)
        temporary.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != checksum:
        raise RuntimeError(f"SHA-256 mismatch: {archive}")
    destination = DEPS / directory
    if not destination.exists():
        shutil.unpack_archive(archive, DEPS)
    return destination


def main():
    if os.uname().machine != "x86_64":
        raise RuntimeError("This SDK bootstrap supports Linux x86_64 only")
    DEPS.mkdir(exist_ok=True)
    ffmpeg = unpack(
        "https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz",
        "b2751fccb6cc4c77708113cd78b561059b6fa904b24162fa0be2d60273d27b8e",
        "ffmpeg-8.0",
    )
    run(
        ffmpeg / "configure", f"--prefix={PREFIX}", "--disable-everything",
        "--disable-autodetect", "--disable-programs", "--disable-doc",
        "--disable-x86asm", "--disable-static", "--enable-shared",
        "--enable-avcodec", "--enable-avformat", "--enable-avutil",
        "--enable-swscale", "--enable-swresample", cwd=ffmpeg,
    )
    run("make", f"-j{min(os.cpu_count() or 2, 4)}", cwd=ffmpeg)
    run("make", "install", cwd=ffmpeg)

    obs_spec = SPEC["dependencies"]["obs-studio"]
    version = obs_spec["version"]
    obs = unpack(
        f"{obs_spec['baseUrl']}/{version}.zip",
        obs_spec["hashes"]["windows-x64"], f"obs-studio-{version}",
    )
    build = DEPS / "obs-linux-build"
    env = os.environ.copy()
    env["PKG_CONFIG_PATH"] = f"{PREFIX}/lib/pkgconfig:" + env.get("PKG_CONFIG_PATH", "")
    env["LD_LIBRARY_PATH"] = f"{PREFIX}/lib:" + env.get("LD_LIBRARY_PATH", "")
    run(
        "cmake", "-S", obs, "-B", build, "-G", "Ninja",
        f"-DCMAKE_INSTALL_PREFIX={PREFIX}", f"-DCMAKE_PREFIX_PATH={PREFIX}",
        "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_BUILD_TYPE=Release",
        f"-DOBS_VERSION_OVERRIDE={version}", "-DENABLE_PLUGINS=OFF",
        "-DENABLE_FRONTEND=OFF", "-DENABLE_SCRIPTING=OFF",
        "-DENABLE_PULSEAUDIO=OFF", "-DENABLE_WAYLAND=OFF",
        "-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF", env=env,
    )
    run("cmake", "--build", build, "--parallel", "4", env=env)
    for component in ("Runtime", "Development"):
        run("cmake", "--install", build, "--component", component, env=env)
    print(f"SDK ready: cmake --preset ubuntu-x86_64 -DCMAKE_PREFIX_PATH={PREFIX}")


if __name__ == "__main__":
    main()
