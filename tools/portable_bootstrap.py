"""Executed only by the copied interpreter in isolated mode; no dependency resolver."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "app"))
DLL_HANDLES = []
if sys.platform == "win32":
    for directory in (ROOT / "runtime/python", ROOT / "runtime/python/DLLs", ROOT / "bin"):
        DLL_HANDLES.append(os.add_dll_directory(str(directory)))


def isolate_tcl(value):
    for key in list(os.environ):
        if key == "TCLLIBPATH" or (key.startswith("TCL") and key.endswith("_TM_PATH")):
            del os.environ[key]
    metadata = value["metadata"]
    tcl_root = ROOT / "runtime/tcl"
    os.environ["TCL_LIBRARY"] = str(tcl_root / ("tcl" + metadata["TCL_VERSION"]))
    os.environ["TK_LIBRARY"] = str(tcl_root / ("tk" + metadata["TK_VERSION"]))
    modules = []
    for directory in sorted({path.parent for path in tcl_root.rglob("*.tm")}, key=lambda path: len(path.parts)):
        if not any(directory.is_relative_to(parent) for parent in modules):
            modules.append(directory)
    version = metadata["TCL_VERSION"].replace(".", "_")
    os.environ["TCL" + version + "_TM_PATH"] = os.pathsep.join(map(str, modules))
    import tkinter
    original = tkinter.Tk

    class BundledTk(original):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            # Replace Tcl's compiled host defaults, also for tkinter.Tcl().
            local = (os.environ["TCL_LIBRARY"], os.environ["TK_LIBRARY"], str(tcl_root))
            self.tk.call("set", "auto_path", local)
            self.tk.call("set", "tcl_pkgPath", local)
            existing = self.tk.call("::tcl::tm::path", "list")
            if existing:
                self.tk.call("::tcl::tm::path", "remove", *self.tk.splitlist(existing))
            if modules:
                self.tk.call("::tcl::tm::path", "add", *map(str, modules))
    tkinter.Tk = BundledTk


def manifest():
    if (ROOT / "BUILD-INCOMPLETE.txt").exists():
        raise RuntimeError("This offline installation was not completely built")
    value = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
    if value.get("schema") != 1:
        raise RuntimeError("Unsupported offline installation manifest")
    metadata = value["metadata"]
    if metadata["sys_platform"] != sys.platform or metadata["machine"].lower() != platform.machine().lower():
        raise RuntimeError("This offline installation targets another OS or CPU architecture")
    if metadata["libc"][0] == "glibc":
        family, current = platform.libc_ver()
        version = lambda text: tuple(int(x) for x in text.split("."))
        if family != "glibc" or version(current) < version(metadata["libc"][1]):
            raise RuntimeError("This Linux bundle needs glibc " + metadata["libc"][1] + " or newer")
    return value


def self_check(value):
    actual = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*") if p.is_file() and p != ROOT / "manifest.json"}
    if actual != set(value["files"]):
        raise RuntimeError("Bundle inventory differs from its manifest (missing or extra files)")
    for relative, expected in value["files"].items():
        path = ROOT / relative
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError("Invalid or external bundle path: " + relative)
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError("Missing or modified bundle file: " + relative)
    import _tkinter
    import encodings
    import ssl
    import tkinter
    interpreter = tkinter.Tcl()
    interpreter.call("package", "require", "msgcat")
    interpreter.call("clock", "format", 0, "-gmt", 1, "-format", "%Y")
    executable = ROOT / "bin" / ("pump.exe" if sys.platform == "win32" else "pump")
    pump = subprocess.run([str(executable), "--version"], capture_output=True, text=True, check=True)
    modules = {name: module.__file__ for name, module in {
        "_tkinter": _tkinter, "tkinter": tkinter, "ssl": ssl, "encodings": encodings}.items()}
    for path in [sys.executable, sys.prefix, *sys.path, *modules.values(), interpreter.eval("info library")]:
        if not path or not Path(path).resolve().is_relative_to(ROOT):
            raise RuntimeError("Runtime unexpectedly uses a path outside its installation: " + path)
    if sys.platform == "linux" and Path("/proc/self/maps").is_file():
        baseline = re.compile(r"^(?:ld-linux.*|ld64.*|lib(?:c|m|dl|pthread|rt|util|resolv|anl|BrokenLocale|nss_[^.]+)\.so(?:\..*)?)$")
        for line in Path("/proc/self/maps").read_text().splitlines():
            fields = line.split(maxsplit=5)
            if len(fields) != 6 or not fields[5].startswith("/"):
                continue
            path = Path(fields[5])
            if ".so" in path.name and not baseline.match(path.name) and not path.resolve().is_relative_to(ROOT):
                raise RuntimeError("Native library loaded outside the bundle: " + str(path))
    print(json.dumps({"python_executable": sys.executable, "python_prefix": sys.prefix,
                      "sys_path": sys.path, "module_paths": modules,
                      "tcl_library": interpreter.eval("info library"), "tk_library": os.environ["TK_LIBRARY"],
                      "pump_version": pump.stdout.strip(), "verified_files": len(value["files"])}))


def main():
    value = manifest()
    isolate_tcl(value)
    mode, arguments = sys.argv[1], sys.argv[2:]
    if mode == "gui":
        if arguments == ["--self-check"]:
            self_check(value)
            return
        from datapump_gui import main as gui_main
        executable = ROOT / "bin" / ("pump.exe" if sys.platform == "win32" else "pump")
        sys.argv = ["datapump-gui", "--pump", str(executable), *arguments]
        gui_main()
    elif mode == "python":
        if not arguments:
            raise RuntimeError("Use python -c CODE, python -m MODULE, or python SCRIPT in this offline runtime")
        if arguments[0] == "-c" and len(arguments) >= 2:
            sys.argv = ["-c", *arguments[2:]]
            exec(compile(arguments[1], "<string>", "exec"), {"__name__": "__main__"})
        elif arguments[0] == "-m" and len(arguments) >= 2:
            sys.argv = arguments[1:]
            runpy.run_module(arguments[1], run_name="__main__", alter_sys=True)
        elif not arguments[0].startswith("-"):
            sys.argv = arguments
            runpy.run_path(arguments[0], run_name="__main__")
        else:
            raise RuntimeError("Unsupported bundled Python invocation")
    else:
        raise RuntimeError("Unknown offline launcher mode")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print("Data Pump offline installation: " + str(error), file=sys.stderr)
        sys.exit(1)
