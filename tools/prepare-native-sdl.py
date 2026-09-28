#!/usr/bin/env python3
"""Explicitly prepare the pinned CPU framebuffer host for portable Linux builds.

System SDL remains available for ordinary development. Distribution SDL builds
enable audio/device services that are unused here and can pull shared liblzma
into the portable closure. This preparation keeps that integrity check intact.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "2.32.10"
ARCHIVE = f"SDL2-{VERSION}.tar.gz"
URL = f"https://www.libsdl.org/release/{ARCHIVE}"
# Same upstream source pin as the prepared Linux SDK's Buildroot recipe.
SHA256 = "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
OPTIONS = {
    "SDL_SHARED": "OFF", "SDL_STATIC": "ON", "SDL_TEST": "OFF",
    "SDL_TESTS": "OFF", "SDL_INSTALL_TESTS": "OFF",
    "SDL_AUDIO": "OFF", "SDL_JOYSTICK": "OFF", "SDL_HAPTIC": "OFF",
    "SDL_SENSOR": "OFF", "SDL_HIDAPI": "OFF", "SDL_LIBUDEV": "OFF",
    "SDL_DBUS": "OFF", "SDL_IBUS": "OFF", "SDL_POWER": "OFF",
    "SDL_RENDER": "OFF", "SDL_OPENGL": "OFF", "SDL_OPENGLES": "OFF",
    "SDL_VULKAN": "OFF", "SDL_KMSDRM": "OFF", "SDL_RPI": "OFF",
    "SDL_VIVANTE": "OFF", "SDL_WAYLAND": "OFF",
    "SDL_X11": "ON", "SDL_X11_SHARED": "OFF", "SDL_DUMMYVIDEO": "ON",
}


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def verify_archive(path):
    if digest(path) != SHA256:
        raise ValueError("SDL source checksum mismatch")


def extract_source(archive, destination):
    """Reject paths/links escaping the one upstream source directory."""
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or ".." in path.parts or not path.parts
                    or path.parts[0] != f"SDL2-{VERSION}"):
                raise ValueError("Unsafe SDL source path")
            if member.issym():
                target = (destination / member.name).parent / member.linkname
                if not target.resolve().is_relative_to((destination / f"SDL2-{VERSION}").resolve()):
                    raise ValueError("Unsafe SDL source link")
            elif not (member.isfile() or member.isdir()):
                raise ValueError("Unsupported SDL source member")
        source.extractall(destination, members=members)


def prepare(destination, archive=None, jobs=2, cc="cc", cxx="c++"):
    if platform.system() != "Linux":
        raise ValueError("The native SDL preparation requires Linux")
    destination = Path(destination).absolute()
    # A failed or old workspace is evidence, never an implicit cache hit.
    destination.mkdir(parents=True, exist_ok=False)
    downloaded = destination / ARCHIVE
    if archive:
        verify_archive(archive)
        shutil.copyfile(archive, downloaded)
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "DataPump-build"})
        with urllib.request.urlopen(request, timeout=120) as response:
            with downloaded.open("xb") as output:
                shutil.copyfileobj(response, output)
        verify_archive(downloaded)
    extract_source(downloaded, destination)
    source = destination / f"SDL2-{VERSION}"
    build = destination / "build"
    prefix = destination / "install"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix}",
                 "-DCMAKE_INSTALL_LIBDIR=lib", f"-DCMAKE_C_COMPILER={cc}",
                 f"-DCMAKE_CXX_COMPILER={cxx}"]
    configure += [f"-D{key}={value}" for key, value in OPTIONS.items()]
    subprocess.run(configure, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(jobs)], check=True)
    subprocess.run(["cmake", "--install", str(build)], check=True)
    library = prefix / "lib/libSDL2.a"
    if not library.is_file() or not (prefix / "lib/cmake/SDL2/SDL2Config.cmake").is_file():
        raise ValueError("SDL did not install the required static host and CMake config")
    configuration = (prefix / "include/SDL2/SDL_config.h").read_text()
    for driver in ("X11", "DUMMY"):
        if not re.search(rf"^#define SDL_VIDEO_DRIVER_{driver} 1$", configuration, re.M):
            raise ValueError(f"SDL did not enable its required {driver} video driver")
    metadata = prefix / "share/datapump-native-sdl"
    metadata.mkdir(parents=True)
    shutil.copyfile(downloaded, metadata / ARCHIVE)
    shutil.copyfile(source / "LICENSE.txt", metadata / "LICENSE.txt")
    manifest = {
        "schema": 1, "name": "SDL2", "version": VERSION,
        "source": {"url": URL, "sha256": SHA256, "archive": ARCHIVE},
        "options": OPTIONS, "configure": configure,
        "compiler": subprocess.check_output([cc, "--version"], text=True).strip(),
        "architecture": platform.machine(),
        "library": {"path": "lib/libSDL2.a", "sha256": digest(library)},
        "presentation": "CPU surfaces through X11/XWayland or SDL dummy video",
    }
    (metadata / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Prepared SDL {VERSION}: -DDATAPUMP_NATIVE_SDL_ROOT={prefix}")
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="new preparation directory")
    parser.add_argument("--archive", type=Path, help="existing pinned source archive (offline)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    prepare(args.destination, args.archive, args.jobs, args.cc, args.cxx)


if __name__ == "__main__":
    main()
