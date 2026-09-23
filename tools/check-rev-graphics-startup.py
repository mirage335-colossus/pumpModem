#!/usr/bin/env python3
"""Check a copied Rev GUI's X11 startup and host software graphics loading.

Run inside a private Xvfb display. This is not a GUI smoke test or certification.
The optional second argument preserves the application's combined output in a
new file outside the package; diagnostic output is also printed to the console.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


TITLE = re.compile(r'^\s*(0x[0-9a-fA-F]+)\s+"Data Pump":', re.MULTILINE)
RUNTIME_DSO = re.compile(r'^(?:libstdc\+\+|libgcc_s|libc\+\+|libc\+\+abi|libunwind)\.so(?:\.|$)')
LOG_LIMIT = 64 * 1024


def inside(path, root):
    return path.resolve().is_relative_to(root)


def xquery(executable, arguments, deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError('The 15-second startup deadline expired')
    result = subprocess.run(
        [executable, *arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        encoding='utf-8', errors='replace', timeout=min(1.0, remaining),
        env={**os.environ, 'LC_ALL': 'C'}, check=False)
    return result.returncode, result.stdout


def visible_window(executable, window, deadline):
    code, output = xquery(executable, ['-id', window, '-stats'], deadline)
    visible = (code == 0
               and re.search(r'^xwininfo: Window id: 0x[0-9a-fA-F]+ "Data Pump"\s*$', output, re.MULTILINE)
               and re.search(r'^\s*Map State:\s+IsViewable\s*$', output, re.MULTILINE))
    return bool(visible), output


def host_graphics_mappings(process, executable, package):
    # A child that exits after poll() remains our unreaped child while /proc is
    # read, so its PID cannot be recycled into an unrelated process here.
    if process.poll() is not None:
        raise RuntimeError(f'GUI exited before mapping inspection: {process.returncode}')
    proc = Path('/proc') / str(process.pid)
    if (proc / 'exe').resolve(strict=True) != executable:
        raise RuntimeError('The owned process is not the exact packaged GUI executable')
    paths = set()
    for line in (proc / 'maps').read_text(encoding='utf-8').splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and fields[5].startswith('/'):
            name = fields[5].removesuffix(' (deleted)').rsplit('/', 1)[-1]
            relevant = (RUNTIME_DSO.match(name)
                        or name.startswith(('libGLX_mesa', 'libEGL_mesa', 'libgallium', 'libLLVM'))
                        or name.endswith('_dri.so'))
            if not relevant:
                continue
            if fields[5].endswith(' (deleted)'):
                raise RuntimeError(f'Process maps a deleted file: {fields[5]}')
            # Linux escapes newlines in maps paths; refuse ambiguous paths.
            if '\\' in fields[5]:
                raise RuntimeError(f'Cannot verify escaped mapping path: {fields[5]}')
            paths.add(Path(fields[5]).resolve(strict=True))
    groups = {
        'Mesa': [p for p in paths if p.name.startswith(('libGLX_mesa', 'libEGL_mesa', 'libgallium'))
                 or p.name.endswith('_dri.so')],
        'LLVM': [p for p in paths if p.name.startswith('libLLVM') and '.so' in p.name],
        'libstdc++': [p for p in paths if re.match(r'^libstdc\+\+\.so(?:\.|$)', p.name)],
    }
    for name, libraries in groups.items():
        if not libraries:
            raise RuntimeError(f'No host {name} library found in the GUI process maps')
        for library in sorted(libraries):
            if inside(library, package):
                raise RuntimeError(f'{name} was loaded from inside the bundle: {library}')
            print(f'HOST_{name}={library}', flush=True)
    for path in paths:
        if RUNTIME_DSO.match(path.name) and inside(path, package):
            raise RuntimeError(f'Bundled C++ runtime mapped by GUI: {path}')
    if process.poll() is not None:
        raise RuntimeError(f'GUI exited during mapping inspection: {process.returncode}')


def stop_owned_process(process):
    if process is None:
        return
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)
    else:
        process.wait()


def print_log(path):
    if not path or not path.exists():
        return
    with path.open('rb') as stream:
        size = stream.seek(0, os.SEEK_END)
        stream.seek(max(0, size - LOG_LIMIT))
        output = stream.read(LOG_LIMIT).decode('utf-8', errors='replace')
    print(f'--- GUI stdout/stderr ({min(size, LOG_LIMIT)} of {size} bytes) ---', flush=True)
    print(output or '<empty>', flush=True)


def check(package, log_path):
    package = package.resolve(strict=True)
    executable = (package / 'bin/datapump-gui').resolve(strict=True)
    if not package.is_dir() or not executable.is_file() or not inside(executable, package):
        raise ValueError('Expected the exact executable PACKAGE_ROOT/bin/datapump-gui inside the package')
    if not os.environ.get('DISPLAY'):
        raise ValueError('DISPLAY must identify a caller-owned private Xvfb server')
    xwininfo = shutil.which('xwininfo')
    if not xwininfo:
        raise ValueError('xwininfo is required (install x11-utils)')
    for directory, directories, files in os.walk(package, followlinks=False):
        for name in [*directories, *files]:
            if RUNTIME_DSO.match(name):
                raise ValueError(f'C++ runtime DSO must not be bundled: {Path(directory) / name}')
    if log_path:
        log_path = log_path.resolve()
        if inside(log_path, package):
            raise ValueError('The diagnostic log must be outside the immutable package')

    with tempfile.TemporaryDirectory(prefix='datapump-rev-startup-') as scratch:
        log = log_path or Path(scratch) / 'gui.log'
        log_created = False
        process = None
        window_output = ''
        try:
            # Reject an already existing title rather than accepting a window
            # from another application as evidence for the owned child.
            code, output = xquery(xwininfo, ['-root', '-tree'], time.monotonic() + 2)
            if code != 0:
                raise RuntimeError(f'Cannot inspect caller X display: {output[:4096]}')
            if TITLE.search(output):
                raise RuntimeError('Data Pump window already exists; use a fresh private Xvfb display')
            env = {**os.environ, 'PATH': '', 'LD_LIBRARY_PATH': '',
                   'PYTHONHOME': '/nonexistent', 'PYTHONPATH': '/nonexistent',
                   'LIBGL_ALWAYS_SOFTWARE': '1', 'HOME': scratch,
                   'XDG_CONFIG_HOME': scratch, 'XDG_CACHE_HOME': scratch,
                   'XDG_DATA_HOME': scratch}
            env.pop('LD_PRELOAD', None)
            env.pop('LD_AUDIT', None)
            print(f'PACKAGE={package}\nDISPLAY={env["DISPLAY"]}', flush=True)
            print('Launching exact bundle GUI --simulation with empty PATH/LD_LIBRARY_PATH and software GL.', flush=True)
            deadline = time.monotonic() + 15
            with log.open('xb') as stream:
                log_created = True
                process = subprocess.Popen([str(executable), '--simulation'], cwd=scratch,
                                           env=env, stdout=stream, stderr=subprocess.STDOUT)
                window = None
                visible_since = None
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'GUI exited before startup completed: {process.returncode}')
                    try:
                        code, tree = xquery(xwininfo, ['-root', '-tree'], deadline)
                        candidates = TITLE.findall(tree) if code == 0 else []
                        if len(candidates) > 1:
                            raise RuntimeError('Multiple Data Pump windows found; cannot identify the owned startup window')
                        candidate = candidates[0] if candidates else None
                        visible = False
                        if candidate:
                            visible, window_output = visible_window(xwininfo, candidate, deadline)
                    except subprocess.TimeoutExpired:
                        candidate, visible = None, False
                    if not visible or candidate != window or visible_since is None:
                        visible_since = time.monotonic() if visible else None
                        window = candidate
                    if visible_since is not None and time.monotonic() - visible_since >= 2:
                        host_graphics_mappings(process, executable, package)
                        print(f'VISIBLE_WINDOW={window}\nVISIBLE_DURATION_SECONDS={time.monotonic() - visible_since:.2f}', flush=True)
                        break
                    time.sleep(min(0.1, max(0, deadline - time.monotonic())))
                else:
                    raise TimeoutError('No live Data Pump window stayed visible for 2 seconds within the 15-second startup deadline')
        except Exception:
            if window_output:
                print('--- Last window inspection ---\n' + window_output[-4096:], flush=True)
            raise
        finally:
            try:
                stop_owned_process(process)
            finally:
                if log_created:
                    print_log(log)
                    if log_path:
                        print(f'GUI_LOG={log_path}', flush=True)
    print('PASS: visible Rev startup and host Mesa/LLVM/libstdc++ loading only; not full smoke or certification.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package_root', type=Path)
    parser.add_argument('log_path', nargs='?', type=Path)
    args = parser.parse_args()
    try:
        check(args.package_root, args.log_path)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}', file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
