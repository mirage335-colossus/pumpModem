# Build and maintenance guide

The [documentation topic index](README.md) also covers modem internals,
computational performance, weak-signal research, interfaces, security and evidence.

From a checkout with its native development dependencies installed:

```sh
./build.sh
./build/dev/datapump-gui
./build/dev/pump --version
./build.sh test contract
```

`build.sh` supports Dash and traditional SVR4 Bourne shell. See the
[shell script inventory](shell-scripts.md) for maintained/generated entry points,
interpreter requirements and compatibility checks. It selects an explicit CMake
preset, reuses its output directory and
prints the application paths. CMake 3.21+, a C++20 compiler and OpenSSL 3
development files are required. The normal FLTK GUI also needs X11/Xft and font
development packages on Linux. FLTK, XZ/liblzma, QR and LDPC sources are vendored.
Python is optional for CLI/build-tool tests, and required only by the opt-in Rev
resource builder or optional dependency preparation helper. Python is not part
of the installed application. The Robust Helper, DATA, receiver and session
static libraries link into the GUI binary; no receiver subprocess or Python
runtime is installed. The default receiver also has no Torch dependency; an
explicit optional native ATen build uses LibTorch C++ libraries. The browser
profile additionally uses Python to
assemble its self-contained static page; it is not a browser runtime dependency.

