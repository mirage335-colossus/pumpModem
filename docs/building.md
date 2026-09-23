# Build and maintenance guide

From a checkout with its native development dependencies installed:

```sh
./build.sh
./build/dev/datapump-gui
./build/dev/pump --version
./build.sh test contract
```

`build.sh` is a POSIX shell wrapper around CMake, not another build system or a
package manager. It selects an explicit preset, reuses its output directory and
prints the application paths. CMake 3.21+, a C++20 compiler and OpenSSL 3
development files are required. The normal FLTK GUI also needs X11/Xft and font
development packages on Linux. FLTK, XZ/liblzma, QR and LDPC sources are vendored.
Python is optional for CLI/build-tool tests, and required only by the opt-in Rev
resource builder or optional dependency preparation helper. Python is not part
of the installed application.

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
its complete source archive. Download or build it once, explicitly install it
with `tools/build-sdk.py install`, then pass `--sdk /absolute/path` to the same
build wrapper. Using a prepared SDK requires no container, chroot or root access.
It includes private C++ runtime libraries for CMake, Ninja and GCC helpers,
supplied by the SDK's own compiler build. Those host tools still require their
documented glibc baseline; release SDKs are built in Bookworm and audited after
relocation. The default native build remains available.

Manual releases with version/date tags, an experiment checkbox and six portable
application downloads are documented in [manual portable releases](releases.md).
Windows release, certification and full CI jobs reuse a checksummed dependency
bundle from the durable `base` release: static OpenSSL, GLEW and FreeType for
both GUI backends. The runner supplies MSVC and the Windows SDK separately.
The [toolchain selector](../tools/select-windows-toolchain.ps1) prefers an
installed Visual Studio 2022, or uses Visual Studio 2026 with its installed
v143 tools. The current larger Windows images have VS2026 and v143 14.44;
the optional `windows-2022` image retains VS2022. The selector sets the matching
CMake generator and pins v143 instead of adopting VS2026's default toolset.
VS2026 requires CMake 4.2 or newer. This host selection does not change the
dependency recipe or rebuild the existing base.
An absent matching recipe requires explicit
[Windows base maintenance](releases.md#windows-dependency-base); routine jobs
never start a cold dependency build. Reuse includes relocation and a check
that the consuming linker is at least as new as the recorded builder. Keep
the compiler at least as new too, within v143; the bundle contains no LTO
objects. This dependency handoff uses neither Actions cache nor artifacts.

## Commands and profiles

| Command | Output/configuration | Work performed |
| --- | --- | --- |
| `./build.sh` | `build/dev`, FLTK Release, system runtime linkage | Application targets only; test cases remain available |
| `./build.sh --cli` | `build/dev-cli` | CLI without the native GUI toolkit |
| `./build.sh test GROUP` | `build/dev` normally | Builds the group's prerequisites, then runs CTest |
| `./build.sh sanitize GROUP` | `build/sanitize`, Debug + ASan/UBSan, headless | Instrumented tests; default group is `contract` |
| `./build.sh package` | `build/release`, portable Release, tests disabled | Creates TGZ/ZIP and verifies both archives and relocation |
| `./build.sh --sdk PATH` | `build/dev-sdk` | Uses the prepared SDK's compiler, build tools and isolated target dependencies |
| `./build.sh package --sdk PATH` | `build/release-sdk` | Collects SDK runtime libraries/notices and enforces its glibc ceiling |
| `./build.sh test packaging` | `build/package-tests`, portable Release | Packaging fixtures and application relocation |
| `CC=clang-19 CXX=clang++-19 ./build.sh --backend rev` | `build/rev` | Optional C++23/Rev build with its extra dependencies |

Native sanitizer/CLI/Rev combinations use distinct directories. `--jobs N`
controls build and test concurrency (default 2); `DATAPUMP_JOBS` or
`CMAKE_BUILD_PARALLEL_LEVEL` can set that default. `--build-jobs N` overrides
compilation concurrency independently, so a large CI runner can compile with all
available cores while tests retain their established concurrency:

```sh
./build.sh test gui --build-jobs "$(nproc)" --jobs 2
```

Prefer a modest limit on memory-constrained hosts. Use `--stop-on-failure` with `test` or `sanitize` for early CI feedback after a
failure; successful runs still execute the entire selected group.
Native GUI tests must have a display, preferably an
isolated Xvfb session; `./build.sh test native` builds and runs them explicitly.
For the full sequence on a slower software-rendered desktop, pass
`-- -DGUI_SMOKE_TIMEOUT=600` to use the same overall allowance as hosted
certification. Individual reception and presentation assertions still apply.
With Mesa software rendering, set `LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2`,
matching CI's renderer thread cap so drawing does not crowd out GUI polling.
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

Do not run simultaneous configure/build operations against the same directory.
Separate agents should share a completed incremental build or use a deliberately
separate toolchain/profile tree. Old top-level `build-native`, `build-agent-*`
and similar directories are not current presets. They may contain unique logs,
captures or dependencies, so the wrapper never deletes them. Archive useful
evidence before removing them yourself.

Every configuration writes `build-info.txt` with source revision, compiler,
backend and build flags; packaging includes it. This is configure-time
provenance, not a claim of byte-for-byte reproducibility or an uncommitted diff
archive. `./build.sh` reconfigures before building to refresh it. Existing
`--version` output remains compatible.
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

Tests retain their individual executables, independent vectors and original
assertions. They are excluded from the default application build. **Build the
test prerequisites before invoking CTest**; CTest by itself does not rebuild.

| Group | Coverage |
| --- | --- |
| `contract` | The entire focused compatibility selection in [development.md](development.md), including slow probability calibration |
| `regular` | Regular transport, DSP, hardware contract and CLI tests |
| `fast` | Fast coding, transfer, modem and shared Fast GUI tests |
| `legacy` | Legacy waveform/session and shared Legacy GUI tests |
| `gui` | All shared GUI tests and native executable self-check; no display tests |
| `native` | Selected backend's display-dependent workflow and conformance probes |
| `packaging` | Runtime collection, checksums, corruption detection and relocation |
| `build` | Build orchestration, dependency preparation and vendored source integrity |
| `all` | All configured non-display tests, including expensive calibration |

Groups overlap deliberately. `contract` is the maintenance gate for changes
covered by the preservation contract; a faster subgroup never substitutes for
it. Shared GUI changes also require native checks for affected backends under
the existing [GUI maintenance rules](gui-architecture.md#verification-and-maintenance-guardrails).
Python-dependent cases are registered only when Python is available. CI keeps
its no-Python application build and explicit Python CLI checks.
The differential receiver calibration runs independent captures on available
cores, capped at 16 workers. CTest records that count and runs this test alone,
even when the overall test-job limit is larger. Every matrix case still uses
the same 64 seeds; numerical checks, full captures and deadlines are unchanged.
Each capture keeps one DSP worker and a 4 MiB receiver budget. Parallelism
increases capture-buffer and sanitizer memory proportionally, so constrain
memory-limited machines with `-DDATAPUMP_CALIBRATION_WORKERS=4` after `--` in
the build wrapper; the default `0` selects available cores. The larger default-
duration controls remain sequential. For an inexpensive scheduling check, run
`test_differential_receiver_probability --check-worker-plan`; this validates
all 1..16 seed partitions without running the calibration. A direct invocation
also accepts `--workers 1..16`. Avoid overlapping independent heavy build/test runs during timing
and display qualification.

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
tests but do not compile them. For individual work, existing targets such as
`test_compression_short` continue to work.

## Testing stages

Use focused feedback while a fault or feature is still changing, then broaden
validation after the candidate is complete. A fast diagnostic pass establishes
that a particular case works; it does not establish that the rest of the
application, another compiler/platform or the distributed package still works.

| Stage | Appropriate checks | Completion condition |
| --- | --- | --- |
| Investigate or iterate | Smallest relevant reproducer, target or affected group; `devfast=true` when its cases match the issue. Build only those prerequisites. | The regression fails before the fix and passes afterward, or the feature's intended behavior is demonstrated. |
| Validate the completed candidate | Normal full source CI with `devfast=false`, plus applicable preservation-contract, GUI/native, SDK, platform and packaging coverage. | Required jobs actually finish successfully for the final source/configuration; failures return to focused diagnosis. |
| Qualify a release | Publish the intended binaries after packaging checks, then run certification with `devfast=false` for their exact tag. | The attached report passes for the actual source and asset hashes being offered to users. |

Agents should progress through the relevant stages without requiring another
reminder after each focused success. Do not stop at the first row for a runtime
fix or completed feature merely because full coverage takes longer. The slow
tests belong at the validation stage instead of every edit. Documentation-only
changes need proportionate checks, but do not erase outstanding validation for
earlier code changes in the same task.

Reuse passing results for the same source and configuration. Avoid scheduling
duplicate push, PR and manual workflows; a full manual dispatch after temporary
`[skip ci]` commits is sufficient when it covers the required final candidate.
Do not rerun an expensive unchanged suite after recording a pass unless later
changes or failures invalidate that evidence. Compile with available cores, but
keep timing-sensitive test concurrency at its documented limits.

For a pending-replay row assertion, build only `test_gui_controller` in an
existing configured tree and run it with `--pending-replay-batch` first. This
small fixture checks real reception reconciliation, explicit retirement chains
and the smoke checker without running a modem replay. Follow a completed
checker change with the affected `gui` and `native` groups. A retry against an
unchanged published archive does not test a newer checker committed to source;
retain and distinguish both results.

Automatic `ci.yml` runs are bounded feedback: PRs and pushes to `main` run the
small Linux/Windows Legacy diagnostic and offline build/SDK helper fixtures.
Branch pushes do not launch a second copy of the PR checks, and documentation
changes alone do not trigger them. Release and base workflows retain their
path-specific helper checks. Automatic green checks do **not** mean the full
application passed regression testing. Dispatch `ci.yml` with `devfast=false`
when the candidate is ready; its native GUI, sanitizer, Windows, application
and archive checks remain intact. `sdk.yml` is now manual so the additional
SDK host-tool and copied Bookworm/Ubuntu matrix runs when relevant, without
duplicating every push and PR. Ordinary consumers fail on a missing base recipe
instead of silently building an SDK.

For changes confined to Linux distribution packaging, start with
`python3 tests/test_apt_release.py`, `python3 tests/test_distro_release.py`,
`python3 tests/test_arch_release.py`, `python3 tests/test_gentoo_sync.py` and
the affected release/certification helper tests; `./build.sh test build` runs their normal group. Once complete, use
`release.yml` with `source_release=SOURCE_RELEASE_TAG` to wrap existing verified archives in a new experiment and
test the same packages on Debian/Ubuntu AMD64 and ARM64 and native Arch/Gentoo
repositories with the larger runners. For update channels, also exercise a signed
A-to-B update, idempotent refresh and rejected tampered/older metadata with the
small local fixtures before native installation. Gentoo host dependencies must come from its
binary repository; a missing binary fails with an actionable error instead of
starting an expensive source build.
This path does not rebuild application binaries or SDKs. Follow publication
with full `certify.yml`, `devfast=false`, for that new release; packaging-only
checks are not a substitute for release certification. See
[APT packaging and validation](releases.md#package-an-existing-release-without-rebuilding-it).

Manual workflows expose `linux_runner` and `windows_runner` dropdowns for the
organization's larger x86-64 runners. Defaults and automatic jobs now use
`ubuntu-latest-h` and `windows-latest-h`; native CI, portable release and
certification default `arm_runner` to `ubuntu-24.04-arm-h`. Explicit smaller
choices remain available when desired, but checks do not repeat on those pools.
The ARM64 L and H tiers provide 8 and 32 CPUs respectively. The ARM64 selector
does not affect x86-64 routing. Package-manager checks remain on larger L/H
pools and use H unless L is explicitly selected.
Agents can pass these input names through `gh workflow run -f`.
Use the [runner selection guide](releases.md#runner-selection-and-build-parallelism)
to choose and verify access before a long run. `ci.yml` with `devfast=true` and
`diagnostic=runner-capacity` checks routing, visible CPUs and small production
compiler fixtures without building SDKs or running calibration. This routing
check does not replace applicable source regression or release certification.

Check a larger ARM64 pool without repeating x86-64 or Windows diagnostics:

```sh
gh workflow run ci.yml --ref REF -f devfast=true \
  -f diagnostic=arm-runner-capacity -f arm_runner=ubuntu-24.04-arm-h
```

Reuse the earlier runner-capacity evidence instead of retesting smaller pools.
Keep the applicable full regression and release certification sequence below
after the focused checks.

For a completed branch candidate, use the full workflow, and SDK qualification
when the change touches its supported toolchains or portable packages:

```sh
gh workflow run ci.yml --ref REF -f devfast=false
gh workflow run sdk.yml --ref REF -f devfast=false
gh run watch RUN_ID --exit-status
```

Replace `REF` and `RUN_ID` with the intended revision and the actual dispatched
run. Check each required result; a queued, running, skipped or cancelled job is
not a pass. Diagnose a failure narrowly and rerun the affected full checks once
fixed. Record source SHA, commands/run links, outcomes and untested boundaries in
[validation](validation.md). If a check cannot finish, state what is blocked and
what remains; do not describe the change as fully validated or the release as
certified. See [release validation](releases.md#diagnose-a-branch-before-full-validation)
for the separate publication/certification sequence and source-pinning rules.

### Sanitizer throughput scope

Native CI's manual `sanitizer_realtime` checkbox defaults to **false**. Only its
instrumented Debug job omits `fast_session` and `gui_fast_live` by default. Both
feed audio at wall-clock speed, so sanitizer overhead can exhaust the bounded
capture queue before decoding catches up. This is not evidence of a hardware-only
fault. The omission removes these scenarios' instrumented end-to-end coverage;
the job notice and summary name it explicitly. No error is converted to success.

Release jobs still require both tests, and all other sanitizer tests remain
required. Calibration, source contract checks and release certification are
unchanged. Include the two instrumented checks explicitly with:

```sh
gh workflow run ci.yml --ref REF -f devfast=false -f sanitizer_realtime=true
```

The checkbox has no effect with `devfast=true`. Local `./build.sh sanitize`
selection is unchanged. To request just these two cases in an already configured
`build/sanitize` tree, rebuild their executables first:

```sh
cmake --build build/sanitize --target test_fast_session test_gui_fast_live --parallel 2
ctest --test-dir build/sanitize --output-on-failure --parallel 1 \
  -R '^(fast_session|gui_fast_live)$' --no-tests=error
```

Failures remain fatal when requested. Keep the production FIFO, physical-end and
pending-content assertions intact; do not classify unrelated sanitizer findings
as performance warnings.

## Focused development diagnostics

The manual `devfast` checkbox in native CI (`ci.yml`), SDK qualification
(`sdk.yml`) and release certification (`certify.yml`) defaults to **false**.
Default manual dispatches retain calibration and their normal regression
selections, with the two instrumented real-time cases opt-in as described above.
With `devfast=true`,
each workflow defaults to the same small Legacy diagnostic on Linux and
Windows: compile the production Legacy controller/session and existing
`gui_legacy_live` fixture and deterministic `gui_legacy_poll` cancellation/error
regression, then require three consecutive serial passes, stopping
at the first failure. Builds use the runner's available CPU cores. This path
does not build the SDK or full application, run the general platform matrix,
create packages, use Actions artifacts/cache, or certify/promote a release.

Dispatch one of these equivalent diagnostic entry points after pushing the
branch under investigation:

```sh
gh workflow run ci.yml --ref codex/portable-releases -f devfast=true
# Alternatively; no release_tag is needed for a source diagnostic:
gh workflow run certify.yml --ref codex/portable-releases -f devfast=true
```

For a Windows Rev compiler or event-loop fault, native CI also offers a focused
selection that first runs the dependency-free Win32 message regression, then
downloads the existing Windows base and compiles only the actual Rev GUI:

```sh
gh workflow run ci.yml --ref REF -f devfast=true -f diagnostic=windows-rev
```

For the adapter, Windows DSP and ARM64 CLI regressions found by full
certification, use the bounded selection:

```sh
gh workflow run ci.yml --ref REF -f devfast=true -f diagnostic=certification
```

It builds only the existing Linux FLTK/Rev adapter tests, Windows FLTK adapter
plus `pattern_code`/`fast_low_rate`, and the ARM64 CLI differential-estimate
case. Linux x86-64 and Windows dependencies come from the exact reusable base;
ARM64 uses the same Clang baseline as the Rev release. Tests retain their
assertions and individual deadlines. No calibration, full smoke sequence,
package publication or certification runs in this diagnostic. Once fixed,
run the affected full checks and publish/certify new binaries when runtime
changes must reach users.

When Linux and ARM checks already passed and only the Windows fault is changing,
reuse that evidence and retry the same three Windows regressions alone:

```sh
gh workflow run ci.yml --ref REF -f devfast=true -f diagnostic=windows-certification
```

This selects the reusable diagnostic's `scope=windows`; `certification` keeps
`scope=all`. It still runs the complete Windows FLTK adapter, `pattern_code` and
`fast_low_rate` checks, without rebuilding unchanged Linux or ARM targets.

For a graphics-driver startup failure after publication, use the bounded
environment diagnostic on the unchanged archive:

```sh
gh workflow run ci.yml --ref REF -f devfast=true -f diagnostic=graphics \
  -f diagnostic_release=RELEASE_TAG
```

It inspects the Windows WGL bootstrap using the installed compiler/SDK and
compares host GLX with the published ARM64 Rev binary on Ubuntu 24.04. It builds
neither the application nor its dependencies. Loader output and bounded startup
failures are diagnostics, not full smoke/certification results. Archives,
drivers and registry settings remain unchanged.

After correcting the ARM64 portable runtime, `devfast=true,
diagnostic=arm-rev-package` builds only that Rev package on Ubuntu 22.04 and
checks its unchanged archive on Ubuntu 24.04. The startup check requires a
visible window and host Mesa/LLVM/C++ runtime mappings within 15 seconds.
It does not run the full GUI smoke or calibration, upload artifacts, or publish
a release. Follow a focused pass with applicable full validation.

For a copied ARM64 Rev smoke failure on Debian Trixie, build only the affected
package from the corrected branch and run its complete GUI smoke there:

```sh
gh workflow run ci.yml --ref REF -f devfast=true -f diagnostic=arm-rev-smoke
```

This mode retains the Ubuntu 22.04 build baseline, verifies archive/package
hashes and the glibc 2.35 ceiling, then runs one full copied GUI smoke with its
600-second allowance and the same Trixie display prerequisites as certification.
It uses the H ARM64 runner by default and the pinned runtime dependency scanner;
it does not rebuild an SDK, run calibration or a general matrix, upload artifacts,
publish or certify a release. Rerunning an older release cannot test this new
checker; this diagnostic rebuilds the selected branch's package explicitly.

The `windows-rev` path performs a bounded headless self-check; it creates no
release or Actions artifact and does not qualify rendering, full regression
coverage or published binaries. `diagnostic=legacy` remains the default. Choose the
selection that reproduces the current fault, then follow with applicable full
checks and certification once the candidate is complete.

The diagnostic logs and run summary identify the checked-out branch SHA.
A new dispatch selects the current branch commit; rerunning an earlier run
uses its original commit. Full certification instead checks out the published
release's recorded source revision and tests its published binary hashes.
Diagnostic jobs have read-only repository permissions and never issue
certification reports or change release status.

The same focused build needs CMake 3.21+, a C++20 compiler and the selected
build generator, without OpenSSL, native GUI dependencies or an SDK. On Linux:

```sh
cmake -S tests/devfast -B build/devfast -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/devfast --parallel "$(nproc)"
ctest --test-dir build/devfast --output-on-failure -R '^gui_legacy_(poll|live)$' \
  --parallel 1 --repeat until-fail:3 --stop-on-failure --no-tests=error
```

On Windows with Visual Studio 2022 or 2026 and installed v143 tools, using
PowerShell (CMake 4.2 or newer is required for VS2026):

```powershell
$toolchain = & ./tools/select-windows-toolchain.ps1
cmake -S tests/devfast -B build/devfast -A x64
cmake --build build/devfast --config Release --parallel $env:NUMBER_OF_PROCESSORS
ctest --test-dir build/devfast -C Release --output-on-failure -R '^gui_legacy_(poll|live)$' `
  --parallel 1 --repeat until-fail:3 --stop-on-failure --no-tests=error
```

Manual `devfast` does not suppress lightweight checks triggered by a preceding
PR update or push to `main`.
For intermediate diagnosis commits, a temporary `[skip ci]` commit-message
marker can [skip automatic push/PR runs](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/skip-workflow-runs)
while still allowing manual dispatch. After the candidate is complete,
explicitly dispatch the ordinary workflows with `devfast=false` (or unchecked), including applicable
contract, GUI/native and packaging checks. Wait for those outcomes as described
in [testing stages](#testing-stages). A focused pass is development feedback,
not a substitute for those gates.

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

Application publication and extensive qualification are separate:
[manual portable releases](releases.md) publishes after packaging checks, then
[certification](../.github/workflows/certify.yml) attaches source/test outcomes
and the exact published binary hashes to that release. Copied GUI checks use
`-DGUI_SMOKE_TIMEOUT=600`. Rev replay/waterfall cadence misses emit visible
warnings and do not prevent publication or certification; pending-progress,
content, physical-completion and cancellation assertions remain mandatory.
See [release warnings](releases.md#rev-display-warnings). Physical-device
qualification remains a separate scope.

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

Historical `docs/validation-data` captures are retained in the source repository
but omitted from normal binary bundles (about 57 MiB at this change). Set
`-DDATAPUMP_INSTALL_VALIDATION_DATA=ON` when an offline evidence bundle is wanted.
Developer documents may link to those optional captures. Dependency provenance,
all required notices and ordinary documentation remain installed. See
[offline installation](offline-installation.md) for platform limits and checks.

## Where to make a change

| Concern | Start here | First focused group |
| --- | --- | --- |
| Wire format, short bits, physical completion, pending rows | [development contract](development.md), `src/transfer.cpp`, `src/stream_codec.cpp`, `src/live.cpp` | `contract` |
| Regular detector/search | `src/pattern_*`, `src/receiver_probability*` | `regular`, then `contract` |
| Independent Fast/Legacy modem | `src/fast/`, `src/legacy/` and matching public headers | `fast` / `legacy` |
| Shared GUI behavior | [GUI architecture](gui-architecture.md), `src/gui/application.cpp`, `src/gui/controller.cpp` | `gui`, then affected native backends |
| Native widgets/platform services | `src/gui/backend_fltk*`, `src/gui/backend_rev*` | `gui`, `native` |
| Build/dependencies/packaging | `CMakeLists.txt`, `cmake/`, `build.sh`, `third_party/README.md` | `build`, `packaging` |

Search maintained `src/`, `include/`, `tests/` and `cmake/` first. Vendored trees
and `docs/validation-data` are reference material; old plans and recorded build
commands are history, not current instructions. Keep independent reference
fixtures even if they resemble production algorithms.
