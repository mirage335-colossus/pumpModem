"""Opt-in relocation test; builds exclusively from locally available files.

Example: python3 tests/portable_smoke.py --pump build/pump
Pass --bundle to test an already built portable directory instead.
"""
import argparse
import base64
import fnmatch
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


PROJECT = pathlib.Path(__file__).resolve().parents[1]


def run(command, *, env=None, cwd=None, timeout=180):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            env=env, cwd=cwd, timeout=timeout)
    if result.returncode:
        raise AssertionError(
            f"{command[0]} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
    return result.stdout


def verify_manifest(bundle):
    manifest = json.loads((bundle / "manifest.json").read_text())
    files = manifest["files"]
    actual = {path.relative_to(bundle).as_posix() for path in bundle.rglob("*")
              if path.is_file() and path != bundle / "manifest.json"}
    assert set(files) == actual, "Manifest must cover every shipped file"
    for relative, expected in files.items():
        path = bundle / relative
        assert path.resolve().is_relative_to(bundle), f"External manifest path: {relative}"
        assert not path.is_symlink(), f"Runtime file must be copied, not symlinked: {relative}"
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, relative
    return manifest


def launcher(bundle, name):
    return bundle / (name + ".cmd" if os.name == "nt" else name)


def check_relocated(bundle, scratch, *, gui_smoke):
    """Exercise with no host Python lookup or inherited Python configuration."""
    verify_manifest(bundle)
    poison = scratch / "host python trap"
    poison.mkdir()
    marker = poison / "HOST-PYTHON-WAS-USED"
    (poison / "sitecustomize.py").write_text(
        f"open({str(marker)!r}, 'w').write('host sitecustomize was imported')\n"
        "raise RuntimeError('Host Python configuration was loaded')\n")
    env = os.environ.copy()
    env.update(PATH="", PYTHONHOME=str(poison), PYTHONPATH=str(poison),
               PYTHONUSERBASE=str(poison), PYTHONSTARTUP=str(poison / "sitecustomize.py"),
               TCL_LIBRARY=str(poison), TK_LIBRARY=str(poison), TCLLIBPATH=str(poison),
               TCL8_6_TM_PATH=str(poison))
    env.pop("DISPLAY", None)
    self_check = run([launcher(bundle, "datapump-gui"), "--self-check"], env=env, cwd=scratch)
    assert self_check.strip(), "Headless self-check must report its result"
    # Read module origins from the running relocated interpreter, rather than
    # trusting values recorded by the builder before the directory was moved.
    probe = """
import _tkinter, json, pathlib, ssl, sys, tkinter
tcl = tkinter.Tcl()
tcl.eval('package require msgcat')
assert tcl.eval('clock format 0 -gmt 1 -format %Y') == '1970'
maps = pathlib.Path('/proc/self/maps')
native_paths = []
if sys.platform == 'linux' and maps.exists():
    for line in maps.read_text().splitlines():
        columns = line.split(maxsplit=5)
        if len(columns) == 6 and columns[5].startswith('/'):
            native_paths.append(columns[5])
print(json.dumps({
    'executable': sys.executable,
    'prefix': sys.prefix,
    'path': sys.path,
    'modules': [module.__file__ for module in (_tkinter, pathlib, ssl, tkinter)],
    'tcl_library': tcl.eval('info library'),
    'tcl_auto_path': tcl.splitlist(tcl.eval('set auto_path')),
    'tcl_module_path': tcl.splitlist(tcl.eval('::tcl::tm::path list')),
    'msgcat_loader': tcl.eval('package ifneeded msgcat [package require msgcat]'),
    'site_loaded': 'site' in sys.modules,
    'native_paths': sorted(set(native_paths)),
}))
"""
    report = json.loads(run([launcher(bundle, "python"), "-c", probe], env=env, cwd=scratch))
    assert not report["site_loaded"], "Bundled interpreter must not load host site packages"
    assert str(poison) not in report["tcl_auto_path"], "Host Tcl package search path leaked into runtime"
    assert str(poison) not in report["tcl_module_path"], "Host Tcl module search path leaked into runtime"
    assert bundle.as_posix() in report["msgcat_loader"].replace("\\", "/"), (
        "Tcl message catalogs loaded from outside the copied installation")
    paths = [report["executable"], report["prefix"], report["tcl_library"],
             *report["path"], *report["modules"]]
    for value in paths:
        assert value, "Empty sys.path entry would import from the working directory"
        assert pathlib.Path(value).resolve().is_relative_to(bundle), (
            f"Runtime depends on a path outside the relocated installation: {value}")
    # A relocated interpreter can still appear healthy by silently loading the
    # builder's Tcl/Tk or OpenSSL libraries. Only the documented OS glibc baseline
    # may be supplied by the host when exercising the packaged native imports.
    os_libraries = ("ld-linux*.so*", "ld64.so*", "libc.so.*", "libm.so.*", "libdl.so.*",
                    "libpthread.so.*", "librt.so.*", "libresolv.so.*", "libutil.so.*",
                    "libanl.so.*", "libBrokenLocale.so.*", "libnss_*.so.*")
    for value in report["native_paths"]:
        path = pathlib.Path(value)
        if ".so" not in path.name or any(fnmatch.fnmatch(path.name, pattern) for pattern in os_libraries):
            continue
        assert path.resolve().is_relative_to(bundle), f"Native dependency loaded from host: {value}"
    assert not marker.exists(), "Host sitecustomize was executed"
    payload = "Offline relocation · café 🌍"
    packet = json.loads(run([launcher(bundle, "pump"), "simulate", "--text", payload,
                             "--snr", "18", "--json"], env=env, cwd=scratch))
    assert packet["validated"], "Relocated modem rejected its own transmission"
    assert base64.b64decode(packet["data_base64"]).decode() == payload
    if gui_smoke:
        if os.name != "nt":
            if not os.environ.get("DISPLAY"):
                raise RuntimeError("--gui-smoke requires DISPLAY (for example a running Xvfb)")
            env["DISPLAY"] = os.environ["DISPLAY"]
        gui_paths = list(bundle.rglob("datapump_gui.py"))
        assert len(gui_paths) == 1, "Expected exactly one bundled GUI entry point"
        gui_pump = bundle / "bin/pump.exe" if os.name == "nt" else launcher(bundle, "pump")
        run([launcher(bundle, "python"), PROJECT / "tests/gui_smoke.py", "--pump", gui_pump,
             "--gui-dir", gui_paths[0].parent], env=env, cwd=scratch)
    assert not marker.exists(), "Host sitecustomize was executed"
    verify_manifest(bundle)
    model = bundle / "app/model.py"
    original = model.read_bytes()
    try:
        model.write_bytes(original + b"\n# Simulated damage while copying the installation.\n")
        damaged = subprocess.run([str(launcher(bundle, "datapump-gui")), "--self-check"],
                                 capture_output=True, text=True, env=env, cwd=scratch, timeout=180)
        assert damaged.returncode != 0, "Integrity check accepted a modified bundled file"
        assert "app/model.py" in damaged.stderr.replace("\\", "/"), (
            "Integrity failure should identify the damaged file")
    finally:
        model.write_bytes(original)
    verify_manifest(bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--bundle", type=pathlib.Path)
    source.add_argument("--pump", type=pathlib.Path)
    parser.add_argument("--python", type=pathlib.Path, default=pathlib.Path(sys.executable))
    parser.add_argument("--runtime-overlay", type=pathlib.Path)
    parser.add_argument("--gui-smoke", action="store_true")
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="datapump-relocation-") as temporary:
        scratch = pathlib.Path(temporary)
        source = scratch / "build source"
        if options.bundle:
            shutil.copytree(options.bundle.resolve(), source)
        else:
            command = [sys.executable, PROJECT / "tools/bundle_portable.py", "--pump",
                       options.pump.resolve(), "--output", source,
                       "--python", options.python.resolve()]
            if options.runtime_overlay:
                command.extend(["--runtime-overlay", options.runtime_overlay.resolve()])
            run(command, timeout=600)
        relocated = scratch / "offline destination" / "Data Pump with spaces"
        shutil.copytree(source, relocated)
        # The source name is now absent, making embedded build paths unusable.
        source.rename(scratch / "unavailable original")
        check_relocated(relocated, scratch, gui_smoke=options.gui_smoke)
    print("Portable relocation smoke passed: manifest and corruption detection, isolated Python/Tk/Tcl, CLI simulation"
          + (", real GUI workflow" if options.gui_smoke else ""))


if __name__ == "__main__":
    main()
