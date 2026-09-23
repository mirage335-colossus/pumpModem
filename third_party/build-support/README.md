# Build dependencies and portable source SDK

Normal builds use the host's installed development packages. CMake and
`build.sh` never download or install dependencies. FLTK, XZ, QR and the LDPC
tables are already vendored; see [the inventory](../README.md).

## Source SDK for the Bookworm ABI baseline

The opt-in [source SDK recipe](source-sdk/manifest.json) uses Buildroot 2026.08
to build an x86_64 Linux compiler, target sysroot, CMake, Ninja and the development
libraries needed by FLTK and Rev. It pins a maintained upstream glibc 2.36
snapshot and GCC 15; it does not reconstruct a Debian installation or download
old Debian development packages. The manifest identifies source locations,
versions, commits and SHA-256 hashes. Buildroot's pinned package recipes retain
the remaining dependency versions, hashes and patches.

The compatibility target is Bookworm and other conventional x86_64 glibc-based
Linux installations with glibc 2.36 or newer. The application bundle uses the
destination's glibc/loader, display server and graphics drivers. This does not
cover musl-based systems, another CPU architecture, missing desktop facilities,
or every possible driver/audio configuration. The separate
[SDK workflow](../../.github/workflows/sdk.yml) qualifies SDK tools on Bookworm
and Ubuntu 24.04, builds both frontends and runs the same application archives on
both systems. Successful qualification is required before making a release
compatibility claim; adding a recipe is not a substitute for running it.

### Use an archived SDK

Download the compiled SDK archive and its published `SDK-SHA256SUMS.txt` (the CI
artifact calls this file `SHA256SUMS`). Save that inventory beside the archive
as `SHA256SUMS`; installation verifies the archive against it before extraction.
Then explicitly install and relocate it:

```sh
python3 tools/build-sdk.py install \
  --archive /downloads/datapump-sdk-ID-linux-x86_64.tar.gz \
  --destination "$PWD/third_party/build-support/cache/sdk-ID"
python3 tools/build-sdk.py verify \
  "$PWD/third_party/build-support/cache/sdk-ID" --max-host-glibc 2.36
./build.sh --sdk "$PWD/third_party/build-support/cache/sdk-ID"
./build.sh package --sdk "$PWD/third_party/build-support/cache/sdk-ID"
./build.sh --backend rev --sdk "$PWD/third_party/build-support/cache/sdk-ID"
```

Replace `ID` with the published recipe identity. Use an SDK installation path
without spaces, as required by the Buildroot relocation machinery. Installation
does not overwrite an existing destination. After moving an installation,
install a fresh copy from its archive and use a fresh application build tree.
Do not edit relocation metadata to bypass this check.

Consuming the SDK needs no container, chroot, package manager or root access.
The host's Python 3.9+ runs the SDK preparation helper. The prepared SDK includes
its own Python for build tools and Rev's resource builder; the application
bundle does not ship Python. Standard Linux shell utilities remain prerequisites.
`--sdk` selects the SDK compiler and gives CMake isolated target search paths;
it rejects conflicting compiler/search-path overrides. SDK profiles have a
`-sdk` suffix so they do not reuse a native build's cached paths. Packaging
collects libraries and license notices from the SDK, not the host package
database. The native Debian fallback below is not used in SDK builds.

The SDK also carries private `libstdc++.so.6` and `libgcc_s.so.1` copies for its
host tools, taken from its own GCC build for the same x86_64 architecture.
CMake, Ninja and GCC helpers/plugins resolve those runtimes inside the SDK,
without depending on an independently installed host C++ runtime. This concerns
the tools that perform compilation; application runtime linkage is described
below. After installing the same archive on each consumer distribution, CI
checks runtime resolution for CMake, Ninja, GCC's C++ frontend, Python and the
SDK's `patchelf` when present, with inherited loader search overrides removed.
It also runs the relocated Python and checks Rev's resource-builder imports.

This isolation also applies when running development binaries and tests before
packaging. SDK builds statically link OpenSSL, `libstdc++` and `libgcc`, and stage
their target shared-library dependencies in `sdk-runtime/` beside the build
executables. Runtime lookup is relative to those executables, without exposing
the SDK's libc directory through a build RPATH. Keep the adjacent runtime
directory when copying a development executable; the `package` command creates
the supported distributable with its notices and inventory.

The initial recipe omits target ASan/UBSan libraries. `./build.sh sanitize`
without `--sdk` remains the ordinary sanitizer path. An SDK sanitizer build
requires its own target sanitizer libraries in the sysroot and refuses to
substitute host runtimes when they are absent.

### Build and preserve the SDK from source

