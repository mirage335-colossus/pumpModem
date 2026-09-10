# Offline, copyable installations

A working Data Pump installation must be movable without downloading packages.
The portable bundle is the distribution format for this: it includes the modem,
GUI, and their application runtimes in one relocatable directory. Copy the whole
directory or archive it for transfer. There is no installation or environment
creation step on the destination.

The Python/Tk GUI uses the Python standard library only. It has no pip packages.
Python, its native extension modules, Tcl/Tk, and Tcl/Tk's script resources are
copied into the bundle. Copying only the GUI scripts or a virtual environment
does not provide an equivalent installation.

## Create a bundle from local files

Build and test the compiled modem first, using an existing toolchain. Then run
the bundler from the repository root on the computer holding the working runtime.
Linux packaging uses the local `ldd` tool to resolve native dependencies:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

python3 tools/bundle_portable.py \
  --pump build/pump \
  --output build/DataPump-portable

./build/DataPump-portable/datapump-gui --self-check
./build/DataPump-portable/datapump-gui
```

`--output` must name a new directory. `--python /path/to/python3` selects another
existing interpreter to package; otherwise the bundler uses the interpreter
running it. Use a full Python installation with Tk, not an environment containing
links to a runtime you do not have. All source files must already be available
locally. The bundler does not run a package manager or download anything.
If packaging fails, the output directory is marked `BUILD-INCOMPLETE.txt` and
must not be distributed as a working installation. Correct the missing local
runtime files and retry with a new output directory.

If Python's Tk modules are present in a locally unpacked distribution tree,
supply that tree as an overlay:

```sh
python3 tools/bundle_portable.py \
  --pump build/pump \
  --python /usr/bin/python3 \
  --runtime-overlay /path/to/unpacked-runtime \
  --output build/DataPump-portable
```

For example, a Linux overlay can contain
`usr/lib/python3.13/tkinter/`,
`usr/lib/python3.13/lib-dynload/_tkinter.cpython-313-x86_64-linux-gnu.so`,
`usr/lib/x86_64-linux-gnu/libtcl8.6.so`,
`usr/lib/x86_64-linux-gnu/libtk8.6.so`, and
`usr/share/tcltk/{tcl8.6,tk8.6}/`, together with the distribution's copyright
notices. The Python ABI, CPU architecture, and Tcl/Tk binaries and resource
versions must match. An overlay is a source of existing files, not a request to
fetch or install packages.

On Windows, run the bundler on Windows with a full local Python installation
that includes Tcl/Tk:

```bat
python tools\bundle_portable.py --pump build\Release\pump.exe --output build\DataPump-portable
build\DataPump-portable\datapump-gui.cmd --self-check
build\DataPump-portable\datapump-gui.cmd
```

Use the supported Windows build described in the main README. A Linux bundle
cannot supply a Windows runtime, and a Windows bundle cannot supply a Linux one.

## Transfer and run

Copy the entire `DataPump-portable` directory to the destination, including
`runtime`, `app`, `bin`, `licenses`, and `manifest.json`. Preserve executable
permissions when copying a Linux bundle. An archive is convenient for removable
media:

```sh
tar -C build -czf build/DataPump-portable.tar.gz DataPump-portable
```

On the destination, extract it anywhere you can read and execute files:

```sh
tar -xzf DataPump-portable.tar.gz
./DataPump-portable/datapump-gui --self-check
./DataPump-portable/datapump-gui
./DataPump-portable/pump simulate --text 'offline copy works' --snr 12 --json
```

These commands use the copied runtimes. Python does not need to be on the
destination's `PATH`. The launchers locate files relative to their own bundle,
so changing the parent directory does not require regeneration. Launch the
root-level `pump`, `python`, and `datapump-gui` wrappers (`.cmd` on Windows),
rather than executables inside `bin` or `runtime`.

`datapump-gui --self-check` verifies the inventory and file hashes, imports the
GUI's Python dependencies, starts Tcl, and checks the bundled modem. It does not
need a display or transmit audio. Start the GUI to verify the destination's
display, clipboard, and font integration; test actual audio separately when
using audio hardware. A successful headless check does not test that hardware.

Keep a known working archive before replacing an installation. Updating means
building and checking a new complete bundle and copying it alongside the old
one. Avoid combining files from different Python or Tcl/Tk versions. Shared
keyfiles and user data are separate from the application bundle and are not
automatically collected by packaging.

## Contents and operating-system boundary

| Location | Contents |
| --- | --- |
| Root launchers | Relocatable CLI, bundled Python, and GUI entry points |
| `bin/` | Compiled modem executable and its Windows native dependencies |
| `app/` | GUI scripts and bundle bootstrap |
| `runtime/python/` | Python interpreter, standard library, native extensions, and Windows Python dependencies |
| `runtime/lib/` | Collected Linux native dependencies |
| `runtime/tcl/` | Tcl/Tk script resources on both platforms |
| `manifest.json` | Bundle metadata and file checksums |
| `licenses/` | Collected application and runtime notices |

Linux dependency collection follows the modem, Python, and Python extension
libraries. The builder checks the copied binaries' dependency resolution and
rejects native dependencies that still resolve to source files outside the
bundle. The headless check also verifies where its native libraries were loaded.
The bundle uses the destination's glibc and ELF loader as an explicit
platform requirement, along with the rest of glibc's library family. Build on the
oldest Linux platform you intend to support, using the same CPU architecture as
the destination. The destination must have glibc at least as new as the build
computer and a compatible kernel. This is not a cross-platform executable or a
replacement for an operating system.

The destination provides its desktop/display server, installed fonts, and audio
devices, drivers, and configuration. Linux bundles carry the locally discovered
ALSA library and its configuration resources, plus plugins when present. Live
audio still needs a functioning audio system on the destination; WAV and
simulation do not need audio hardware. These are operating-system services,
separate from the copied Python/Tk runtime. A minimal server without a desktop
can run the headless check and CLI but cannot display a Tk window.

Windows packaging reads ordinary and delay-load PE imports and recursively
copies their DLL dependencies. Python and the modem have separate dependency
directories, allowing their private runtimes to coexist. Private DLLs beside an
application or in its explicitly supplied runtime directories take precedence
over other installed versions on `PATH` or in `System32`. Conflicting private
copies cause packaging to fail. Visual C++ runtime DLLs such as `MSVCP140.dll`
and `VCRUNTIME140.dll` are copied even when their source is `System32`; their
presence on the build computer is not treated as proof of destination support.

Windows 10 system DLLs and API sets form the Windows platform baseline and are
not copied. Bundles require the matching CPU architecture, Windows 10 version
1903 or newer for the modem's Unicode path support, and any additional Windows
version requirements of the selected Python runtime. The PE parser and collector
have automated synthetic PE32/PE32+ fixture tests. Actual Windows packaging,
runtime relocation, and hardware operation have not been validated by the Linux
test environment.

The manifest records the packaged contents. File hashes detect missing or
changed files relative to that manifest; they are not a digital signature.
Retain the runtime notices when copying the bundle. If you supply a custom
Python or native-library distribution, keep its accompanying license and source
materials with your distribution as appropriate to those components.

Ordinary CPack archives and `cmake --install` remain available for machines where
the runtimes are already managed separately. Those packages contain the GUI
source launcher, not this complete Python/Tk runtime. Use the portable bundler
for the offline copy workflow described here.
