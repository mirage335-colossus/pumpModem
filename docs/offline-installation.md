# Native offline installation

Data Pump's desktop application and CLI are C++ executables. Python, Tk, virtual
environments, and package downloads are absent from the application installation.
A working installation can be copied as one directory to another compatible
computer and run immediately.

The complete FLTK 1.4.5 source is pinned in `third_party/fltk`; the QR encoder and
the XZ 5.8.4 liblzma source subset are also vendored. liblzma is compiled into
the application statically, so the destination needs no compression package or
liblzma DLL. Its license and source provenance are included in each bundle.
CMake uses these local sources and an already installed C/C++
toolchain, OpenSSL development files, and desktop development libraries. There is
no `FetchContent`, runtime bootstrap, or dependency download during configuration,
compilation, installation, or packaging.

## Build and package

From the repository root, using the build dependencies listed in the README:

```sh
./build.sh test contract
./build.sh package

cmake --install build/release --prefix "$PWD/build/DataPump-portable"
./build/DataPump-portable/bin/datapump-gui --self-check
./build/DataPump-portable/bin/datapump-gui
```

The package command uses a separate portable Release tree, builds only application
prerequisites, and verifies both TGZ and ZIP archives with the existing inventory,
relocation and ABI checks. It does not require Python for FLTK. To demonstrate
that explicitly, append `-- -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE`.

For direct CMake use, configure `cmake --preset release`, build with
`cmake --build --preset release`, then create archives with
`cmake --build build/release --target package`. When using a test-enabled
configuration, build `datapump-tests` before CTest; normal builds now omit tests.
See [build profiles and test groups](building.md).

The optional `-DDATAPUMP_GUI_BACKEND=rev` profile requires Python at build time
to embed resources, plus its C++23/OpenGL toolchain. Its runtime still needs no
Python or source assets. Follow [Rev backend](rev-backend.md) for that build;
host OpenGL loaders and drivers are excluded from its collected libraries.

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
cmake --build build --config Release --target datapump-apps datapump-tests --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$PWD/DataPump-portable"
& ./DataPump-portable/bin/datapump-gui.exe --self-check
& ./DataPump-portable/bin/datapump-gui.exe
cmake --build build --config Release --target package
```

Keep build toolchains and dependency source/development packages locally if you
also need to rebuild offline. Transferring an existing working installation does
not require those build tools or source packages.

Normal bundles include dependency provenance, build configuration and all
required notices. Historical validation captures stay in the source repository;
use `-DDATAPUMP_INSTALL_VALIDATION_DATA=ON` to include them in an offline evidence
bundle. References into `docs/validation-data` in developer documents require
that option or a matching source checkout.

## Copy and run

Copy the entire `DataPump-portable` directory, or extract a CPack TGZ/ZIP archive
on the destination, anywhere your account can write. Keep `bin/`, `lib/` and
`share/` together. The bundle's `lib/` is private to this installation: do not
copy its contents into `/lib` or `/usr/lib`. No administrator access, system
library installation or `ldconfig` command is needed. Preserve executable
permissions on Linux. The layout is:

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
CI configuration require a successful native Windows CI run before a Windows
artifact is declared verified.

GLib (the GNOME utility library) and glibc (the Linux C runtime) are different
dependencies. The FLTK GUI uses neither GTK nor GLib. The Linux CI build uses
Ubuntu 22.04 and audits every executable and bundled shared library for a maximum
glibc requirement of 2.35. The same downloaded archives must then run in fresh
Ubuntu 22.04 and 24.04 compatibility jobs. A local build on a newer distribution
can have a newer glibc requirement; the CI baseline does not retroactively make
that local binary compatible with older systems.

The locally available ALSA shared library is included in Linux packages even
though audio loads it dynamically. The destination still supplies its audio
configuration, plugins, devices, and drivers. If no ALSA library was available
when packaging, live audio can use an existing compatible system ALSA library;
otherwise it reports that audio is unavailable. WAV transfer and simulation
remain available without audio hardware. Windows uses its native WinMM service.

On Linux, sharing audio with another instance or application depends on the
selected ALSA endpoint. The `default` endpoint follows the host's audio
configuration; Data Pump reports an open/configuration failure instead of
silently switching to a hardware device. Use a shared desktop audio route,
such as a working PulseAudio or PipeWire ALSA endpoint, when other applications
need the same device. Explicit `hw:` or `plughw:` endpoints can reserve a device
for one client. The bundle's private libraries do not provide an audio server
or change the host's device-sharing policy.

## Verify a copied installation

`bin/datapump-gui --self-check` runs native application checks without opening a
window. Opening the application verifies display integration; `--smoke-test`
exercises its native GUI workflow and needs a desktop display. Neither test
transmits live audio.
Successful simulation and GUI smoke checks do not verify physical audio-device
access or sharing between applications.

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

## GitHub Actions binaries

The **Portable native binaries** workflow runs on pushes, pull requests, and
manual dispatch. Its successful jobs attach these downloadable build artifacts
to the Actions run:

| Artifact | Build and validation |
| --- | --- |
| `DataPump-Linux-x86_64-glibc-2.35` | GCC 11 on Ubuntu 22.04; native tests, GUI workflow, relocation, ELF ABI audit, and copies tested on Ubuntu 22.04/24.04 |
| `DataPump-Windows-x86_64` | Visual Studio 2022 x64; static CRT and OpenSSL, native tests, GUI workflow, and relocation |

Each artifact contains the application TGZ and ZIP archives and an outer
`SHA256SUMS.txt` inventory. Both archive formats are extracted and tested before
upload, including empty `PATH`, dependency closure, the native GUI self-check,
and a simulated GUI transfer. The Linux sanitizer job also runs independently.
Archives are retained for 30 days; copy a verified release into local storage for
long-term offline use. The workflow does not publish GitHub Releases.

GitHub Actions and the Windows vcpkg registry are pinned to upstream commit IDs.
CI may download its compiler/development dependencies while preparing a build.
Those downloads are unrelated to installing or moving the resulting application;
the transferred directory still runs offline without vcpkg, Python, or an
installer. The checked-in workflow must execute on GitHub before its hosted
Windows and distribution compatibility results exist.

Developers can apply the same archive checks locally:

```sh
cmake -DARCHIVE_DIR=/absolute/path/to/build/releases \
  -DBUILD_DIR=/absolute/path/to/build -DGUI_SMOKE=ON \
  -P tools/verify-native-archives.cmake
cmake -DPACKAGE_ROOT=/absolute/path/to/DataPump-portable \
  -DMAX_GLIBC=2.35 -P tools/verify-linux-abi.cmake
```

The optional `MAX_GLIBC` ceiling is also accepted by the archive verifier on
Linux. It checks all packaged ELF files, rejects accidental GTK/GLib dependencies,
and fails if a binary needs a newer glibc symbol version. Use a separate archive
directory for each build when retaining earlier archives or checksum files.
