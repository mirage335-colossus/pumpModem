# Native offline installation

Data Pump's desktop application and CLI are C++ executables. Python, Tk, virtual
environments, and package downloads are absent from the application installation.
A working installation can be copied as one directory to another compatible
computer and run immediately.

The complete FLTK 1.4.5 source is pinned in `third_party/fltk`; the QR encoder is
also vendored. CMake uses these local sources and an already installed C++
toolchain, OpenSSL development files, and desktop development libraries. There is
no `FetchContent`, runtime bootstrap, or dependency download during configuration,
compilation, installation, or packaging.

## Build and package

From the repository root, using the build dependencies listed in the README:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake --install build --prefix "$PWD/build/DataPump-portable"
./build/DataPump-portable/bin/datapump-gui --self-check
./build/DataPump-portable/bin/datapump-gui

cmake --build build --target package
```

The Python switch demonstrates that the complete native build needs no Python.
Omit it if you want CTest to run additional Python CLI integration tests when an
interpreter is already available. Those tests are developer tooling and are not
installed.

Both `DATAPUMP_BUILD_GUI` and `DATAPUMP_PORTABLE` default to `ON`. Setting the GUI
option to `OFF` creates a CLI-only package. Setting the portable option to `OFF`
disables runtime collection and creates an installation for systems where native
libraries are managed separately; use the default for copyable installations.

FLTK is linked statically. Portable builds default to static OpenSSL, the static
MSVC runtime on Windows, and static GCC C++ support libraries on Linux release
builds. Remaining native dependencies are discovered recursively from the
compiled executables and copied by CMake. Packaging uses the toolchain's local
`objdump` or `dumpbin`, with no Python packaging tools. Missing or conflicting
native dependencies cause installation to fail; do not distribute a partially
installed directory after an error.

Additional native-library search directories can be supplied with
`-DDATAPUMP_RUNTIME_DIRS="/local/path/one;/local/path/two"`. Runtime notices are
collected from local package metadata where available, including the static
OpenSSL and GCC runtime inputs. For a custom toolchain or dependency distribution,
provide its accompanying notices with
`-DDATAPUMP_EXTRA_LICENSES="/local/license-one;/local/license-two"`.

For Windows, use a Windows C++20 toolchain and an already available static OpenSSL
installation. The README's Visual Studio/vcpkg configuration uses the
`x64-windows-static` triplet. After configuring:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$PWD/DataPump-portable"
& ./DataPump-portable/bin/datapump-gui.exe --self-check
& ./DataPump-portable/bin/datapump-gui.exe
cmake --build build --config Release --target package
```

Keep build toolchains and dependency source/development packages locally if you
also need to rebuild offline. Transferring an existing working installation does
not require those build tools or source packages.

## Copy and run

Copy the entire `DataPump-portable` directory, or extract a CPack TGZ/ZIP archive
on the destination. Preserve executable permissions on Linux. The layout is:

| Location | Contents |
| --- | --- |
| `bin/datapump-gui` | Native desktop application; `.exe` on Windows |
| `bin/pump` | Native command-line modem; `.exe` on Windows |
| `lib/` | Collected Linux shared libraries |
| `bin/*.dll` | Any remaining Windows application DLLs |
| `share/doc/datapump/` | Documentation, licenses, dependency provenance, and notices |
| `manifest.sha256` | Checksums of the complete installed file inventory |

Run the installed executables directly:

```sh
./DataPump-portable/bin/datapump-gui
./DataPump-portable/bin/pump simulate --text 'offline transfer' --snr 18 --json
```

Linux executables find bundled libraries relative to their own directory, and
the search path also applies to indirect dependencies. Copied libraries have
their original runtime search paths removed. Native-library symlinks are copied
as owned files, so no link points back to the source computer. Windows locates
the application's remaining DLLs beside its executables. Moving the installation
does not require an environment variable, launcher script, registry entry, or
regeneration step.

Archive a known working installation before updating. Install and check a new
complete release alongside it; avoid mixing executable and library files from
different builds. User files, shared keys, and optional pads are separate from
the application and are not automatically collected by packaging.

## Platform requirements

Build a separate package for each operating system and CPU architecture. Linux
packages use the target's glibc family and ELF loader. The destination must have
glibc at least as new as the build computer, plus a compatible kernel; build on
the oldest platform you intend to support. The package includes application
libraries such as X11/font libraries, while the destination supplies its display
server, fonts, and desktop session.

Windows system DLLs and API sets are an operating-system requirement. Visual C++
redistributable DLLs are not excluded merely because they happen to reside in
System32; the default static CRT avoids requiring their installation. The modem's
Unicode path support requires Windows 10 version 1903 or newer. Windows code and
CI configuration require actual Windows validation before a Windows release is
declared verified.

The locally available ALSA shared library is included in Linux packages even
though audio loads it dynamically. The destination still supplies its audio
configuration, plugins, devices, and drivers. If no ALSA library was available
when packaging, live audio can use an existing compatible system ALSA library;
otherwise it reports that audio is unavailable. WAV transfer and simulation
remain available without audio hardware. Windows uses its native WinMM service.

## Verify a copied installation

`bin/datapump-gui --self-check` runs native application checks without opening a
window. Opening the application verifies display integration; `--smoke-test`
exercises its native GUI workflow and needs a desktop display. Neither test
transmits live audio.

On Linux, inventory verification can also use the operating system's checksum
tool from inside the copied directory:

```sh
cd DataPump-portable
sha256sum -c manifest.sha256
```

The manifest detects missing or changed files relative to the included inventory;
it is not a signature or proof of a publisher's identity. Keep all notices with
the installation.

Developers can verify the complete inventory, native dependency resolution, and
isolated execution with the repository's CMake scripts:

```sh
cmake -DPACKAGE_ROOT=/absolute/path/to/DataPump-portable \
  -DBUILD_DIR=/absolute/path/to/build -P tools/verify-native-package.cmake
cmake -DBUILD_DIR=/absolute/path/to/build -DGUI_SMOKE=ON \
  -P tests/package_native.cmake
```

The relocation test copies an installation to a path containing spaces, removes
its original pathname, clears `PATH` and runtime overrides, verifies that every
application dependency resolves within the copy, runs the modem and GUI checks,
and confirms that file tampering and unlisted additions are rejected. On Linux,
run the GUI test under an existing desktop or `xvfb-run -a`. CMake and these test
scripts are not required to run the transferred software.
