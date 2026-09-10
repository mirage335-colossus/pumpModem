#!/usr/bin/env python3
"""Snapshot a working local Python/Tk runtime. This tool never downloads anything."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
EXCLUDED = {"__pycache__", "site-packages", "dist-packages", "ensurepip", ".git"}
GLIBC = re.compile(r"^(?:ld-linux.*|ld64.*|lib(?:c|m|dl|pthread|rt|util|resolv|anl|BrokenLocale|nss_[^.]+)\.so(?:\..*)?)$")


def copy_tree(source: Path, destination: Path) -> None:
    """Own every copied byte; never leave links back into the build machine."""
    def visit(src, dst, ancestors):
        real = src.resolve(strict=True)
        if real in ancestors:
            raise RuntimeError(f"Runtime contains a cyclic symlink: {src}")
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            for child in sorted(src.iterdir()):
                if child.name not in EXCLUDED and child.suffix not in (".pyc", ".pyo"):
                    visit(child, dst / child.name, ancestors | {real})
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst, follow_symlinks=True)
    if destination.resolve().is_relative_to(source.resolve()):
        raise RuntimeError("Runtime destination cannot be inside its source")
    visit(Path(source), Path(destination), set())


def write_manifest(root: Path, metadata: dict) -> None:
    files = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Portable runtime contains a symlink: {path}")
        if path.is_file() and path != root / "manifest.json":
            files[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (root / "manifest.json").write_text(json.dumps(
        {"schema": 1, "metadata": metadata, "files": files}, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def native_modules(root: Path):
    return [path for path in root.rglob("*.so") if not EXCLUDED.intersection(path.relative_to(root).parts)]


def overlay_environment(overlay):
    environment = os.environ.copy()
    for name in ("PYTHONHOME", "PYTHONPATH", "PYTHONUSERBASE", "PYTHONSTARTUP", "TCL_LIBRARY", "TK_LIBRARY", "TCLLIBPATH"):
        environment.pop(name, None)
    if overlay:
        libraries = [p for p in (overlay / "usr/lib", overlay / "lib") if p.is_dir()]
        libraries += sorted((overlay / "usr/lib").glob("*-linux-gnu"))
        libraries += sorted((overlay / "lib").glob("*-linux-gnu"))
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(map(str, libraries))
        for name, pattern in (("TCL_LIBRARY", "tcl*/init.tcl"), ("TK_LIBRARY", "tk*/tk.tcl")):
            candidates = sorted((overlay / "usr/share/tcltk").glob(pattern))
            if len(candidates) > 1:
                raise RuntimeError("Overlay has multiple Tcl/Tk versions; select a matching runtime")
            if candidates:
                environment[name] = str(candidates[0].parent)
    return environment


PROBE = r'''
import json, os, pathlib, platform, struct, sys, sysconfig
overlay = pathlib.Path(sys.argv[1]) if sys.argv[1] else None
version = f"python{sys.version_info.major}.{sys.version_info.minor}"
if overlay:
    for prefix in (overlay/"usr/lib"/version, overlay/"lib"/version):
        if prefix.is_dir():
            sys.path[:0] = [str(prefix), str(prefix/"lib-dynload")]
import _tkinter, tkinter
tcl = tkinter.Tcl()
tcl_library = pathlib.Path(tcl.eval("info library"))
tk_candidates = [pathlib.Path(os.environ.get("TK_LIBRARY", "/nonexistent")),
                 tcl_library.parent/("tk"+_tkinter.TK_VERSION),
                 pathlib.Path(sys.base_prefix)/"tcl"/("tk"+_tkinter.TK_VERSION)]
tk_library = next((p for p in tk_candidates if (p/"tk.tcl").is_file()), None)
if tk_library is None:
    raise RuntimeError("Tk script resources are missing; supply the complete installed runtime")
print(json.dumps({"python_version":platform.python_version(), "version_dir":version,
    "stdlib":sysconfig.get_path("stdlib"), "platstdlib":sysconfig.get_path("platstdlib"),
    "destshared":sysconfig.get_config_var("DESTSHARED"), "base_prefix":sys.base_prefix,
    "executable":sys.executable, "module_paths":{"tkinter":tkinter.__file__, "_tkinter":_tkinter.__file__},
    "TCL_VERSION":_tkinter.TCL_VERSION,"TK_VERSION":_tkinter.TK_VERSION,
    "tcl_patchlevel":tcl.eval("info patchlevel"), "tcl_library":str(tcl_library), "tk_library":str(tk_library),
    "machine":platform.machine(), "pointer_bits":struct.calcsize("P")*8, "platlibdir":sys.platlibdir,
    "sys_platform":sys.platform,"libc":list(platform.libc_ver())}))
'''


def probe_runtime(python: Path, overlay: Path | None = None) -> dict:
    result = subprocess.run([str(python), "-I", "-S", "-B", "-c", PROBE, str(overlay or "")],
                            env=overlay_environment(overlay), capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("A complete working Python/Tk runtime is required locally. "
                           "No packages were fetched.\n" + result.stderr.strip())
    return json.loads(result.stdout)


def copy_linux_libraries(roots, target, environment, *, verify_root=None):
    ldd = shutil.which("ldd")
    if not ldd:
        raise RuntimeError("Packaging requires the local ldd tool to resolve native dependencies")
    target.mkdir(parents=True, exist_ok=True)
    origins, excluded = {}, set()
    for binary in roots:
        with binary.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        result = subprocess.run([ldd, str(binary)], env=environment, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Cannot inspect native dependency {binary}: {result.stderr}")
        for line in result.stdout.splitlines():
            if "not found" in line:
                raise RuntimeError(f"Missing local native dependency for {binary}: {line.strip()}")
            match = re.match(r"\s*(\S+)\s+=>\s+(/.+?)\s+\(0x[0-9a-fA-F]+\)\s*$", line)
            direct = re.match(r"\s*(/.+?)\s+\(0x[0-9a-fA-F]+\)\s*$", line)
            if match:
                name, source = match.groups()
            elif direct:
                source = direct.group(1)
                name = Path(source).name
            else:
                continue
            if GLIBC.match(name):
                excluded.add(name)
                continue
            source = Path(source).resolve()
            if verify_root and not source.is_relative_to(verify_root):
                raise RuntimeError(f"Native dependency still points outside the copied installation: {name} => {source}")
            if name in origins and origins[name] != str(source):
                previous = Path(origins[name])
                if previous.read_bytes() != source.read_bytes():
                    raise RuntimeError(f"Conflicting native libraries named {name}; use one consistent runtime")
            if name not in origins:
                if not verify_root:
                    shutil.copy2(source, target / name)
                origins[name] = str(source)
    return origins, sorted(excluded)


def licenses(root, probe, origins, overlay):
    destination = root / "licenses"
    destination.mkdir()
    copy_tree(PROJECT / "LICENSE", destination / "DataPump-MIT.txt")
    copy_tree(PROJECT / "third_party/qrcodegen/LICENSE", destination / "QR-MIT.txt")
    for candidate in (Path(probe["stdlib"]) / "LICENSE.txt", Path(probe["base_prefix"]) / "LICENSE.txt"):
        if candidate.is_file():
            copy_tree(candidate, destination / "Python-LICENSE.txt")
            break
    else:
        raise RuntimeError("Python runtime license is missing; include its original LICENSE.txt locally")
    if overlay:
        for copyright in sorted((overlay / "usr/share/doc").glob("*/copyright")):
            copy_tree(copyright, destination / (copyright.parent.name + "-copyright.txt"))
    package_sources = {}
    dpkg = shutil.which("dpkg-query")
    if sys.platform == "linux" and dpkg:
        for source in {probe["executable"], probe["module_paths"]["_tkinter"], *origins.values()}:
            result = subprocess.run([dpkg, "-S", source], capture_output=True, text=True)
            if result.returncode and source.startswith("/lib/"):
                result = subprocess.run([dpkg, "-S", "/usr" + source], capture_output=True, text=True)
            for line in result.stdout.splitlines():
                if ": " not in line:
                    continue
                package = line.split(": ", 1)[0]
                short = package.split(":")[0]
                copyright = Path("/usr/share/doc") / short / "copyright"
                if copyright.is_file():
                    copy_tree(copyright, destination / (short + "-copyright.txt"))
                version = subprocess.run([dpkg, "-W", "-f=${binary:Package} ${Version} ${source:Package} ${source:Version}", package],
                                         capture_output=True, text=True)
                if version.returncode == 0:
                    package_sources[package] = version.stdout
    if sys.platform == "win32":
        for path in Path(probe["base_prefix"]).glob("tcl/**/*license*"):
            if path.is_file():
                copy_tree(path, destination / ("Tcl-" + "-".join(path.parts[-3:])))
    (destination / "README.txt").write_text(
        "This installation contains a snapshot of local runtime dependencies.\n"
        "Retain these notices when copying the complete installation.\n"
        "Binary redistribution can carry additional license/source obligations;\n"
        "the inventory is not a claim that all redistribution duties are discharged.\n", encoding="utf-8")
    return package_sources


def launchers(root, probe):
    if probe["sys_platform"] == "linux":
        setup = '''#!/bin/sh
set -eu
case "$0" in */*) bundle_dir=${0%/*} ;; *) bundle_dir=. ;; esac
bundle_dir=$(CDPATH= cd -- "$bundle_dir" && pwd -P)
unset PYTHONHOME PYTHONPATH PYTHONUSERBASE PYTHONSTARTUP PYTHONINSPECT TCLLIBPATH
export LD_LIBRARY_PATH="$bundle_dir/runtime/lib"
export TCL_LIBRARY="$bundle_dir/runtime/tcl/TCL_DIR"
export TK_LIBRARY="$bundle_dir/runtime/tcl/TK_DIR"
'''.replace("TCL_DIR", "tcl" + probe["TCL_VERSION"]).replace("TK_DIR", "tk" + probe["TK_VERSION"])
        if (root / "runtime/alsa/alsa.conf").is_file():
            setup += '''export ALSA_CONFIG_DIR="$bundle_dir/runtime/alsa"
export ALSA_CONFIG_PATH="$bundle_dir/runtime/alsa/alsa.conf"
'''
        if (root / "runtime/alsa-lib").is_dir():
            setup += 'export ALSA_PLUGIN_DIR="$bundle_dir/runtime/alsa-lib"\n'
        for name, mode in (("datapump-gui", "gui"), ("python", "python")):
            text = setup + f'exec "$bundle_dir/runtime/python/bin/python3" -I -S -B "$bundle_dir/app/bootstrap.py" {mode} "$@"\n'
            (root / name).write_text(text, encoding="utf-8")
            (root / name).chmod(0o755)
        (root / "pump").write_text(setup + 'exec "$bundle_dir/bin/pump" "$@"\n', encoding="utf-8")
        (root / "pump").chmod(0o755)
    else:
        setup = '@echo off\r\nsetlocal\r\nset "BUNDLE_DIR=%~dp0"\r\nset "PYTHONHOME="\r\nset "PYTHONPATH="\r\n'
        setup += 'set "PYTHONUSERBASE="\r\nset "PYTHONSTARTUP="\r\nset "TCLLIBPATH="\r\n'
        setup += 'set "PATH=%BUNDLE_DIR%runtime\\python;%BUNDLE_DIR%runtime\\python\\DLLs;%SystemRoot%\\System32;%SystemRoot%"\r\n'
        setup += f'set "TCL_LIBRARY=%BUNDLE_DIR%runtime\\tcl\\tcl{probe["TCL_VERSION"]}"\r\n'
        setup += f'set "TK_LIBRARY=%BUNDLE_DIR%runtime\\tcl\\tk{probe["TK_VERSION"]}"\r\n'
        for name, mode in (("datapump-gui", "gui"), ("python", "python")):
            command = f'"%BUNDLE_DIR%runtime\\python\\python.exe" -I -S -B "%BUNDLE_DIR%app\\bootstrap.py" {mode} %*\r\nexit /b %errorlevel%\r\n'
            (root / (name + ".cmd")).write_bytes((setup + command).encode("utf-8"))
        (root / "pump.cmd").write_bytes((setup + '"%BUNDLE_DIR%bin\\pump.exe" %*\r\nexit /b %errorlevel%\r\n').encode("utf-8"))


def build(python, pump, output, overlay):
    if output.exists():
        raise RuntimeError(f"Output already exists; refusing to overwrite a working installation: {output}")
    if not pump.is_file():
        raise RuntimeError("--pump must name an existing compiled executable")
    probe = probe_runtime(python, overlay)
    if probe["sys_platform"] not in ("linux", "win32"):
        raise RuntimeError("Offline snapshots currently support Linux and Windows only")
    for source in (Path(probe["stdlib"]), Path(probe["base_prefix"]), overlay):
        if source and output.is_relative_to(source):
            raise RuntimeError("Output cannot be nested inside the source runtime")
    output.mkdir(parents=True)
    try:
        runtime = output / "runtime/python"
        if probe["sys_platform"] == "linux":
            stdlib = runtime / probe["platlibdir"] / probe["version_dir"]
            copy_tree(Path(probe["stdlib"]), stdlib)
            copy_tree(Path(probe["executable"]), runtime / "bin/python3")
            if probe["destshared"] and Path(probe["destshared"]).is_dir():
                copy_tree(Path(probe["destshared"]), stdlib / "lib-dynload")
            copy_tree(Path(probe["module_paths"]["tkinter"]).parent, stdlib / "tkinter")
            extension = Path(probe["module_paths"]["_tkinter"])
            copy_tree(extension, stdlib / "lib-dynload" / extension.name)
        else:
            base = Path(probe["base_prefix"])
            for name in ("Lib", "DLLs"):
                copy_tree(base / name, runtime / name)
            for path in base.iterdir():
                if (path.name.lower().startswith(("python", "vcruntime")) and
                        path.suffix.lower() in (".exe", ".dll", ".zip") and path.is_file()):
                    copy_tree(path, runtime / path.name)
            version = "".join(probe["python_version"].split(".")[:2])
            (runtime / ("python" + version + "._pth")).write_text(
                f"python{version}.zip\nDLLs\nLib\n", encoding="utf-8")
        for name, field in (("tcl" + probe["TCL_VERSION"], "tcl_library"), ("tk" + probe["TK_VERSION"], "tk_library")):
            copy_tree(Path(probe[field]), output / "runtime/tcl" / name)
        # Some distributions keep Tcl modules beside tcl8.6 instead of inside it.
        for sibling in Path(probe["tcl_library"]).parent.glob("tcl[0-9]"):
            if sibling.is_dir():
                copy_tree(sibling, output / "runtime/tcl" / sibling.name)
        executable_name = "pump.exe" if probe["sys_platform"] == "win32" else "pump"
        copy_tree(pump, output / "bin" / executable_name)
        for name in ("datapump_gui.py", "model.py"):
            copy_tree(PROJECT / "gui" / name, output / "app" / name)
        copy_tree(PROJECT / "tools/portable_bootstrap.py", output / "app/bootstrap.py")
        copy_tree(PROJECT / "docs", output / "docs")
        copy_tree(PROJECT / "README.md", output / "README.md")
        origins, excluded = {}, []
        if probe["sys_platform"] == "linux":
            environment = overlay_environment(overlay)
            native = [Path(probe["executable"]), pump, extension]
            native += native_modules(Path(probe["stdlib"]))
            if probe["destshared"]:
                native += native_modules(Path(probe["destshared"]))
            # Audio is loaded dynamically, so it is not visible in pump's ldd output.
            asound = next(iter(sorted(Path("/usr/lib").glob("*-linux-gnu/libasound.so.2"))), None)
            if asound:
                native.append(asound)
                plugin_directory = asound.parent / "alsa-lib"
                if plugin_directory.is_dir():
                    copy_tree(plugin_directory, output / "runtime/alsa-lib")
                    native += list(plugin_directory.rglob("*.so"))
            origins, excluded = copy_linux_libraries(sorted(set(native)), output / "runtime/lib", environment)
            if asound:
                copy_tree(asound, output / "runtime/lib/libasound.so.2")
                origins["libasound.so.2"] = str(asound.resolve())
                if Path("/usr/share/alsa").is_dir():
                    copy_tree(Path("/usr/share/alsa"), output / "runtime/alsa")
        else:
            from pe_dependencies import copy_dependencies
            base = Path(probe["base_prefix"])
            python_images = [base / "python.exe", *base.glob("*.dll"), *base.glob("DLLs/*.pyd"), *base.glob("DLLs/*.dll")]
            python_origins, python_system = copy_dependencies(python_images, runtime, [base, base / "DLLs"])
            pump_origins, pump_system = copy_dependencies([pump], output / "bin", [pump.parent])
            origins = {"python/" + name: path for name, path in python_origins.items()}
            origins.update({"pump/" + name: path for name, path in pump_origins.items()})
            excluded = sorted(set(python_system + pump_system))
        packages = licenses(output, probe, origins, overlay)
        metadata = {key: probe[key] for key in ("python_version", "sys_platform", "machine", "pointer_bits", "libc", "TCL_VERSION", "TK_VERSION", "tcl_patchlevel")}
        metadata.update(native_libraries=origins, platform_libraries=excluded, package_sources=packages,
                        runtime_policy="owned interpreter and stdlib; isolated -I -S -B; no package installation or network bootstrap")
        launchers(output, probe)
        if probe["sys_platform"] == "linux":
            # Catch absolute DT_RPATH/DT_NEEDED links to a source installation.
            # Merely running on the build machine could otherwise mask them.
            relocated_environment = overlay_environment(None)
            relocated_environment["LD_LIBRARY_PATH"] = str(output / "runtime/lib")
            images = [output / "bin/pump", runtime / "bin/python3"]
            images += list(runtime.rglob("*.so"))
            images += list((output / "runtime/lib").glob("*.so*"))
            copy_linux_libraries(images, output / "runtime/lib", relocated_environment, verify_root=output)
        write_manifest(output, metadata)
        launcher = output / ("datapump-gui.cmd" if probe["sys_platform"] == "win32" else "datapump-gui")
        result = subprocess.run([str(launcher), "--self-check"], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("Copied runtime failed its offline self-check:\n" + result.stderr)
        print(f"Created verified offline installation: {output}\nCopy this entire directory to a compatible computer.")
    except Exception:
        (output / "BUILD-INCOMPLETE.txt").write_text("Packaging did not finish. This directory is not a verified installation.\n")
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pump", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    parser.add_argument("--runtime-overlay", type=Path, help="Locally unpacked matching Python/Tk packages; never downloaded")
    options = parser.parse_args(argv)
    try:
        build(options.python.resolve(), options.pump.resolve(), options.output.resolve(),
              options.runtime_overlay.resolve() if options.runtime_overlay else None)
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Offline bundle: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