SDK construction is occasional maintenance work, separate from application
compilation. It requires a Linux x86_64 host with the standard
[Buildroot prerequisites](https://buildroot.org/downloads/manual/manual.html#requirement),
Python 3.9+ and ample disk space. The workflow's Debian bootstrap package list is
the maintained reference. The build uses several GB and can take substantial
time. The default parallelism is two jobs.

A local Debian 13 validation of recipe `b8685ab239d7ac8650e6` produced a 226 MiB
compiled SDK archive, a 430 MiB source archive and a 744 MiB installed SDK. Its
compiler/package intermediate tree occupied 8.9 GiB, in addition to downloads,
archives and installations. Sizes depend on the bootstrap environment and
filesystem; allow room for intermediates when building from source. This local
SDK's observed glibc requirements were 2.38 for host tools and 2.36 for target
libraries, so it is not a Bookworm-qualified SDK. The Bookworm CI bootstrap and
copied-artifact checks remain the release qualification gate.

```sh
# This is the explicit network-enabled preparation step.
python3 tools/build-sdk.py fetch --jobs 2

# No downloads are permitted by default; missing sources cause a failure.
python3 tools/build-sdk.py build --jobs 2
python3 tools/build-sdk.py sources --jobs 2
```

`build --download` explicitly permits fetching during construction.
`--cache-dir /persistent/source-sdk` selects a different persistent cache.
The default is `third_party/build-support/cache/source-sdk/`; downloaded
archives, extracted sources and build output stay there rather than `/tmp`.
The cache is ignored by Git. `python3 tools/build-sdk.py id` prints the recipe
identity used to distinguish SDK versions.

The `releases/` directory contains:

- `datapump-sdk-ID-linux-x86_64.tar.gz`: relocatable compiled SDK, including its
  manifest and collected license notices;
- `datapump-sdk-sources-ID.tar.gz`: preserved source downloads and the exact
  replay helper, recipe and patches needed to reconstruct the SDK offline;
- `SHA256SUMS`: checksums for the release archives.

The source archive includes only the recipe's resolved downloads and pinned
Buildroot archive. Older versions may remain cached for other recipes but do
not inflate new release archives.

Preserve the compiled SDK and source archive together in durable release
storage. Mirror these archives if long-term availability matters. Hashes verify
downloads but cannot recover disappeared upstream files. A complete source
archive removes that network dependency; the bootstrap compiler and ordinary
host utilities remain prerequisites. Application sources are supplied by the
matching repository release, not duplicated into the SDK source archive.

To reconstruct it, verify the source archive against the published checksums,
extract it, enter its `datapump-sdk-sources-ID` directory and run:

```sh
python3 tools/build-sdk.py build --jobs 2
```

The archive includes the helper and preserves its ordinary relative cache
layout, so this replay does not require an application checkout or network.

Target ABI and compiler-host ABI are separate. The private C++ runtime copies
do not lower the tools' glibc requirement. Building the SDK with a newer host
compiler can make its tools require that host's newer glibc even though the
target sysroot remains at 2.36. For a distributable Bookworm-compatible SDK,
the workflow builds the host tools in a Bookworm bootstrap environment and runs
`verify --max-host-glibc 2.36` after relocation. The newer CI runner merely hosts
that bootstrap environment. Developers using the resulting SDK do not need it.
A locally constructed SDK on a newer system must pass the same audit before
being advertised as Bookworm-compatible.

### CI, publication and upgrades

The SDK workflow runs for SDK/build infrastructure changes, vendored Rev or
resource-preparation changes, and manual dispatch.
It caches completed SDK and source archives by the recipe/helper hash, so
ordinary application edits do not rebuild the compiler or dependencies. The
existing native CI and compatibility gates remain active. Artifact retention
in CI is temporary; it is not the durable source archive. Binary SDK and source
archives are uploaded separately so application jobs download only the compiler
SDK. Each artifact has its own `SHA256SUMS`; the source artifact also retains
the combined `SDK-SHA256SUMS.txt` inventory published with both release archives.
The Bookworm application job also runs the unchanged wire/shared GUI contract
group with the SDK compiler; both consumer jobs run build and packaging
isolation tests without a separate host compiler.
They reuse the application's compiled libraries from the packaging build.
The Rev consumer then runs the native conformance group under a private Xvfb
display, separately from the other build/test steps.
The SDK producer discards only its own intermediate CI build tree after export
to leave space for relocation checks; local preparation retains its build cache.

To attach a qualified SDK, its sources and the default FLTK application bundle
to an existing GitHub release, dispatch the workflow using that release's tag
as the workflow ref and supply the same `release_tag` input. Publication checks
the exact source revision and waits for compatibility checks. It does not
create a release or overwrite existing assets. An empty input, ordinary push,
or pull request only produces CI artifacts. Rev remains a tested optional
backend; this workflow does not publish its bundle automatically.

For upgrades, change [the manifest](source-sdk/manifest.json),
[Buildroot configuration](source-sdk/configs/datapump_defconfig) and local
package overrides together. Review upstream release notes, security fixes,
patches and license changes. Resolve glibc updates from its recorded maintained
branch to an exact commit and independently verify source and license hashes.
Keep the glibc ABI baseline unless intentionally changing the support promise.
Use a fresh SDK cache when changing its toolchain; do not mix old build output
with a new recipe. Retain the previous release artifacts for reconstruction.

Run the build-tool tests, a complete source fetch/build/archive cycle, offline
replay from the source archive, SDK relocation and host/target ABI verification,
both GUI builds and copied-artifact compatibility checks. Review the collected
license inventory before publishing. Source retention makes reconstruction
possible; it is not a claim of bit-identical output or indefinite compatibility
with every future bootstrap toolchain.

## Native Debian 13 dependency supplement

For machines without permission to install development packages, the optional
`debian-13-amd64.json` manifest records 16 official Debian development/display
archives: package versions, original URLs and SHA-256 hashes. This is a native
Debian 13 amd64 fallback, **not** a portable binary distribution or a complete
cross-compilation sysroot. Other distributions should use their native packages
or supply their own existing prefix with `-DDATAPUMP_DEPENDENCY_PREFIX=/path/usr`.

### Prepare once, then build offline

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

### Provenance and upgrades

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

For a release, use a qualified source SDK above or the existing Ubuntu 22.04
builder and its own installed dependencies. Never combine this native Debian
supplement with an older release sysroot.
See [build profiles](../../docs/building.md) and
[portable installation](../../docs/offline-installation.md).

These preparation commands and source-file references assume a source checkout;
the installed application bundle includes the provenance notes, not build tools.