For Debian/Ubuntu, see the package list in the [README](../README.md#build-and-run).
On Fedora/RHEL-family systems, the corresponding development packages include
`gcc-c++`, `cmake`, `openssl-devel`, `libX11-devel`, `libXft-devel`,
`libXext-devel`, `libXrender-devel`, `libXcursor-devel`, `libXfixes-devel`,
`libXinerama-devel` and their font dependencies. Optional static OpenSSL package
availability varies by distribution; ordinary development uses shared OpenSSL,
while the release preset requests static OpenSSL. Supply an appropriate native
toolchain/prefix when building a portable release. There is no automatic package
installation. A missing dependency should be installed or explicitly supplied,
not recovered from an old application bundle.

For restricted Debian development hosts, the optional
[verified native dependency supplement](../third_party/build-support/README.md#native-debian-13-dependency-supplement) prepares
headers and linker aliases in a persistent ignored cache. The helper retains
archive hashes and checks installed runtime versions. The default prepared cache
is detected automatically; no build relies on `/tmp` symlinks. An explicit
`DATAPUMP_DEPENDENCY_PREFIX` overrides it.

For an isolated Bookworm-compatible target toolchain, use the separate
[source SDK](../third_party/build-support/README.md#source-sdk-for-the-bookworm-abi-baseline).
It builds pinned upstream sources rather than extracting distribution packages.
The Git repository retains the recipe; release storage holds a compiled SDK and
its complete source archive. Every new binary release retains its required
reusable dependency archives, sources and per-recipe checksums in its own
checksummed inventory. A surviving release can therefore bootstrap
those dependencies after an older release is deleted; see
[recovery from a binary release](releases.md#bootstrap-from-a-surviving-binary-release).
Download or build the SDK once, explicitly install it
with `tools/build-sdk.py install`, then pass `--sdk /absolute/path` to the same
build wrapper. Using a prepared SDK requires no container, chroot or root access.
It includes private C++ runtime libraries for CMake, Ninja and GCC helpers,
supplied by the SDK's own compiler build. Those host tools still require their
documented glibc baseline; release SDKs are built in Bookworm and audited after
relocation. The default native build remains available.

Manual releases with version/date tags, an experiment checkbox and six portable
application downloads are documented in [manual portable releases](releases.md).
Windows release and full CI jobs reuse a checksummed dependency bundle from the
durable `base` release: static OpenSSL, GLEW, FreeType and SDL2. The terminal host
uses the Windows console API and needs no curses library.
Schema-6 certification uses the target binary release's retained copy, verified
against its pinned inventory; older schemas keep the base lookup. The runner
supplies MSVC and the Windows SDK separately.
The [toolchain selector](../tools/select-windows-toolchain.ps1) prefers an
installed Visual Studio 2022, or uses Visual Studio 2026 with its installed
v143 tools. The current larger Windows images have VS2026 and v143 14.44;
the default `windows-2022` image retains VS2022. The selector sets the matching
CMake generator and pins v143 instead of adopting VS2026's default toolset.
VS2026 requires CMake 4.2 or newer. This host selection does not change the
dependency recipe or rebuild the existing base.
An absent matching recipe requires explicit
[Windows base maintenance](releases.md#windows-dependency-base); routine jobs
never start a cold dependency build. Reuse includes relocation and a check
that the consuming linker is at least as new as the recorded builder. Keep
the compiler at least as new too, within v143; the bundle contains no LTO
objects. This dependency handoff uses neither Actions cache nor artifacts.

The independent `--tui` and `--fb` options add `datapump-tui` (ncurses on POSIX,
native console on Windows) and
`datapump-fb` (SDL2 software framebuffer). Combine with `--cli` for no native GUI.
`./build.sh test frontends --cli --tui --fb` runs their focused integration group;
normal full regression requirements still apply. The TUI and framebuffer have
separate presentation/interaction implementations behind the common application.
See [frontend interfaces](frontend-interfaces.md) for dependencies and the small
single-framebuffer engine API.

Native application builds also place `datapump-audio-entropy` beside their
executables. On supported platforms, keyfile generation uses this private helper
when it needs to open a fresh audio input, so a blocked device driver can be
stopped. Where isolated capture is unsupported, it can still borrow a suitable
active capture or proceed with the warned operating-system-only fallback. Native
portable packages include and verify the helper. An application embedding the framebuffer or
terminal library must place the helper beside its own executable; if it is
unavailable, keyfile generation can continue with operating-system randomness
and displays a warning. The browser worker and WebAssembly compositions use
their host audio provider and do not launch this native helper.

Portable native Linux producers explicitly run `python3 tools/prepare-native-sdl.py
/absolute/new/preparation-directory --jobs N`, then configure with
`-DDATAPUMP_NATIVE_SDL_ROOT=/absolute/new/preparation-directory/install` (or that
environment variable). This verifies and builds the pinned SDL source with only
the CPU presentation dependencies, retaining its source, license and build
provenance in the package. Ordinary development may use system SDL; Linux SDK
and Windows builds continue to use their prepared bases. The SDL2 software host
uses X11, including XWayland on Wayland desktops; native Wayland software surfaces
require a future host adapter.
New release producers enable both frontends in Linux SDK and Windows archives,
which also supplies them to the Debian, Arch and Gentoo package pipelines.
Their `frontends` regression group runs in separate CI jobs parallel to existing
regressions. New dependency recipe IDs require explicit base maintenance; routine
release jobs never rebuild missing dependencies implicitly.


## Browser application and pipe worker

The optional `datapump-worker` and `datapump-wasm` compositions reuse the common
C++ application with browser audio. The worker has no HTTP server or socket
transport. An existing embedding application supplies actual inherited anonymous
pipes and owns all network connections. The worker closes unrelated inherited
descriptors and installs a no-socket boundary before starting the application.
Unsupported enforcement platforms fail closed. The initial worker implementation
is Linux-specific; Windows/macOS worker qualification is not implied by a
successful build of their existing native frontends.

```sh
./build.sh --cli --web-worker
./build.sh test web --cli
./build/dev-cli-web/datapump-worker --self-check
# Prepared Linux source SDK remains a separate choice for native binaries:
./build.sh package --cli --web-worker --sdk /absolute/prepared/linux-sdk
```

`--web-worker` is independent of `--backend`; it adds an executable named
`datapump-worker` and hosted browser assets under `share/datapump/web/hosted`.
Existing coinstallable native package ownership is retained: Debian commands are
`datapump-worker` for the FLTK-owned package and `datapump-worker-rev` for the
Rev-owned package, each pointing into its own `/opt/datapump/<backend>/bin`.
The worker itself links neither toolkit nor a native audio provider. There is no
service installer, listener or CGI socket helper. Direct interactive terminal
input is not a worker transport; use the embedding application's pipe launcher.
Host file selection is a separate explicit inherited-pipe capability.

For repository experiments, [COMPILE-web](../COMPILE-web) is the short build,
test and run reference. After building, `python3 tools/preview.py worker` opens
a temporary loopback host for the native worker; `python3 tools/preview.py wasm`
serves the built static page. Open the printed URL and stop with Ctrl-C. Python's
standard library is sufficient; neither mode installs a service or builds for you.
Use `--build-dir PATH` for custom builds and `--port N` for a fixed port.

Worker mode optionally accepts `--simulation` and `--files DIR`. File choices
are relative to that explicitly permitted directory, individual files only,
at most 256 MiB; symlinks and overwrites are rejected. One tab owns one worker;
closing it or losing its heartbeat stops the child. The preview's authenticated
HTTP relay belongs to this development tool, outside the no-socket worker and
isolated renderer. It is loopback-only and is not included in application packages.

The browser-only product requires an independently prepared
[Wasm SDK recipe](../third_party/build-support/wasm-sdk/README.md). Preparation
pins official Emscripten inputs and cross-builds static OpenSSL with socket,
dynamic loading and thread support disabled. It preserves inputs and checksums;
ordinary application builds run offline with a frozen compiler cache.
The SDK entropy adapter uses browser WebCrypto through Emscripten `getentropy`,
including subsequent OpenSSL reseeds. Preparation tests real random generation,
repeated reseeding, and failure when secure browser entropy is unavailable.
SDKs prepared before this adapter must be rebuilt into a new directory from
the retained inputs; use a fresh application build tree with the new SDK.

```sh
python3 tools/build-wasm-sdk.py fetch --download --sources /absolute/wasm-inputs
python3 tools/build-wasm-sdk.py prepare --sources /absolute/wasm-inputs \
  --destination /absolute/new/wasm-sdk --jobs 4
./build.sh --wasm-sdk /absolute/new/wasm-sdk
./build.sh test web --wasm-sdk /absolute/new/wasm-sdk
./build.sh package --wasm-sdk /absolute/new/wasm-sdk
```

The Wasm profile creates `build/wasm/web/datapump-wasm.html`, an ordinary static
page with preloaded compiled C++, browser adapters and AudioWorklet bytes. Its
adjacent `web-manifest.json` records the exact input hashes and Wasm imports;
`manifest.sha256` covers those generated page artifacts. Static packages install
these files under `share/datapump/web/wasm`, with no native executable or desktop
launcher. Keep Wasm and native output trees separate. Compiler-host support is
Linux x86_64; the generated browser page is portable to Windows and Linux.
The reusable [Wasm SDK](../third_party/build-support/wasm-sdk/README.md) has its
own immutable `base` recipe, source archive and checksums. Ordinary CI and release
jobs fetch and relocate that prepared SDK; they never prepare missing dependencies.
Use explicit `sdk-base.yml` maintenance with `platform=wasm` (or `all`) when a
new Wasm recipe needs publishing.

Native CI runs focused core, modem, frontend, native, package and web scopes.
Wasm package and regression jobs run independently as well.
The `web` label is excluded from `ci-core` in both its build prerequisites and
test selection. SDK and release certification use the same separation; native
and Wasm live tests therefore never compete on one runner. Automatic push/PR
checks remain lightweight and include changes under `web/`.

Release producers build native archives and the browser archive pair in parallel.
The finalizer checks the browser source commit, prepared SDK recipe, notices and
file hashes, then adds the identical browser payload to all six native bundles.
Linux bundles also include the pipe worker and hosted adapters. The Windows
bundle includes the standalone page; it does not advertise an unsupported native
pipe worker. Debian/APT, pacman/Arch and Gentoo packages retain those same files.
Their `datapump-html` and `datapump-html-rev` commands open the coinstalled page
with the user's browser; the desktop menu also provides an HTML entry.

The page uses a single Worker and cooperative execution, without pthreads,
SharedArrayBuffer or cross-origin isolation headers. Its CSP forbids connections;
module and worklet bytes are supplied locally after page loading. HTTPS static
hosting, including GitHub Pages, can provide the browser secure context needed
for audio. A user action and the browser's ordinary microphone permission remain
required. Frame embedding also depends on the host's permission policy. Browser
files use explicit import and download; host file access belongs to the pipe
worker case. See [web frontend contracts](web-frontends.md) for the integration
boundary and remaining browser/device qualification.

The native `web` CTest group covers execution, host audio, the C++ bridge, protocol worker,
renderer, preview lifecycle/file transfers and SDK/package tools. With
`--wasm-sdk`, the same group builds the actual
module and exercises its application frames through an isolated Node VM using
preloaded assets and denied network globals. It also runs the cooperative fiber
lifecycle/stack-alignment and secure-entropy tests compiled for Wasm. Both web
profiles also drive production browser audio and actual C++ Fast live RX/TX/RX
at 44.1 and 48 kHz with a simulated physical device. The native fixture caps its
anonymous-pipe relay at 10 Mbit/s to exercise backpressure and concurrent edits.
These wall-clock tests run serially; do not overlap native and Wasm live suites
on the same host. They check every emitted sample and physical-drain acknowledgment. They supplement the normal contract/general
regressions. Successful C++ compilation, Node tests and static import checks do
not certify microphone, playback timing or file-download behavior in a real
browser; those require the actual browser/device matrix.

## Commands and profiles

| Command | Output/configuration | Work performed |
| --- | --- | --- |
| `./build.sh` | `build/dev`, FLTK Release, system runtime linkage | Application targets only; test cases remain available |
| `./build.sh --cli` | `build/dev-cli` | CLI without the native GUI toolkit |
| `./build.sh --cli --web-worker` | `build/dev-cli-web` | Native pipe worker plus CLI, without a GUI toolkit |
| `./build.sh --wasm-sdk PATH` | `build/wasm` | Compiled C++ and self-contained static browser page using a prepared Wasm SDK |
| `./build.sh test GROUP` | `build/dev` normally | Builds the group's prerequisites, then runs CTest |
| `./build.sh sanitize GROUP` | `build/sanitize`, Debug + ASan/UBSan, headless | Instrumented tests; default group is `contract` |
| `./build.sh package` | `build/release`, portable Release, tests disabled | Creates TGZ/ZIP and verifies both archives and relocation |
| `./build.sh --sdk PATH` | `build/dev-sdk` | Uses the prepared SDK's compiler, build tools and isolated target dependencies |
| `./build.sh package --sdk PATH` | `build/release-sdk` | Collects SDK runtime libraries/notices and enforces its glibc ceiling |
| `./build.sh test packaging` | `build/package-tests`, portable Release | Packaging fixtures and application relocation |
| `CC=clang-19 CXX=clang++-19 ./build.sh --backend rev` | `build/rev` | Optional C++23/Rev build with its extra dependencies |

Native sanitizer/CLI/Rev combinations use distinct directories. By default,
compilation uses **one fewer than the available logical CPUs**, at least one job,
subject to a RAM budget. Tests still default to two jobs. Explicit `--jobs N`,
`DATAPUMP_JOBS`, or `CMAKE_BUILD_PARALLEL_LEVEL` keep their existing meaning: they
set both build and test concurrency. `--build-jobs N` overrides compilation only,
regardless of option order. Explicit limits take precedence over automatic detection.

```sh
./build.sh test gui
./build.sh test gui --build-jobs 8 --jobs 2
```

The optional [capacity selector](../tools/build_capacity.py) uses only Python's
standard library. It accounts for Linux CPU affinity and visible cgroup quotas,
and uses available physical memory (including remaining cgroup headroom), not
total installed RAM. It budgets 768 MiB per simultaneous job after reserving the
larger of 256 MiB or 10% of available memory. Thus 2 GiB available permits up to
two jobs; a two-CPU machine uses one. This is a conservative scheduling estimate,
not an enforced memory ceiling: large compiler/linker tasks or concurrent builds
may need an explicit lower limit. No compiler flags or optimization levels change.

Detection is best-effort: no downloads, third-party Python packages, or new
required system utilities. Unknown memory caps the automatic count at two;
unsupported or failed probes fall back conservatively, and the wrapper uses one
job if Python is unavailable. macOS memory availability is an estimate from
`vm_stat`; Windows uses the built-in system memory API. No oversubscription is
assumed from idle time or compiler-cache hits.

For direct CMake or maintenance recipe invocations, pass the selector explicitly:

```sh
cmake --build build/dev --parallel "$(python3 -B tools/build_capacity.py)"
python3 tools/build-sdk.py build --jobs "$(python3 -B tools/build_capacity.py)"
```

The immutable Linux/Wasm SDK recipe builders retain their historical standalone
defaults to preserve existing recipe identities; base-maintenance workflows pass
the automatic count explicitly. Normal application builds continue reusing the
prepared SDK. No base rebuild is required for this scheduling change.

Use `--stop-on-failure` with `test` or `sanitize` for early CI feedback after a
failure; successful runs still execute the entire selected group.
Native GUI tests must have a display, preferably an
isolated Xvfb session; `./build.sh test native` builds and runs them explicitly.
With Mesa software rendering, set `LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2`
for H-sized hosts, or `LP_NUM_THREADS=1` as used on standard CI runners, so
drawing leaves CPU time for sampled processing and GUI polling.
Run native workflow checks separately from other heavy test/build processes:
their measured replay cadence is sensitive to CPU contention. Rev cadence
misses are advisory warnings; correctness checks still fail normally.
The wrapper does not open or control a user's desktop.

Use `--build-dir PATH` for a different compiler/toolchain. Never switch compilers
inside one configured tree. For new trees Ninja is preferred when available;
Unix Makefiles is the fallback. Existing generators are retained. Paths with
spaces and additional CMake arguments are supported:

```sh
./build.sh --build-dir 'build/custom compiler' --jobs 3 -- \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
./build.sh -- -DDATAPUMP_DEPENDENCY_PREFIX=/persistent/native-sdk/usr
```

The SDK itself must be installed at a path without spaces because its upstream
relocation machinery has that restriction. `--sdk` owns compiler and target
dependency selection: unset `CC`, `CXX` and compiler include/library environment
overrides instead of combining them with it. SDK changes or relocation require a
fresh application build tree. For the optional Rev frontend, the source SDK
supplies its newer GCC/CMake requirements:

```sh
./build.sh --backend rev --sdk /persistent/datapump-sdk
./build.sh test build --sdk /persistent/datapump-sdk
```

SDK development and test executables also use the SDK's static OpenSSL,
`libstdc++` and `libgcc`, so they do not require the host's newer C++ or OpenSSL
runtime. Needed target shared libraries are copied into an adjacent
`sdk-runtime/` directory and found relative to the executable. Keep that
directory with development executables; use `package` to distribute a complete
bundle with notices and manifests. The destination still supplies glibc and
graphics drivers.

The initial source SDK does not include ASan/UBSan runtimes. Running `sanitize`
with that SDK fails with an explanation rather than taking those libraries from
the host. Use `./build.sh sanitize GROUP` with the native development toolchain,
or prepare an SDK that supplies the target sanitizer runtimes explicitly.

### Local output retention

Do not run simultaneous configure/build operations against the same directory.
Concurrent contributors should use separate output directories under `build/`,
keep source inputs stable during checks, and coordinate access to timing-sensitive
tests and devices. Serialize shared Git mutations and final integration. Old
top-level `build-native`, `build-agent-*` and similar directories are historical,
not current presets. For sequential work, use the standard `build/` profile and
let it rebuild incrementally across source revisions while the compiler,
generator, configuration and SDK stay compatible. Avoid date-, revision- or
session-named build copies. Use separate output under `build/` for a concurrent
comparison; after its process exits, save only useful source, input,
configuration and compact result evidence, then remove its compiled outputs. A
failed run alone is not a reason to retain its whole build tree.
Retire abandoned profiles, stale target trees and experiment-only outputs once
their active comparison or diagnosis is complete; reuse compatible incremental
outputs and caches that current work still uses. Do not purge caches wholesale
or keep dated build copies as an informal archive.

Keep only compatible local SDK/dependency caches that current work uses, plus
downloaded inputs needed for offline rebuilding. Native and Wasm SDK caches are
separate families. Before removing a superseded local SDK installation, build
work tree or archive, close processes using it and migrate or reconfigure its
dependents. Use explicit bounded paths; do not use broad cleanup globs or
automatic age-based deletion. Preserve immutable published assets under the
release contract. Keep original failure reports while required for certification
or an unresolved diagnosis; record the reason and retirement condition. Otherwise
retain the concise conclusion and provenance, then remove redundant raw outputs
when the work closes.

Workspace contents have separate purposes:

- `build/`: reusable configure, compile and package outputs for compatible
  profiles. Its `build-info.txt` records the configured source and toolchain
  context; reconfigure through `build.sh` when building newer source. Keep
  historical binary output only while a concrete comparison or diagnosis needs
  it. Prepared SDK/dependency caches remain useful only while compatible with
  current work or needed for offline rebuilding; follow their recipe, checksum
  and provenance requirements.
- `local-evidence/` (ignored): temporary, compact source-bound results,
  essential reproduction inputs and patches for active unfinished work. Each
  item needs a stated current purpose and cleanup trigger. It is not a
  general evidence or binary archive, or a session, ownership or task registry,
  and is not routine startup reading. Promote concise useful conclusions,
  including negative findings and assumptions, to maintained docs; remove raw
  captures and superseded reports when their purpose ends.
- `docs/`: current contracts, concise validation status and unfinished plans
  with a concrete next action and review/retirement condition. Git history
  retains superseded tracked narratives; do not append session diaries or keep
  obsolete implementation guides as historical documentation.

Every configuration writes `build-info.txt` with source revision, compiler,
backend and build flags; packaging includes it. This is configure-time
provenance, not a claim of byte-for-byte reproducibility or an uncommitted diff
archive. `./build.sh` reconfigures before building to refresh it. The application
display version is `001_00`, reported by the CLI and GUI `--version` commands.
CMake and distribution packaging use the numeric equivalent `1.0.0`; build
information records both identities. See [release versioning](releases.md).
Configuration also compiles the receive-hardening header's capability probes and
records index/barrier support, or an explicit unsupported status, in the build
information. These compile-only probes support cross-compilation; they do not
execute on or assess the target CPU. The status covers selected receive accesses
and validation boundaries, not whole-program, firmware or OS protection. See
[receive processing hardening](receive-processing-hardening.md) for architecture
coverage and the `speculation` / `build_speculation_codegen` checks.
Ninja/Make profiles also export `compile_commands.json` for editors and code
analysis, avoiding guesswork about include paths and compiler options.

## Test selection without stale executables

Tests are excluded from the default application build. Build a group's
prerequisites before invoking CTest; CTest by itself does not rebuild. Use `./build.sh --help` for the current group list and group contracts maintained
by the build. Shared GUI changes require affected frontend checks.
Python-dependent tests are registered only when Python is available.

`contract`, `regular`, `fast`, `legacy`, `gui`, `frontends`, `web`, `native`,
`packaging`, `build`, `triad`, and `all` currently select their named build/test
scopes. Group membership changes with maintained implementation. Old
single-carrier Robust calibration suites are not current tests; standalone triad
performance campaigns are replaced by the bounded method in
[triad-performance.md](triad-performance.md).

Direct CMake remains supported, including on Windows:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 2
cmake --build build/dev --target datapump-tests-contract --parallel 2
ctest --test-dir build/dev --output-on-failure -L '^contract$' -j 2
```

Other aggregate targets are `datapump-apps`, `datapump-tests`, and
`datapump-tests-GROUP`. For a Visual Studio/multi-configuration generator, add
`--config Release` to builds and `-C Release` to CTest. CMake test presets select
tests but do not compile them. For individual work, build the corresponding
`test_NAME` target first.

## Testing stages

### Robust modem

The [current Robust architecture](robust-current.md) is one compiled C++
waveform/Helper/receiver stack linked into the GUI session. Run affected cheap
deterministic native checks first, then the shared GUI/audio groups. Python
tools orchestrate native probes and statistics; receiver DSP and persistent
state remain C++. Old Python/process-runtime costs are inadmissible as native
calibration. See the
[native integration results](validation.md#native-codec-and-measurement-migration).

```sh
./build.sh test triad
./build.sh test gui
```

`tools/triad_manual_cases.py --probe build/dev/triad_receiver_probe` and its
`triad_abbreviated.py --cases` forwarder measure the compiled receiver. Their
native report schemas require current source, binary, configuration and work
receipts; old Python calibration is rejected.
Ordinary `triad_abbreviated.py` runs measure relative computational regression
against a matched baseline; they are not absolute capacity results. See the
[manual capacity contract](robust-manual-capacity.md) for complete work,
statistical gates and limits, and the [regression guide](triad-performance.md)
for relative timing. Do not replace the bounded method with full, sustained,
multi-day, exhaustive, or heavy-calibration campaigns. RunPod may accelerate
the same method but does not authorize larger experiments.

A shared security, codec, audio, or GUI change still needs its applicable
component checks. Keep their contracts distinct from Robust performance
estimation. Documentation edits need proportionate link and consistency checks,
not modem benchmarks.

### CI package downloads

Container jobs use [`tools/ci-apt.sh`](../tools/ci-apt.sh) for their Debian/Ubuntu
prerequisites. Containers have their own APT sources; the hosted runner's mirror
configuration does not carry into them. The helper prefers Azure for recognized
Ubuntu archive/security sources on supported architectures and retains alternate
official mirrors. Debian uses its own archive and security mirrors. Ubuntu ports
sources remain separate, including containers that still use ports for newer ARM
releases; those sources receive the bounded acquisition and index-refresh retries.
The helper never substitutes a different distribution, suite or architecture.
Source signing keys, components, requested versions and package verification
remain unchanged.

APT file downloads have explicit retry and timeout limits. A failed fetch can
also trigger a bounded fresh index update and installation attempt, so a package
404 does not keep requesting an obsolete version indefinitely. Index updates
fail on partial errors; authentication, dependency and package-configuration
errors are not converted into successful or skipped installations. Exhausted
retries fail the job and retain the diagnostic output.

For containers without Git, a separate checkout first obtains the current
workflow's helper using the checkout action's archive fallback. The helper is
copied to the runner's temporary directory before installing prerequisites.
The ordinary Git checkout then runs, preserving source revision provenance.
Certification still checks out the published application's exact source and
tests its original binary hashes. Preparing host tools does not certify or
repair an older published binary.

The offline regression `python3 tests/test_ci_apt.py` exercises mirror selection,
fetch recovery and fatal errors without changing the machine's package sources.
It is also included in `./build.sh test build` and automatic helper checks.

## Test selection and candidate validation

Tests are excluded from the default application build. Build a group's
prerequisites before invoking CTest; CTest by itself does not rebuild. Use
`./build.sh --help` for the current group list and `./build.sh test GROUP` to
build and run the selected scope. For individual work, build the matching test
target first. Shared GUI changes need affected frontend checks; Python-dependent
tests are registered only when Python is available.

Start with the smallest deterministic check that covers the changed behavior.
After the change is complete, run applicable groups for shared codecs, security,
audio, GUI, Fast, Legacy, packaging, and frontends. Reuse results only when
source, configuration, and relevant inputs match. Keep failed, skipped, and
incomplete coverage visible; a focused pass does not qualify unrelated behavior
or a release.

### Robust computational checks

The `triad` group covers bounded native source, Helper, complete receiver,
short/long message/FEC, stream and audio checks, plus Python orchestration and
report-validation tests when Python is available. Run it before refreshing local
CPU calibration. The measurement driver uses Python's standard library; all
receiver DSP, source generation and persistent measurement state are compiled.

```sh
./build.sh test triad
python3 tools/triad_manual_cases.py \
  --probe build/dev/triad_receiver_probe \
  --output local-evidence/robust-cases.json
```

The manual suite validates the whole current `_tests-RobustModem.md`, including
all sixteen required fields and ordered duplicates. Repeatable `--case-index N`
selects explicitly partial coverage. Default inference uses `2*H` workers after
a serial/physical/logical allocation pilot; explicit limits remain exact. Use
`--deadline-ms`, `--row-deadline-ms`, `--max-samples` and `--coarse-tiles` to declare
bounded work. Hitting a bound leaves coverage unresolved. The
[manual capacity contract](robust-manual-capacity.md) defines full work, memory,
uncertainty and margin gates. `--skip-message-gate` is diagnostic and prevents
an integrated pass. Separate `--sensitivity` trials measure native message
reception and cannot supply CPU capacity.

The ordinary [`triad_abbreviated.py`](../tools/triad_abbreviated.py) harness uses
native slot/coarse/fine/audio receipts plus a complete five-slot mixed stream
for relative regression, with matched native baselines and 1/2/4-unit,
3/5/9-look limits. Its `native-regression.v2` schema rejects saved v1 reports;
the current driver measures both source binaries. Every slot and mixed-stream
unit uses a fresh native call; active/noise timing describes the first scored
slot in a fresh process, not a fully cold GPU or steady state. Earlier v2
same-process policy reports are also incompatible. The overall
`--benchmark-deadline-ms` defaults to five minutes, including subprocess,
generation and control work. Baseline establishment is not a relative-regression
pass. See [computational checks](triad-performance.md) for
commands, optional compiled ATen/CUDA builds and GPU-assisted phase forecasts.
Old Python reports are inadmissible as native timing calibration. A forecast
pass is separate from complete real-time capacity or communication sensitivity.
No full, sustained, multi-day, exhaustive or heavy-calibration campaign is a
routine fallback for unresolved abbreviated evidence.

### CI, sanitizers, and release validation

Automatic CI is fast feedback for its selected source scope and does not by
itself certify a release. The native Linux and Windows manual matrices
include the `triad` group and its `datapump-tests-triad` build target. The CPU
`triad-abbreviated` workflow builds both candidate and same-run native reference
probes before comparison; it does not install numerical Python packages. The
v2 driver uses the existing native `message` command on both binaries, so its
mixed-stream addition needs no baseline-transition exception. Missing or broken
references fail; explicit manual diagnostic establishment remains separate and
does not report a relative-regression pass.

The opt-in `triad-gpu` workflow requires a preprovisioned self-hosted
`linux/x64/datapump-cuda` runner and CUDA-enabled C++11-ABI LibTorch. It builds the
same native stack with `DATAPUMP_TRIAD_TENSOR=ON` and
`DATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON`. The CUDA implementation requires NVCC
and the CUDA SDK. The workflow's `cuda_architectures` input applies to both
candidate and reference: `native` (default, requires CMake 3.24 or newer),
`89` for the representative Ada targets, or `120` for Blackwell. Explicit
choices set `TORCH_CUDA_ARCH_LIST` to `8.9` or `12.0`; `native` clears an inherited
value so stale environment selection cannot override the actual runner. This
selection is compilation coverage, not a measurement on the named GPU.

For a local Ada build with preinstalled CUDA-enabled C++11-ABI LibTorch,
set `LIBTORCH_PREFIX` to its absolute CMake prefix:

```sh
TORCH_CUDA_ARCH_LIST=8.9 ./build.sh configure --cli --build-dir build/triad-cuda --jobs 2 -- \
  -DCMAKE_PREFIX_PATH="$LIBTORCH_PREFIX" -DCMAKE_CUDA_ARCHITECTURES=89 \
  -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON
cmake --build build/triad-cuda --target triad_receiver_probe test_triad_tensor_cuda --parallel 2
ctest --test-dir build/triad-cuda --output-on-failure -R '^triad_tensor_cuda$' --no-tests=error
```

Use the actual device's architecture for other GPUs. The workflow executes
`triad_tensor_cuda` and a probe
that requires actual accelerator execution, then performs a bounded matched
CUDA regression. Optional `components=fp16|int8|peaks|all` (default `off`) also
runs the [bounded native component mode](triad-performance.md#bounded-native-component-comparisons)
with one selected `matrix_case=0|1`, exact `device_workspace_mib` (default 2048)
and a 300-second aggregate deadline. `peaks`/`all` require exactly 2048 MiB.
Its separate receipt and CI summary cannot supply a relative-commit regression
or capacity pass. CUDA device zero is the visible device; configure
`CUDA_VISIBLE_DEVICES` on the runner when selection is necessary. This workflow
does not provision a runner, launch or purchase cloud capacity, or implement a
Vulkan backend. Use the separate Vulkan recipe below for the local Radeon;
retain CPU checks separately on the representative Ryzen systems.
Configured workflow coverage is not evidence of an actual hosted/platform run;
see [recorded outcomes](validation.md#native-codec-and-measurement-migration).

Use the current workflows in `.github/workflows/` for
platform, SDK, package, and published-archive checks. Use
`./build.sh sanitize GROUP` for the selected instrumented tests. Report the exact
scope and any declared exclusions; sanitizers cover only executed instrumented
paths. Follow [release validation](releases.md#diagnose-a-branch-before-full-validation)
when release assurance is in scope. Publication, certification, and promotion
remain separate operations.

Documentation-only edits need proportionate link and consistency checks, not
modem benchmarks. Keep maintained outcomes concise in
[validation.md](validation.md). Keep research only when a concise finding informs
current work or a specific raw record is required to interpret it; record its
provenance, limitations and cleanup reason. Do not accumulate historical runs.

### Vulkan compute on integrated Radeon

The optional `DATAPUMP_TRIAD_VULKAN` build links the native Vulkan/VkFFT coarse
backend without Torch or a Python runtime. VkFFT 1.3.4 is pinned in
[`third_party/vkfft`](../third_party/vkfft/README.datapump.md); Vulkan and glslang
are native development dependencies. On Debian, the relevant packages are
`libvulkan-dev`, `glslang-dev`, `glslang-tools` and `spirv-tools`. Normal configure
and build commands never install packages, replace drivers or download sources.

With those dependencies already available:

```sh
./build.sh configure --cli --build-dir build/dev-vulkan --jobs 2 -- \
  -DDATAPUMP_TRIAD_VULKAN=ON -DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON
cmake --build build/dev-vulkan --target triad_receiver_probe test_triad_vulkan --parallel 2
ctest --test-dir build/dev-vulkan --output-on-failure \
  -R '^triad_vulkan(_plan)?$' --no-tests=error
./build/dev-vulkan/triad_receiver_probe describe --backend vulkan
```

Both options default to `OFF`. `test_triad_vulkan --plan` is a CPU-only geometry
preflight registered as `triad_vulkan_plan` when the backend is built. Setting
`DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON` also registers `triad_vulkan`, which
requires an actual GPU, runs serially and has a 120-second timeout. A software
Vulkan device does not satisfy it; missing device access is not a skipped pass.
Only request that option on a runner with suitable render-device access.
`--device-index` selects the enumerated physical device; probe receipts identify
its actual vendor/device, driver and executed work.

For the desktop application, omit `--cli` and build the GUI in a compatible
development profile, with the same dependency configuration:

```sh
./build.sh configure --build-dir build/dev --jobs 2 -- \
  -DDATAPUMP_TRIAD_VULKAN=ON -DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=OFF
cmake --build build/dev --target datapump-gui --parallel 2
./build/dev/datapump-gui
```

Select Robust Modem and use the **GPU** checkbox immediately left of Developer.
It defaults off and persists as `--gpu yes|no`; CPU-only builds keep the control
disabled with an explanation. Requested GPU initialization failures are reported
without silently starting a CPU session. See the [shared GUI contract](gui-contract.md).

Vulkan builds also register `gui_robust_gpu_unavailable`, which uses an invalid
ICD path to check the missing-device failure and return to CPU. With
`DATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON`, `gui_robust_gpu` invokes
`test_gui_robust_live --gpu`. Its one declared simulation covers five public
windows, at most 600,000 samples and a 90-second receive deadline, checks decoded
`e`/`001` plus actual GPU work, and switches GPU off/on. The strict device check
runs serially under a 120-second CTest timeout. These registered checks are not
evidence of a passed run or sustained receiver capacity.

The local dependency preparation uses verified Debian archives extracted
without system installation into
`third_party/build-support/cache/vulkan-native/usr-prefix/usr`. When reusing that
current cache, pass its absolute path as `CMAKE_PREFIX_PATH`; its linker setup
also uses `-DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1` for the
already-installed host loader. This is a local development recipe, not a portable
binary or driver bundle. Keep the cache while the Vulkan build uses it.

Run numerical/device checks before the
[bounded CPU/Vulkan comparison](triad-performance.md#local-radeon-vulkan-processing).
Preserve the default native worker allocation and matched public search. Actual
GPU access on the development host requires execution outside the restricted
sandbox that hides `/dev/dri`; no driver or device-permission change was made.
An unavailable access route must remain an unavailable result. This build does
not by itself qualify a release or all physical audio configurations.

## Compile-time policy

GNU/Clang builds disable implicit fused multiply/add contraction in the modem
library and its consumers. This preserves exact scalar, cached and batched
receiver equivalence on ARM64 as well as generic x86-64; the independent
reference assertions remain unchanged.

When available, ccache is enabled through CMake's compiler launchers. Existing
explicit launchers take precedence. Disable auto-detection with
`-DDATAPUMP_COMPILER_CACHE=OFF`. Its persistent cache is managed by ccache; this
build does not redirect it into `/tmp` or check it into the repository. There is
no required cache package and no correctness-relaxing ccache configuration.

Both frontends reuse compiled libraries; shared GUI model/render sources are
also reused by their tests. Native adapter probes are compiled only when
requested. Vendored toolkit examples, tools and unused codec libraries stay out
of the application build. Separate modem engines, platform-stub compilations
and the two GUI boundary canaries remain separate because their behavior and
test purposes differ.

Measure cold/warm application builds, test compilation, and a representative
source/header edit separately before changing optimization or adding precompiled
headers/unity builds. Retain compiler identity, job count, memory conditions and
cache statistics. Build artifacts from different compilers or sanitizer modes
are not interchangeable. No machine-specific CPU flags are added to releases.

## Portable releases

The portable product is one relocatable directory containing CLI and GUI
executables, required application libraries and notices. It is not a single ELF
for every UNIX, CPU architecture or libc. Native Linux packages retain the
glibc/loader requirement of their build environment. SDK packages use their
selected target ABI; building on a newer host does not raise it. The initial
source SDK targets x86_64 glibc 2.36, suitable for Debian Bookworm and compatible
newer glibc-based distributions. Packaging refuses to collect target libraries
from outside that sysroot and obtains notices from the SDK's own license
inventory. It does not copy the host's glibc or graphics drivers.

Use `./build.sh package --sdk /path/to/installed-sdk` for this path. The separate
[SDK workflow](../.github/workflows/sdk.yml) qualifies the SDK's host tools and
copied FLTK/Rev bundles on Bookworm and Ubuntu 24.04. It downloads the exact
recipe from the durable `base` release without rebuilding the toolchain.
[Base maintenance](../.github/workflows/sdk-base.yml) explicitly builds/reuses
and preserves SDKs and complete source archives; see
[SDK maintenance](../third_party/build-support/README.md#ci-publication-and-upgrades).
New schema-6 binary releases also retain the exact Windows dependency binary,
complete source archive and recipe checksum file; `bookworm-sdk` releases retain
the Linux SDK triplet as well. Their global `SHA256SUMS.txt` covers all these
copies. Repackaging preserves the dependencies used by the original binaries,
even when the packaging checkout uses a newer recipe. Missing assets require
explicit maintenance. The runner's MSVC and Windows SDK remain separate and are
not redistributed. This policy does not backfill older releases.

Application publication and extensive qualification are separate:
[manual portable releases](releases.md) publishes after packaging checks, then
[certification](../.github/workflows/certify.yml) attaches source/test outcomes
and the exact published binary hashes to that release. For schema 6, source
certification restores its Linux SDK and Windows dependencies from that release's
own assets, so deleting `base` does not block it. Older schemas retain the base
fallback. The current `tools/release-dependencies.py fetch` helper retrieves a
full verified archive pair by default; use `--binary-only` when sources are not
needed. Package verification checks the selected binaries, notices, checksums and
relocation. Native Robust integration, complete-receiver capacity and
physical-device qualification remain separate scopes; package verification
alone establishes none of those operating results.

The [signed Debian repository](releases.md#debian-installation-from-github-releases)
is generated entirely as GitHub Release assets. `datapump-fltk` and
`datapump-rev` wrap those same Linux portable directories under separate
`/opt/datapump/` backend paths, with GUI/CLI wrappers in `/usr/bin`; they can
coexist. No SDK is needed on the user's machine. The regular source file follows
GitHub's Latest download URL after full certification, while an experiment's
source file pins its own tag. Signing setup and installation commands are in the
release guide. Generated `.deb` files, indexes and private keys stay out of Git.

The existing native CI still uses Ubuntu 22.04 and audits a glibc 2.35 ceiling,
then tests copied artifacts on Ubuntu 22.04 and 24.04. A 2.36 SDK does not imply
compatibility with those older 2.35 systems. Do not merge the two promises.

`DATAPUMP_MAX_GLIBC=2.35 ./build.sh package` enforces that ceiling; it does not
make a newer host's binary compatible. The ordinary command audits the observed
ABI without inventing a compatibility claim. Use the corresponding baseline
builder or SDK for a release with that guarantee. Native display checks and
hardware qualification remain distinct from headless package verification.
An unchanged bundle can run across compatible distributions; another CPU
architecture, musl libc, absent X11/XWayland or inadequate OpenGL support for Rev
is outside that guarantee. ALSA plugins/configuration and hardware drivers remain
host integration boundaries.

Selected `docs/validation-data` research is omitted from normal binary bundles
(about 57 MiB at this change). Keep an offline capture only when it supports a
documented current finding or an active reproducible question, and retire
redundant raw data when that purpose ends. Set
`-DDATAPUMP_INSTALL_VALIDATION_DATA=ON` when a needed offline evidence bundle is
wanted. Dependency provenance, all required notices and ordinary documentation
remain installed. See
[offline installation](offline-installation.md) for platform limits and checks.

## Where to make a change

| Concern | Start here | First focused group |
| --- | --- | --- |
| Wire format, short bits, physical completion, pending rows | [development contract](development.md), `src/transfer.cpp`, `src/stream_codec.cpp`, `src/live.cpp` | `contract` |
| Robust modem | [Current architecture and test policy](robust-current.md) | `triad`, then affected shared component groups |
| Independent Fast/Legacy modem | [Fast topics](indexes/fast.md), [Legacy reference](legacy-modem.md), `src/fast/`, `src/legacy/` and matching public headers | `fast` / `legacy` |
| Shared GUI behavior | [GUI architecture](gui-architecture.md), `src/gui/application.cpp`, `src/gui/controller.cpp` | `gui`, then affected native backends |
| Native widgets/platform services | `src/gui/backend_fltk*`, `src/gui/backend_rev*` | `gui`, `native` |
| Build/dependencies/packaging | `CMakeLists.txt`, `cmake/`, `build.sh`, `third_party/README.md` | `build`, `packaging` |

When locating implementation, search maintained `src/`, `include/`, `tests/`
and `cmake/` first. Vendored trees and `docs/validation-data` are reference
material; old plans and recorded build commands are history, not current
instructions. Keep independent reference fixtures even if they resemble
production algorithms.
