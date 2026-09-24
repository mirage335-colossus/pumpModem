# Optional native build dependencies

Normal builds use the host's installed development packages. CMake and
`build.sh` never download or install dependencies. FLTK, XZ, QR and the LDPC
tables are already vendored; see [the inventory](../README.md).

For machines without permission to install development packages, the optional
`debian-13-amd64.json` manifest records 16 official Debian development/display
archives: package versions, original URLs and SHA-256 hashes. This is a native
Debian 13 amd64 fallback, **not** a portable binary distribution or a complete
cross-compilation sysroot. Other distributions should use their native packages
or supply their own existing prefix with `-DDATAPUMP_DEPENDENCY_PREFIX=/path/usr`.

## Prepare once, then build offline

Python 3.8+, dpkg and dpkg-deb are needed only for this preparation helper.
The matching runtime packages must already be installed. No root access is used.

```sh
# Fully offline, using previously downloaded archives:
python3 tools/prepare-build-deps.py --packages-dir /path/to/debian-archives

# Alternatively, explicitly permit downloading the missing pinned archives:
python3 tools/prepare-build-deps.py --download

./build.sh
```

The helper verifies every archive before extraction, checks that the relevant
installed runtime packages exactly match the development package versions, and
creates development linker aliases to those installed libraries. It never takes
runtime libraries from an old DataPump installation. A mismatch fails with an
explanation; update the manifest or install matching native development packages.

The ignored `cache/packages/` retains verified archives. `cache/sysroot/` contains
the extracted headers and tools; `cache/sysroot/prepared.json` records the
manifest hash and actual installed runtime versions, paths and SHA-256 values.
Re-running without arguments uses the retained archives and repairs the managed
SDK. Preparation must finish before configuring/building; do not run it while a
compiler is using that SDK. No unrelated build directory is deleted.

CMake automatically detects the prepared default prefix for native Linux amd64
builds, never for cross-compilation or another target architecture. To opt out,
use a system prefix explicitly, for example `-DDATAPUMP_DEPENDENCY_PREFIX=/usr`.
`--cache-dir /persistent/path` selects another cache; pass its `sysroot/usr`
directory to CMake explicitly. Switching dependency prefixes or moving a checkout requires a fresh build tree,
as it does for ordinary absolute CMake paths.

The FLTK wrapper marks checkout-local dependency includes as build-only in the
unused upstream install export. Vendored FLTK sources remain unmodified. There
is no `/tmp` symlink or dependency on an old `build-native` tree.

## Provenance and upgrades

The initial archive manifest was recovered from the former local
`build-native/dev-support/packages.json`. Those records used the official Debian
APT indexes and package archive URLs. Runtime-copy records from the old setup
are deliberately not used by the new helper.

For an upgrade, obtain package versions and SHA-256 values from authenticated
Debian package metadata, update URLs and matching runtime entries together, and
prepare a fresh cache. Review the packages' copyright files under the extracted
`usr/share/doc/` directories. Keep downloaded/extracted files out of Git. Record
the new manifest and run the build-tool tests, application build, compatibility
tests and native GUI checks. A new SDK does not lower a binary's glibc ABI floor.

For a release, prefer the documented Ubuntu 22.04 builder and its own installed
dependencies. Never combine this Debian SDK with an older release sysroot.
See [build profiles](../../docs/building.md) and
[portable installation](../../docs/offline-installation.md).

These preparation commands and source-file references assume a source checkout;
the installed application bundle includes the provenance notes, not build tools.
