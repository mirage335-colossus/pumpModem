# Validation record — version 0.7.2

The application and portable runtime are native C++. Python is optional test
tooling for FLTK/CLI builds and required to embed Rev resources at build time;
it is not installed with the application.

## CI cleanup and Windows graphics coverage — 23 September 2026

Following the recorded sanitizer throughput diagnosis below, native CI now makes
only `fast_session` and `gui_fast_live` opt-in in its instrumented Debug job via
`sanitizer_realtime=true` (default false). Release coverage remains mandatory.
The job records omitted instrumented coverage explicitly, and every selected
test failure remains fatal. Local test selection and runtime behavior are
unchanged. This policy does not establish a hardware-only cause, erase earlier
failures or turn omitted tests into passes.

Actionlint and whitespace checks pass. Twenty shell probes cover default/empty
input, explicit opt-in, unchanged Release selection and CTest nonzero status
propagation in both `sh` and `bash`. CTest discovery against the existing local
sanitizer and Release trees confirms that the anchored expression removes
exactly the two named tests (140 to 138 and 142 to 140 respectively); these are
selection checks, not executions or new regression passes. The shell probes
verify that Release never applies that expression. No full native run,
calibration or SDK rebuild is needed to verify this CI-only policy change.

The Windows certification harness recognizes only exit 1 with the exact Rev
message `[NativeWindow] Required WGL ARB extensions not available` as a hosted
graphics warning. Compilation, clipboard, headless GUI/CLI, modem, calibration,
and published-archive checks remain required. The four omitted OpenGL tests and
published GUI smoke are listed in a per-run warning log and certificate. An
otherwise successful run is green with `passed_with_warnings`; it does not
qualify Windows Rev desktop graphics or promote the release to Latest. A user's
machine with the same limitation cannot open the Rev GUI, so this is an
environment limitation with a functional consequence, not merely poor cadence.

Automatic CI now runs bounded diagnostics. Full native and SDK qualification
remain explicit manual gates, and larger H runners are the defaults. The
`ci.yml` / `devfast=true` / `diagnostic=certification` route reuses prepared
dependencies and isolates the known adapter, Windows DSP, and ARM estimate
failures. It does not produce a release certificate.

Local build-helper checks pass **17/17 in 10.25 seconds**, including **71**
certificate and **5** WGL-policy cases. The initial sandboxed GPG fixture could
not start its disposable signing agent; the unchanged fixture passes outside
that restriction. Actionlint and whitespace checks pass. Focused modem tests
retain the original error tolerance, physical completion and decoded-message
assertions. Portable test noise freezes the existing GNU fixture; it does not
claim that all noise realizations decode successfully.

The [focused hosted run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35920196103)
uses candidate `92992e121641e63a24f12abd65fac37789dae2ef`. ARM64 probability
vectors pass in **12.00 seconds** and the formerly timed-out CLI estimate passes
in **2.066 seconds**, retaining its original 30-second limit. The estimate's
seeded stream and modeled thresholds are preserved while avoiding repeated
software long-double engine-range calculations. Windows `fast_low_rate` and
`pattern_code` pass in **24.05** and **1.80 seconds**. Complete Linux SDK adapter
checks pass for Rev (**129.44 seconds**) and FLTK (**67.87 seconds**). The earlier
Rev label-only clipping failure does not reproduce in this run. Windows' focused
color check passes, but its complete adapter suite fails later at
`Layout lifecycle fixture lost native controls`; that failure remains fatal and
is investigated separately from WGL. Full regression results follow the focused
fixes.

FLTK's fixture now identifies the actual `NativeWindow`, rather than assuming
the first event-ordered FLTK window belongs to the application. A competing
window regression passes locally in **0.42 seconds**, and the complete cached-
SDK FLTK adapter suite passes. The live-profile fixture also grants bounded
PCM delivery credits instead of flooding the asynchronous decoder at roughly
20 times real time. Its previously failing fixed-interval case passes in
**6.353 seconds** normally and **23.409 seconds** under ASan/UBSan. The focused
short/long transmit-profile case passes in **4.895** and **35.702 seconds**,
respectively. Queue limits, deadlines and reception assertions are unchanged;
stage-specific diagnostics now identify any remaining general `live` timeout.

Calibration uses available cores up to 16, with matching CTest processor
accounting and exclusive scheduling. Cheap partition checks verify all 64
seeds appear exactly once for every supported worker count; no capture or
numerical acceptance criterion is removed. The complete Windows and Linux
Release numerical runs below pass. Sanitizer real-time overruns remain
failures; the WGL exception does not apply to them.

The [Windows Rev diagnostic](https://github.com/mirage335-colossus/pumpModem/actions/runs/35920957351)
is **green** on `f582d8eb2827c2cdff02d7e8df40adac55c0ce74`: the native event test,
Rev compilation and headless self-check pass, and the actual missing-WGL
message produces the intended warning. Dependency-base reuse takes about
**14 seconds**, with no SDK rebuild. This diagnostic does not certify published
archives.

The first Windows fixture retry exposes a second independent assumption:
the lifecycle fixture placed a bitmap in the full console's bottom plot area,
which correctly has zero height in the hosted **1028×749** client area. The
fixture now uses the bounded QR area and FLTK explicitly exercises the reported
small size. All retained-control assertions remain. The affected FLTK and Rev
probes pass locally in **0.38** and **0.29 seconds**. The application's normal
minimum layout remains **1030×968**; this is a potentially significant usability
limit for an actual undersized desktop, not a transport or data-integrity fault.
The [corrected Windows-only run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35922024305)
is **green** on `81858d2ffd7fecfbf3fcc23d121efcfb69a7b33f`: both targeted adapter
probes and all **three** complete adapter/DSP tests pass (**99.20 seconds** for
the CTest selection). Unchanged Linux and ARM diagnostics were not repeated.

Two cached ASan/UBSan real-time checks still fail when run alone. New failure-only
diagnostics show Fast RX receiving **156,000 samples in 3.225 seconds** while its
last decoder telemetry reaches **105,600 samples**, followed by the production
one-second FIFO overrun. No coding cycle or LDPC frame fails before that overrun.
The GUI case records three such overruns followed by automatic listening retries;
the retained row remains `INCOMPLETE`, nonactivatable and without exposed source
content. This reproduces instrumented throughput failure, not demonstrated row
identity corruption or a sanitizer memory/UB report. The unchanged assertions
still fail (**4.83** and **49.28 seconds**); they are not covered by the WGL waiver.
Release build results are separate evidence, and no conclusion about physical
hardware performance follows from these hosted or simulated checks alone.

[Experiment `v001_00-2026-09-23-1629CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1629CDT)
is published successfully by [run 35922653913](https://github.com/mirage335-colossus/pumpModem/actions/runs/35922653913),
with all six freshly built application archives and **50** delivery assets,
including signed Debian, Arch and Gentoo update channels. Source and packager
are `81858d2ffd7fecfbf3fcc23d121efcfb69a7b33f`;
inventory SHA-256 is
`e57ecf5f0099a290e4180fb72e2a3b61e60b3729debbccddcee3316e8b3ffa43`.
The separate full [certification run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35923981422)
uses tooling `cf2553e8eeec21ed60cc40d12fa546b3404338bc`, `devfast=false`,
unchanged prepared SDKs and H runners. All **nine** Debian/Ubuntu installation
jobs and both Arch/Gentoo jobs pass, including Bookworm on x86-64 and ARM64.
The [Windows Rev job](https://github.com/mirage335-colossus/pumpModem/actions/runs/35923981422/job/107394669632)
is **green**: 38 GUI/clipboard tests, 29 non-calibration contract tests, two
packaging tests, full calibration (**182.01 seconds**) and exact published
archive checks pass. Only the enumerated native WGL-dependent checks are
omitted, with the intended warning.
Windows FLTK and both Linux backends on x86-64 and ARM64 also pass their
complete source and published-archive certification jobs. Their source contract
groups retain all 30 tests, including numerical calibration; the Linux contract
groups finish in **690–849 seconds**. ARM64 Rev initially waits about 30 minutes
for larger-runner capacity; this is queue time, not an SDK rebuild.

The copied ARM64 Rev archive passes on Bookworm and Ubuntu 22.04, 24.04 and
26.04, but its first Trixie check fails at smoke phase 21 (`Pending replay
signal was not presented`). Earlier cadence messages are already warnings;
this separate pending-row assertion remains fatal. A deterministic fixture
using real `apply_receptions` proves a checker defect: a batch can explicitly
retire an earlier pending ID, so that ID correctly has no retained row. The
corrected smoke helper requires actual retirement and a visible pending
replacement, including retirement chains. Missing active rows, stale or
ambiguous replacement claims, eviction, cycles and completed replacements still
fail. The production ingestion behavior and 64-ID retirement bound are unchanged;
the only model addition is a read-only retirement query. Focused fixtures pass
in **0.002 seconds**, and all **36** shared GUI tests pass in **107.64 seconds**.
The original hosted log lacks identity detail, so this establishes a checker
defect without proving the exact cause of that particular failure. New errors
include batch/retained identities and revisions. This correction is newer than
the immutable experiment above; an old-release retry cannot validate it.

The first certification attempt records **19/20** copied-distribution checks
passing and correctly reports overall failure for the Trixie assertion. Its
[attempt-1 report](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-23-1629CDT/certification-35923981422-attempt-1.md)
and warning log remain attached to the release. Only the failed Trixie job and
its dependent report are retried; successful source, package-manager and
distribution checks are retained.

The single-job retry passes, and [attempt 2](https://github.com/mirage335-colossus/pumpModem/actions/runs/35923981422/attempts/2)
is **green**, with certificate status `passed_with_warnings`. All five required
job categories succeed, including all **20** copied-distribution checks. The
[attempt-2 certificate](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-23-1629CDT/certification-35923981422-attempt-2.md)
and [warning log](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-23-1629CDT/certification-35923981422-attempt-2-warning.log)
bind the same source/inventory and explicitly omit only the five Windows Rev
graphics checks. The downloaded warning hash matches its certificate. The
experiment remains a prerelease and is not Latest; native Windows Rev graphics
remain unqualified. The retry establishes an intermittent old-checker failure,
not proof of its cause or validation of the newer checker.

The [corrected-source ARM64/Trixie diagnostic](https://github.com/mirage335-colossus/pumpModem/actions/runs/35929767834)
passes on `c04cff85e32b9f51bb5e6f3b395c8d84d1277ec7`. It builds only Rev in
the Ubuntu 22.04 baseline (**4m37s**), then verifies the same archive's hashes,
inventory, glibc 2.35 ceiling and complete GUI smoke on Trixie (**5m38s** for
the verification/display setup step). Cadence misses remain warnings, including
phase 21; pending-row and content assertions pass. It uses the ARM64 H runner,
does not rebuild an SDK or change release assets, and is focused evidence for
the new checker rather than certification of a new release.

The local software-rendered Rev smoke reaches its existing **300-second**
budget in phase 15, then the hosted **600-second** allowance in phase 21.
Neither run reports a pending-row/content assertion failure, but both are
timeouts and provide incomplete local full-smoke coverage. The successful
larger-runner check above provides the complete new-source Rev smoke result;
local time limits are not extended further. These local Rev runs use
`LIBGL_ALWAYS_SOFTWARE=1` with `LP_NUM_THREADS` unset, unlike CI's cap of two;
their timing is not a like-for-like comparison with the hosted display.
The remaining four local Rev native adapter/platform/coordinate tests pass
in **67.95 seconds** on the same isolated display.
All three local FLTK native tests pass in **296.53 seconds**, including the
complete shared smoke (**228.33 seconds**), adapter conformance (**68.13
seconds**) and document conformance (**0.06 seconds**). The private Xvfb display
is stopped after the sequential checks. Actionlint and whitespace checks pass
for the final workflow/documentation state.

Full [native regression](https://github.com/mirage335-colossus/pumpModem/actions/runs/35921401021)
uses `4656214c0b448ef8fc8c278357340b86d19d6bd9`; subsequent source differences
are test fixtures, failure diagnostics and documentation, not application
behavior. Both full Linux GUI selections pass (**39 FLTK**, **42 Rev**), as do
all **121 Windows tests in 1,225.03 seconds**, followed by GUI relocation and
both native archive checks. Its complete receiver calibration passes in
**178.42 seconds** with 16 workers and every original capture retained.
Linux Release passes all **125 tests in 1,218.52 seconds**, followed by native
desktop, CLI, relocation and both archive checks.
Linux Debug with ASan/UBSan completes **120/122 tests in 3,155.76 seconds**;
full numerical calibration passes in **1,066.64 seconds**. The only failures
are `fast_session` (**4.22 seconds**, capture overrun) and `gui_fast_live`
(**49.43 seconds**, incomplete reception), consistent with the isolated
diagnosis above. No sanitizer memory/UB report appears. **Overall native CI
remains failed**. Its dependent copied-Ubuntu jobs are skipped, not passed;
the separate release certification checks published binaries independently.

An older FLTK rendering diagnostic also remains: its default ABI clipping
stack has ten entries, and local complete adapter logs emit one overflow /
underflow pair. Inspected application push/pop calls are balanced and older
logs contain the same pair. Nested native/offscreen drawing can exhaust that
limit; the existing logs do not establish whether the triggering draw is
test-only. All adapter assertions pass. This is a minor unresolved clipping
diagnostic that could affect a drawn frame, not an observed transport or
data-integrity failure.


## Arch/Gentoo signed update channels — 23 September 2026

Schema 5 adds native signed pacman packages and per-architecture `.db`/`.files`
indexes, plus a signed Gentoo overlay channel and authenticated Portage sync
adapter. Existing binary payloads are reused. APT's signed manifest also binds
all new assets. Schemas 1–4 remain readable, but only fully certified regular
schema-5 releases can become Latest, preventing an older delivery layout from
removing update channels. Experiments remain explicitly tag-pinned.

Focused fixtures cover real signatures, native package bytes/modes, generated
mtree data, required asset inventories, APT's complete signed distribution hash
set and certification gates. Gentoo HTTP fixtures exercise a moving Latest URL,
A-to-B refresh, unchanged refresh, downgrade/tamper/wrong-key/unsafe-archive
rejection and atomic replacement failures retaining the previous tree. Its
inventory can be read on Windows without Linux-only runtime imports. Native
pacman update fixtures run in an isolated root in the Arch installation job.
The full build-tool group passes **16/16 in 12.22 seconds**. Final focused
helpers pass **74 release**, **67 certification**, **9 APT**, **8 recipe**,
**8 Arch** and **11 Gentoo** tests. Two additional native pacman cases require
the root Arch container and are skipped locally. Native Portage plugin
discovery and construction also pass against the upstream Portage source.
Workflow lint and all 29 documentation shell examples pass. The [schema-5 publication run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35901986281)
published [experiment `v001_00-2026-09-23-1322CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1322CDT)
with **50 assets**, packager `8b90225786d84775ef311c1552fdc1a9692ce768`,
application source `88fb87bc491c5a0a487b68894ae6890239793e6b`, and inventory
SHA-256 `b58aed15904aae6024cf59117602bf04100c54c575d518dc78f42f6f3313436f`.
All six application archives were reused unchanged; publication took **82 seconds**.
All nine Debian/Ubuntu jobs pass, covering the same Bookworm-onward matrix.

The first Arch attempt exposed only fixture errors: `--noprogressbar` is invalid
for `pacman -Q`, and libalpm may stop after the first missing package. The
[corrected Arch-only run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35902631226)
passes all **10** helper/native tests and installs both real release packages
with required database/package signatures. `pacman -Qkk` reports zero altered
files for both backends, exact private payload verification passes, and both
CLI/headless GUI checks pass. The complete job took **96 seconds**.

Gentoo's authenticated installer and native `emaint sync` passed in the initial
run. Its host-dependency installation then exposed a bootstrap-directory error:
creating `/etc/portage/gnupg` early made `getuto` skip initialization of Gentoo's
own trust anchor. The bootstrap now uses the separate
`/etc/portage/datapump-release-keyring.gpg` file. Signatures remain required;
no application or release asset needed changing. The [focused Gentoo retry](https://github.com/mirage335-colossus/pumpModem/actions/runs/35902837031)
passes in **7m26s**, including native overlay sync, signed prebuilt host
dependencies, both backend installations, exact payload verification and
CLI/headless GUI checks. These package checks all use larger H runners.
Final bootstrap review additionally requires exactly one pinned primary key in
all downloaded keyrings, excluding signing subkeys from that count. APT's source
configuration is authenticated through the signed Release and manifest before
installation. The documented APT and Gentoo verification commands pass against
the published assets without installing anything; parser probes reject added
primary keys and accept a legitimate signing subkey.

Separate [full certification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35903397995)
uses workflow revision `9ed4c6899ed1493998ee7b7021d173d0efa512b1`, all-H
Linux/ARM64/Windows runners, `devfast=false`, the exact published hashes and
existing SDKs. Gentoo passes again, including another native sync after both
packages are installed. All **20 copied Linux distribution checks**, **nine
signed APT jobs** and **both native package-manager jobs** pass. Linux FLTK
passes on both x86-64 and ARM64, including all 30 contract tests with numerical
calibration, shared/native GUI tests, packaging, audio routing and the exact
published archive. SDK fetch/install on x86-64 takes **27 seconds**; its complete
contract group takes **1,648.66 seconds**, compared with **1,563.53 seconds** on
ARM64. The extended duration is full regression, not an SDK rebuild.

Full certification is **failed**. Windows Rev compilation and base reuse succeed, but its initial
native graphics probe reproduces the known unavailable WGL ARB extensions on
the hosted runner. Windows FLTK's adapter fixture also reports an estimate-label
warning-tone mismatch, and Linux x86-64 Rev's native adapter fixture reports a
label-only document clip retaining editable input/focus after all 37 shared GUI
tests passed. ARM64 Rev passes all 37 shared GUI, five native GUI and four
packaging tests, then its differential-receiver CLI estimate exceeds the
30-second subprocess bound. These failures are not the advisory waterfall
cadence warning. The attached [certification report](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-23-1322CDT/certification-35903397995-attempt-1.md)
is bound to the exact source and release inventory; the release remains an experiment and is not Latest.
Package checks alone do not certify application correctness or physical
audio/hardware.

## Arch/Gentoo recipes and shared Debian/Ubuntu packages — 23 September 2026

New schema-4 releases add Arch `PKGBUILD`/`.SRCINFO` recipes and a Gentoo EAPI-8
local overlay for separate FLTK/Rev binary packages on x86-64 and ARM64. The
recipe archives and their manifest are signed through their hashes in
`apt-repository.json`. Native package managers fetch immutable tagged application
archives and verify SHA-256 (plus BLAKE2B/SHA512 for Gentoo). The complete private
payload, wrappers and desktop files remain byte-for-byte equivalent to the
Debian installation; package tools must not strip or rewrite binaries. Original
bundled notices remain present. Generated recipes stay in release assets.

Focused coverage passes **8 distro**, **9 APT**, **72 release** and **54
certification** helper tests. Distro coverage executes both actual shell recipe
installation functions for all four Linux target/backend combinations and
checks their resulting file bytes and modes, tampering, extra files, safe
extraction, version syntax and source hashes. A real signature regression proves
that changing a recipe asset invalidates the signed APT manifest. Legacy
metadata schemas remain readable. Schema 4 required successful distribution
checks for Latest at this stage; current promotion additionally requires the
schema-5 update channels described above. The full build-tool group passes **14/14 in 6.92
seconds**. Workflow lint, documentation shell syntax and whitespace checks pass.

The APT workflow installs the same four `.deb` files on Debian 12 Bookworm,
Debian 13 Trixie, Ubuntu 24.04 and Ubuntu 26.04, with both AMD64 and ARM64 jobs.
Ubuntu 22.04 is additionally checked on ARM64. Bookworm-SDK AMD64 requires
glibc 2.36, so Ubuntu 22.04's glibc 2.35 is correctly excluded for that archive.
No runtime dependency names were changed: distro-native packages resolve their
own ALSA time64 transition and graphics/font dependencies. The Arch check uses
unprivileged `makepkg` then pacman; the Gentoo check uses a local binary package
and binary-only Portage dependency resolution, with no source fallback.

The [first native installation run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35891683676)
passed Arch and six APT environments. Gentoo rejected the missing EAPI-8
`eapply_user` preparation hook; the corrected recipe and fixture now require
it. Both Trixie jobs lacked the test tool `gpgv` (APT uses its own verifier), so
the harness now installs it explicitly. Ubuntu 26.04 ARM64 installed both
packages and verified their bytes, but Rev's headless self-check exceeded the
new harness's 30-second cap. This check includes a modem simulation, not a
display-cadence probe; its bound now matches the existing portable-package
verifier's 120 seconds. No application assertion was removed or downgraded.
These failures remain recorded against the initial `1152CDT` experiment.

The [corrected package run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35892588927)
published [`v001_00-2026-09-23-1200CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1200CDT)
with all 25 assets from packaging commit `c251077`. All nine Debian/Ubuntu
installation jobs pass, each in **63–95 seconds** on architecture-specific H
runners. Arch passes in **66 seconds**; signing, packaging and publication take
**43 seconds**. The application archives remain byte-for-byte copies of the
`88fb87b` build. Gentoo's corrected EAPI preparation/package phase passes, but
the first binary-only dependency resolution selected incompatible USE/ABI
variants. This separate CI setup fault does not change the published recipes.
The full build-tool group after the preparation fix passes **14/14 in 6.71
seconds**.

Gentoo dependency diagnosis uses `package_check=gentoo` against the unchanged
`1200CDT` release, without republishing or repeating the nine passing APT jobs
and Arch job. Its official generic binhost lacks the required ALSA audio bridge;
the v3 binhost supplies it. CI verifies CPU support, uses strict USE matching
and aligns its desktop profile/global CPU flags with the official
`tintin/openrc-v3-23` builder (upstream configuration `e3df757d`). This affects
only the disposable container's host packages, not the application CPU baseline.
The first focused retry retained stage3's SSE2 flags and rejected pixman;
that failure remains recorded in run `35893912339`. Dependency source builds
remain disabled.

The [Gentoo-only corrected run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35894812942)
passes on `149d985`: both binary packages install through Portage, installed
payloads match, and both CLI/headless GUI self-checks pass. The whole job takes
**9m56s**, mostly installing 177 prebuilt desktop dependencies into the empty
container. It found a desktop-menu category warning: `Audio` requires the
`AudioVideo` parent category. The shared Debian/Arch/Gentoo desktop generator
and a semantic regression now enforce that parent. The final experiment below
contains this correction; original application archive bytes stay unchanged.

The final Gentoo harness enables parallel package installation and uses all
runner cores, while retaining Portage's merge locks, merge-wait and system
dependency ordering. Per-package `merge-sync` durability writes are disabled
only in the discarded container. This follows the
[Portage feature definitions](https://raw.githubusercontent.com/gentoo/portage/master/man/make.conf.5)
and does not bypass signature, dependency, installed-payload or application
self-checks.

The [final package-only run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35896483127)
passes from packaging commit `f2aecd2`, publishing
[`v001_00-2026-09-23-1234CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1234CDT)
as **experiment** with all 25 assets. All **11 native installation jobs** pass:
the nine Debian/Ubuntu jobs take **80–105 seconds**, Arch **87 seconds**, and
Gentoo **7m08s**. Signing, packaging and publication take **49 seconds**.
Gentoo's observed whole-job time fell by **2m48s** from the preceding **9m56s**
run; these are ordinary hosted-job observations, not a controlled benchmark.
Both GUI backends' installed files and bounded CLI/headless GUI checks pass.
The corrected desktop entries produce no invalid-category warning.

All six application archive hashes remain identical to the `1200CDT` release
and original `88fb87b` application build. The final checksum inventory SHA-256
is `c8c05e962e115970f4465ae32fa6602cc97bb3d6b8770ccb76b70e4fa8bc1384`.
No application/SDK rebuild, smaller-runner retest or Actions artifact/cache
storage was used. Earlier experiments and failure records remain preserved;
their descriptions point to the corrected packages. These delivery checks do
not grant full application or hardware certification, and no regular Latest
release has qualified. Native Arch/Gentoo frontend checks cover x86-64; ARM64
recipes share the payloads tested by the Debian/Ubuntu matrix and local recipe
fixtures, without a separate native ARM64 Portage/makepkg claim.

## Signed flat APT release assets — 23 September 2026

Schema-3 application releases include four Debian packages, a flat package
index, signed release metadata, the public key and a Deb822 source file.
Generated packages, indexes and keys remain in GitHub Releases, outside Git.
Both GUI backends coexist under separate `/opt/datapump/` directories. Package
payload bytes and executable modes are verified against the portable archives;
APT uses a pinned signing key and immutable versioned package URLs even when
the index is read through `releases/latest/download/`.

Focused coverage passes **7 APT**, **70 release** and **43 certification**
helper tests. It includes real GPG signatures and an isolated local
`apt-get update`/download after the mocked Latest route moves, modified payload
and signature rejection, restrictive signing umasks, directory permissions,
both architectures/backends, packaging/application source identity and bounded
GitHub error diagnostics with credential redaction. The entire build-tool
group passes **13/13 in 5.20 seconds**. Workflow lint and whitespace checks pass.

The first live packaging attempt passed its helper tests and package
construction, then failed while trying to tag the older application commit.
Repackaged releases now tag the current packaging revision and preserve the
original `source_sha`, tag, inventory hash and all six archive byte streams.
Certification validates the packaging tag and uses `source_sha` for application
tests. No personal token or SDK rebuild is required for this path.

The [live release and installation run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35888336578)
passes from packaging commit `53c54c3964c532efeb7d25a609f71689c60688cc`.
It published [`v001_00-2026-09-23-1123CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1123CDT)
as **experiment**, with all 22 assets. Its six portable archives are byte-for-byte
copies of finalized draft `v001_00-2026-09-23-1033CDT`, built from application
source `88fb87bc491c5a0a487b68894ae6890239793e6b`. The final checksum inventory SHA-256
is `4ff3d58260197ef62729cfa90756ab8e57d80bc64d78bad8f42b666264db799a`.

| Job | Runner | Whole job |
| --- | --- | ---: |
| Sign, package and publish | `ubuntu-latest-h` | 44 seconds |
| Bookworm AMD64, both backends | `ubuntu-latest-h` | 74 seconds |
| Bookworm ARM64, both backends | `ubuntu-24.04-arm-h` | 86 seconds |

Both clean Bookworm containers successfully fetched the public signed GitHub
index with `apt-get update`, installed both backends and their declared runtime
dependencies through APT, verified every installed payload, and passed CLI
version/GUI self-checks. Only inspection/display harness tools were installed
before the application packages. All application build jobs were skipped;
no SDK/base was rebuilt and no smaller runner was retested. The times exclude
queueing. These packaging checks do not replace full source/binary certification
or qualify physical audio/graphics hardware. The older full certification run
`35878492914` is complete with its recorded failures; all 20 published Linux
distribution checks passed. No regular Latest release exists yet.

APT signing uses the dedicated primary fingerprint
`8C3DD4A727C83B93374C993B1F94BC4CEC2DF307`; private material is held in the
repository Actions secret, never in Git or release assets. At this stage,
qualified regular schema-3 releases could become Latest; new releases now
now require schema 5 and the distribution update checks above. Experiments remain pinned to an
explicit tag, and earlier release schemas cannot remove the APT update channel.

## Larger runners and independent compilation concurrency — 23 September 2026

Manual workflows now propagate Linux x86_64, Windows x64 and, where applicable,
ARM64 runner selections into their reusable build and diagnostic jobs. The
organization's new `ubuntu-24.04-arm-l` and `ubuntu-24.04-arm-h` pools use the
official Ubuntu 24.04 ARM64 image with 8 and 32 CPUs respectively. ARM64 defaults
to L. Container/SDK ABI baselines remain unchanged. Compilation uses the selected
host's available cores independently of the existing test-concurrency limits;
Windows enables bounded MultiToolTask compilation across projects.

Focused ARM-only checks passed on source `5b33ec8`: [L](https://github.com/mirage335-colossus/pumpModem/actions/runs/35881699198)
reported aarch64, 8 CPUs and about 32 GB RAM, with a **25-second** whole job;
[H](https://github.com/mirage335-colossus/pumpModem/actions/runs/35881705867)
reported aarch64, 32 CPUs and about 128 GB RAM, with a **28-second** whole job.
Both compiled and ran the existing Legacy fixture inside Ubuntu 22.04. These
small jobs verify routing/compiler access, not a general speedup benchmark or
release certification. No smaller ARM pool was retested.

The x86_64 capacity checks passed on M/L/H Linux pools. Their Windows jobs
immediately exposed an image mismatch: the larger Windows hosts supply VS2026,
while the workflow requested VS2022. The new selector uses the installed v143
compiler under the matching IDE, retaining the existing Windows base identity
`1be51afd94ed0cb4555a`. Certification uses current runner discovery while keeping
the published source's dependency helper/recipe. No Windows base is rebuilt
implicitly. The local build group passed **12/12 in 5.52 seconds**, including
**40 wrapper cases** for independent compilation/test limits. The unchanged
Windows base helper passed **22/22**; workflow lint and whitespace checks passed.

The [larger Windows follow-up](https://github.com/mirage335-colossus/pumpModem/actions/runs/35882155071)
passed on `88fb87b`: the toolchain fixture selected VS2026 with
`v143,version=14.44.35207,host=x64` and linker `14.44.35228.0`, then compiled
the actual Rev style modules and passed their regression in **0.07 seconds**.
The whole Windows H job took **40 seconds**. It used all 32 visible CPUs for
compilation without raising the serial test limit.

The [all-H six-package verification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35882420441)
passed on the same `88fb87b` source. All six build jobs ran on the selected
32-CPU architecture-specific H hosts; only helper/metadata/finalization jobs
used standard runners. It left `v001_00-2026-09-23-1033CDT` as an **experiment
draft**, containing six archives, metadata, notes, `warning.log` and checksums.
Archive relocation, dependency/ABI checks and the complete inventory gate passed.

| Build job | Whole job | Windows base restore, when applicable |
| --- | ---: | ---: |
| Linux x86_64 FLTK | 2m15s | — |
| Linux x86_64 Rev | 2m35s | — |
| Linux ARM64 FLTK | 3m30s | — |
| Linux ARM64 Rev | 5m03s | — |
| Windows x64 FLTK | 2m15s | 15s |
| Windows x64 Rev | 2m32s | 20s |

The preceding standard-runner publication's Windows jobs took 5m43s/6m49s.
These observed job timings include setup and package work and are not a
controlled benchmark or a guarantee. The Linux x86_64 SDK and Windows dependency
base were reused; cold dependency production was deliberately not repeated. This
package verification is separate from full source/binary certification.

An earlier full certification of source `e72906d`, [run 35878492914](https://github.com/mirage335-colossus/pumpModem/actions/runs/35878492914),
was already running on standard hosts before the larger-runner changes. Its
Windows Rev native probe lacks the required WGL extensions. Windows FLTK's
adapter fixture reports "Layout lifecycle fixture lost native controls" after
its full GUI workflow passed. ARM64 Rev passed all **37 shared GUI**, **5 native
GUI** and **4 packaging** tests, then the CLI differential-estimate subprocess
exceeded its 30-second deadline. These are separate environment, assertion and
timeout failures; none is the advisory Rev cadence condition. They remain
recorded failures, and this runner work does not waive them or certify that
release. Logs are preserved under `build/ci-diagnostics-20260923/backend-runs/`.

## Backend-specific releases and advisory Rev cadence — 23 September 2026

New release metadata requires separate FLTK and Rev archives for Linux x86_64,
Linux aarch64 and Windows x64. Download names and CPack extraction roots identify
the backend. The release helper checks both generated archive formats, embedded
GUI provenance and the complete six-target inventory; certification requires
those same six identities. Existing schema-1 releases remain readable and retain
their original FLTK assets. The SDK recipe remains unchanged; both x86_64
backends use the existing `base` SDK. Native Ubuntu 22.04 Rev builds use signed,
pinned LLVM 19 and a checksum-pinned Ninja 1.13.2 bootstrap.

The maintainer has explicitly classified Rev replay/waterfall display cadence
as advisory. The earlier 13-frame/0.898309-progress and 6-frame failures below
were display-cadence misses. They now emit `WARNING REV_REPLAY_CADENCE` with
measured elapsed time, frames, changes, fraction and phase. Required physical
observations, pending presentation, chronology, source/bitmap consistency,
content and completion checks remain hard failures. FLTK retains its cadence
check. This is a diagnostic policy change, not a rendering-performance fix.
Every new release includes a checksummed `warning.log` describing the known
Rev limitation; it does not claim that publication measured its framerate.
Successful portable smoke checks preserve observed warnings in CI logs, and
certification reports link the warning notice without failing on cadence alone.

Focused helper coverage passes **58 release**, **35 certification**, **27 SDK
storage** and **4 native Rev toolchain** tests. The deterministic replay fixture
covers both recorded misses, healthy/boundary cases and mandatory correctness
failures even when cadence also misses. It passes in **0.01 seconds**; actual
FLTK/native and Rev/SDK source compilation probes and GUI boundary regressions
also pass. Packaging fixtures cover both backend roots, missing pairs, warning
propagation from stdout/stderr, and nonzero GUI exits remaining fatal. The
small upstream Ninja archive's SHA-256 matches the existing SDK source pin;
installer tests mock apt rather than changing the developer host.

Full local follow-up uses the existing SDK through `./build.sh test GROUP
--stop-on-failure --sdk "$PWD/build/sdk-qualified-local" --backend fltk
--build-dir build/sdk-local-fltk --jobs 2 -- -DDATAPUMP_PORTABLE=ON`:

- `gui`: **35/35**, **112.74 seconds**, with native display tests disabled.
- `build`: **11/11**, **6.78 seconds**.
- `packaging`: **3/3**, **39.96 seconds**, including real relocated application
  checks and the SDK-compiler fixture.

Logs are under `build/ci-diagnostics-20260923/backend-full-*.log`. Workflow lint
and whitespace checks pass. The [six-package publication attempt](https://github.com/mirage335-colossus/pumpModem/actions/runs/35865999118)
used source `c4309fc191539f748b3d3b0b1b1a2c636924ec11`. All four Linux
backend/architecture packages and Windows FLTK passed. Windows Rev failed to
compile read-only style equality operators because MSVC rejects their ambiguous
C++20 reversed candidates. The release correctly remained an incomplete draft;
certification was not dispatched for it. This is a separate compilation issue,
not a cadence warning. Windows dependency preparation took **5m57s (FLTK)**
and **5m58s (Rev)**; FLTK application building and packaging took **5m48s**.
The Windows SDK itself was already installed on the runner.

The follow-up adds a shared Windows dependency base: a relocatable static vcpkg
export containing OpenSSL, GLEW and FreeType, plus the pinned vcpkg source and
download inventory. Consumer workflows verify the exact recipe and checksums,
check that the runner's linker is at least as new as the producer's, then reuse
the export without rebuilding dependencies. Microsoft compiler/SDK files are
not included. **22 focused helper tests** pass, including Windows-safe imports,
raw-export layout, corruption/traversal, immutable publication, authentication
errors and linker compatibility. The full local build group passes **12/12** in
**6.63 seconds**; workflow lint and whitespace checks pass.

The MSVC failure is corrected by const-qualifying four read-only Rev style
equality operators, leaving their comparison bodies unchanged. A dependency-free
probe compiling the four actual production modules reproduces the original
ambiguity under strict Clang diagnostics and passes after the change. Its
regression checks mutable/const symmetry, values/units, color alpha, notification
and transition metadata, and layout-versus-paint decisions. The case is also
retained in the normal Rev GUI group. The focused Windows diagnostic runs the
small event probe before dependency download or application compilation; ordinary
publication leaves those regressions to diagnosis/certification after the fix
is established.

[Windows base production](https://github.com/mirage335-colossus/pumpModem/actions/runs/35868536506)
passes on source `ba80ff6e300ab0fe885067206d56be54b9f2428f`. Recipe
`1be51afd94ed0cb4555a` retains a **57,012,854-byte** compiled export and
**549,046,640-byte** source/download archive plus their checksum inventory in
`base`. The cold dependency build/export took **6m17s**, relocation **4s**, and
the actual static OpenSSL/GLEW/FreeType compile/link/run probe **12s**. The Windows
job completed in **7m24s** including upload. All 22 helper tests also pass on
Windows. A preceding fast attempt caught ZIP fixture separator normalization
before compilation; the literal-path regression and original-name validation
were corrected without weakening the unsafe-path assertion. Git attributes keep
the recipe identity identical under Windows checkout line-ending conversion.

Static Rev packaging now explicitly includes the GLEW/FreeType vcpkg notices,
which a DLL dependency scan cannot discover for statically linked libraries.
The focused packaging fixture verifies their exact contents after relocation and
that CLI/FLTK packages do not select unused Rev notices. Existing archive,
corruption and timeout checks pass. Full SDK packaging follows up with **3/3**
in **37.95 seconds**.

[Windows base-only reuse](https://github.com/mirage335-colossus/pumpModem/actions/runs/35869666909)
passes on source `b8c260b319428b3bc18d582d918a3edadbfc8dac`: download and verified
inventory **7s**, installation/compiler check **3s**, fresh link/run probe **17s**,
whole Windows job **44s** including checkout. The vcpkg checkout, dependency
build/export and upload steps are skipped. Both base workflows use zero Actions
artifacts/cache.

The [second six-package attempt](https://github.com/mirage335-colossus/pumpModem/actions/runs/35869672617)
uses that same source and again passes all four Linux packages and Windows FLTK.
Windows dependency reuse takes **19s (FLTK)** and **11s (Rev)**. The actual MSVC
style probe passes (25s including configure/build, 0.12s test), confirming the
first compiler correction. Rev compilation then exposes a missing Win32
`NativeWindow::pumpEvents` implementation; publication correctly remains a draft.
The next iteration uses native CI's targeted `devfast=true,
diagnostic=windows-rev` selection before another full release attempt, retaining
all required regression and exact-release certification steps afterward.

The Win32 implementation now pumps the entire thread queue without blocking,
dispatches at most 64 messages per application poll, yields after a paint, and
reports `WM_QUIT` to the application. This preserves service-window input and
prevents continuous repaint feedback from starving application progress. The
Linux pump retains its existing bounded behavior and reports its continuing
state through the same API. A dependency-free regression covers input
translation, FIFO/service delivery, close, bounded feedback, paint fairness and
quit; mutations of the yield, translation, bound and quit behavior fail their
respective assertions.

The first [focused Windows event run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35871173191)
caught a fixture failure in 0.01 seconds: a fully hidden HWND did not generate
the expected paint. No dependencies or application compilation followed that
failure. Using a small nonactivating window fixes the fixture without changing
production behavior or weakening its assertions. The corrected
[Windows diagnostic](https://github.com/mirage335-colossus/pumpModem/actions/runs/35871985005)
passes on source `1bbc8a724c32aa467d9eef94f313a359bdeeb462`: the real Win32
regression takes **0.05 seconds**, existing dependencies are reused in **21
seconds**, and actual MSVC Rev application compilation and the bounded headless
self-check pass. This establishes compilation/event behavior, not graphics
qualification.

Windows Rev certification now runs the existing real-context coordinate check
before building the full regression selection. The coordinate fixture explicitly
injects `WM_DPICHANGED` for its 1x/2x cases rather than assuming that the hosted
monitor has both DPI settings. It preserves all physical/client, caret, focus,
wheel and relayout assertions. The two Linux coordinate cases pass on a private
Xvfb display; actual Windows graphics qualification remains a later check.

After these changes the complete local Rev shared GUI group passes **37/37**
in **120.56 seconds**, using `CC=clang-19 CXX=clang++-19 ./build.sh test gui
--backend rev --build-dir build/rev --jobs 2 -- -DDATAPUMP_TEST_NATIVE_GUI=OFF`.
The log is `build/ci-diagnostics-20260923/backend-final-rev-gui.log`; the separate
native coordinate checks above provide display coverage for that fixture.

The [final six-package publication](https://github.com/mirage335-colossus/pumpModem/actions/runs/35872706235)
passes on source `81d5aaab0ed93b48e979db581de116959ec37ada`. The published
[v001_00-2026-09-23-0914CDT release](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-0914CDT)
has the exact title `experiment`, remains a prerelease, and contains all six
backend/architecture archives plus metadata, notes, `warning.log` and checksums.
Publication completes in about seven minutes. Windows dependency setup takes
**13 seconds (FLTK)** and **14 seconds (Rev)**; entire Windows jobs take **6m01s**
and **4m48s**, respectively. Neither Windows nor Linux rebuilds its saved base.
The separate [full certification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35873643774)
is dispatched with `devfast=false` against that release's source and binary
hashes; publication itself is not a certification claim.

The first certification attempt finds additional failures before long tests:
Windows Rev's real-context check reports unavailable required WGL extensions in
**0.28 seconds**; ARM64 Rev on Ubuntu 24.04 cannot choose a GLX visual. Neither
is a display-cadence warning. A separate ARM64/Trixie job fails while downloading
its asset before testing. The detailed graphics implementation and loader cause
require focused inspection of the unchanged published binaries.

The SDK/GCC 15 Rev source build exposes a module-import failure in the new style
fixture's implicit `DirtyFlag` constructor. The four-module reproducer fails
before the correction and passes after explicit aggregate initialization with
`DirtyFlag dirty{}`. GCC 15.3 and strict Clang 19 both pass the unchanged
assertions. No production module or SDK changes are needed; the original
certification source/run retains its recorded failure.

The [bounded graphics diagnostic](https://github.com/mirage335-colossus/pumpModem/actions/runs/35874888207)
identifies both environments without rebuilding the app or SDK. Windows reports
**Microsoft Corporation / GDI Generic / OpenGL 1.1.0**, with both required WGL
ARB entrypoints absent. ARM64's loader reports that bundled `libstdc++.so.6`
lacks **GLIBCXX_3.4.32**, required by Ubuntu 24.04's host `libLLVM.so.20.1`.
The host itself supplies working software GLX; inheriting the older application's
C++ runtime prevents that driver from loading.

The packaging correction extends static GNU C++ runtime linking to portable
Linux Clang builds selecting libstdc++, and hides only the selected C++/unwind
archive exports so host drivers bind to their own runtime. Existing GNU behavior,
nonportable/sanitizer configurations, libc++ selection, runtime dependency checks
and ABI ceilings remain intact. Static license notices are retained. A small
before/after packaging fixture reproduces inherited bundled-runtime lookup and
then verifies a separately loaded C++ plugin uses the host runtime. Removing
archive-symbol hiding fails its RTTI isolation assertion. Native Clang 19,
native GCC 14 and SDK GCC 15 fixtures pass. The actual Clang Rev packaging group
passes **4/4** in **71.92 seconds**, including relocation; its bundle contains
neither `libstdc++.so` nor `libgcc_s.so` and retains both runtime notices.

Remaining long certification jobs for the known-bad candidate were cancelled
after diagnosis. The automatic reporting job attached the
[failed attempt report](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-23-0914CDT/certification-35873643774-attempt-1.md)
without changing any binary or original checksum. Copied-binary checks finish
**17 passed / 3 failed**: the ARM64 GLX failure, the ARM64/Trixie asset download,
and a separate x86_64 Rev/Ubuntu 26.04 phase-21 text-reception assertion. Its
earlier cadence warning does not cause that semantic failure. All ten FLTK
compatibility jobs pass. Partial source checks include both FLTK GUI groups
**36/36**, ARM64 Rev GUI **37/37**, and ARM64 FLTK native/packaging **3/3** each.
Windows FLTK completes 24 GUI cases before cancellation. No completed full
source contract/calibration or release certification is claimed for this attempt.

Focused inspection of the phase-21 failure finds that the 17 received bits are
exactly the suffix of the intended 32-bit `Help` payload, after a 15-bit loss;
the editor's pre-transmission assertion had verified all 32 bits. A small keyed
Session probe against the current libraries receives 32/32 with immediate,
frozen-epoch and warmed starts. This does not establish a deterministic cause or
justify adding a readiness delay. The relevant receiver/smoke paths are unchanged;
the semantic qualification failure remains unresolved, separate from cadence.

After the fixture and runtime-packaging corrections, the complete local Rev
shared GUI group passes **37/37** with the existing **SDK GCC 15** in **114.34
seconds**, and **37/37** with **native Clang 19, portable runtime linking** in
**119.14 seconds**. Logs are
`build/ci-diagnostics-20260923/backend-final-sdk-rev-gui.log` and
`build/ci-diagnostics-20260923/backend-final-static-clang-rev-gui.log`.

The [focused ARM64 packaging and graphics run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35876268016)
passes on source `e72906dc431192372b1b81562cc7cd9112d113c5`. It builds the Rev
package on Ubuntu 22.04 and verifies both TGZ and ZIP archives, including
relocation, dependency closure and all **35 packaged ELF files** against the
**glibc 2.35** ceiling. On Ubuntu 24.04, the unchanged copied GUI runs
`--simulation` with empty `PATH`/`LD_LIBRARY_PATH` under private Xvfb. Its exact
`Data Pump` window remains visible for **2.05 seconds** while the process stays
alive. Process mappings confirm host **Mesa 25.2.8**, **LLVM 20.1** and
**libstdc++.so.6.0.33**, all outside the package; no C++ runtime DSOs are bundled.
Strict package inventory verification passes both before and after startup.
The log retains an ALSA missing-configuration warning; this simulation startup
does not qualify audio devices. This is focused packaging and host-driver
startup evidence, not full GUI smoke, calibration or release certification.
No existing release assets change. The completed log records checkout in
**2.26 seconds**, with no fetch retry, and the package-build step starting at
**14:43:35 UTC**; earlier live status did not establish a checkout delay. The
hosted log is `build/ci-diagnostics-20260923/backend-runs/107232881295.log`.

The [corrected six-package publication](https://github.com/mirage335-colossus/pumpModem/actions/runs/35877402140)
passes on `e72906dc431192372b1b81562cc7cd9112d113c5` and publishes
[v001_00-2026-09-23-0952CDT](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-0952CDT)
with all six archives and the original metadata/notes/warning/checksum assets.
It retains the exact title `experiment`, prerelease status and no Latest
promotion. Windows restores the existing dependency recipe in approximately
**13 seconds (FLTK)** and **25 seconds (Rev)**; the complete Windows jobs take
**5m43s** and **6m49s**. Application compilation and packaging account for the
remaining time; neither dependency base is rebuilt.

Separate [full certification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35878492914)
is dispatched with `devfast=false` for the new tag. The workflow pins its source
and inventory and attaches its own immutable result report without replacing
binaries. This entry records dispatch, not a pass: follow that run and its
attached report for the result. The known hosted Windows OpenGL limitation and
prior semantic acquisition failure are not waived.

## Full validation after focused diagnosis — 23 September 2026

Agent and development guidance now requires focused diagnosis followed by full
applicable validation once a candidate is complete. A fast pass cannot complete
a runtime change. Passing evidence for the same source/configuration is reused;
manual full runs after `[skip ci]` avoid duplicate automatic runs. Documentation
changes receive proportionate checks without discarding outstanding code
validation. Release certification remains tied to the actual published source
and binary hashes.

Full validation uses `129f064ba8db6b777bc80bcb248f965f69998e66`, including
the Legacy cancellation fix and alignment of full-CI time budgets and dependency
inspection with the existing release checks. Local
`cmake -DBINARY_DIR="$PWD/build/ci-timeout-fixture" -P tests/packaging_support.cmake`
passes real installation/relocation, tamper and unlisted-file rejection, ELF ABI
rejection, ALSA SONAME and smoke-timeout forwarding/bounds checks. Configure-only
probes verify the default GUI smoke/test limits of 300/330 seconds, configured
600/630 seconds, and rejection of 601 seconds. Actionlint accepts the changed
native/SDK workflows; whitespace checks pass. These checks do not stand in for
application regression coverage.

The full [native CI run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35852524828)
and [SDK qualification run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35852528862)
use `devfast=false` on `129f064ba8db6b777bc80bcb248f965f69998e66`.
Saved logs and run snapshots are under
`build/ci-diagnostics-20260923/full-runs`. Completed evidence is:

| Scope | Recorded result |
| --- | --- |
| Native Linux Release | **Passed:** **122/122** source tests in **2,191.07 seconds**, including numerical calibration in **1,327.83 seconds**. Desktop workflow, CLI regressions, relocation and both archive formats also pass. |
| Native Linux Debug with ASan/UBSan | **Failed:** **115/120** pass in **5,996.03 seconds**. `fast_session`, `gui_fast_live`, `live` and `live_profiles` fail; `differential_receiver_probability` reaches its **3,600.11-second** timeout. Later desktop/CLI/archive steps are skipped. |
| Native FLTK GUI job | **Passed:** build checks **10/10**, GUI checks **37/37**. |
| Native Rev GUI job | **Failed:** GUI checks **38/39**; `gui_workflow` phase 21 reports replay fraction **0.898309**, with 13 observed frames over 3.014539 seconds. |
| Native Windows source suite | **Failed:** **117/119** pass in **2,973.90 seconds**. `fast_low_rate` and `pattern_code` fail. Both Legacy cancellation regressions pass; numerical calibration passes in **1,501.85 seconds**. Later Windows archive/relocation steps are skipped. |
| Native copied-distribution matrix | **Skipped** because the prerequisite Linux Debug job fails. |
| SDK FLTK job | Both copied TGZ/ZIP archives pass verification including GUI smoke; build checks pass **10/10**. Packaging checks pass **2/3**, with `packaging_support` failing on the synthetic ALSA fixture; real native relocation passes. Later contract coverage does not run. |
| SDK Rev job | **Failed:** copied-archive GUI smoke phase 13 observes only **6 frames** over 2.701923 seconds. Later regression coverage does not run. |
| SDK distribution matrix | **Skipped** because the prerequisite SDK jobs fail. |

Commit `ec93a9932e4e66ecdfd84129478667160a3bf769` fixes only the synthetic
ALSA fixture's unused C++ runtime dependencies. Focused packaging checks then
pass with both native and SDK compilers, including relocation, corruption and
smoke-timeout checks. Application/runtime sources and the SDK recipe remain
unchanged. This local correction does not change the original SDK run's failed
status. The supplemental full SDK preservation contract passes **30/30** in
**1,643.99 seconds**, including unchanged numerical calibration in **1,289.66
seconds**. It uses:

```sh
./build.sh test contract --stop-on-failure \
  --sdk "$PWD/build/sdk-qualified-local" --backend fltk \
  --build-dir build/sdk-local-fltk --jobs 2 -- \
  -DDATAPUMP_PORTABLE=ON -DDART_TESTING_TIMEOUT=3600
```

The log is
`build/ci-diagnostics-20260923/sdk-full-contract.log`.
The same wrapper's full `packaging` group then passes **3/3** in **40.36
seconds**, including the fixed fixture, real native relocation and SDK isolation.
Its log is `build/ci-diagnostics-20260923/sdk-full-packaging.log`.

Both hosted workflows finish with **failure**. Windows exposes a
`fast_low_rate` failure; a scratch
probe reproduces the failure with the exact MSVC Gaussian sampling algorithm.
The current Windows run also fails `pattern_code` at its unchanged waveform
chunking tolerance. A scratch double-precision probe reproduces that assertion
without changing the tolerance; its proposed runtime correction is not applied.
The Windows, Rev replay and Debug failures remain unresolved. No assertion,
internal deadline or production runtime behavior was changed to make these
results pass. Scratch candidate changes have not been incorporated into the
application. This is a completed validation attempt, not a full passing
qualification.

The observed Debug `live` timeout and `live_profiles` queue-overrun messages
come from explicit regression assertions, not sanitizer diagnostics themselves.
The generic `live` wait needs stage-specific evidence to identify the stalled
predicate. The `live_profiles` synthetic source delivers about 20 ms of PCM
per 1 ms sleep; its assertion detects an actual bounded-queue discard and
reacquisition. Instrumentation cost or test contention may affect throughput,
but the cause is not established. Focused follow-up should isolate the failing
stage/case and measure progress/queue occupancy while preserving deadlines,
queue limits and assertions. Debug `fast_session` reports capture overrun and
missing physical end; `gui_fast_live` reports changed pending identity or exposed
completed content. These failures also remain open; the passing Release run does
not substitute for their sanitizer configuration. The calibration timeout leaves
that configuration's numerical coverage incomplete, even though the Release,
Windows and supplemental SDK calibration runs pass.

The existing `v001_00-2026-09-23-0453CDT` experiment remains bound to source
`17199557113ea52b44e72ba40724f7a20c3a9dee`, with its failed certification report
and all six original assets unchanged. It does not contain the later controller
fix. Qualifying the fixed release requires a new publication followed by full
certification of that new tag; source-CI success alone does not certify its
published binaries.

## Focused Legacy cancellation diagnosis and devfast — 23 September 2026

The Windows certification timeout exposed a reproducible Legacy controller race.
The audio worker may finish between `observe()` and the next `Session::active()`
query. Restarting capture immediately then hides the inactive snapshot that
clears cancellation, leaving a listening controller stuck on “Cancelling…”.
The same interleaving could discard the worker's final error. The fix observes
that final state after confirming closure and before starting another session.
No wire format, audio timing, test deadline or physical-completion rule changes.

`gui_legacy_poll` scripts this legal interleaving without sleeps, clocks or
production test hooks. It fails immediately before the fix and passes afterward,
checking resumed capture, Transmit/status presentation, retained draft, final
error visibility and closure. An independently delayed real-worker interleaving
also changes from a 10.408-second timeout to a 0.500-second pass. The existing
live fixture retains every assertion, sleep and ten-second per-stage deadline.
Both tests are included in the ordinary GUI and Legacy groups.

The manual `devfast` checkbox defaults to **false** in native CI, SDK qualification
and certification. When checked, each calls one read-only Linux/Windows workflow
that compiles only the affected controller, modem/session and fixtures. Common
sources compile once per platform. It uses available cores for compilation and
requires three serial passes of each test, stopping on the first failure. It
does not configure OpenSSL/FLTK, build SDKs or applications, run calibration or
the general matrix, upload packages, or issue/promote certification. Diagnostics
use the selected branch SHA; full certification still pins the published source
and hashes. Full job selections remain unchanged with `devfast` off.

The hosted [before-fix reproducer](https://github.com/mirage335-colossus/pumpModem/actions/runs/35850779905)
uses source `240a5d17e7df86c3abbe489610815539665c8206`. Both platforms build
successfully and fail `gui_legacy_poll` with “Resumed capture retained stale
cancellation state”. The Windows job takes **52 seconds** total and its actual
regression fails in **0.01 seconds**; Linux takes **24 seconds**.

The [fixed diagnostic run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35850989064)
uses source `adde430f8cbb1aa7600fbe2d5862a0a4c7c1b0b7` and passes on both
platforms in **1m10s** overall. Windows takes **61 seconds**, including its six
test executions in **14.97 seconds**; the three unchanged live cases take
4.95/5.00/4.97 seconds. Linux takes **29 seconds**. Preparation, full source
suites, compatibility and certification reporting are visibly skipped, and
GitHub reports **zero Actions artifacts**. The signed-in GitHub browser also
confirms Success and the checked-out source SHA.

After focused proof, `./build.sh test gui --stop-on-failure --build-dir
build/audio-portable-validation --jobs 2 --cli` passes **33/33** in **104.48
seconds**. The same wrapper's `legacy` group passes **8/8** in **2.14 seconds**.
Workflow lint and whitespace checks pass. These affected integration groups
include the shared controller/application boundaries. At that focused stage,
the regular modem's slow calibration and native adapter suites had not yet been
rerun. The later full validation above records their follow-up; release
certification remains a separate qualification step.

Redundant broad runs were stopped while this focused diagnosis proceeded.
The previously published release's [certification attempt](https://github.com/mirage335-colossus/pumpModem/actions/runs/35846897564)
finishes cancelled; its always-run reporting job adds a **failed** JSON/Markdown
report recording compatibility success, Linux cancellation and Windows failure.
All six original release assets, their hashes, source and tag remain unchanged.
The release stays `experiment` and a prerelease. This source fix is in the PR;
it is not retroactively included in those already published binaries.

## Separate publication, durable SDK and shared audio — 23 September 2026

Publication now builds and verifies packages before releasing three binaries;
extensive source/GUI/copied-binary qualification is a separately dispatched
workflow. Certification pins the source commit and published checksum inventory,
records exact binary hashes and required job outcomes, and attaches immutable
JSON/Markdown reports. SDK archives and preserved sources live once per recipe
in the `base` release. The release, certification and SDK maintenance paths have
no Actions artifact/cache dependency. Cold SDK compilation uses available CPUs
through the existing SDK helper, leaving recipe `b8685ab239d7ac8650e6` unchanged.

Local validation passes **43** release-helper, **27** SDK-storage and **23** certification tests and
all **10/10** build suites. Packaging validation passes **3/3** suites, including
a new sentinel proving that `dlopen("libasound.so.2")` resolves to the packaged
library; the sentinel fails before the missing alias fix. Audio routing,
Windows audio stubs, rate conversion and resampling pass **4/4** focused suites.
The application build also passes. Workflow lint and whitespace checks pass.
The qualified 668 MiB SDK/source pair passes outer hashes, source replay hashes,
recipe identity and binary manifest verification without rebuilding it.

Linux audio now retains the requested endpoint if opening/configuring it fails;
it no longer silently substitutes an exclusive card endpoint for `default`.
Tests preserve rate/channel negotiation and exact PCM assertions while checking
busy/shared endpoints and independent capture handles. Relocated ALSA discovers
architecture-matched host plugins when no user override/private plugin directory
is present. A probe with the released SDK library and a synthetic PulseAudio
endpoint confirms automatic host-plugin discovery; an explicit invalid plugin
path remains authoritative. No physical audio devices were exercised, so this
is not a hardware or desktop-session concurrency certification. No application
singleton restriction was found.

The first GitHub split-pipeline rehearsal found two CLI/API integration issues:
`gh release download` saw an empty embedded asset list even though the dedicated
assets endpoint had the complete uploads, and `gh release view` did not support
the requested `databaseId` field. Helpers now enumerate paginated REST release
and asset inventories and stream downloads by asset ID, verifying server digests,
recorded hashes and sizes. Regression fixtures cover empty embedded inventories
and multi-page asset responses. Its incomplete draft is retained for inspection.

The corrected [durable SDK reuse run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35845549918)
passes on source `17199557113ea52b44e72ba40724f7a20c3a9dee`. Its SDK job takes
**64 seconds**, skips every cold-build step, verifies the archived SDK after
relocation with the glibc 2.36 host ceiling, and reports `published=false`,
`reused=true`. Automatic cold-build parallelism resolves to **4 jobs** on this
runner. Recipe `b8685ab239d7ac8650e6` remains unchanged. The durable `base` release
contains the 244,502,346-byte compiled archive, 455,866,711-byte source archive
and matching per-recipe checksum file; all three server digests match the
qualified local seed. A fresh binary-only download also passes local validation.

The corrected [publication run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35845553697)
passes on that same exact source and publishes
[`v001_00-2026-09-23-0453CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-0453CDT)
as `experiment`, prerelease and never Latest. Total workflow elapsed time is
**11m50s**. Both Linux builds, the Windows build, archive relocation/ABI checks,
direct uploads and final inventory verification succeed before publication.
GitHub reports **zero Actions artifacts** for both this publication and the
successful base maintenance run. Independent local downloads verify all six assets: three app archives,
metadata, notes and checksum inventory. The x86_64/ARM64/Windows downloads are
15,724,956 / 14,506,413 / 6,502,297 bytes. The published x86_64 bundle contains
both `libasound.so.2` and `libasound.so.2.0.0`.

A local integration probe runs **two copies of this published Linux GUI**
concurrently under a private Xvfb display. Both show “Listening for Fast modem
training” and live receive plots at 48.0 kHz. Each uses the built-in ALSA file
plugin over a null PCM, fed synthetic zero-valued mono PCM through a separate
paced FIFO (960 frames every 20 ms). Both load the bundle's `libasound.so.2`;
neither opens `/dev/snd`. Two mapped native windows have distinct process IDs.
All probe children are stopped afterward. This establishes concurrent GUI and
synthetic capture operation, not physical-device sharing. Local evidence is in
`build/release-evidence-split/multi-instance-paced-probe/report.json` and
`both-windows.png`.

The separate [certification run](https://github.com/mirage335-colossus/pumpModem/actions/runs/35846897564)
starts only after publication, pins the release source and checksum inventory,
and runs the unchanged source/GUI/calibration groups plus focused audio and
copied-archive checks. All **10** published-Linux compatibility jobs pass.
The Windows job builds successfully but `gui_legacy_live` fails after 14.89
seconds with its existing generic timeout. Later Windows tests and its published
archive qualification are consequently skipped; this attempt cannot grant a
passing certification. The binaries remain available as an experimental release.

That fixture and its controller/session sources are identical to the previously
passing release. Eight local diagnostic trials with every fixture sleep rounded
up to 16 ms still pass in approximately 5.1 seconds, so coarse timer resolution
alone does not reproduce this failure. The test now names each wait stage and
reports synchronized sample counters and controller state on timeout. Its
assertions, sleeps and ten-second per-stage deadlines remain unchanged. This
diagnostic-only change is for later source revisions; the published release and
its source-pinned certification are not modified. The final local shared GUI
selection passes **32/32** in **104.16 seconds** with `./build.sh test gui --cli`
in the native audio-validation tree. An initial link failure came from stale
cached static OpenSSL paths in that non-portable tree; clearing those library
cache entries restores its configured shared-OpenSSL selection, with no source
change. A certification-helper
regression also exercises a failed first attempt followed by a successful retry,
retaining both reports, their history and the same binary/source identities.
Experimental retries remain prereleases and never Latest.

The earlier artifact-only runs remain historical evidence of the previous
combined build/test/publication workflow.

## Manual portable release qualification — 23 September 2026 UTC

The manual release workflow builds three application bundles: Linux x86_64,
Linux aarch64 and Windows x64, each with the FLTK frontend and CLI. The default
x86_64 builder uses the existing Bookworm source SDK; the selectable Ubuntu
22.04 baseline and the ARM64 builder use glibc 2.35. Windows uses the Windows
SDK and static MSVC/OpenSSL runtimes. [Release instructions](releases.md)
describe version/date labels, the experimental prerelease checkbox, the
artifact-only default and the platform limits.

Both final artifact-only rehearsals pass after the fixture corrections:
[Ubuntu baseline](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814958363)
and [source SDK](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814967358).
The SDK dispatch uses source `0e98b72242c49793114cfb134d4930f955b374ca`.
Its **10** copied-distribution jobs and the native rehearsal's **11** jobs
all pass, followed by successful assembly. The SDK inventory includes three
application archives and the matching compiled SDK/source archive pair.
The combined inventory also verifies after downloading with GitHub CLI:
all file hashes, the `v001_00-2026-09-22-2242CDT` label, exact `experiment`
title, source SHA and matching SDK/source recipe identifiers pass.

Local validation passes all **26** release-helper tests, **37** wrapper tests,
**8/8** build suites, **33/33** shared GUI suites and **3/3** packaging suites.
Actionlint accepts the release and shared SDK workflows. The complete local
preservation-contract selection passes **30/30** after the floating-point
compiler change, including calibration in **1,100.60 seconds**; total elapsed
time is **1,395.03 seconds**. A final packaging run passes in **36.65 seconds**.
Both locally SDK-built archive formats pass copied-directory CLI/GUI smoke,
relocation and glibc 2.36 audits.

The complete [Ubuntu-baseline GitHub rehearsal](https://github.com/mirage335-colossus/pumpModem/actions/runs/35812235690)
passes on source `960c0cd`. Both Linux architectures pass all **74** selected
build/contract/GUI/packaging executions, including overlapping selections.
Windows passes **65** GUI/contract/packaging executions, including calibration
in **1,497.94 seconds**. Every platform verifies both archive formats and the
complete GUI smoke. All **11** copied-Linux distribution jobs pass: Debian
12/13 and Ubuntu 22.04/24.04/26.04 on both architectures, plus Arch x86_64.
The Linux audit covers 45 ELF files per bundle and enforces glibc 2.35.

Assembly produces exactly three application downloads with one metadata,
notes and checksum inventory, which also verifies after downloading with
GitHub CLI. The tag is `v001_00-2026-09-22-2154CDT`, title `experiment`, with
the exact source SHA retained. The Linux x86_64/aarch64 archives are
12,945,357/12,531,371 bytes and the Windows ZIP is 6,497,042 bytes. These are
observed sizes, not limits. Earlier rehearsals exceeded the 300-second outer
GUI allowance late in the cumulative workflow; the successful runs use the
harness's existing 600-second allowance. Frame, pending-progress,
cancellation and replay assertions retain their original limits.

The [source SDK release builder](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814967358/job/107035855339)
on `0e98b72` reuses, relocates and verifies recipe `b8685ab239d7ac8650e6`,
with host tools and target libraries both meeting glibc 2.36. All **74**
selected SDK-built executions pass, including calibration in **1,318.89
seconds**. Both copied archive formats pass complete GUI smoke and audits
covering 36 ELF files. The separate
[SDK FLTK qualification job](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814958358/job/107034690615)
also passes both archive formats and all **8/8** build, **3/3** packaging and
**30/30** contract suites. Its calibration takes **1,560.30 seconds**, exceeding
CTest's default process allowance. Release and SDK qualification explicitly
set a 3,600-second outer limit without changing samples or numerical
assertions.

Actual cross-platform failures exposed compiler and fixture assumptions.
Disabling implicit floating-point contraction preserves the exact scalar,
cached and batched receiver comparisons on ARM64; their independent reference
assertions remain unchanged. The bitmap-noise fixture now specifies the same
sampling algorithm that libstdc++ previously supplied; six million generated
values match its previous output bit for bit. UTF-8 fixture paths, GCC 11
constant-expression compatibility, ARM disassembly comments, mocked SDK host
identity and Windows vendored-source line endings are corrected without
altering transport or GUI behavior. Checksum-pinned CMake 3.31.10 fixes native
dependency inspection's inherited-RPATH handling; strict host-library
rejection and the application ABI floors remain enforced.

The [final Windows job](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814967358/job/107035657934)
passes all **65** selected executions and both copied archive GUI checks.
The profile and transmit-lock fixtures pass in **179.48/7.61 seconds**, and
calibration in **1,503.69 seconds**. The live-profile fixture requests and
restores 1 ms timer resolution for its existing 1 ms sleeps. A coarse-timer
reproduction delivers only 571,392 of
723,328 required samples by the unchanged 30-second deadline while the decoder
queue is empty. An earlier run also exposes a separate transmit-lock fixture
synchronization race after cancellation. The fixture now waits for capture
to resume before queuing its next request, preserving
the exact status, no-output checks and deadlines. A deliberately delayed
capture handoff reproduces the original failure and passes with this fix;
the complete local transmit-lock suite passes in **7.27 seconds**.

Release-helper tests cover timestamp/DST naming, exact experimental titles,
strict inventories, checksum failures, SDK/source pairing, immutable tag
creation and publication ordering. Real read-only GitHub API checks verify
missing tag/release handling and rejection of an existing ref. No release or
tag has been created by these rehearsals; public publication remains an
untested side effect until a maintainer explicitly dispatches with `publish`.

The separate general CI retains its older, insufficient GUI/job budgets; its
FLTK workflow reaches the 300-second smoke limit. The independent
[Rev SDK check](https://github.com/mirage335-colossus/pumpModem/actions/runs/35814958358/job/107034690546)
retains its replay-frame failure (nine frames in 3.139409 seconds, where ten
are required); it is not an overall process timeout and its
assertion is not weakened. Release bundles select FLTK. Hosted containers
share the runner kernel, and Windows hosted tests use Windows Server rather
than separate Windows 10/11 installations. These results do not qualify
physical audio devices, Chromebook/Pi graphics or sound drivers, Gentoo,
32-bit Raspberry Pi OS or musl installations.

## Source SDK and glibc 2.36 target — 22 September 2026 UTC

The opt-in source SDK recipe builds GCC 15.3, a maintained glibc 2.36 snapshot,
CMake and the development dependencies for both native GUI backends. Normal
application builds select the prepared SDK with `./build.sh --sdk PATH`; they
do not fetch sources or rebuild the compiler. Target library discovery, runtime
collection and license inventory use the SDK instead of the build host. Neither
GTK nor GLib is included. See [building](building.md) and
[SDK preparation and maintenance](../third_party/build-support/README.md).

Recipe `b8685ab239d7ac8650e6` was built from verified, cached sources on Debian
13, exported, installed at a different path and verified after relocation.
The compiled archive is 226.4 MiB, the preserved source archive 430.3 MiB and
the installed SDK 744 MiB; compiler/package intermediates occupy 8.9 GiB in
addition to downloads and exports. These measurements are local observations,
not size limits or a reproducibility claim.

The source archive was extracted independently of the application checkout.
Its helper/recipe identity matches, all 59 preserved downloads pass their
hashes, and all 77 resolved packages have their required sources. Unselected
cache files are absent. The pinned Buildroot/glibc archives and fresh Buildroot
overlay also verify. A second complete cold compiler build from that extracted
archive was not repeated locally; the new CI producer explicitly builds from
the source archive's offline replay before qualifying its SDK.

Both FLTK and Rev Release applications compile with the SDK's GCC 15.3. Rev
also rebuilds with the existing Clang 19 profile. The Rev compatibility changes
make its negative-zero sentinel constant-expression eligible, add explicit
module-local namespace/header dependencies, and name the adapter's width and
height aggregate fields. Their values and application behavior are unchanged;
[vendored provenance](../third_party/rev/README.datapump.md) records the patches.

The build group passes **7/7** with each SDK backend. The native build and
packaging groups also pass. Focused SDK fixtures exercise source/hash failures,
archive traversal and link rejection, host C++ runtime resolution, target ABI
ceilings, runtime/plugin loading and rejection of libraries outside the SDK.
The optional archive GUI deadline fixture checks defaults, bounds, the actual
GUI command/process allowance and forwarding through both archive formats.
CLI/self-check deadlines and omitted-option GUI defaults remain unchanged.
The native packaging fixture receives the selected compiler explicitly and
verifies its configured path, so SDK consumers need no separate host compiler
to run this test. It still exercises native shared-library collection;
the separate SDK fixtures check sysroot isolation.

All **30/30** preservation-contract suites pass with the SDK compiler in one
complete run. The differential receiver calibration passes in **1,407.61
seconds** within its unchanged **1,500-second** timeout; total elapsed time is
1,507.97 seconds. Exact wire vectors, physical completion, next-poll pending
progress and recovery behavior remain intact. The test build reuses the
application's compiled libraries. CTest now accounts for the calibration's
four existing workers, so a two-job run schedules it alone; this metadata
change does not alter test code, assertions or the timeout.

All **5/5** Rev native display checks pass on a private 2400×1800, 96 DPI Xvfb
display with software OpenGL after heavy workloads finish: production workflow
275.33 seconds, adapter 94.28 seconds, platform 6.19 seconds and both coordinate
scales 5.16 seconds each. The production replay assertions are unchanged.

Both TGZ and ZIP packages for each backend pass copied-directory inventory,
checksum, isolated CLI/codec and GUI self-check verification. ELF audits cover
36 FLTK and 24 Rev files, find no GTK/GLib dependency and enforce glibc 2.36.
A CLI short-text simulation also passes using the SDK's actual glibc 2.36
loader and libraries, retaining its exact text and physical completion result.

The full copied-directory GUI workflow passes for both FLTK archive formats
with an explicit 300-second allowance, matching the existing native group.
Initial FLTK archive checks with the application's 100-second smoke default
expired during later workflow phases; that default remains unchanged. The
copied Rev tar bundle instead reproduces the previously recorded phase-13
replay timing failure: elapsed 2.979890 seconds, eight frames, seven changes,
fraction 0.915255, 1,276 dropped samples and pending count 406. This is a failed
replay assertion, not an overall smoke timeout. It is retained without a
weakened threshold or repeated attempts to obtain a pass. The copied Rev ZIP
workflow is not rerun; its headless package checks above pass. Thus the local
Rev native group passes, but complete copied-Rev GUI qualification does not.
Display checks use private Xvfb displays, not the user's desktop; sandboxed
attempts that could not bind the X socket ran no GUI assertions.

The local SDK's observed requirements are **glibc 2.38 for host tools** and
**2.36 for target libraries**. This local build is therefore not a
Bookworm-qualified SDK. The new CI workflow constructs host tools inside
Bookworm on a newer runner, audits their 2.36 ceiling, builds with the same SDK
on Bookworm and Ubuntu 24.04, and checks copied application archives on both.
That workflow has not been executed during this local validation. Kernel,
distribution and physical audio/graphics-driver qualification are not implied
by the local ABI and generated-audio checks.

Logs and generated archives remain in ignored `build/sdk-*` trees and
`third_party/build-support/cache/source-sdk/`. No SDK binaries, downloads or
compiler intermediates are added to Git.

## Received line-feed newlines — 22 September 2026 UTC

The shared received-text allowlist now permits ASCII LF (`0x0a`) in both
restricted and Shellcode views. Robust, Fast and Legacy received display,
clipboard and received-derived drafts retain newlines, including after
Shellcode permission is withdrawn. Every other previously forbidden byte
remains an underscore. CR and tab are still blocked; CRLF therefore becomes
an underscore followed by LF. Exact received bytes remain available through
explicit saving and raw-bit operations.

The filter still uses bounded, byte-by-byte comparisons with same-size output
and the existing receive-processing barrier. CLI JSON escapes the filtered
text, including LF, before emitting each record. Plain stdout/pipe text retains
actual LF. The CLI regressions include leading/trailing and embedded newlines,
all 256 byte values, exact saves and complete live reception JSON records.
GUI regressions cover fixed-interval source text, short/raw-bit interpretations,
Fast copy/paste and QR revocation, and Legacy live/all-byte presentation.

The runtime all-byte tests and speculation semantic/code-generation checks pass
with GCC and Clang. The Fast CLI suite also passes. A standalone GCC Release
filter comparison against `a00e880` validates all 256 byte values independently,
retains both barriers, and alternates nine baseline/current pairs for each input
size and view. With no competing project tests/builds, the default-view median
changes from 25.90 to 27.88 ns for 16 bytes, 4.555 to 4.751 µs for 4 KiB, and
4.907 to 5.129 ms for 1 MiB. Shellcode medians change from 24.09 to 24.20 ns,
2.896 to 3.841 µs, and 2.769 to 2.889 ms respectively. These allocation/filter/
barrier measurements use deterministic mixed-byte data, not whole-modem or
cross-platform timing. The benchmark source, samples and disassembly remain
under the ignored `build/received-newlines/` directory.

All **33/33** shared GUI cases pass with each compiler across the group runs
and focused reruns. The initial concurrent runs encounter Fast acoustic-short
capture overruns; the unchanged complete Fast live suite passes alone in
102.54 seconds with GCC and 102.79 seconds with Clang. Clang also initially
runs a binary-editor target compiled before its LF expectation was updated;
rebuilding that target passes the final all-byte test. No capture limits,
assertions or timeouts are weakened.

The initial preservation-contract run passes **29/30** cases but the unchanged
differential receiver calibration reaches its **1,500-second timeout** while
other test groups and builds overlap the run. It reports no failed numerical
assertion before the timeout. The original timeout is retained for the isolated
rerun after all other workloads finish, which passes in **1,094.18 seconds**.
All **30/30** preservation-contract cases therefore pass across the initial
run and isolated rerun, including exact wire vectors, whole-symbol completion,
next-poll pending progress and recovery behavior.

All six targeted native conformance cases pass on isolated 2400×1800, 96 DPI
Xvfb displays after the heavy workloads finish: FLTK adapter/document in
67.99/0.03 seconds, and Rev adapter/platform/1×/2× coordinates in
96.11/6.15/5.13/5.13 seconds. The production replay workflow is not rerun for
this character-policy change; its latest recorded timing result below remains
unchanged. Both applications are rebuilt. The private display helper disables
TCP and does not use the user's desktop.

## Unencrypted transmit wait and one-shot override — 22 September 2026 UTC

The shared Robust Console now offers **Force next transmission** beside
**Previous message - click to paste** during either a key-reuse lock or the
ordinary receiver-separation wait. **TX wait** also explains that it is waiting
for the receiver silence check. The ordinary hardware deadline remains
`ceil(6 / symbol_seconds) * symbol_seconds + 1` seconds after completion, so
long symbols can require minutes of silence even without encryption. No
receiver-completion rule, waveform or backend deadline changes.

The live transmit-lock regression passes with both GCC and Clang. It adds
unencrypted output with a 1,600-second symbol, consumed by deterministic audio
fixtures without waiting 1,600 seconds. Both key-reuse counters remain zero;
ordinary separation lasts about 1,601 seconds. A forced request starts promptly,
retains the prior deadline after cancellation, and does not allow the following
ordinary request through. Another case selects no transmit key while retaining
receive keys, verifies zero key lock, then restores the old transmit key and
verifies that its usage history remains protected.

All **33/33** shared GUI tests pass with GCC/FLTK (124.54 seconds) and Clang/Rev
(165.78 seconds). Updated controller coverage exercises an unencrypted
601-second wait, force visibility and readiness, empty/invalid draft rejection,
and restoration of ordinary controls when the wait expires. Existing one-shot,
busy-state and minimum/default-size placement checks remain intact. The force
tooltip explains that skipping silence can merge unencrypted messages.

All **30/30** preservation-contract suites pass, including the unchanged
receiver-probability calibration in **1,264.31 seconds**, within its original
1,500-second timeout. Exact wire bits, whole-symbol completion, next-poll
pending progress and recovery checks remain intact.

After the heavy tests and builds finish, isolated 2400×1800, 96 DPI Xvfb checks
pass for both native backends: **2/2** FLTK conformance cases (adapter 67.94
seconds, document 0.03 seconds), and **4/4** Rev conformance cases (adapter
95.15 seconds, platform 6.19 seconds, and 1×/2× coordinates 5.13 seconds each).
These are the native control/layout checks affected by this shared command and
help change. The production replay workflow is not rerun here; its most recent
result remains recorded in the CPU-estimate validation below. No replay
assertion or timeout is changed.

Both application profiles are rebuilt. Logs are retained under the ignored
`build/transmit-wait-override/` directory. The existing private-display helper
disables TCP and does not use the user's desktop. Tests use generated audio
and Linux GUI fixtures; they are not physical-link or Windows qualification.

## Robust CPU mitigation measurements and estimates — 22 September 2026 UTC

The [Robust CPU cost study](robust-cpu-costs.md) compares the same 17 workloads
against pre-mitigation `f715839` and hardened `a062386` GCC Release libraries on
one AMD Ryzen 5 PRO 5650U Linux host. Each version has two seven-sample runs in
baseline/current/current/baseline order, with at least 250 ms per sample and no
competing project build/test workload. All 476 samples preserve fixture metadata
and exact results. The [retained samples and analysis](validation-data/robust-mitigations-2026-09-22/README.md)
document artifact hashes, commands and limitations. Short timings are calculated
from sample totals divided by iterations, avoiding nanosecond-rounded quotient
error.

Complete 8,192-assignment searches take 0.42% longer with one worker and 1.26%
longer with four. Equivalent five-minute candidate coverage decreases by 0.42%
and 1.25%; these are fixture-rate extrapolations, not additional observed trials.
The 32,769-position all-missing planning case takes 3.54% longer, an additional
2.48 ms. Authenticated RS60 correction changes by 1.14–2.51% across clean,
damaged and correction-limit inputs. Full sampled FFT reception changes by
0.32%, within the observed pair variation; small negative stream/sample changes
are not treated as speedups. Every recovery fixture retains the same search
coverage, authentication and missing-bit accounting, and sampled reception must
reach physical completion before EOF.

Console and Link Planner now share an ordinary receive-processing allowance
with a separately identified mitigation subset. The one-bit Planner receives
2.455 µs, including 0.453 µs for mitigations. Framed Console drafts additionally
budget interval repair, keyed-stream work and source decompression. Both remain
fixed-reference engineering estimates; the AMD measurements do not establish
absolute i9-13900H, MSVC or ARM performance. DSP operation budgets, probabilities,
wire behavior and search limits are unchanged. Exceptional recovery remains
outside nominal headroom and is quantified separately in the study.

The shared GUI group passes **33/33** with GCC/FLTK (130.11 seconds) and
Clang/Rev (148.87 seconds). The final simulation estimate suite passes with
both compilers (55.43 seconds for Clang); its source-size checks distinguish
decompression scaling from the shared copy allowance. CMake-built benchmarks
pass all 17 fixture correctness checks
with each compiler. Those smoke runs overlapped other tests and supply
correctness evidence only, not additional timing samples.

All **30/30** preservation-contract suites pass, including the unchanged
receiver-probability calibration in **1,265.04 seconds**, within its original
1,500-second timeout. Exact wire vectors, physical completion, next-poll pending
progress, correction/recovery behavior and CLI presentation checks are intact.

On an isolated 2400×1800 Xvfb display at 96 DPI, all **3/3** FLTK native cases
pass: the production workflow (217.60 seconds), adapter conformance
(68.03 seconds) and document conformance (0.03 seconds). They ran after heavy
tests/builds finished. Initial sandboxed display attempts could not bind or
connect to the local X socket and ran no GUI assertions; the host-level
private-display invocation disables TCP and does not use the user's desktop.

Rev's production workflow reproduces the previously documented phase 11
replay-frame timing failure (3.211450 seconds, eight frames, seven changes,
fraction 0.932204). It stops after 91.41 seconds, so a complete Rev workflow pass
is not claimed. The existing timing assertion is unchanged; this estimate update
does not modify the replay/refresh implementation.
All four other Rev native checks pass: adapter conformance (95.97 seconds),
platform conformance (6.16 seconds), and 1×/2× coordinate checks (5.12 seconds
each). No timeout, replay assertion or modem regression was weakened.

Build/test logs and the private-display helper are retained under the ignored
`build/robust-cpu-estimates/` directory. These are Linux generated-audio and
GUI checks, not Windows rendering, physical-link qualification or CPU-attack
immunity tests.

## Targeted receive-processing hardening — 22 September 2026 UTC

The [receive-processing review](receive-processing-hardening.md) adds selected
index dependencies and validation barriers to short decoding, regular/Fast
Reed–Solomon correction, recovery, OFDM interpolation, LZMA history access,
attachment metadata, text filtering and explicit file writes. No process or VM
isolation is added or counted as protection. These are defensive changes to
selected accesses, not demonstrations of previously exploitable gadgets or a
claim of immunity to Spectre, Meltdown or all CPU weaknesses.

Both GCC 14.2 FLTK and Clang 19.1.7 Rev Release applications build. The 11-case
focused selection passes with each compiler: helper semantics and generated
code, pristine XZ inventory, short/source decoding, Reed–Solomon, recovery,
regular/Fast attachment handling and explicit runtime file operations. All
30 preservation-contract cases pass, including the 1,274.03-second receiver
probability calibration. All 33 shared GUI cases pass in 226.00 seconds,
including Legacy and Fast received-text policy checks. The four build-tool,
generated-code and vendor-integrity cases also pass. Existing vectors,
completion rules, progress assertions and timeouts remain unchanged.

All 36 Fast cases pass across the group run and isolated rerun. The initial
concurrent run passed 35 cases; the acoustic portion of the 92-case SNR matrix
hit its existing 300-second subprocess timeout while other builds/tests were
active. The complete SNR matrix, bulk-transfer and argument checks subsequently
passed alone in 236.48 seconds. No timeout or success assertion was relaxed.

All nine targeted ASan/UBSan cases pass in 130.70 seconds, including the actual
instrumented private LZMA library and standalone index/load witness. UBSan
halts on error; LeakSanitizer is disabled for the restricted process environment.
Added malformed-input checks cover extreme erasure indices, invalid bit values,
truncated UTF-8 metadata, incomplete authenticated tails and invalid LZMA
properties. An exact-byte compression roundtrip crosses the fixed dictionary
boundary twice; it does not assert which SIMD instructions or match distances
the encoder selects.

Generated-code witnesses pass with GCC and Clang, with and without stack
protection, using GNU and LLVM disassemblers. AArch64 LP64 ordinary and
stack-protected objects cross-compile and pass the codegen check; no ARM runtime
result is claimed. Forced narrow `size_t` preprocessing reports unsupported.
Synthetic bypass branches are rejected by the witness checker. Independent
optimized regular-codec object inspection also retained the index dependencies
and barriers. None of these checks measures transient hardware execution or
proves that every application access is covered. MSVC/Windows runtime performance
and native display workflows were not exercised by this processing-only change.

The [comparison harness](../tools/benchmark_receive_processing.cpp) ran on a
Linux x86-64 AMD Ryzen 5 PRO 5650U with GCC 14.2, Release libraries and an
`-O3 -DNDEBUG` harness. Baseline archives and the two changed inline headers were
saved from `f715839b5b921576c15d2179e7fb1d51dc7194d5` before rebuilding; their
checksums were verified before measurement. Runs used baseline/current/current/
baseline order after project builds and tests finished. Each run contains seven
samples of at least 150 ms per workload. The table averages the two run medians;
the [complete CSV](validation-data/receive-processing-2026-09-22.csv) retains
per-run medians, minima, maxima and iteration counts.

| Workload | Before, µs | After, µs | Runtime change |
| --- | ---: | ---: | ---: |
| Regular RS, 128 bytes / parity 48, clean | 15.952 | 15.921 | −0.2% |
| Regular RS, 10 errors + 6 erasures | 41.522 | 41.961 | +1.1% |
| Fast RS, 25,200 bytes / parity 38, clean | 3,165.174 | 3,306.313 | +4.5% |
| Fast RS, 19 errors | 7,329.751 | 7,395.807 | +0.9% |
| Short dictionary, 16 source bytes / 98 bits | 0.068 | 0.273 | +300.0% |
| Raw LZMA2, 1 MiB high-entropy source | 93.303 | 97.603 | +4.6% |
| Raw LZMA2, 1 MiB patterned source | 241.138 | 245.978 | +2.0% |
| Received-text filter, 1 MiB high-entropy input | 5,040.587 | 4,944.779 | −1.9% |
| Sampled Fast OFDM, complete raw 16-QAM cycle | 129,139.698 | 130,643.414 | +1.2% |

These are local processing times, not changes in on-air bit count, airtime or
detection probability. The short decoder's fourfold relative increase adds about
0.205 µs per message. Small differences, including apparent speedups, should not
be treated as exact improvements: CPU boost and scheduling were not controlled,
and high-entropy LZMA run medians varied from 90.1–104.9 µs. Its mostly
uncompressed chunks also exercise a different path from the patterned source.

RS measurements include copying the input before correction. LZMA throughput
counts decompressed source bytes; the short decoder counts its 16 source bytes.
The OFDM fixture includes receiver construction, acquisition, scored physical
completion and exact-bit validation. Its CSV throughput counts all 2,671,980
bytes of supplied float PCM, including training and eight seconds of trailing
silence, not payload throughput or isolated interpolation speed. Encoding/setup
is outside timing. These results do not establish performance on MSVC, ARM,
other profiles or physical audio/radio devices.

## In-memory Robust transmit epoch lock — 22 September 2026 UTC

The live hardware sender now records the greatest exposed symbol epoch per
selected key, including the leading pulse of a future symbol. Accounting occurs
before PCM leaves the playback callback and survives cancellation, profile
changes, key reloads and stop/start within the same session. The next profile's
prefix determines its lock deadline. Clock rollback extends the countdown;
simulation and a new session have separate history. Normal completion and
cancellation retain their existing receive-separation behavior.

The shared GUI displays a compact `TX lock` countdown and replaces the airtime
line with the explanation. Its adjacent `Force next transmission` action sends
one valid draft through the normal path while bypassing that request's key and
separation waits. Earlier history remains recorded. No native adapter code,
confirmation dialog, keyfile option, persistent state, receiver hypothesis or
wire format was added or changed.

The new `live_transmit_lock` suite independently compares two rendered slow
waveforms to establish an early contribution from the next symbol. It then
exercises cancellation, six seconds of clock advance and a faster profile;
ordinary sends remain locked. Other cases cover phase and subsecond boundaries,
tail clamping, short/long prefix deadlines, forward/backward clock corrections,
same/different keys, reload and session lifetime, invalid forcing, explicit
epochs, queued-request rechecks, one-shot quiet-period bypass and retained
history. Release coverage passes in 7.12 seconds; its ASan/UBSan run passes in
7.81 seconds with UBSan halting on error and LeakSanitizer disabled for the
restricted process environment.

All 30 preservation-contract suites pass across the full run and corrected
controller rerun, including the 1,013.48-second differential receiver
calibration. The new controller fixture initially inspected the default
pre-poll snapshot; it now observes a real transmission and finishes its normal
replay before asserting exact `001` output. The existing assertions remain
unchanged. The full controller suite passes in 76.82 seconds. All 33 headless
GUI checks pass across these runs and the additional 29-case GUI selection
(101.81 seconds), including the shared/native boundary, compact minimum-size
layout, validation guards and restoration of the airtime estimate.

The FLTK production workflow, adapter conformance and document conformance
pass on a private 2400×1800 Xvfb display at 96 DPI (292.59 seconds together;
226.71 seconds for the complete workflow). Rev adapter/platform conformance
and both 1×/2× coordinate checks also pass (118.87 seconds together). Its
production workflow reaches the unchanged 300-second smoke limit in phase 17
both in the native group and on an isolated rerun (300.46/300.42 seconds).
The reported states were replacement-transmission generation and receiver
search limited by its configured DSP workspace. No complete Rev workflow pass
is claimed, and neither its timeout nor its assertions were relaxed.

Both Release GUI executables rebuilt. `git diff --check` passes. These are
generated-audio, hardware-callback-stub and Linux GUI checks; physical radio/audio
links and native Windows rendering were not exercised. The private display was
closed after testing, and no running user GUI or audio device was changed.

## Build and dependency simplification — 22 September 2026 UTC

This change alters build orchestration, dependency preparation, packaging and
documentation. Sanitizer validation also found an existing zero-length
`fwrite` call with a null data pointer; empty-file writes now skip that call
while retaining exclusive creation and flushing, with a regression for empty
files, binary contents and rejection of an existing destination. No wire format
or message behavior was changed. The POSIX
`build.sh` entry point selects explicit incremental, sanitizer, release and
optional Rev profiles. Application builds exclude test executables; named test
groups build their prerequisites before running the existing individual tests.
The complete 29-case preservation-contract selection remains intact.

A fresh GCC/Ninja FLTK application build with three jobs took 72.16 seconds;
compiling the full test prerequisites afterward took another 87.00 seconds.
These are local elapsed measurements without ccache, not a controlled
before/after speedup benchmark. The application graph has 308 compilation
commands and no test/probe sources; the contract graph has 147 compilation
commands, including 28 C++ test sources, and needs no FLTK compilation. Python
provides the additional CLI contract case. Optional ccache was unavailable on
this host; no cache-hit performance claim is made.

The pinned Debian development archives were verified and prepared under
`third_party/build-support/cache`, using matching installed runtime libraries.
Fresh builds no longer depend on the former `/tmp/datapump-native-headers`
symlink. Five previously ignored FLTK libdecor build-support files were checked
byte-for-byte against the recorded upstream commit; upstream content is
unchanged. Build information and dependency notices are included in packages;
historical validation captures remain available in the source checkout and as
an explicit packaging option.

Validation results:

- Fresh GCC/Ninja FLTK and Clang 19/Ninja Rev application builds pass. A separate
  Unix Makefiles CLI build passes with GUI and Python discovery disabled.
- All 123 headless cases pass across the full run and focused reruns, including
  all 29 preservation-contract cases. The full run took 1,501.55 seconds. Its
  only failure was an older planner fixture assuming Robust was the default;
  three fixtures now select Robust explicitly, preserving every assertion and
  the application's Fast default. The corrected planner case passes.
- All 24 wrapper and seven dependency-helper checks pass; CI now explicitly
  runs the build-tool group. SDK checks include offline repair, corruption,
  version mismatches, directory ownership and cross-compilation selection.
- Nine focused ASan/UBSan cases pass across the initial run and reruns: runtime,
  short compression, transfer, stream codec/reception, GUI application,
  controller, inspection and binary editor. The initial controller run exposed
  the empty-file issue and missed a pending-progress deadline under concurrent
  load. Its sequential rerun passes with UBSan configured to halt on error;
  the original pending-progress assertions and deadlines are unchanged.
  LeakSanitizer was disabled for the restricted process environment.
- Packaging fixtures pass, covering inventory, relocation, checksums and
  deliberate corruption. Both local TGZ/ZIP archives pass dependency-closure,
  isolated-command and ELF ABI checks. Their observed glibc floor is **2.38**;
  these host-built artifacts do not claim the CI release floor of 2.35.

All three FLTK native cases pass on an isolated Xvfb display, including its
182.93-second end-to-end workflow. All five Rev native cases pass across the
conformance run and isolated workflow rerun (260.70 seconds for the latter).
Initial concurrent Rev workflow attempts missed replay frame/time bounds;
the fresh isolated run preserves the original assertions and 300-second budget.
Do not overlap these wall-clock-sensitive GUI simulations with other heavy
test/build processes. Xvfb required permission to create local display sockets
outside the execution sandbox; it did not use the user's desktop.

Windows, the Ubuntu 22.04 release builder, copied binaries on other
distributions and physical audio hardware were not exercised locally; their
existing CI and qualification gates remain in place.

## Short acoustic LDPC alternatives — 22 September 2026 UTC

Implemented 648-, 1,296- and 1,944-bit QC LDPC at rates 1/2, 2/3 and 3/4,
restricted to Fast `acoustic-short`. The selected −6 dB profile uses one
1,944-bit rate-3/4 frame with compact SC tracking. Its minimum remains 9.844
seconds; steady public/keyed rates improve from 445/315 to 794/696 bit/s.
A tested depth-2 option takes 12.765 seconds with 895/830 bit/s. At 0 dB,
small LDPC OFDM gives 3,141 bit/s with a 12.328-second minimum. Default 3 dB
and stronger menu settings are retained. All 172 saved reports for other
channel profiles are identical.

The [comparison record](validation-data/fast/short-ldpc-20260922/README.md)
contains 14,336 ideal-noise codeword trials, nine independent frozen vectors,
full audio results and rejected sparse-marker alternatives. Selected and
extended −6 dB waveforms each pass all ten sampled cases, retaining six-second
physical absence, withheld source, timed holes and bounded memory. The original
convolutional wire vectors and frozen audio fixture remain active.

All 46 selected Fast/shared GUI/ordinary compatibility checks pass across the
broad run and focused reruns. Both native GUI adapters pass conformance on
private displays. LDPC AddressSanitizer/UndefinedBehaviorSanitizer checks pass.
This is deterministic generated-audio validation, not physical room testing or
a statistical whole-file reliability guarantee.

## Fast receive browser and named attachments — 21 September 2026 UTC

Fast opens on Speakers / microphone at 3 dB expected SNR. Switching channel
profiles reapplies their default SNR/waveform while retaining routing choices.
The composer omits the source-byte summary and listening controls; Cancel
applies only to transmission. QR brightness provides Normal, Dim, Dark and Off.
Signals contains receptions only and single-click activation copies completed
text. Files in memory contains completed attachments only, with their filenames.

Fast file sources prepend exactly `#ATTACHMENT### filename.ext #ATTACHMENT### `
before XZ compression. Only an exact, bounded, valid leading basename envelope
is interpreted, after physical completion and decompression. Counts and saved
bytes exclude the prefix. Invalid/nonleading markers remain ordinary content;
additional prefix storage cannot bypass content quotas. Fast owns this convention
independently of the unchanged regular attachment syntax and modem framing.

Both Release GUI builds passed. `gui_fast` and `gui_fast_live` passed, including
continuous reception, acoustic defaults, profile resets, QR changes while
listening, receive-only history, text/attachment separation, completion between
polls and exact named-file saves over sampled wire/acoustic paths. All 18 selected
shared GUI/regular compatibility checks passed in 72.38 seconds:
`gui_application`, `gui_layout`, `gui_contract`, `gui_bindings`,
`gui_adapter_boundary`, `gui_overlay`, `gui_inspection`, `gui_binary_editor`,
`gui_controller`, `gui_legacy`, `gui_legacy_live`, `compression_short`, `transfer`,
`stream_codec`, `stream_receive`, `attachment`, `pattern_correlator` and
`fast_boundary`.

The seven focused Fast suites passed: `fast_attachment`, `fast_compression`,
`fast_codec`, `fast_files`, `fast_session`, all 21 `fast_cli` cases and
`fast_boundary`. Coverage includes an independent exact prefix vector, frozen
raw wire vectors, empty/binary files, UTF-8 basenames, ambiguous delimiters,
malformed/nonleading/nested markers, quota edges, deferred interpretation and
public/keyed sampled text/file round trips. No regular runtime code changed.

FLTK native adapter and document conformance passed on isolated Xvfb displays.
Rev native adapter/platform conformance and both 1×/2× coordinate checks also
passed (four suites, 102.68 seconds).
A captured default screen confirmed acoustic continuous reception and the new
controls; visual review shortened the Cancel label to fit its button. Both GUIs
rebuilt and the Fast, application and layout tests passed again after that label
change. `git diff --check` passed. Tests use
the workspace build directory for temporary files. This does not establish
physical-link throughput/reliability or native Windows-window behavior.

## Fast console, audio channels and XZ source — 21 September 2026 UTC

Ordinary GUI launches now select Fast Modem and continuously listen. Fast owns
its Console composer, expandable QR, retained signal/file browsers and developer
Modem details page. Audio settings sit at the bottom with net modem rate,
Shannon–Hartley limit and occupied band; a spectrum-derived SNR precedes the
status line. The cable/acoustic expected-SNR defaults are 36/3 dB. All modem
GUIs offer left mono (default), right mono and stereo. Output-only routing
changes preserve ongoing reception.

Production Fast text/files are prepared as bounded XZ bytes before opening audio.
Raw and XZ sources use distinct bootstrap integrity contexts with unchanged
wire dimensions, so old literal Fast files cannot be silently reinterpreted as
XZ. Decompression remains behind fully observed physical completion and local
source/output/memory quotas. The regular dictionary, exact-bit and fixed-interval
formats are unchanged. Both production Fast peers must use the new source format.

Release build and `git diff --check` passed. Verification includes:

- All 15 focused shared GUI suites passed: `gui_fast`, `gui_fast_live`,
  `gui_application`, `gui_controller`, `gui_layout`, `gui_legacy`,
  `gui_legacy_live`, `gui_overlay`, `gui_inspection`, `gui_binary_editor`,
  `gui_adapter_boundary`, `gui_contract`, `gui_link_boundary`, `gui_self_check`
  and `gui_bindings`. The Fast live fixture covers continuous listen, editable
  drafts, receive/transmit handoff, stable pending identity, retained completed
  files, copy/paste/save, clear, pause and exact sampled wire/acoustic reception.
  Final review added a deterministic completion-between-polls regression:
  starting another transfer must first retain the previous terminal receive
  snapshot, and a physically complete row must be labeled completed even while
  its worker is finishing cleanup. Session teardown also preserves an already
  published complete result if cancellation arrives during cleanup; sampled
  public/keyed results and pending damaged-source controls cover that boundary.
  Clear consumes an unpolled terminal revision before removing rows, so the
  next refresh cannot recreate an already cleared result.
- `fast_codec`, `fast_compression`, `fast_files`, `fast_session`, `fast_cli`,
  `fast_presets`, `fast_boundary` and `fast_output_power` passed. XZ tests use an
  independent container vector, both raw/XZ mismatch directions, incomplete
  physical-end controls, malformed/truncated data, prepared-source bounds and
  source/output/decoder-memory quotas. A 32 MiB incompressible fixture checks
  the streaming encoder's explicit chunk-overhead bound rather than relying
  on liblzma's single-call-only bound. Raw coding vectors remain frozen.
- `compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`
  and `recovery` passed. ALSA and WinMM stub suites verified left/right/stereo
  PCM, mono-only fallback, partial writes and resampling. Fast CLI routing and
  benchmark argument/evidence tests passed.
- FLTK native adapter and document conformance passed on an isolated Xvfb
  2400×1800×24 display. The Fast declarations were rendered at default/minimum
  sizes, including Console/details visibility and retained source controls.
  An actual Fast startup capture confirmed continuous listening and the bottom
  diagnostics. Final visual review widened the Pause listening button.
  Shared layout/application tests passed again after that final geometry change.
- Rev native adapter conformance passed at 1× scale; platform conformance and
  both 1×/2× coordinate checks also passed on the isolated Xvfb display. An
  additional full adapter run at 2× reached its unchanged 330-second limit,
  without a reported assertion failure; that extra run is not a conformance
  pass. Both GUI variants rebuilt after all final changes; their shared
  layout, application and contract suites passed again.
- All 19 regular receiver/CLI suites passed: `live_profiles`,
  `live_receptions`, `live`, `live_resources`, `pattern_correlator`,
  `pattern_receiver`, `pattern_drift`, `pattern_differential`,
  `receiver_differential`, `pattern_fft_batch`, `pattern_correlator_batch`,
  `pattern_search`, `tuning`, `simulation_estimate`, `receiver_probability`,
  `differential_probability`, `differential_receiver_probability`, `weak_signal`
  and `cli`. These include the fully scored four-hour-symbol and immediate
  pending-prefix regressions. The separate sampled receiver-probability
  calibration completed successfully in 1,464.74 seconds.
- The full FLTK `gui_workflow` reached its unchanged 300-second limit in phase
  15 (sampled-audio transmission). Earlier validation entries reproduce this
  same phase-15 limit on an unchanged baseline; this run does not establish a
  complete workflow pass. No smoke assertion or timeout was weakened.

Initial keyfile tests exhausted the pre-existing nearly full `/tmp`; reruns used
workspace build directories for `TMPDIR` and passed. An unrelated FLTK native
pointer-tooltip probe failed under Xwayland; the unchanged probe passed under
Xvfb. Xvfb was extracted locally under the ignored build directory, without
installing a system package. Linux native and mocked WinMM checks do not establish
Windows native-window behavior or physical-link BER/throughput qualification.

## Fast channel-profile menu — 21 September 2026 UTC

The shared Fast Modem channel selector now offers cable QAM/LDPC, SSB, FM and
acoustic OFDM/LDPC. The cable and acoustic classic APSK entries were removed.
The existing GUI regression checks the four remaining options, rejects callbacks
using removed profile IDs without changing current settings, and retains checks
for capacity defaults, radio controls and output routing. Classic CLI/codec
support and regular transport behavior are unchanged.

Release configuration and the full build passed. Of 36 selected CTest suites,
35 passed: the [development contract](development.md)'s headless selection except
`differential_receiver_probability`, plus `gui_fast`, `gui_fast_live`,
`gui_self_check`, `gui_bindings`, `gui_contract`, `fast_boundary` and `fast_cli`.
The remaining `differential_receiver_probability` calibration was interrupted
after an extended run and has no result for this change; no completed test
failed. `git diff --check` passed. No native adapter code changed; native window
conformance and physical audio tests were not rerun.

## Fast damaged-cycle continuation — 21 September 2026 UTC

The [continuation fix](fast-cycle-continuation.md) separates an incomplete file
from a stopped decoder. Rejected source cycles retain their fixed positions and
consume quota; later cycles continue through FEC and independent integrity
verification. Physical whitening and group ordinals advance across holes.
Classic cycles can retain good groups alongside a rejected group. Bootstrap,
quota, nonfinite-input and internal failures remain fatal. The session, CLI and
shared GUI report missing data while verified-group counts continue to advance.
Missing data never becomes a completed file or a Save handle.

Replay of the unchanged real 64-QAM/right-only amplitude-0.20 recording now
attempts **32 LDPC frames instead of 16** and verifies both cycles after the
failed source cycle. Its whole file remains incomplete. The more impaired
64-QAM recording attempts all 32 frames but has three bad source cycles; the
failed-bootstrap control still stops after eight frames. The saved **500,000-byte
16-QAM** recording still recovers exactly with 96/96 converged frames and the
original SHA-256. All four input hashes match the preceding live-study manifest.
These are saved-waveform replays, not new live tests.

The Release build and **20/20** selected CTest suites passed in **143.08 seconds**,
including new public/keyed cycle-continuation, exact retained-position, erased
cycle, quota, sampled-audio session, GUI progress and CLI state checks. Existing
Fast and regular short-text, fixed-stream, physical-end, attachment and shared
GUI regressions passed. Both Release GUI variants rebuilt and the Rev shared
GUI self-check passed. Independent wire vectors remain unchanged; no native
adapter, waveform or encoder changes were made. The
[evidence archive](validation-data/fast/cycle-continuation-20260921/README.md)
contains replay outputs, build/test logs and source/binary/input hashes.

## Fast acoustic 16/64-QAM and output routing — 21 September 2026 UTC

The [routing diagnosis](fast-acoustic-routing-diagnosis.md) reproduces the
reported failure after interval reception. At unchanged 95% playback and 27%
capture, 100 KB tests passed with both 16-QAM routes and with 64-QAM stereo;
64-QAM right-only failed twice. Halving amplitude also failed, including a
fresh trial with stronger LDPC 2/3. Each failure still received all 1016
physical intervals. Known-bit replay found insufficient decoder information
margin and a persistent frequency-dependent prediction mismatch in part of
one failed recording. Freezing or fully replacing the refresh estimate did
not recover that file; neither experiment was retained. The precise physical
or receiver-internal source of the changing mismatch remains unresolved.

The acoustic preset now uses **16-QAM, LDPC 3/4, depth 8, right-output-only**
playback, with 38.80 kbit/s steady public-source capacity. Cable/classic presets
and explicit 64-QAM/stereo choices remain available. A real 500,000-byte
confirmation passed exactly in **140.497 seconds**, with 96/96 LDPC frames,
3048/3048 intervals, no overflow, and matching source/received SHA-256.
The final shared GUI Application transmit/listen/Save workflow passed exact
60-byte text through real default-device audio in **41.456 seconds**. It used
two Application instances with real production audio, not native widget clicks.

The acoustic constellation display now samples across a publication batch
instead of retaining only its highest-frequency points, and labels observations
that stop updating while PCM continues. Focused telemetry checks cover frequency
coverage, bounded storage, finite filtering, immutable frames, independent input
retention, and freshness. Single-carrier chronological sampling is unchanged.
The Release build and **20/20** selected CTest targets passed in **142.80 seconds**,
including Fast codec/acoustic/session/transfer/files/CLI, both shared Fast GUI
suites, telemetry, GUI self-check/controller/application, and regular short-text,
fixed-stream, attachment, inspection, and explicit-binary regressions.
Both Release GUI variants rebuilt; the Rev shared GUI self-check also passed.
No native adapter implementation changed.

These sequential transfers do not establish an 80% success rate for 50 MB or
attainment of acoustic Shannon capacity. Methods, unsuccessful controls, exact
results, and retained-input hashes are in the
[evidence archive](validation-data/fast/acoustic-routing-20260921/README.md).

## Fast acoustic reception diagnosis — 21 September 2026 UTC

The [reception diagnosis](fast-acoustic-reception-diagnosis.md) reproduces and
repairs startup gain sensitivity, rejection of a valid training body at capture
start, and excessive influence from noisy low-gain training blocks. Final-block
gain/timing fitting excludes all 128 verification tones; inverse-noise weighted
training uses only earlier fitting blocks. Wire geometry and throughput remain
unchanged.

On the current physical speakers/microphone at unchanged 75% output and 27%
input, a fresh **500,000-byte** transfer with a 14 dB startup gain ramp passed
in **103.632 seconds**, with matching SHA-256 hashes and **96/96 LDPC frames**.
The final shared GUI application path also transmitted, received and saved exact
60-byte text through real audio in **35.285 seconds**. A saved real 100 KB stress
recording changed from no synchronization in the old receiver, through incomplete
FEC after the synchronization fixes, to exact recovery with weighted estimation.
Replays preserve the original successful 100 KB and 5 MB captures; deterministic
noisy echo/gain controls reproduce the before/after decoding distinction.
The final Release build and all twelve selected acoustic/Fast/session/shared-GUI
CTest targets passed; the diagnosis records their exact scope.

These findings establish concrete receiver defects, but the user's original
failed attempt was not recorded and cannot be assigned a unique cause. Full
methods, limitations, source fixtures and logs are in the diagnosis and
[`validation-data/fast/acoustic-reception-20260921/`](validation-data/fast/acoustic-reception-20260921/).

## Fast speaker/microphone OFDM and live bulk transfer — 20 September 2026

The [acoustic study](fast-acoustic-live-study.md) records live sounders of the
unchanged default speaker/microphone hardware, guard/constellation/pilot and
channel-estimator experiments, and a complete **5,000,000-byte** transfer in
**746.471 seconds**. All 840 LDPC frames converged with matching independent
source/received hashes, no missing intervals, clipping, or audio/FIFO error.
The production CLI also transferred 100,000 bytes exactly in 47.976 seconds.

The new speaker/microphone default uses 64-QAM, LDPC 3/4, depth 8, a 32,768-point
OFDM transform with an 85.33 ms prefix, pilot stride 16, 500–18,000 Hz, nominal
amplitude 0.40, and stereo output. Steady public source rate is 56.04 kbit/s;
RS parity/data is 0.30546%. The measured acoustic noise/response model predicts
121–131 kbit/s, so this implementation does not claim to have reached Shannon
capacity. 1024-QAM/LDPC 1/2/depth 16 passed a shorter 500 KB trial and provides
61.15 kbit/s modeled steady throughput. Its long-file reliability is unqualified.
A standard 2/3 LDPC option was independently verified but did not make the
higher-rate 256-QAM trial reliable as its channel margin changed.

Acoustic acquisition uses sixteen training blocks and independent verification;
frequency-specific likelihoods down-weight deep fades, and smoothed full-band
refreshes maintain the response. The acoustic capture queue is fixed at four
seconds for up to four concurrent LDPC workers. Cable capture retains its
one-second limit. A shared end-tail calculation covers six seconds of complete
physical absence blocks in both live playback and WAV output. Codec flags, EOF,
and cancellation never manufacture reception completion. The classic acoustic
fallback and existing cable/Regular formats remain separate.

The Release build passed. All 43 selected Fast/shared-GUI and development-contract
targets passed after updating one stale CLI invalid-rate fixture (2/3 is now a
supported rate; 2/5 remains rejected). The initial CTest run passed 42/43; the
corrected complete 18-case CLI suite then passed separately. All eleven
independent acoustic measurement/BICM controls passed. Coverage includes fixed
wire vectors, short dictionary endpoints, pending GUI behavior, whole-symbol
absence, noisy multi-cycle echo/gain changes, extended OFDM WAV tails, all six
independent LDPC fixtures, and the preserved 92-case classic Fast SNR matrix.
Native adapter code was unchanged; these are shared GUI checks, not a new native
Windows/Rev rendering qualification. Logs and exact scope are in the archive.

Total live acoustic experimentation was approximately 29 minutes 38 seconds,
within the user's 30-minute budget. No 50 MB acoustic transfer or 80% success
probability was demonstrated; the 119.44-minute default-profile estimate is a
projection. Detailed successful/failed trials, fixed-recording controls, source
hashes, and normal CLI results are archived in
[`validation-data/fast/acoustic-capacity-20260920/continued/`](validation-data/fast/acoustic-capacity-20260920/continued/).

## Fast capacity implementation and full-file cable test — 20 September 2026

The [capacity cable study](fast-capacity-live-study.md) records a successful
**50,000,000-byte live default-device transfer in 1,290.664 seconds**, with exact
bytes and matching independent SHA-256 hashes. All 6,980 LDPC frames converged;
there was no clipping, dropped interval or FIFO overrun. Maximum diagnostic
capture backlog was 0.304 seconds, below the production one-second bound.

The new cable default is 4,194,304-QAM, LDPC 8/9, four frames per coding cycle,
0.30649% RS parity/data, one full marker every 16 intervals, raw source bytes and
one protected continuation/final flag per cycle. The 18 kHz waveform uses
17,647.0588 symbols/s, 2% rolloff, a 2,048-symbol training preamble and amplitude
0.30. Nominal 50 MB payload rate is 310.72 kbit/s; observed end-to-end rate was
309.92 kbit/s. Classic Fast and Regular wire formats are preserved.

The complete initial 99-test suite passed 98 tests; its Fast session fixture
fed 50 ms audio blocks every 3 ms and overran the capacity receiver. Fast capture
in that fixture now uses audio pace while retaining production's one-second
FIFO. The final 10-target Fast/shared-GUI suite passed after all runtime/default
changes, including session ownership, codec, modem, QAM, LDPC, files, transfer,
telemetry, CLI and GUI tests. The CLI target contains 16 cases, including eight
public/keyed dense-QAM S16 WAV combinations at 48/44.1 kHz. QAM tests include 21
sampled scenarios, clock offsets of ±100 ppm, carrier offset, echo and exact
LDPC recovery. Independent classical vectors and physical-end checks remain.
The LDPC and standalone GF(65536) RS audits also passed ASan/UBSan checks.

Native FLTK adapter and document conformance passed on a private Xvfb display.
The initial sandboxed display attempt could not connect to its socket; the same
checks ran successfully outside that sandbox. See the archived logs and exact
evidence scopes in the [study](fast-capacity-live-study.md). Linux FLTK validation
does not establish native Windows or Rev runtime validation.

Live experiments used 29 minutes 56 seconds in total, including failures and
silence tails. One full-file success is not a demonstrated 80% reliability rate.
The final short margin sweep passed both 8/9 and 9/10 at amplitudes 0.30 and 0.27,
but failed both at 0.24. Device mixer levels remained unchanged. The archive
separates those live trials, recorded replays, ideal-AWGN experiments and airtime
projections; none is substituted for another.

## Physical cable SNR measurement — 20 September 2026 UTC

The [SNR report](cable-snr-live-study.md) records live ALC257 headphone-to-mic
measurements through the default analog endpoints. At a 997 Hz, .99-peak tone,
100% playback, and zero-dB capture gain, three repeated plateaus measured
**78.96 dB SNR on the better individual input** and **81.48 dB after averaging
both inputs**, integrated unweighted over 20–20,000 Hz. The combined-input
SINAD was 80.22 dB. Across 300–18,300 Hz, the corresponding SNRs were 80.01 and
82.50 dB. These are tested best-case results, not a calibrated hardware limit.

Five repetitions at .95 peak/95% playback measured 79.92 dB combined-input SNR
and 79.06 dB SINAD over 20–20,000 Hz. Eight sampled frequencies from 313 to
18,203 Hz showed similar noise performance. The production S16 audio API,
measured separately at .90 peak/95% playback, gave 76.31 dB SNR and 75.95 dB
SINAD. The path comparison includes channel selection and client precision;
its difference is not attributed solely to the sample format. A 19-tone
wideband signal gave a 71.62 dB combined-input linear residual ratio at its
lower average power; this is not a calibrated modem SNR or file-success rate.

The +6/+12 dB capture-gain screens did not improve stable SNR. Two transient
windows remain in the results and retained raw samples. Digital silence and
60-dB-lower tone controls had closely matching noise powers, without evidence
of a large silence-only muting benefit in these captures. All original mixer
levels were restored: playback 95%, hardware Capture/Digital/Mic Boost 0 dB.

The [evidence archive](validation-data/fast/cable-snr-20260920/README.md) retains
original selected PCM windows, complete-run hashes, device and mixer metadata,
full numerical analysis, reproduction tools, and the independent FFT audit.
Eleven synthetic SNR/SINAD/multitone tests pass; the independent FFT estimates
agree within 0.0221 dB SNR and 0.0151 dB SINAD across 21 strong-tone windows.
All 53 retained PCM windows pass their SHA-256 checks and reproduce the reported
metrics within 0.001 dB.
The standalone production-audio capture diagnostic builds cleanly and completes
its real-device comparison. This work changes diagnostics and documentation
only; modem runtime, framing, defaults, and existing regression expectations
are unchanged. Their full runtime suites were not rerun for this work.

## Physical cable throughput and revised cable defaults — 20 September 2026 UTC

The [live cable study](fast-cable-live-study.md) records actual simultaneous
ALC257 headphone-output/microphone-input tests through the production default
ALSA device selection. Device inspection confirmed analog ports, not a monitor
source. The initial microphone path's +60 dB gain severely clipped a 5%-amplitude
tone; setting the Pulse source to 10% selected 0 dB Capture/Mic Boost hardware
levels. Playback remained at 95%. These host-specific calibrated levels were
retained for cable operation.

Six 100,000-byte screening transfers all recovered exactly. Three faster,
tighter-rolloff 100 KB trials also recovered, with 15/12/74 corrected bytes.
Raw probes and matched offline controls show increasing error margin consumed
by the tighter waveforms, not a measured whole-file failure cliff. Generated
amplitude-0.5 PCM also had occasional excursions beyond full scale before S16
conversion. The new cable amplitude 0.35 has a conservative generated-PCM peak
bound of 0.894854 for the existing 20% rolloff; this precedes optional resampling
and any external gain. Radio/acoustic amplitudes remain unchanged.

One full **5,000,000-byte** transfer at 256-APSK, 7/8, high-rate RS, depth 62 and
amplitude 0.5 recovered exactly in **735.879 seconds** from TX process launch to
RX delivery, with **seven corrected bytes, zero erased bytes**, and matching
SHA-256 `d4b49bedb47fd93ed6b86110602a65641a2dd21b40b5d244afd7560453bc0c2f`.
Fourteen fresh **100,000-byte** transfers at the final amplitude **0.35** all
recovered exactly, with zero corrected or erased bytes. These are distinct
configurations, not pooled trials. Both series used stereo output. Their
[raw records and reproduction instructions](validation-data/fast/cable-live-20260920/README.md)
retain every trial, device warning, source hash, executable hash and assumption.

Wire defaults now select 256-APSK, convolutional 7/8, high-rate RS(128,120),
depth 62, amplitude 0.35 and both-channel output. A separate 100 KB check of
the former right-only output failed with excessive RS erasures; it remains in
the archive as a negative control. After fixing the cable output default, an
additional 100 KB transfer with no routing flag recovered exactly. Other channel
CLI/GUI presets retain right-only output, with explicit `--mono`/`--stereo`
overrides available. All regular modem runtime code is unchanged. The exact public 50 MB estimate falls from 17,195.124 to
7,275.599 seconds; encrypted airtime is 7,882.072 seconds. Depth 62 minimizes
that size's airtime over depths 1–64. GUI selection includes 62 and retains all
previous choices. CLI profile information now includes amplitude and routing. The standalone
SNR diagnostic accepts explicit RS and amplitude choices and records depth,
RS and amplitude so the previous 92-case matrix retains its exact settings.

No actual 50 MB file was transferred within the user's 30-minute live-test
budget. Fourteen 100 KB successes give an 80.736% one-sided 95% lower success
bound only for that directly tested configuration and size, under independent,
stationary attempts. The 5 MB success is one observation, and 50 MB reliability
remains unqualified. No LDPC, 0.3% RS, marker shortening or sparse marker cadence
was introduced. The marker analysis does not certify a file-wide 2^-80
false-match probability for the existing tolerant detector.

The Release application and diagnostic tools build successfully. Original Fast
crypto/wire fingerprints remain unchanged; earlier PCM/SNR/burst fixtures pin
their old profile explicitly rather than changing their expected outcomes.
New tests cover the exact 50 MB estimates, depth selection, new-default physical
completion, pulse peak bound, and benchmark statistics/process cleanup.
The final focused Fast/GUI/CLI/benchmark checks pass, including all nine Python
benchmark checks. One concurrent `fast_session` run failed its text-reception
assertion; an unchanged isolated run and seven diagnostic repeats passed. The
fixture feeds audio 16.7 times faster than real time, so scheduling/queue pressure
is plausible but unproven. The failure message now preserves status/error details;
its geometry, deadline and assertions remain unchanged. Its final isolated CTest
run passes in 1.92 seconds. The full 92-case SNR matrix and new-default bulk case
pass in 389.18 seconds. Four additional routing/session/GUI/CLI suites pass
in 10.03 seconds after the routing change. All **29 development-contract suites
pass** in 1,704.77 seconds, including independent short-message vectors, physical
completion, receiver probability and pending GUI behavior. The
[archived validation logs](validation-data/fast/cable-live-20260920/README.md#build-and-regression-evidence)
retain successful checks and the earlier concurrent session failure. No native
GUI adapter code was changed.

## Fast LDPC, RS and channel-capacity study — 19 September 2026

[The offline coding study](fast-coding-study.md) records a 120-point
constellation-information sweep, independent GMI cross-check, 33 finite LDPC
trial runs and 32 sampled production-receiver channel points. The best tested
uniform-QAM candidates use DVB-S2X B10 LDPC(64800,50400), rate 7/9, with
256/1024/4096-QAM at 20/25/30 dB in-band AWGN SNR. Each final candidate recovered
800/800 words, 5,040,000 information bytes, exactly. This is an ideal symbol
channel experiment without outer RS, integrity fields or production framing;
0/800 failures gives a 0.374% one-sided 95% FER upper bound.

The sampled receiver audit retains and identifies 64/64 intervals and observes
physical completion at every point. Its acoustic echo results favor smaller
constellations than the ideal AWGN study. It measures soft-bit information,
not decoded LDPC file throughput. RS choices are calculated with an independent
erasure model; no outer RS implementation or interruption qualification was
added. Diagnostic builds, analytical binomial/confidence checks, CSV integrity
checks and plot inspection pass. Raw data and reproducibility instructions
are linked from the study. Runtime code, wire formats, defaults and tests are
unchanged; the existing runtime regression suites were not rerun for this
documentation/diagnostic-only change.

The documentation follow-up links this study from the README, development
contract, Fast specification, historical Fast plan and regular-throughput scope
note. It adds an evidence inventory, reproducible shaping extrapolation,
interleave/rate/parity tradeoffs and a future development sequence. Relative
artifact links, analytical examples and archived RS-model reproduction were
checked. This follow-up changes documentation only; it adds no new channel
measurement or runtime test result.

## Fast bulk-file coding defaults — 19 September 2026

Fast defaults now use rate-3/4 convolutional coding, robust RS(128,112),
and interleave depth 16 for cable/radio or 5 for acoustic. The original
rate-1/2 crypto and full-wire vectors retain their original explicit profile
and expected bytes. Regular-mode framing and receiver code are unchanged.

The new airtime regression bounds cycle rounding plus bootstrap/final fill below
10% against ideal continuous interleaving at the same FEC rate for all files
at least 100,000 bytes. Exact estimates at 100,000 bytes, 100 KiB, 1 MiB and
16 MiB cover every profile, encrypted and public; throughput is at least 25%
higher than the former defaults. At 100,000 bytes, interleave overhead is
4.98–6.91%. A 16-byte Fast source takes 8.14/20.40/33.07/40.58 seconds for
wire/SSB/FM/acoustic, including 6.25 seconds of silence. These are airtime
calculations, not hardware throughput measurements.

Release build and final focused `fast_codec`, `fast_transfer`, `fast_cli`, and
`gui_fast` tests pass. All 29 development-contract suites pass, including
`differential_receiver_probability`, physical-end checks, and pending GUI
regressions. All 11 Fast/shared-Fast-GUI suites pass (SNR retry noted below). Sampled interruption assertions remain unchanged.
The full 92-case SNR matrix, added default 100 KB cable case, and argument
checks pass. Its first run timed out on the acoustic sweep at 300 seconds
while the bulk acoustic simulation was also active; the rerun passes with
unchanged timeout and decoding assertions.
The codec's ordinary public/encrypted streaming fixtures are now 100 KiB;
`fast_regression` defaults to 100,000 bytes. A default encrypted cable run
at 30 dB SNR and seed 417 recovers all 100,000 bytes exactly, with 902 physical
intervals and no corrected/erased bytes. The matching 100,000-byte acoustic
run also recovers exactly, with 903 intervals and no corrected/erased bytes.
[Bulk simulation CSV](validation-data/fast/bulk-defaults-20260919.csv) retains
both results; concurrent tests make timing/deadline columns unsuitable as
isolated real-time benchmarks. Simulated AWGN results do not establish
field error rates, and no native adapter or physical audio-device test was run.

## Fast acoustic/cable controls and memory-only reception — 19 September 2026

Fast's acoustic preset now uses 500 symbols/s QPSK at a 1.8 kHz carrier with
600 Hz shaped bandwidth, amplitude 0.35 and interleave depth 4. A 21-tap
half-symbol-spaced equalizer trains on known markers before payload slicing;
raw marker coherence still independently gates presence. Acoustic QPSK uses a
wider tracking cutoff and downweights coherent but noisy pilot groups instead
of erasing them. This preserves recovery at the existing 10 dB SNR requirement. Both peers must use the revised acoustic preset. Cable/radio waveform
constants, the independent Fast wire vectors, and Robust short/raw/fixed-interval
wire behavior remain unchanged.

The GUI exposes interleave depth 1/4/16/64, exact-source airtime estimates including
bootstrap/cycle padding/pilots/filter tail/end silence, generated-audio percentage,
and an explicitly assumed 30 dB Shannon-Hartley example. Depth 64 raises the
asymptotic public cable payload ceiling from 53.078 to 54.513 kbit/s for 256-APSK,
7/8 coding and high-rate RS; this is a geometry calculation, not a hardware
throughput measurement. Legacy adds Low/Normal/High squelch above its waterfall;
Normal retains the original PSK/Olivia confidence thresholds.

Fast GUI/CLI receive previews and their conversion/splitting API are removed.
Fast and Robust corrected source areas stay in bounded RAM, capped at 256 MiB;
Fast compacts source cells in place only after physical completion. Robust's
memory accounting includes retained buffer capacity, and its shared quota also
bounds reserved source capacity across receiver candidates. The recovery-anchor
regression still requires constant diagnostic/recovery overhead after retention;
it now subtracts the separately measured, intentionally growing source buffer.
FLTK file-chooser preference writes are disabled. Explicit Save, requested
keyfile creation and CLI output remain available. OS paging/hibernation/crash
storage and graphics-driver shader caches are outside this application guarantee.

Validation performed on Linux Release builds:

- All 14 focused Fast/Legacy/shared-live-GUI suites pass, including exact Save
  comparison after preview removal. CLI results omit both former preview fields.
- The full 92-case Fast SNR matrix passed before the final acoustic tuning.
  All 23 acoustic cases (10–120 dB in 5 dB steps) were rerun on the final
  500-symbol/s profile and tracking code, with exact recovery in every case.
  [Final acoustic CSV](validation-data/fast/acoustic-500-20260919.csv) records
  these runs (three concurrent workers; timing columns are not an isolated
  performance benchmark). Independent code/crypto/marker vectors remain passing.
  All nine focused Fast DSP/codec/CLI/live-GUI suites pass again after tuning.
- New sampled file regressions pass with 60% first and 20% second echoes at
  1, 2.5 and 4.5 ms, added noise, and exact byte comparison after physical end.
  Acoustic noise plus an unrelated carrier cannot acquire or hold reception.
- Bandlimited resampler chains across 44.1/48 kHz logical peer clocks and
  44.1 kHz hardware bridges recover exact acoustic and 256-APSK cable sources.
  The test independently compares estimated sample counts with generated PCM.
  The existing `audio_rates`, ALSA and Windows audio contract fixtures pass.
- A temporary Linux preload guard rejected `tmpfile` and writable `fopen`/`open`
  calls. Robust's stream-receive integration and a complete Fast CLI WAV receive
  without `--save` passed under that guard; no implicit write was attempted.
- FLTK shared/native checks pass. The production simulation workflow first
  timed out under concurrent load in phase 15, then passed on retry in 219.63
  seconds with the unchanged timeout and assertions. The final adapter rerun
  passes, including disabled preference writes.
- All 29 required Robust contract suites pass, including the long differential
  receiver calibration. The `transfer` memory regression was adapted like the
  recovery-anchor regression to separate retained source capacity from core
  working state; its unchanged core bound and source-quota assertions pass.
- Rev's 36 shared/native tests excluding the production workflow pass, including
  adapter, platform, and 1x/2x coordinates. Both native backends were built in
  Release mode and `git diff --check` passes.

The sandbox hides `/dev/snd`, but an escalated host check found the physical
ALC257 analog speaker/microphone and PipeWire. Live tests retained all content
in RAM and compared all 40 source bytes, rather than relying only on checksums:

| Acoustic settings | Logical rate | Physical result |
| --- | --- | --- |
| Initial 1,000 symbols/s, depth 1 | 48 kHz | Acquired and observed physical end; RS recovery failed |
| 500 symbols/s, 1.8 kHz carrier, amplitude 0.35, depth 1 | 48 kHz | Exact recovery |
| Same, depth 1 | 44.1 kHz | One RS failure, then exact recovery on diagnostic repeat |
| Final preset: 500 symbols/s, 1.8 kHz, amplitude 0.35, depth 4 | 44.1 kHz | Exact recovery; final EVM 14.09% |
| Final preset, depth 4 | 48 kHz | Exact recovery; final EVM 12.68% |
| Final preset after tracking adjustment, depth 4 | 44.1 kHz | Exact recovery; final EVM 14.70% |

The hardware negotiated 48 kHz when requested at 44.1 kHz, exercising the live
resampler; native 44.1-kHz-only hardware is covered by the fake-device and sampled
conversion regressions, not a second physical sound card. The final preset's
40-byte test takes about 50.3 seconds including end silence. These are proximity
link observations, not general room/cable qualifications. The short-cycle failure
is why acoustic defaults to depth 4; cable/radio defaults remain depth 16.

Rev's full production workflow remains a validation limitation. Its first final
run failed the phase-11 replay-display assertion after 92.00 seconds (seven
measured frames versus the required ten). An isolated retry passed that stage
but reached the unchanged 300-second timeout in phase 18, simulating independent
radios with sampled audio, clock error and phase noise (300.28 seconds total).
Neither the replay assertion nor the workflow timeout was weakened. This is
separate from the passing 36 shared/native Rev checks; full Rev workflow
completion is not claimed. Private validation displays were closed afterward.

## Modem dropdown and Fast live plots — 19 September 2026

The shared header now selects **Robust Modem** (default) or **Fast Modem** from
a dropdown. Mode routing retains the existing audio ownership, background
polling, draft/page preservation and service-generation checks. Invalid choice
IDs and obsolete toggle callbacks cannot change modes. The Robust transport,
crypto and DSP sources remain unchanged.

Fast adds isolated display telemetry: 1,024 recent PCM samples, a 512-point
Hann FFT with 256 amplitude-dBFS bins, and 512 recent payload constellation
points. RX points come from equalized observations before decisions; TX points
come from the actual mapper. Immutable snapshots publish at most 10 Hz, and
the GUI retains at most 96 waterfall rows. Plot storage never grows with the
transfer. Initial, stalled, active and retained captures have distinct labels;
new transfer identities reset history without changing modem completion.

The new telemetry suite checks ring order/bounds, an independent −6.0206 dBFS
tone/bin reference, publication cadence, immutable snapshots, stream identity
and cancellation. Enabled and deliberately throwing observers produce exactly
the same TX PCM and RX soft evidence as the original no-observer path. The
focused telemetry suite also passes ASan/UBSan with no findings; LeakSanitizer
is disabled because it cannot run under the sandbox's tracing environment.

The live GUI test replaces only device callbacks, sends real generated PCM
through the Fast transmitter/receiver, and verifies all three plots advance
during TX and RX, old bitmap handles remain immutable, idle captures persist,
and received UTF-8 text remains exact. Direct renderer checks cover bounded
history, stream resets, actual point rendering and full-versus-damaged repaint
equivalence in RGB, grayscale and monochrome.

The complete FLTK Release build passed. The focused headless run passed
**48/48** in 165.65 seconds, including the new telemetry/live-GUI suites, all
Fast suites, the 92-case SNR matrix (121.85 seconds), shared GUI checks, original
short/interval vectors, sampled long-symbol physical-end checks and CLI/crypto
regressions. FLTK native adapter/document conformance passed 2/2 in 52.89 and
0.07 seconds on a private X display.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure -j 2 \
  -R '^(fast_.*|crypto|keyring|compression_short|transfer|stream_codec|stream_receive|recovery|attachment|pattern_correlator|cli|gui_.*)$' \
  -E '^(gui_workflow|gui_adapter_conformance|gui_document_conformance)$'
```

Rev production GUI/CLI and affected test targets also rebuilt. Its Fast GUI,
live-GUI and self-check suites passed 3/3 in 22.97 seconds. Standard native
adapter/platform conformance and 1×/2× coordinate tests passed in 43.15, 6.21,
5.18 and 5.17 seconds, with explicit `REV_SCALE=1` for the adapter. An initial
auto fractional-DPI run failed a one-pixel expanded
bitmap size assertion after the new mode/plot probes passed; no baseline rerun
was used to assign its cause, and no assertion was weakened.

Native screenshots were inspected at default and minimum window sizes. FLTK
used a real 16,012-byte Fast PCM loopback with stubbed device callbacks, covering
idle, live TX, live RX and completed captures. Rev used actual noisy modem
observations in temporary display fixtures, covering Text and File views.
Waveform, waterfall and constellation plots remained readable without overlap
or unintended clipping. These fixtures are not part of production code.

The unchanged long probability calibration and full GUI production workflows
were not repeated for this presentation/diagnostics change. No physical audio,
radio or Windows native execution is implied by the fixture checks.

## Fast optional encryption and text — 19 September 2026

Fast now sends text and files through the same fixed source stream. The GUI
defaults to Text with Encryption off; the CLI accepts exactly one of `--text`
and `--input`, and an existing `--keyfile` still enables encryption. Public mode
uses separate SHA-256 domains, no IV or cipher, and distinct checksum counters.
No source type, received length, mode negotiation or encryption fallback was
added. The encrypted bootstrap, CBC/HMAC format and independent wire vectors
are unchanged. Regular transport, crypto, DSP and receiver source files were
not modified for this revision.

Codec checks cover both protection modes at all three convolutional rates and
both RS settings, independent public checksum/wire vectors, exact source
endpoints, corruption, mode mismatch, quotas and physical-end gating. Separate
2 MiB streaming checks passed for both modes, as did AddressSanitizer and
UndefinedBehaviorSanitizer (LeakSanitizer disabled for the container tracing
limitation). Production S16 WAV and stubbed-device live session tests preserve
UTF-8, newlines, embedded/trailing zero bytes and empty sources; no transmitter
claims receiver completion or authentication. Text is bounded to 32,768 bytes
and invalid oversized input is rejected before starting output.

Review fixed two edge cases: orphan `--key-name`/`--pad` options now require a
keyfile unless explicit `--no-encryption` selects public mode, and cancellation
observed during post-end WAV interpretation clears completion and save access.
GUI checks cover source draft/key retention, encryption-on without a key,
literal bounded previews after completion, unchanged result protection labels
when the next-transfer setting changes, and regular draft/pending isolation.

Both production GUI/CLI builds succeeded. The final GCC focused headless run
passed **52/52** in 355.16 seconds, including every Fast suite, all shared GUI
suites matched below, regular tiny-message/interval/recovery/live checks,
sampled long-symbol physical-end checks, weak-signal, crypto/keyring and CLI.
`fast_snr` passed its 92-case matrix in 143.98 seconds. The unchanged long
differential-probability calibration and full production GUI workflows were
not repeated for this follow-up; their earlier results remain recorded below.

```sh
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure -j 2 \
  -R '^(fast_.*|crypto|keyring|compression_short|transfer|stream_codec|stream_receive|recovery|attachment|pattern_correlator|pattern_receiver|live_profiles|live_receptions|live|live_resources|weak_signal|cli|gui_.*)$' \
  -E '^(gui_workflow|gui_adapter_conformance|gui_document_conformance)$'
```

Final FLTK adapter/document conformance passed 2/2 on a private X display
(52.24 and 0.13 seconds). Final Clang/Rev checks passed 5/5 in 97.18 seconds:
`gui_fast`, adapter/platform conformance and the 1×/2× coordinate suites, with
`REV_SCALE=1` and software rendering. Both adapters' native Fast probes exercise
plain defaults, encryption-on without a key, UTF-8/newline input, source switching,
retained drafts and hidden callbacks. Default/minimum-size screenshots in both
backends were inspected for Text/File and public/encrypted states; no overlap or
unintended clipping was found. The FLTK screenshots precede the final idle-status
wording change; final native tests use the rebuilt binaries. `git diff --check`
passed. No physical audio/radio link was used.

This follow-up does not add hardware qualification or change the broader native
workflow limitations recorded in the original Fast validation below.

## Independent Fast APSK mode — 19 September 2026

Fast is implemented in a separate codec, sampled modem, streaming session and
GUI controller. The [Fast specification](fast-mode.md) records the actual
fixed 256-byte coded intervals, crypto domains, coding cycles, source endpoint,
channel profiles, commands and limits. Production build/CTest dependency guards
reject cross-imports between regular and Fast DSP. Regular short-message wire
vectors, encryption streams, symbol admission and pending/end rules are unchanged.
The regular live service adds explicit idle-device suspension; sampled tests
show that a pending regular `001` reception refuses suspension and remains
pending, and that acknowledgment waits for actual capture closure.

New codec coverage includes independently generated K=7/puncturing vectors,
CBC/HKDF/HMAC known answers and a complete independent wire fingerprint, all
coding rates and RS choices, canonical source boundaries, errors/erasures,
wrong keys, changed IV/ciphertext/tags, lost/reordered intervals, physical-end
gating and exact combined-spool quota limits. A 2 MiB streamed codec test passed
in 4.41 seconds at approximately 11,004 KiB process peak RSS. The codec also
passed AddressSanitizer/UndefinedBehaviorSanitizer; LeakSanitizer was disabled
because leak checking is unavailable under this container's tracing environment.

Sampled DSP and integrated file tests cover all four profiles and constellations,
fractional start timing, 44.1 kHz samples, chunk-size invariance, 100 ppm clock
offset, AWGN, hum/harmonics/narrowband interference, phase slips, noise/tone
rejection, exact encrypted binary files, EOF without sufficient absence and
missing final intervals. Selected 1/10/50 ms erasure fixtures and a 10 ms additive
sound effect recover; a destructive 500 ms interruption fails closed. Intermediate
100/250 ms cases must either return exact authenticated bytes or no completed file.
This does not establish recovery from arbitrary interference.

Production-random S16 WAV tests cover empty/binary sources, trailing zeros,
wrong keys, malformed/bounded RIFF input, trimmed silence, quota/cancellation
cleanup and exclusive output creation. CLI tests use an actual production-size
128 MiB keyring, two named keys, all profile/constellation/rate choices, invalid
settings, encrypted WAV transfer, wrong-key rejection and overwrite refusal.
No test keys or fixture entropy enter production settings.

The SNR tool uses measured waveform power and in-band AWGN normalized to the
declared RRC bandwidth. It runs independently of regular simulation and emits
all 23 levels from 10 to 120 dB in 5 dB steps. Recorded seed-417, 1,024-byte
results are checked in as [wire](validation-data/fast/wire.csv),
[SSB](validation-data/fast/ssb.csv), [FM](validation-data/fast/fm.csv) and
[acoustic](validation-data/fast/acoustic.csv). Wire/SSB default 16-APSK recover
22/23 exactly (10 dB fails closed); FM/acoustic QPSK recover 23/23. These 92
samples are regression fixtures, not statistically qualified error-rate curves.
The registered `fast_snr` CTest passed in 148.47 seconds, checking the full
92-case matrix, numeric bounds, option-order independence and exact completion.

The [2 MiB sampled transfer](validation-data/fast/wire-2m.csv) uses wire,
256-APSK, rate 7/8, robust RS, depth 16, seed 417 and 60 dB SNR. All 15,941
intervals and every source byte recovered exactly. File goodput including
six seconds of physical absence was **44,139.9 bit/s** over about 381 seconds
of audio; execution took 33.82 seconds wall/33.67 seconds CPU in this environment.
Reported modem plus soft-buffer workspace was 313,944 bytes and combined spool
storage 4,456,960 bytes. The former excludes other codec/device allocations and
is not total process RSS. Some 509-sample receive calls exceeded their 10.6 ms
audio duration (maximum 22.4 ms, 838 occurrences), so average throughput alone
does not establish a hard real-time callback guarantee. The live capture queue
separates these decode bursts from audio capture and fails explicitly on overrun.

A separate [production 16-bit WAV run](validation-data/fast/wire-2m-s16.json)
transferred 2 MiB containing every byte value through `pump fast-tx` and
`fast-rx`, with an actual keyring and production-random salt/IVs. The source and
saved destination SHA-256 values matched. At 256-APSK/rate 7/8 it recovered all
15,941 intervals, corrected one byte, and measured 44,132 bit/s source goodput;
the TX-plus-RX command pair took 23.20 seconds and produced a 36,512,846-byte
S16 WAV. `fast_files` now retains a 32 KiB version of this dense S16 regression
in addition to its QPSK/error tests; that final suite passed in 0.67 seconds.

Both Release GUI builds succeeded. Shared Fast tests cover view switching,
regular draft/page retention, ongoing hidden regular simulation, encryption
gating, stale callbacks/service replies, and fixed control layout. FLTK adapter
and document conformance pass. Rev adapter conformance passes at scale 1;
platform/clipboard and the standard coordinate suites pass at scales 1 and 2.
Default/minimum-size Fast screenshots were reviewed for fit and readability.
After final GUI refinements, `gui_fast`, `gui_application`, `gui_controller`
and `fast_boundary` passed 4/4 in 74.03 seconds.
An additional full Rev adapter run at scale 2 reached an existing planner
document clipping assertion and failed; no baseline comparison established its
cause. The standard scale-2 coordinate test passed, and no assertion was weakened.
An earlier unforced fractional-DPI Rev run failed the exact-size QR probe;
the standard scale-1 run passed with explicit `REV_SCALE=1`.

The FLTK production workflow initially reached its unchanged 300-second limit
in phase 17 while the long calibration and other GUI work competed for CPU.
After all other tests stopped, the isolated workflow passed without source or
timeout changes, covering key generation/reload, text/file transfer and saves,
binary editing, cancellation, retained results, plots and page switching.
The subsequent isolated Rev workflow at scale 1 completed keys, text and
attachment receive/save, then reached the unchanged 300-second limit in phase
17 while transmitting sampled audio to the independent receiver. Existing
validation history reports the same phase, but no baseline rerun establishes
causality for this attempt. Rev's full production workflow remains a validation
limit despite passing standard adapter/platform/coordinate conformance.
Evidence was retained in `/tmp/datapump-fast-fltk-isolated-final` and
`/tmp/datapump-fast-rev-isolated-final`; all test processes exited.

Reproduction commands (display-dependent tests require a private X display):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDATAPUMP_TEST_NATIVE_GUI=ON
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure -j 3 \
  -E '^(gui_workflow|gui_adapter_conformance|gui_document_conformance)$'
./build/fast_regression --profile wire --bytes 1024 --seed 417
./build/fast_regression --profile ssb --bytes 1024 --seed 417
./build/fast_regression --profile fm --bytes 1024 --seed 417
./build/fast_regression --profile acoustic --bytes 1024 --seed 417
./build/fast_regression --profile wire --apsk 256 --code-rate 7/8 \
  --bytes 2097152 --snr 60 --seed 417 --require-success
```

The full headless run passed **86/86** in 1,487.80 seconds, including every
development-contract suite. The unchanged differential receiver probability
calibration took 1,192.80 seconds. Together with the subsequently registered
`fast_snr`, all 87 distinct headless checks pass. Native workflow limitations
are recorded above separately. Physical DAC/ADC, IC-7100/FM/SSB radio, loudspeaker paths, RF masks,
and Windows audio/native GUI execution have not been qualified by these tests.
Whole-stream replay is not prevented; no adversarial-security certification is
claimed. Fast has no LPI claim and does not modify regular private waveforms.

## Joint differential reception probability — 19 September 2026

The selected estimate now samples all eligible detector branches jointly. It
calls the production differential accumulator on correlated noisy local fits,
shares observations with coherent/four-quarter fits, and retains their guards,
choice penalties, competing-bit margin and whole-symbol energy denominator.
It accounts for finite template power, shaped transmitter limiting, projection
averaging, separate alternative-template norms and finite carrier selection.
The compact receiver now uses its actual threshold accounting rather than
being charged for FFT-only initial private stream templates.

The model runs 4096 fixed draws for the selected estimate and 512 per curve
location. The CLI and shared GUI expose detector geometry, actual trial count,
95% sampling intervals, carrier-search approximation and explicit coverage
limits. Curve caching separates the selected estimate from lower-precision
curve points. Intervals exclude model error and physical-link uncertainty;
headline probabilities no longer imply tenths-of-a-percent reliability.

The main independent production-receiver matrix retains 15 conditions with
64 captures each. Thirteen one-bit conditions (832 captures) span public/private
patterns, stable phase, 10/25/40 degrees/sqrt(second) phase diffusion, weak
signals and detection transitions. At 64 samples/second each 512-second bit
contains 512 local one-second windows of sixteen chips; local duration is
scaled for repeated PCM testing. No channel noise seed or phase trajectory is
shared with the probability model. The compact receiver runs actual waveform
acquisition, exact-bit delivery and whole-symbol absence completion.
Predictions have RMS error **0.0289727** and maximum error **0.0695801** against the
observed proportions. Predeclared per-case `0.10 + 3*binomial_SE` and RMS `0.07`
gates remain unchanged. The two stronger drifting conditions recover 128/128
captures with the differential branch versus 92/128 with the older detectors
on the same audio: 36 additional recoveries. The two multi-bit conditions
(128 more captures) retain the explicit limitation described below.

Two additional full-receiver shaped conditions retain a separate RMS gate,
so they cannot dilute the original matrix's error target. Their natural
carrier-edge geometry uses the default one-frequency bank and compact engine;
512 eight-second windows each contain 128 chips, satisfying the local
real-quadrature covariance guard. Both use 28 dB nominal Es/N0 and
8.838835 degrees/sqrt(second) diffusion:

| Full shaped receiver case | Predicted | Observed |
| --- | ---: | ---: |
| Public pattern | 0.626953 | 41/64 = 0.640625 |
| Private pattern | 0.633301 | 38/64 = 0.593750 |

Shaped RMS error is **0.0295904**, maximum **0.0395508**. All per-case and
aggregate gates pass. Including the two tracking-limit cases, null/key controls
and default-duration captures below, the full new receiver suite completes
**1,124 PCM captures**, plus 128 comparisons using the older detector on the
same audio. The main matrix took 289 seconds and the two shaped cases 765
seconds in this run; four captures run concurrently with separate receiver
states. The CTest `calibration` label identifies this longer suite.

Independent shaped real-PCM statistic tests use 128 captures each, actual
radially limited transmission, sampled Brownian phase/noise, real Gram fits,
and production detector scoring. They do not run adaptive acquisition:

| Shaped statistic case | Predicted | Observed |
| --- | ---: | ---: |
| Stable, nominal Es/N0 = 40 linear | 0.782471 | 97/128 = 0.757813 |
| 25 degrees/sqrt(second), nominal Es/N0 = 500 linear | 0.815674 | 106/128 = 0.828125 |

Accounting for the actual finite shaped waveform reduced the stable-case
prediction error from 0.0886 to 0.0247. Independent noncentral-beta coherent
limits, zero-signal controls, weak noisy products, a useful drifting regime,
finite-trial interval endpoints and bounded unsupported geometry also pass.

The held-out compact `001` captures exposed an additional limitation rather
than supporting the old independent-bit formula: the earliest admitted timing
lane can retain a stream even when a later, closer lane is stronger. One
private case produced 54/64 exact completed receptions against a roughly 99.9%
independent-bit prediction. The new model explicitly withholds the aggregate
compact differential multi-bit percentage while retaining the first-bit
estimate. Both multi-bit fixtures and their physical-completion checks remain;
no numeric-error tolerance was relaxed to admit this discrepancy.

All 32 additional noise-only/wrong-key controls rejected unrelated input.
Four sampled 14.2-hour bits using the actual default 100-second windows also
completed with exact bits and a full absent symbol: public captures assert the
FFT path and private captures assert the compact path. These four are engine
and duration coverage, not a population reliability estimate. Existing FFT
batch/local-score parity and receiver differential regression suites also pass.

The exact reported GPSDO-TCXO command (0 dBm, 200 dB loss, -164 dBm/Hz,
0.01 Hz, target -44.25748830262745, 1500 Hz carrier) was reproduced through the
shared GUI controller and CLI. It selects 1,024 local windows of 3,200 seconds
and a 3,276,800-second bit (37.93 days). The joint model is active; all 4096
draws admit the bit, with a 99.9063–100% sampling interval and a labeled
17-candidate approximation to the 4,097-frequency bank. The local branch
supplies no unique recoveries in this mild-diffusion scenario; the older fits
already succeed. The earlier -38.092699609758306 target selects 409,600 seconds
and only 128 local windows, correctly retaining the older detector model.
Neither path is gated by an OCXO requirement. CPU-time figures remain the
existing unbenchmarked reference-work heuristic. In particular, the additional
shaped compact PCM captures perform expensive interpolation across timing
lanes; their wall-clock cost is not calibrated by the probability model.

The Release build, development-contract receiver/transport regressions,
estimator, CLI and shared GUI tests passed. Initial shared-GUI failures were
expectations about the previous unavailable model and shared availability/
curve-precision fields; the corrected semantic tests pass without changing
physical receiver assertions. `git diff --check` passes.

These are synthetic-channel and matched-statistic checks. The wide coupled
FFT bank in the 37.93-day example is not empirically calibrated by short
compact captures. Local covariance/phase/geometry guards bound model coverage;
full adaptive search, physical oscillator behavior, VLF noise, antenna
performance and RF propagation are not established by these tests. No wire
format, transmitter, receiver admission, bit progress or physical completion
behavior changed.

## Local differential receiver — 19 September 2026

Added a differential detector to both the streaming correlator and FFT
acquisition/tracking paths. Compact receivers enable its optional state when
workspace permits; FFT receivers include its scratch in their required budget.
It matches local windows and
accumulates soft products of disjoint neighboring pairs. The default window
is 100 seconds, rounded upward to whole chips and at least sixteen chips;
256 complete windows are required. Local duration is independent of complete
bit duration. The previous coherent and four-quarter detectors remain in the
comparison, with an additional detector-selection penalty. Transmitted samples,
wire bits, pending publication and full-symbol physical absence are unchanged.

Real-sample quadratures are whitened using their deterministic local Gram
matrix. Eight fixed product-phase directions use a conditional Gaussian-noise
tail bound, and the strongest positive quarter contribution is removed to
prevent evidence confined to one fixed quarter from supplying a whole bit.
This is a reference
noise bound, not a calibrated false-alarm guarantee for a physical front end.

The Release build and six detector-focused suites passed. The broader selection
also passed all 33 suites covering the development contract, receiver,
estimator, CLI and shared GUI. Its first run passed 32; the planner regression
still expected the older probability model for eligible local comparisons.
After updating that expectation, its rerun passed. The existing weak/strong
link and exact-bit-count probability checks remain at shorter supported
geometry, with the original phase-loss stress retained.

Sampled public and
private `001` captures with 32 phase rotations per bit plus a varying phase
component decode through both receiver paths at approximately -6.8 and -6.7 dB
sample SNR; the same captures fail the old coherent/four-quarter comparison.
Coverage includes seeded noise, wrong keys, unrelated carriers, isolated
quarters, EOF, partial symbols, immediate bit publication, complete absent
symbols, scalar/worker/chunk consistency, and releasing optional local state
without changing already accumulated legacy evidence. Compact constructor
checks show equal allocated state for 100-hour and 1,000-hour symbols.

Independent numerical tests cover weak individual matched windows, thousands
of seeded circular-noise trials, extreme Gram scaling, geometry/overflow,
skipped windows, and direct/FFT equivalence with nonorthogonal projections,
clock offsets and partial final windows. The 100-hour CLI analysis reports
3,600 local windows of 100 seconds and a null current-receiver probability.
The planner deliberately withholds its old probability model when the new
branch is eligible; timing, search, phase and compute diagnostics remain.

Wide FFT acquisition can cost substantially more because it evaluates many
local templates. Small start batches use direct matching; compact accumulation
retains constant state per hypothesis. Compute estimates include the extra
work. These checks use generated PCM and mathematical fixtures; no 100-hour RF
link, hardware oscillator or native display workflow was measured.

## Developer mode visibility — 18 September 2026

Added an initially unchecked Developer mode checkbox immediately left of Clear
received. The eight advanced controls and three inspection tabs are hidden in
place. The application retains their values, command-line overrides and layout;
switching the view does not edit the draft, invalidate inspection or reconfigure
reception. Disabling it on an advanced tab selects Console. Both adapters consume
shared tab visibility and continue to reject callbacks from hidden controls.

Release builds completed for FLTK and Rev. All 25 focused development-contract
tests passed, including independent short-wire vectors, physical-end checks and
pending reception regressions. All 28 headless GUI checks passed after updating
advanced-view fixtures to enable the toggle explicitly and clearing the draft
before the new exact-raw-bit preservation check. New shared coverage checks
default visibility, preserved geometry/settings, CLI overrides, stale callbacks
and page fallback; existing assertions remain intact.

Native FLTK workflow, adapter and document conformance passed on a private Xvfb
display. The document fixture now waits up to one second for its first paint
before checking the same exact framebuffer dimensions; its immediate X11 poll
could return before mapping finished. Rev adapter, platform and 1×/2× coordinate
conformance also passed, with advanced-control fixtures explicitly enabling
developer mode.
The adapters exercise real checkbox/tab callbacks, hidden focus eligibility,
active-tab fallback and unchanged native bounds. Final FLTK screenshots were
visually checked with the mode off and on at both 1180×1048 and 1030×968; the
checkbox fits the header and the hidden controls leave their allocated spaces.
These are Linux display checks; Windows and physical audio links were not run.

Rev's full production workflow did not complete within its existing test budget.
A concurrent run missed the phase-11 replay fraction (0.830509); an isolated
rerun passed that stage, then timed out at phase 17 after 300.55 seconds while
transmitting sampled audio to an independent receiver. The 300-second budget,
replay assertions and modem runtime remain unchanged. This limits end-to-end
Rev workflow validation, despite the passing feature and native conformance
checks above.

## Receiver probability and GPSDO assumptions — 18 September 2026

The planner and top bar now model both implemented long-pattern detector
branches. The bounded statistical calculation shares noise between competing
bits, retains unfitted signal in the observed-energy denominator, subtracts the
strongest section, and applies the actual rank and detector-choice penalties.
It approximates finite carrier refinement, fractional start timing, projection
loss and the standalone confirmation required for every long bit. The coherent
comparison uses the same draft and correction assumptions. Search-only planner
probes bypass the probability calculation. No wire, receiver, physical-end,
pending-progress or LPI calculation changed.

The GPSDO presets now share an illustrative 0.0001 ppm relative frequency
residual. Their separate phase-diffusion values remain 0.5, 0.05 and 0.005
degrees/sqrt(second). This removes the previous unsupported assumption that
locked frequency accuracy differs by 1,000 times solely with oscillator class.
The presets are sensitivity scenarios, not device specifications; GPS phase
corrections remain outside the channel model. Hard Clock/RAM coverage is tested
independently of phase diffusion. See [oscillator models](oscillator-models.md).

The new `receiver_probability` regression uses 20 conditions and 64 independent
sampled channel captures per condition (1,280 captures), with 128 additional
coherent-only receiver comparisons on the same audio. It spans public/private
patterns, 64/256-chip symbols, stable and wandering phase, frequency/sample-clock
offsets, shaped/unshaped waveforms, and exact one-bit and `001` messages. Every
case uses production sampled-channel generation and receiver admission, and
requires observed whole-symbol absence and exact received bits. Tests retain
their predeclared per-case discrepancy and 0.18 RMS limits; no gate was relaxed
to accommodate the new model.

Across these 20 scenarios, RMS discrepancy was 0.07214 and maximum discrepancy
0.14893 (probability units). Selected results at a 128 Hz sample rate and
16-second bit duration:

| Scenario | Modeled success | Sampled success |
| --- | ---: | ---: |
| Public, stable, 18 dB Es/N0, 256 chips | 81.2% | 57/64 |
| Private, stable, 18 dB Es/N0, 256 chips | 69.1% | 49/64 |
| Public, 60 degrees/sqrt(second), 26 dB Es/N0 | 91.1% | 59/64 |
| Private, same drift and energy | 88.1% | 56/64 |
| Public `001`, 30 degrees/sqrt(second), 20 dB Es/N0 | 40.1% | 35/64 |
| Private `001`, 60 degrees/sqrt(second), 26 dB Es/N0 | 58.0% | 43/64 |
| Shaped private, same drift and energy, one bit | 93.2% | 60/64 |

For the two 60-degree one-bit cases, section-enabled reception recovered 115/128
captures versus 87/128 with the coherent-only receiver. These comparisons use
independently sampled phase trajectories rather than the earlier handcrafted
phase/gain fixture. They validate a useful statistical approximation, not
hardware reliability or extreme false-alarm tails.

Additional temporary probes checked the live compact policy with a real test
key (64-second symbols, 26 dB Es/N0): stable reception was 64/64 versus modeled
100%, and 30-degree diffusion gave 60/64 versus 79.9%. A public 2,049-sample
symbol forces one-sample projections: at the same energy, stable reception was
53/64 versus 77.5%, and 30-degree diffusion gave 6/64 versus 2.0%. This supports
keeping physical noise dimensions distinct from capped evidence dimensions.
Permanent direct tests also cover detector-choice cost, shared competing-bit
noise, finite frequency refinement, unfitted-energy ceilings, and the absence
of an artificial phase-coherence floor at extreme durations.

Selected planner computations took roughly 58–96 ms in a local probe, with
search-only navigation around 10 ms. The estimator uses 4,096 fixed statistical
draws (approximately 5 MiB shared storage), a small cache and bounded quadrature;
its work does not grow with represented symbol duration. These timings are
local observations, not UI latency guarantees or simulated decoder timings.

Validation completed with GCC/FLTK and Clang 19/Rev Release builds. GCC passed
the 29 selected contract, LPI, correlation and shared-GUI suites, plus the new
sampled probability regression. Twelve targeted Clang/Rev suites passed.
During final checking, a seed-derivation change incorrectly demanded transfer
credentials for an already seeded private modem configuration. The permanent
matrix caught it; the estimator now derives a transfer seed only when a key is
provided, and both compiler builds passed the affected probability and
simulation-estimate suites again after the fix. No acceptance gate changed.

The updated `simulation_estimate` suite passed AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer first failed because this sandbox's
tracing environment prevents its operation; the successful rerun used
`ASAN_OPTIONS=detect_leaks=0`, so this is not a leak-check result. These checks
do not establish native-window rendering or physical-radio performance.

## Bounded phase and gain tolerance — 18 September 2026

Long pattern reception adds four fixed section fits alongside the original
coherent match. Each quarter needs at least sixteen complete chips, and the bit
must last at least sixteen seconds. Section boundaries use symbol coordinates,
including the tested clock hypothesis, rather than input chunks. The score
removes the strongest quarter's explained energy so a filter tail cannot
supply a whole bit, uses a conservative four-coefficient noise tail, and charges
`ln(2)` for choosing between two detectors. The compact branch caps repeated
samples within a chip at the existing FFT evidence scale. This is a bounded
channel-fit improvement, not arbitrary phase tracking over a days-long symbol.

The compact receiver retains one active section and fixed summaries per
hypothesis. It reserves this state when selecting the bank, materializes it
at the first quarter boundary, and retains the coherent detector if the extra
state cannot fit. FFT sections reuse product scratch and stream templates;
small start batches directly reuse generated samples. Runtime getters identify
the selected detector. The planner and top bar label eligible numerical
probabilities **RX reference** because the added gain is not calibrated. Compute
estimates include the new work and scratch. LPI formulas, waveform generation,
dictionary/raw endpoints, framing and physical completion rules are unchanged.

The GCC Release build succeeded. The existing `pattern_receiver`,
`pattern_correlator` and `simulation_estimate` suites passed together in
120.28 seconds while a full build ran concurrently. They include sampled
four-hour progress, compact 64 KiB idle bounds, full clock-bank coverage,
coupled-clock decoding and whole-symbol absence. Initial checks exposed a
compact carrier-evidence regression, extra memory pressure and an FFT filter
tail admitted as a fourth bit; these were corrected without relaxing those
regressions. Original optional-template-cache coverage remains explicit with
the coherent detector, alongside a new assertion that section scoring streams
its templates.

The new `pattern_drift`, `pattern_fft_batch` and `pattern_correlator_batch`
suites passed together in 5.93 seconds. Sampled phase-flip and phase-wander
fixtures at approximately −8.3 dB measured sample SNR recover exact `001` on
both receivers while the coherent baseline fails. Coverage includes gain
steps, unrelated carriers and keys, noise-only input, isolated quarters and
tails, whole-symbol absence versus EOF, chunk/worker invariance, budget fallback,
and unchanged ineligible scores. Direct/FFT comparisons cover public/private,
shaped/plain, real/complex observations and clock offsets; independent binomial
tails check the section evidence calculation.

Thirty other selected GCC suites passed in the 341.51-second contract run.
Its new CLI assertion initially assumed identical quarter lengths; integer
sample boundaries can differ by one sample. After checking the exact longest
quarter instead, `cli` passed all 25 cases in 32.65 seconds. Together these
runs cover 37 distinct suites, including every development-contract suite and
the LPI, waveform and shared GUI regressions.

On the small identical-PCM fixture, FFT time changed from 20.81 to 34.62 ms and
peak workspace from 193,747 to 173,267 bytes; compact time changed from 86.53
to 107.54 ms and workspace from 49,006 to 49,806 bytes. These are diagnostic
measurements under concurrent build/test load, not calibrated throughput.
An additional warmed public FFT probe measured approximately 3.4–3.7 times
the coherent runtime on the same exact decoded `001` input.
Full acquisition has about four times the uncached template FFT work; tracking
and tiny direct batches have different costs. Compact retained state remains
independent of symbol duration.

The three new/focused DSP suites also passed ASan, UBSan and LeakSanitizer
in 40.10 seconds with leak detection enabled. The sandbox prevented the initial
LeakSanitizer process inspection; the automatically approved unsandboxed rerun
passed without errors or leaks.

The final GCC/FLTK and Clang/Rev Release builds succeeded. All 17 selected
Clang/Rev DSP, estimator, LPI and shared GUI suites passed in 153.01 seconds.
`git diff --check` passed. Native window rendering and physical radio links
were not tested; the sampled fixtures do not calibrate sensitivity for arbitrary
oscillators, interference or channel trajectories.

## Relative LPI observation against one receiver bit — 17 September 2026

The advisory now compares total observer bit durations with the receiver's
one-symbol design reference. Both listeners use the same received C/N0,
normalized so one sampled symbol has the existing 18 dB Es/N0 planning
reference. It is explicitly uncalibrated. Simulation/live link power and
oscillator presets no longer enter the LPI estimator. TX targets affect the
ratio only through selected waveform geometry. GUI details and CLI JSON
distinguish total N:1 observation, N-1 additional durations, normalized C/N0,
and the supplemental current-draft exposure comparison. The encryption-off
hypothetical warning remains, including for tone and unavailable numbers.
No waveform, wire format, receiver admission, physical completion or pending
progress behavior changed; the tuning reference retains its existing value.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). GCC `cli` passed in
30.84 seconds. The final estimator and eight shared GUI checks passed in
11.71 seconds: application, inspection, inspection page, layout, contract,
link boundary, adapter boundary and boundary regression. The seven selected
Rev checks passed in 1.58 seconds: estimator, application, inspection,
inspection page, layout, contract and link boundary. An initial fixture for
equal time-bandwidth products used an explicit decimal duration that rounded
up one sample; the corrected fixture uses exact spreading geometry. The separate
sample-rounding regression remains. Fixed numerical references, duration and
bandwidth scaling, unchanged keyless/keyed ratios, invalid input and numerical
limits passed. CLI assertions compare identical LPI objects under different
channel power, noise figure, oscillator and RX search settings.

The remaining 17 non-controller development-contract suites passed in 179.24
seconds. The isolated controller suite passed in 71.68 seconds, including
identical LPI summaries and details across every simulation and oscillator
preset, geometry-dependent TX changes, fixed-geometry target changes,
encryption/tone transitions and exact 001/1,216-bit draft endpoints. Together
the 28 selected GCC suites include all 21 development-contract suites.
`git diff --check` passed.

Native window rendering and physical reception were not tested. This remains
a relative Gaussian energy-detector model, not a measured reception threshold,
interference measurement or guaranteed hidden-traffic allowance. See
[the model and its limitations](lpi-estimates.md).

## Hypothetical LPI estimates without encryption — 17 September 2026

The advisory now computes the encrypted private-pattern scenario without a
selected key, including tone experiments. It preserves the current sample,
chip and symbol timing and C/N0; tone experiments use the corresponding
private pattern's modeled bandwidth. The GUI prominently labels encryption-off
results as hypothetical, and CLI JSON exposes `hypothetical_encryption` and a
warning. The warning persists when the existing strong-signal or numerical
limits prevent a number. Current draft exposure stays a duration comparison;
the advisory neither selects a different automatic profile nor re-encodes
authentication overhead. No key generation, encryption enablement, waveform
change or receiver change occurs.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). GCC `lpi_estimate` and
`cli` passed in 27.91 seconds, followed by eight shared GUI checks in 10.36
seconds: application, inspection, inspection page, layout, contract, link
boundary, adapter boundary and boundary regression. The corresponding seven
selected Rev checks passed in 1.57 seconds: estimator, application, inspection,
inspection page, layout, contract and link boundary. Added assertions compare
keyless and keyed numerical results at identical geometry, cover tone and
model-limit warnings, and preserve the independent exact `001` wire endpoint
and actual transmission settings. `git diff --check` passed.

The remaining 17 non-controller development-contract suites passed in 174.60
seconds, and the controller suite passed separately in 68.88 seconds. The
controller assertions cover encryption off/on/off/on/tone transitions, numerical
availability, warning withdrawal/restoration and exact draft bits. Together the
28 selected GCC suites include all 21 development-contract suites.

Native window rendering and physical reception were not tested; adapters and
desktop geometry are unchanged. The hypothetical result describes private
patterns under the existing model, not the actual public or tone waveform.

## LPI energy-detection advisory — 17 September 2026

Added a shared GUI advisory and `lpi` JSON results to CLI `estimate` and
`analyze-link`. The weak-signal radiometer model reports the listening time and
equivalent wire symbols for 90% detection and 1% false alarm per known window,
assuming equal received C/N0, a known band and known stationary noise power.
Simulation supplies its link C/N0; otherwise the TX target is explicitly an
assumption. Inspection also shows the assumed bandwidth, noise rise and
whole-burst exposure, with settling/filter/suppression as an equal-power
approximation. See [the model](lpi-estimates.md) for equations and limits.

The model uses sample-quantized geometry, accounts for shaped bandwidth, and
does not multiply the gain for the independent DSSS layer. Eligibility follows
transfer's existing automatic private scrambling for every keyed non-tone
transmission, including manual CLI profiles. Strong signals and numeric limits
have no numerical protection interval. Sub-symbol detection is explicit;
neither the count nor the exposure ratio is a safe traffic quota.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). The 11 focused GCC
checks passed in 27.86 seconds: `lpi_estimate`, `cli`, `gui_layout`,
`gui_contract`, `gui_link_boundary`, `gui_inspection`, `gui_inspection_page`,
`gui_application`, `gui_self_check`, `gui_adapter_boundary` and
`gui_boundary_regression`. The remaining 17 non-controller development-contract
checks passed in 184.32 seconds. The controller suite passed separately in
67.78 seconds, covering key loading, actual versus assumed C/N0, stale-estimate
withdrawal, short/long target selection, and existing pending reception behavior.
Together these cover all 21 required development-contract suites and eight
additional suites. Final wording was rebuilt and its two affected inspection
and application suites passed again. After the final numerical range guard,
the estimator, controller and CLI suites passed together in 97.05 seconds.

The eight selected Rev checks passed in 35.41 seconds: `lpi_estimate`,
`gui_layout`, `gui_contract`, `gui_application`, `gui_inspection`,
`gui_inspection_page`, `gui_link_boundary` and `gui_self_check`. Final wording
was rebuilt with the affected two suites passing again; the final numerical
range guard was rebuilt with `lpi_estimate` passing again. Tests include fixed
numerical references, signal-strength and symbol-duration scaling, no duplicate
DSSS gain, private manual CLI profiles, fractional-symbol estimates, numerical
limits, exact three-bit short/raw equivalence, and unchanged 1,216-bit interval
geometry. `git diff --check` passed.

No transport, waveform, receiver-admission, physical-end or pending-bit logic
changed. Native windows were not rendered: this environment has no display or
Xvfb. Shared layout tests and both backend builds do not establish native pixel
conformance. No physical-link or adversarial-detector measurement was performed;
the new probabilities remain an idealized model rather than calibration data.

## 1.2 kHz feasibility and tracking cost — 17 September 2026

The fixed-laptop compute estimate now includes serial continuation of one
desired stream per matching FFT profile, through nominal complete-symbol
absence. That component remains in both CPU and hypothetical GPU totals and
is exposed as `tracking_seconds` / `tracking_symbol_windows` in `analyze-link`.
No hardware benchmarking, receiver algorithm, confidence probability, wire
format or pending/completion behavior changed. Added regressions cover exact
one-/three-/ten-bit workload scaling, whole-symbol absence, empty drafts,
matching profiles, unrelated keys and correlator accounting.

The 1.2 kHz / -10 dB-Hz target / +3 dBm/-170 dB / crystal case reproduces
1,895.899 seconds of TX waveform and 2,527.892 seconds of simulated media.
Its corrected i9 estimate is 1,461.826 seconds, including 331.056 seconds of
serial tracking; the hypothetical GPU total is 448.252 seconds. A seed-1
sampled probe with an explicit matching RX target and 1 GiB DSP workspace
was stopped at its 240-second verification limit without output. Reception
remains unverified; the limit is not a failure result or calibration datum.

Bounded 100,000-trial reference experiments examined day-long -200 dB and
-230 dB cases. The 2 dB noise-figure / GPSDO-TCXO / -200 dB candidate has
99,936 correct reference detections, conditional on acquired timing/clock,
zero residual frequency and prescribed orthogonal templates. Its current
receiver workspace remains unsupported and requested-work CPU estimate is
about 41.3 hours. The [case study](1200hz-weak-link-planning.md) records the
commands, oscillator/energy limitations and distinction from actual reception.

Both Release builds succeeded (GCC/FLTK and Clang/Rev). The 24 selected GCC
checks covered the full development-contract filter plus `correlation_experiment`,
`gui_contract` and `gui_layout`; 23 passed in the concurrent 672.30-second run,
and `gui_controller` passed an isolated rerun in 77.46 seconds. The seven
selected Clang checks covered the estimator, statistical experiment, CLI and
shared GUI application/controller/contract/layout; six passed in the initial
83.98-second run, and the final controller rerun passed in 90.54 seconds.

The convenience-message GUI test initially exceeded its fixed 15-second wait,
including in an isolated Clang run. A temporary instrumented Clang test observed
the exact message and finished transmission after 15.9868 seconds; all original
byte, metadata and composer-state assertions passed. Its test-only deadline
was therefore increased to a bounded 60 seconds, retaining 10 ms polling and
every assertion. The Clang rerun above uses that correction. No application
timeout or completion behavior changed. Native display workflows, physical
audio and Windows were not rerun. `git diff --check` and case-study numbers
and local documentation links passed their checks.

## Corrected transmit power and oscillator selection — 16 September 2026

The two mistaken -3 dBm presets were removed; the existing +3 dBm/-200 dB and
+3 dBm/-230 dB entries are the corrected choices. Their actual C/N0 values
are -33 and -63 dB-Hz under the unchanged noise model. Custom negative transmit
power remains supported by the analysis command. The planning examples and
statistical API defaults now use the corrected +3 dBm case. Seven 10,000-trial
reference cases were rerun at that power; current results are recorded in
[fast weak-link planning](weak-link-planning.md).

The shared GUI now offers free-running crystal, hobbyist GPSDO/XO without an
oven, GPSDO/TCXO without an oven, and GPSDO/OCXO models. Numeric residual clock
and phase values are visible. Both GUI backends use the same declarations and
configuration path, with one extra 48-pixel row preserving existing content
allocations. The selector changes both sampled channel settings and the
estimate, follows the transmission busy lock, preserves draft wire geometry,
and invalidates estimates made with the previous oscillator. Other modem
edits retain the choice. The original 100 ppm / 0.5 degrees/sqrt(second)
crystal scenario remains the default.

The same `--oscillator` profiles apply to `simulate`, `listen` and
`analyze-link`. Explicit clock/phase flags override their respective values.
New CLI tests compare sampled WAV output byte-for-byte between GPSDO profiles
and equivalent explicit channel settings, and compare analytical outputs for
all four profiles. They also check corrected link arithmetic, unknown profile
rejection, override metadata and command scope. These checks validate the
configuration mapping, not a physical GPSDO's performance.

At +3 dBm/-200 dB and 125,892.541-second symbols, the 10,000-trial coherent
reference model gives 2.45% for crystal and hobbyist XO, 99.65% for TCXO, and
99.73% for OCXO. GPSDO profiles bring the modeled carrier into the existing
search, but its modeled FFT workspace remains unsupported for this case.
Production RX confidence therefore remains unavailable. The profiles are
illustrative relative link impairments, not device specifications or models
of GPS servo dynamics, warm-up, holdover, or oscillator aging. The
[oscillator documentation](oscillator-models.md) records the primary sources
and why long-term GPS accuracy does not imply short-term phase coherence.

Both Release GUI executables and CLIs rebuilt. All 66 headless tests passed
(280.15 seconds), retaining the independent short-wire vectors, sampled weak
reception, physical-end and pending-bit regressions. Rev's eight focused
configuration, layout, controller, application and CLI suites passed
(68.28 seconds). Native Rev self-check, adapter/platform conformance and
1x/2x coordinate checks passed (five tests, 71.91 seconds). FLTK self-check
and document conformance passed; adapter conformance passed in 40.36 seconds
after a test-only timing correction. Its expanded-bitmap caption test had
raced the separate 40 ms source and 100 ms presentation polls with a fixed
130 ms delay. It now waits at most two seconds for the same caption,
visibility and exact tone predicate. No runtime behavior or assertion was
weakened. Both actual native windows were also captured on private displays
and visually inspected; the oscillator row and existing controls fit.

```sh
ctest --test-dir build --output-on-failure -j 2 \
  -E '^(gui_self_check|gui_workflow|gui_adapter_conformance|gui_document_conformance)$'
ctest --test-dir build-rev --output-on-failure -j 2 \
  -R '^(tuning|simulation_estimate|correlation_experiment|gui_layout|gui_contract|gui_application|gui_controller|cli)$'
```

## Bounded extreme-link planning — 16 September 2026

`pump analyze-link` now runs three matched-correlation reference experiments
without PCM generation or production receiver execution. Its statistics have
constant storage and work proportional to the Monte Carlo trial count, even
for represented symbols lasting years. The output retains exact draft wire
size and airtime, current receiver coverage limits, and the fixed i9-13900H /
RTX 4090 Laptop GPU compute estimates. The GPU estimate remains hypothetical;
no hardware benchmark or new receive backend was added. The initially added
-3 dBm/-200 dB and -3 dBm/-230 dB presets were subsequently removed after the
user corrected transmit power to +3 dBm; the existing positive-power entries
already cover those links.

The statistical unit suite compares reduced draws against an independent
explicit complex-segment implementation for one and four segments, including
correlated alternatives. It checks moments, detection probabilities, exact
integer-shape Gamma noise tails, search penalties, joint phase/frequency
coherence by numerical quadrature, the long-duration Wiener energy asymptote,
full-duration segment coverage, deterministic seeds and invalid inputs.
The extreme-geometry case represents 1e12 segments over 1e18 seconds without
iterating over them. These are statistical-model checks, not physical modem
calibration.

New CLI tests preserve exact `0`, `001` and `a` wire lengths and existing
airtime estimates; separate channel power from the TX design target; preserve
explicit independent RX profiles; test both custom and preset links; reject
misplaced/invalid options; and analyze a billion-second symbol under a
10-second subprocess timeout. Analysis outputs contain no received bits,
decoded source or physical-completion events. The four new CLI tests also
pass with the Rev/Clang-built CLI (0.31 seconds combined).

Eleven bounded 10,000-trial scenario runs covered -3 dBm at -200, -210 and
-220 dB attenuation, durations from 501,187 seconds to 1e10 seconds, 3,600-
and 20,000-second coherent segments, 0.5 and 0.05 degrees/sqrt(second) phase
diffusion, and 1e6 or 1e8 prescribed search alternatives. Their computed
examples were recorded in the original planning document; that document now
contains rerun +3 dBm examples following the power correction.
They expose the strong loss from phase diffusion and the growing time cost
of segmented energy accumulation. They do not establish actual reception
at -200 dB, whole-message probabilities, real-world confidence intervals or
an interception bound. Sampled receiver behavior is unchanged by this work.

Both Release GUI executables and CLIs build successfully. Rev's focused
`correlation_experiment`, `tuning`, `simulation_estimate`, `gui_controller`
and `gui_application` suites pass (five tests, 62.43 seconds). Native adapter
code was unchanged; native display conformance was not repeated for this
planning-only addition.

The complete focused development-contract run plus the new statistical suite
passes: 22 tests in 222.87 seconds, including sampled `weak_signal`, whole-symbol
physical-end tests, short wire vectors, fixed intervals, pending GUI updates,
live profile arbitration and the full CLI suite. No existing assertions were
relaxed. Commands used after both Release builds:

```sh
ctest --test-dir build --output-on-failure -j 2 \
  -R '^(live_profiles|live_receptions|live|live_resources|compression_short|transfer|stream_codec|stream_receive|recovery|attachment|pattern_correlator|pattern_receiver|pattern_search|tuning|simulation_estimate|correlation_experiment|weak_signal|gui_application|gui_controller|gui_inspection|gui_binary_editor|cli)$'
ctest --test-dir build-rev --output-on-failure -j 2 \
  -R '^(correlation_experiment|tuning|simulation_estimate|gui_controller|gui_application)$'
python3 tests/test_cli.py build-rev/pump -k link_analysis
```

## Sub-Hz planning and weak-signal clock search — 16 September 2026

The application now accepts 0.01 Hz through 30 MHz. Sub-Hz tests preserve exact
`a=011` and raw-bit lengths, fixed message framing, bounded waveform generation
and whole-symbol absence. An independently sampled 0.01 Hz control at Fs=64 Hz,
carrier=16 Hz and zero clock/phase drift recovers `a` using 12,800-second symbols.
That control exercises long coordinates efficiently; it does not establish
unrestricted oscillator tolerance at 0.01 Hz.

For pattern symbols of at least 16 seconds, application receive paths request
a bounded carrier lattice at spacing 0.25/T, targeting ±200 ppm with at most
4097 distinct offsets. Each has nominal and carrier-coupled timing alternatives.
FFT templates and next-symbol predictions use the hypothesized clock rate;
the longest hypothesis must be observed before comparison. Projection bins
retain the expanded offsets. Additional trials increase evidence penalties.
Matching-time carrier alternatives compete before admission, preventing the
existing bit-mask harmonics from producing wrong-label alias receptions.
Weak candidates cannot migrate onto a confirmed stream and publish duplicate
suffixes. No waveform bits, short dictionary codes, FEC or framing changed.

Wide FFT banks can generate templates in bounded scratch instead of retaining
all transformed rows. Expanded live banks sharing RAM stream those rows so
early key/epoch caches cannot crowd out the remaining banks. Private expanded
searches also use FFT competition. If its core cannot fit, application callers
may retain the original local five-bin, nominal-clock correlator and live status
reports the narrower coverage. Explicit expanded searches reject insufficient
workspace by default; expanded per-lane correlator admission is never used.
The existing local correlator and its four-hour physical progress tests retain
their original path. The model uses the requested wide search geometry
and regeneration work while keeping the fixed i9-13900H/RTX 4090 Laptop reference
budgets. It withholds percentages outside modeled carrier or FFT-workspace
coverage. No hardware benchmark or GPU execution backend was introduced.

Actual sampled PCM tests use the +3 dBm/-170 dB preset (-3 dB-Hz actual C/N0),
the default 1,500 Hz carrier, 100 ppm crystal error, 0.5 degrees/sqrt(second)
phase diffusion and the exact three-bit message `a`. The receiver gets no
channel offset, channel seed, expected text or payload length:

| Rate and design target | Symbol duration | Sampled results |
| --- | ---: | --- |
| 1 Hz, target -6 | 256 seconds | Seeds 1, 2 and 3 each recover exact `011`/`a`, one stream and one physical completion |
| 100 Hz, target -6 | 327.68 seconds | Seeds 1 and 2 each recover exact `011`/`a`, one stream and one physical completion, with a 64 MiB DSP limit |
| 100 Hz, target -3 | 163.84 seconds | Seed 1 has insufficient acquisition margin in the full blind search and does not recover the complete message |
| 1 Hz, target 32, +3 dBm/-120 dB | 128 seconds | Original strong-link failure now recovers exact `a` once; former alias duplicates are rejected |

Matched-duration noise-only controls produce no reception at 1 Hz or 100 Hz.
The 100 Hz streamed search retained about 9.45 MB idle state within its 64 MiB
ceiling. The finite seed checks establish these cases, not calibrated 99.9%
population reliability, practical -200/-230 dB links or an interception bound.
The permanent `weak_signal` suite retains the three 1 Hz seeds, a separate
noise-only control and the 100 Hz seed-1 sampled path.

Cached and streamed FFT tests compare exact candidates, scores and every
progress poll across serial and parallel execution. Coupled-clock tests cover
±200 ppm and ±8000 ppm stress cases, nine exact bits, duplicate carrier/timing
alternatives, bounded memory, immediate pending drainage, EOF/partial-silence
rejection and fully observed physical completion. Both compact preference
settings retain FFT competition; unsupported direct-correlator requests reject.

Both Release GUI executables were rebuilt. Native FLTK self-check, adapter and
document conformance passed (three checks, 60.68 seconds). Native Rev self-check,
adapter/platform conformance and 1x/2x coordinates passed (five checks,
95.33 seconds), each on a private virtual display. Rev's focused tuning,
search-geometry, FFT-batch, estimator and controller checks passed
(five suites, 70.54 seconds). Reference-hardware benchmarking was not performed.

Follow-up memory regressions preserve the original local five-bin candidate
scores, every progress poll and physical completion when an application opts
into fallback. Explicit frequency/clock requests and missing clock windows
remain strict. Shared expanded banks retain their full search while releasing
template-cache space. The original live and live-resource assertions pass.
An added sampled CLI case also receives exact `a` at a valid shaped passband
edge; implicit local searches retain their feasible offsets, including a
center-only bank, while explicit invalid offsets are rejected.

After the final receiver changes, native self-checks passed again for FLTK
(16.29 seconds) and Rev (20.12 seconds). The revised per-bank memory estimate
also passed on Rev (13.70 seconds), including the half-budget ceiling used by
live reception. Both GUI executables were rebuilt with these changes.

The final full headless run passed all 65 suites (256.45 seconds), including
the development-contract coverage, all 23 shared GUI suites, the unchanged
live/resource checks and the new `weak_signal` suite (142.37 seconds). Command:
`ctest --test-dir build --output-on-failure -j2 -E '^gui_(self_check|workflow|adapter_conformance|document_conformance)$'`.
`git diff --check` also passed. These are software and sampled-channel checks;
physical audio, RF links and interception performance were not measured.

## TX design target versus simulation channel strength — 16 September 2026

The 100 Hz comparison was checked with public auto-pattern, text `a`, default
1,500 Hz carrier, 100 ppm clock error and 0.5 degrees/sqrt(second) phase noise.
The +3 dBm/-170 dB preset supplies -3 dB-Hz C/N0; target -61 selects about
79,432,823 seconds per bit and falls outside carrier-search coverage. The
+3 dBm/-120 dB preset supplies +47 dB-Hz C/N0; target 140 reaches the applicable
64-chip floor at 1.28 seconds per bit. Its sampled-channel regression receives
exact `011` and decoded `a` with physical completion. The numerical target is
a design input for duration, not a required minimum received C/N0. No reversed
units or change to simulated power was found.

Shared help now explains the two independent settings and the drift limitation.
New model/controller regressions cover both reported 100 Hz cases and verify
that changing the target cannot change channel SNR. The local Rev executable
still contained the estimator from before the carrier-search fix; both default
FLTK and Rev builds were refreshed and checked for the corrected explanation
and target help. Six focused suites passed in the default Release build
(57.01 seconds): estimator, tuning, controller, application and both adapter
boundary guards. Rev's estimator/tuning/controller suites passed (60.97 seconds),
as did all five native checks on a private software-GL display: self-check,
adapter/platform conformance and 1x/2x coordinates (67.82 seconds).
These sampled checks validate the specific cases, not a
calibrated population-wide 99.9% success rate. No full multi-year symbol or
reference-hardware benchmark was run.

## Carrier-search coverage in simulation confidence — 16 September 2026

The reported public auto-pattern case (1 Hz, 32 dB-Hz target, +3 dBm/-120 dB,
text `a`) reproduced the misleading >99.9% estimate. The exact wire bits remain
`011`. At the default 1,500 Hz carrier, 100 ppm clock error shifts the carrier
by 0.15 Hz; the 128-second symbol's five frequency hypotheses span only
±0.00390625 Hz. A sampled-channel reproduction retained a maximum score of
12.52 against an admission threshold of 39.67 and admitted no reception. With
zero clock error, the same source/channel strength produced `011` and `a`
after the fully sampled absence tail.

The estimator now marks numeric confidence unavailable outside its modeled
carrier-search span, including either sign of explicit frequency offset plus
carrier clock drift. The GUI shows **Carrier outside RX search** and retains
both reference compute estimates. This deliberately makes no zero-probability
claim: the actual normalized energy fits can sometimes admit signals outside
the bank, but the previous attenuation-only approximation cannot predict that
regime. Receiver search, sampled channel, wire format and completion rules are
unchanged.

The Release build and all 22 selected contract, estimator and shared GUI suites
passed (172.71 seconds). The estimator regression includes the actual sampled
failure and successful zero-drift control, a separate EOF-without-absence
negative control, both frequency-bank edges, sample-quantized durations, and
stronger-SNR exclusion. GUI checks verify the exact reported settings, preserved
CPU/GPU values and restoration of numeric confidence when coverage returns.
The explanation was visually inspected in native FLTK windows at default and
minimum size, including a stronger 1 Hz preset. `git diff --check` passed.
Rev native conformance was not rerun for this shared text/model change; no
adapter code or geometry changed. No reference-hardware benchmark was run.

## Simulation probability and reference compute estimates — 16 September 2026

The persistent Simulation row now shows a modeled whole-draft reception
probability, an Intel Core i9-13900H CPU computation estimate and a projected
RTX 4090 Laptop GPU computation estimate. A bounded analytical model consumes
the existing encoded draft estimate and independently configured receiver
profiles. It preserves exact short/raw bit lengths, applicable interval FEC,
sample-quantized symbol timing and fully scored absence tails. It runs no local
benchmark and does not inspect processor/GPU identity. Fixed throughput budgets
and probability assumptions are documented in [simulation estimates](simulation-estimates.md);
these checks do not calibrate the model or establish GPU execution support.

The shared controller invalidates displayed estimates on draft/settings changes,
rejects stale preparation results, and explicitly presents off, invalid and
unavailable states. FLTK and Rev use the same declarations and geometry. The
default/minimum desktop heights increased by 43 logical pixels to retain the
existing content space while making room beside the dropdown.

Release builds succeeded. All 23 selected suites passed in 174.69 seconds:
the complete focused development-contract set, the new `simulation_estimate`
suite, shared layout/contract/link-boundary tests and adapter-boundary guards.
The new checks cover SNR and length response, unprotected short/raw input,
fixed-interval FEC, unmatched profiles, independent bank cost, impairment losses,
four-hour absence accounting, equivalent dictionary/raw draft estimates and
stale/off/invalid GUI states. No transport regression assertions were relaxed.

FLTK self-check and native adapter/document conformance passed. Rev self-check,
native adapter/platform conformance and 1x/2x coordinates all passed (five tests,
74.49 seconds). Default
1180 by 952 and minimum 1030 by 872 windows were captured and visually inspected
in both FLTK and Rev; the three estimate labels were readable without overlap.
The initial sandboxed native attempt could not create Xvfb's local display
socket; successful native retries used private displays outside that sandbox.
No reference laptop benchmark, physical-link calibration, Windows execution or
GPU execution was performed.

## Narrow-band simulation scheduling — 16 September 2026

A reported 37% CPU utilization in a public 1 Hz simulation exposed a scheduling
limit in the numerical correlator. The live receiver uses the default single
clock-rate hypothesis, not the three-rate bank in the earlier standalone
benchmark. At a 6 kHz sample clock, a two-second chip and seven-second start
uncertainty produce 15 origins across five frequencies: 75 lanes. Fixed groups
of 16 gave the executor only five runnable jobs, regardless of its 11-worker
setting. Correlation now chooses smaller groups for small banks, retaining the
previous maximum grain for large banks. No hypothesis or arithmetic changes.

A bounded actual `live::Session` reproduction used 1 Hz, the default 1,500 Hz
carrier, a -30 dB-Hz TX target, public/no-key operation, RX targets `-30,55`, a
six-second epoch search setting, 2,600 MiB workspace, and the +3 dBm/-170 dB link
preset. The harness waited for at least 30 seconds of sampled media, then
measured ten seconds of processing and cancelled. The two versions linked the
same library except for the correlator batch object; the second pair reversed
their order. Both selected 11 workers from 12 available logical CPUs.

| Actual simulation processing | Fixed grain of 16 | Adaptive grain |
| --- | ---: | ---: |
| First pair, simulated seconds per wall second | 10.6367 | 14.8282 |
| Reversed pair, simulated seconds per wall second | 10.7666 | 14.7070 |
| Mean | 10.7017 | 14.7676 |
| Mean busy logical CPUs | 3.8780 | 6.6286 |
| Mean CPU utilization across 12 logical CPUs | 32.3% | 55.2% |

This is approximately 38.0% higher throughput, or 27.5% less time for the same
sampled-media workload. Neither run reported errors, and reported DSP workspace
was unchanged at 684,584,464 bytes. An additional instrumented run attributed
9.11 of 10.01 wall seconds to correlation batch execution and 0.51 seconds to
the sampled channel, including 0.34 seconds of transmitter rendering. FFT
scoring did not run in that measurement window. The numerical backend still
has synchronization and worker-idle time; this change does not establish full
CPU utilization or a general speedup for other configurations.

The -30 target has approximately 17.5-hour symbols. These bounded measurements
cover partial-symbol simulation throughput, not a completed decode or detection
probability. A new 75-lane regression compares exact one/eleven-worker and
seven-lane tiled results with active and future origins. The existing wire,
physical-end, progress and memory checks remain intact.

The Release build, including `build/datapump-gui`, and all 20 selected contract,
executor and numerical batch suites passed; the suites took 214.99 seconds.
The updated correlator batch suite also passed AddressSanitizer/UBSan and
ThreadSanitizer with the test, backend, executor, PatternCode and crypto objects
instrumented. Other archive/external dependencies were uninstrumented and
AddressSanitizer leak detection was disabled. `git diff --check` passed. Native
window rendering and a complete 17.5-hour-symbol decode were not exercised.

## Numerical search batches for GPU preparation — 16 September 2026

FFT acquisition and long-symbol fit accumulation now have typed numerical batch
interfaces with CPU reference backends. Logical work scales independently of CPU
worker scratch. FFT batches can cover the available search bank; long-symbol
batches cover up to 64 original blocks and 65,536 lanes, reduced as necessary by
workspace and the next possible symbol completion. Host admission, trial order,
physical completion and next-poll publication remain in their original order.
No GPU runtime or kernel is enabled; [search-compute](search-compute.md) records
the interfaces and remaining device work.

The full Release build passed, including the native FLTK executable. All 20
selected headless suites passed in 187.71 seconds: the 17 suites listed in the
[development contract](development.md), plus `search_parallel`,
`pattern_fft_batch` and `pattern_correlator_batch`. After rebuilding, all four
final focused suites (`pattern_receiver`, both batch suites and
`search_parallel`) passed in 57.31 seconds, covering the optional FFT capacity
guard and completed batch fixtures. Independent wire vectors and existing memory assertions were not
changed. No native adapter or message presentation code changed.

New coverage includes 100,003 executor jobs and overflow-safe sparse ranges
spanning `SIZE_MAX`, 16,387 FFT jobs with varied tiling and reversed execution
order, and 10,019 correlator lanes with exact one/multiple-worker results.
Additional cases exercise phase alternatives, private templates, pulse shaping,
tones, malformed extents, cancellation and workspace reuse. Twelve receiver
cases compare every progress poll around symbol and physical-absence boundaries
with roomy and tight workspace. An independent executable built from the prior
committed correlator (`a8978b3`) matched all 336 per-poll digests from the updated
implementation byte-for-byte, including scores, coordinates, bit events and
constellation output, with cropped negative and future origins.

Focused AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer runs
passed for the executor, both numerical backends, FFT exact-progress and
physical-absence cases, and the new correlator coordinator boundary cases.
Relevant test, backend, coordinator, executor, PatternCode and crypto source
objects were instrumented; other archive dependencies and external libraries
were not. AddressSanitizer leak detection was disabled in this environment.
These checks establish sampled CPU behavior and shared GUI policy, not native
window rendering, physical weak-signal detection or device numerical accuracy.

Paired Release timings used the new `benchmark_correlator` tool on the Ryzen 5
PRO 5650U host (6 physical cores, 12 logical CPUs). Each run used the same 11,265
hypotheses, approximately 631-second symbols and 2,048 deterministic noise
samples: `1200 -10 11 2048 .25 1`. The baseline replaced the search/receiver
objects with the committed versions and linked the same remaining library and
benchmark objects. The run order was reversed for the second pair.

| Partial-symbol CPU workload | Committed, 11 workers | Batched, 11 workers |
| --- | ---: | ---: |
| First pair | 2.6663 s | 2.5038 s |
| Reversed pair | 2.4219 s | 2.3056 s |
| Mean | 2.5441 s | 2.4047 s |

The updated path used about 10.6 logical CPUs on average and retained the same
5,425,312 idle bytes. Two updated one-worker runs took 9.7087 and 8.3535 seconds
(9.0311-second mean), about 3.76 times the parallel mean for this workload. The
batch refactor itself reduced the paired mean by about 5.5%; it is preparation
for wider device execution, not a claim of GPU-like CPU scaling. FFT timing
smoke checks had substantial host variance and support no reliable additional
speedup claim. These partial-symbol generated-noise timings do not measure
successful reception or detection probability. No GPU was available for tests.

## Exhaustive post-reception hard-bit recovery — 16 September 2026

Unresolved interval receptions now retain a separate bounded hard-bit capture
and, only after physical completion, may search missing-bit assignments and
fixed-size alignment hypotheses. The default run lasts at most five minutes
plus its final bounded worker batches, using up to the available CPU cores.
Incomplete and cancelled searches retain their exact search position for an
explicit resume. Candidates remain private until the competing alignments are
resolved; keyed candidates still require the original HMAC verifier. Source
interpretation then follows the existing bounded path. No encoder, RS primitive,
cryptographic primitive, waveform or symbol-admission algorithm changes.

The extra capture stores four hard decisions per byte, including explicit
missing values, and releases storage if its configured retention is exceeded.
This preserves the existing receiver working-memory assertion without relaxing
its limit. The 4,096-bit diagnostic prefix and next-poll pending progress remain
independent. Live recovery has a separate bounded queue and coordinator; later
receptions continue while it searches. The shared Console and Compression menus
provide resume/cancel actions, with physical completion and recovery status
shown separately in the original row. CLI receive commands expose the local
budget, workers, retained slots and error reserve, plus recovery JSON counters.

New regression fixtures cover sparse missing bits beyond RS byte-erasure
capacity, additional known byte errors, exact public and authenticated source
reconstruction, and deterministic one/multiple-worker coverage. One keyed case
exhausts all 8,192 assignments for 57 missing bits in separate bytes and preserves
all 57 as missing in diagnostics. Further cases cover deadlines and cancellation
with resumption, wrong authentication coordinates, competing public alignments,
missing middle intervals, memory and integer limits, and uneven receive chunks
crossing the packed retention boundaries. Live and shared GUI regressions cover
same-row updates, stale result rejection, independent later reception, and
clearing both staged and already-completed recovery publications.

The Release build and all 22 selected suites passed in 182.53 seconds:
`live_profiles`, `live_receptions`, `live`, `live_resources`, `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `recovery`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `cli`, `gui_layout`, `gui_contract`,
`gui_link_boundary`, `gui_adapter_boundary` and `gui_native_policy`.
`git diff --check` passed. The independent wire vectors, sampled hours-long
physical-end cases and original memory assertions remain intact. The Compression
geometry check now uses the same grouped menu controls as the native adapters,
while checking that every menu entry agrees on its shared rectangle.
These are synthetic hard-bit, sampled-audio and headless shared GUI checks;
physical weak-signal/LPI performance and native window rendering were not
exercised. No display server was available, and neither native adapter changed.

## RX target combinations and revised interpretations — 15 September 2026

The receive-bank fix now covers differing automatic chip floors, integration
durations, keyed patterns, target ordering and late long-symbol observations.
This supersedes the completed-prefix limitation in the earlier entry below.
Stronger accepted observations revise the original reception identity, including
completed-to-pending transitions, and retire any overlapping fragment identities.
Obsolete cached content is removed before replacement publication. Shared GUI
consumers withdraw copy/save eligibility and reject stale revisions and retired
IDs. Local content identity remains stable across revisions and never enters the
wire. Neither native GUI adapter changes.
Live CLI JSON also exposes the revision and retired identities on raw-bit and
completed-content events, with `reception_update` events for decoded rows.
Plaintext output and explicitly saved files remain append-only/local outputs;
they cannot retroactively retract bytes already consumed outside the receiver.

Native log-evidence values are not comparable across exact-sample, compact-bin
and long correlator paths. Clean sampled probes found a short false prefix with
score 893 competing with a correct full stream scoring 293. Arbitration now caps
each accepted symbol's evidence at its known media duration, accumulates that
support independently of output chunking, and excludes missing positions. Capping
each symbol also prevents excess confidence in an initial exact fit from lending
support to a later partial fit. The native diagnostic scores, admission gates,
physical-absence rule, source interpretation timing and transmitted bits stay
unchanged. This is a bounded selection heuristic, not a false-alarm guarantee.

Pending disjoint hypotheses must follow the longer symbol clock within the
earlier observation's absence window. Completed history still requires actual
sample overlap. Weak observations cannot enlarge the winning span or join two
stronger independent receptions. History, retired aliases, replay storage and
GUI rows remain bounded; the history and event storage count toward DSP quotas.

The expanded sampled test matrix exercises every ordering of `55,62,72` and
`20,26,32`, all three transmit profiles, one-bit and three-bit inputs, noisy mixed
profiles, keyed reception with an unrelated receive key, tone-mode one-bit input,
and reversed pairs/triples containing 32-second symbols. Existing `55,32`
short-text, exact raw-bit, fixed-interval and consecutive-transmission fixtures
remain. The helper and GUI regressions additionally cover multi-fragment merges,
stale events, obsolete attachments, copy/save revocation, independent frequencies
and keys, independent later transmissions, alias limits and hours-long progress.
There are 76 live scenarios containing 81 physical transmissions, plus two
generated waveforms for the tone identity check. The additional separation case
starts a short transmission after seven seconds of silence while the previous
32-second profile still awaits its full absent symbol; both receptions retain
their own exact bits and identity. Native support regressions vary PCM and
decision chunk sizes and confirm that gaps and terminal events add no support.
GUI cache ownership survives the shorter row history, so revisions can also
withdraw obsolete attachments after their displayed rows have been evicted.
The initial metadata implementation exceeded one existing long-search workspace
fixture. Reusing already stored committed scores and endpoints removed the
redundant state; the original 1/16/32 MiB checks pass with their full search
coverage and budgets unchanged.

Tone mode has a wire ambiguity: two consecutive zero tones at target `55` and
one zero tone at `32` produce identical sampled PCM. A regression preserves that
independent observation; no target-selection rule can recover the originating bit
count from identical waveforms. A separate exploratory `001` tone probe also
found the existing single-profile receiver can abandon its first timing track
for a later independently timed start without completing the earlier track.
The multi-profile regression uses a single tone bit; this native tone tracking
limitation is not fixed by the arbitration change.

The Release build passed. All 16 focused suites passed in 190.22 seconds:
`live_profiles`, `live_receptions`, `live`, `live_resources`, `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `gui_application`, `gui_controller`, `gui_inspection`,
`gui_binary_editor` and `cli`. The separate native GUI policy test also passed.
The full rebuild reported the existing `std::filesystem::u8path` deprecation in
unchanged `src/gui/application.cpp`. `git diff --check` passed. These are headless
sampled-audio and shared GUI checks; physical audio links and native window
rendering were not exercised.

## Competing RX target profiles — 15 September 2026

RX targets `55,32` and `32,55` now arbitrate overlapping pattern observations
in the live receiver bank. Public patterns of different lengths share chip
prefixes, so a mismatched duration can admit real correlation evidence and
previously publish its own bogus message. A bounded history now assigns one
pending identity to competing observations of the same signal and selects the
strongest cumulative pattern evidence. All receivers consume each PCM block
before any completion is published; a weaker profile cannot complete the
selected pending row or publish extra content. Hardware capture and simulation
use this same policy.

Receiver creation and recovery retain their origin on the bank's sample clock.
The 64-entry arbitration history and drained event storage count toward the DSP
budget. Completed history suppresses late weaker copies without extending its
sample span into later independent transmissions. Wire bits, dictionary codes,
interval geometry, admission thresholds and fully scored physical-end rules are
unchanged. Source validity is not a selection criterion.

`live_profiles` feeds deterministic PCM through the real asynchronous capture
queue with stubbed audio I/O. Both target orders and both transmit profiles
cover short `e`, explicit `001`, `hello`, fixed-interval text and consecutive
transmissions switching profiles. It checks one signal ID, exact pending bits,
one completed result, independent short-message vectors, and no completion at
five seconds of silence. The test fails against the original library with
multiple live signal IDs. `live_receptions` covers evidence replacement,
losing completion, immediate per-observation progress, distinct frequencies and
keys, large sample coordinates, bounded history and later independent messages.

There remains an ambiguity when a much longer symbol first becomes admissible
after a short prefix has already completed. A clean sampled probe with targets
`55,5` demonstrated a short-prefix completion at about six seconds and stronger
first-symbol evidence at about twenty seconds. Stronger late evidence receives
a fresh pending identity instead of being discarded or reopening a completed
row; the earlier interpretation can therefore remain visible. The helper
regression preserves those newly accepted long-symbol bits. No additional wire
framing or wait for every configured long profile was introduced.

The Release build passed without compiler warnings. All 15 focused suites passed
in 123.98 seconds: `live_profiles`, `live_receptions`, `live`, `live_resources`,
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection` and `gui_binary_editor`. This includes the existing sampled
four-hour symbols, exact short endpoints and pending GUI prefixes. These are
headless sampled-audio and shared GUI checks, not physical-link measurements or
native-window rendering checks; neither GUI adapter was changed.

## Pattern steps beside evidence — 15 September 2026

The Console's scrollable **Pattern steps** text list now sits at the right edge
beside **Pattern evidence**, with the same top and height as the plot row.
Received-message history starts at the left margin again. Plot widths and the
reference's 280-pixel width are preserved; showing or hiding the generation
scope resizes the reference with the plots. This changes shared layout and
declaration order only.

The FLTK Release build passed. All 31 focused shared GUI and compatibility
tests passed in 92.06 seconds, including layout at supported window sizes,
hidden-scope reflow, independent short-message vectors, fixed intervals,
physical completion, four-hour sampled symbols and pending prefixes.
FLTK windows were captured at 1180 by 866 and 1030 by 786 with the generation
scope hidden and visible. The reference aligns with the plots, and wheel
scrolling reaches the final longer-integration row at minimum height.
FLTK document conformance passed; adapter conformance reproduced the existing
minimum-size Compression dictionary-label clipping failure. Its assertion was
retained.

The ordinary Rev Release build hit an existing Clang 19 error in
`src/live_pattern_scores.hpp`: `key()` has a deduced return type and is used
before its definition. For native layout validation only, a temporary Clang
virtual-file overlay supplied the equivalent explicit return type; repository
source was unchanged. That build passed, as did Rev adapter, platform and
1x/2x coordinate conformance (49.75 seconds total). Rev's default and minimum
windows with the scope hidden, plus its minimum window with the scope visible,
also show the reference aligned at the right, with lower rows reachable by
scrolling. This is not a clean unmodified Rev build result. Native transmission
workflows and physical audio links were not rerun for this layout change.
`git diff --check` passed.

## Default target C/N0 — 15 September 2026

CLI and GUI target C/N0 now default to 32 dB-Hz (32 dB/1Hz). Initial receive
targets and invalid-list resets use the same value, and the GUI preset list
includes 32. Help and current documentation reflect the new default, including
the GUI's approximately 1.89 kbit/s Shannon-Hartley limit at 3.6 kHz.

The Release build passed. All 14 focused tests passed in 78.99 seconds:
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_profile_reference`, `tuning` and `cli`.
The default CLI estimate also matched an explicit `--target-snr 32`.
`git diff --check` passed. Native window rendering and physical audio links were
not exercised for this shared default-setting change.

## Automatic profile reference — 15 September 2026

The Console now has a compact **Pattern steps** list on the left of reception
history and scope plots. Rows show the C/N0 changeover, complex chips per raw
bit, symbol duration and gross bitrate; the active profile is bold. Boundaries
come from the existing tuner with the selected rate, carrier, clock and key
geometry. Forced modes show their fixed profile, and extended integration shows
the active duration. Thresholds are rounded for display and are not measured
receive-confidence limits. The transmit-generation scope retains its full width.

Both FLTK and Rev Release builds passed. The 13 focused compatibility and GUI
tests passed in 70.86 seconds, including independent dictionary vectors, fixed
intervals, physical completion, sampled four-hour symbols and pending prefixes.
After the final presentation and forced-target fix, all 24 shared GUI checks
passed in 65.38 seconds. New coverage pins the default 29.480625, 26.470325 and
23.460025 dB-Hz transitions, actual clock/carrier/key floors, forced targets below
the receive-list range, extended integration, active-row updates and minimum-size
sidebar geometry. No transport or automatic selection rules changed.

Native windows were captured and inspected at 1180 by 866 and 1030 by 786. FLTK
was checked with the generation scope hidden and visible; Rev was inspected
with it hidden, with both layouts covered by shared geometry tests. Compact row
text fits beside the native scrollbar; plot headings and waveform action buttons
remain visible. Rev adapter
conformance and FLTK document conformance passed. FLTK adapter conformance reached
the previously documented minimum-size Compression dictionary-label clipping
failure; the unrelated label and its assertion were retained. Full native
transmission workflows and physical audio links were not rerun for this display
change. `git diff --check` passed.

## Continuous tuning transmission — 15 September 2026

Both shared compose pages now provide **Transmit noise** and **Stop noise**.
Noise uses the regular encrypted pattern transmitter with fresh temporary
Data/Scrambler/DSSS key material and a bounded source of Data-masked dummy bits.
The ordinary pattern alternatives, epoch/ordinal schedule, settling, shaping,
carrier and signal level remain in use. No user content, keyfile entry or
receive-bank key is created. Normal message framing, exact short/raw endpoints,
physical completion and pending presentation remain unchanged.

FLTK and Rev Release builds passed. `streaming_modem` and `noise_receive`
passed in both builds. Independent lazy-source/ordinary-source comparisons
check sampled waveform and chip equality through partial-chip symbols,
multiple epochs, shaping on/off and both surrounding noise sections. Other
new checks cover fresh starts, non-looping symbol/cache blocks, preview/read
continuity, ordinary power and sidelobes, bounded storage, four-hour symbols,
coordinate exhaustion and cancellation. The receiver comparison admits zero
bits for tuning noise, unknown-key encrypted transmission and Gaussian
background across 16/128/512-chip public/private/tone receiver scenarios;
actual-message positive controls must decode exactly. These are finite sampled
regressions, not a lifetime false-detection or thermal-indistinguishability proof.

All 15 focused compatibility/live/GUI suites passed in 119.65 seconds, including
the independent dictionary vectors, fixed intervals, physical absence,
four-hour sampled symbols and exact pending prefixes. All 23 shared GUI tests
passed in 58.63 seconds after the final presentation change. Noise-specific GUI
coverage includes empty/invalid drafts, attachments, loaded/failed keys,
repeated start/stop, unchanged draft/inspection/history/settings, immediate
busy state, elapsed status, and automatic/manual capture presentation.

FLTK native document conformance passed. Its adapter conformance reached the
already documented minimum-size Compression dictionary-label clipping failure;
that assertion was retained. The updated FLTK native workflow passed in 200.16
seconds, including keyed noise start/stop before its existing message and
attachment tests. Rev adapter, platform, 1x and 2x coordinate conformance passed.
Its concurrent workflow missed a late replay frame (maximum observed fraction
0.847458 rather than the required 0.9); the isolated rerun passed in 260.88
seconds. No assertion was changed. Both private virtual displays were stopped
after validation.
Hardware audio/RF operation and Windows runtime behavior were not exercised.
No radio was keyed by these tests. `git diff --check` passed.

## Transmission scope space reclamation — 15 September 2026

Hidden scopes now collapse their entire 206-pixel area. The signal and file
browsers move upward and gain 108 pixels, fitting two more full signal rows;
all four plots gain the remaining 98 pixels. The display choice stays available.
Showing Hex/Bits or starting transmission in auto-hide restores the scope;
completion and None return the space to reception and plots on the same
presentation update. Both native adapters consume the shared geometry.

Both FLTK and Rev Release builds passed. All 23 shared GUI checks passed in
58.29 seconds, including minimum/default/larger window bounds, exact space
reclamation, dropdown transitions, retained control geometry, reception progress
and adapter boundaries. Rev adapter conformance passed in 37.07 seconds after
its expected-geometry helper was routed through the current application layout;
the platform and both coordinate-scale checks also passed. The same helper
correction preserves every FLTK geometry assertion. An isolated FLTK rerun
passed the layout and QR focus checks, then reached the previously recorded
Compression-page dictionary clipping failure at minimum size. Its earlier
concurrent run had missed the QR focus assertion. Neither assertion was relaxed.

Native FLTK and Rev windows were inspected at default and minimum sizes, with
Hex, None and auto-hide transitions. The expanded browsers and plots reclaim
the full area while preserving the dropdown. Actual simulated replay showed
the scope in both adapters, then completion restored the expanded layout with
the same received `e` row. Both GUI processes and the private display closed
cleanly. `git diff --check` passed.

## Transmission scope auto-hide — 15 September 2026

The Console scope dropdown now offers **None**, **Hex, auto-hide**, **Hex** and
**Bits**. Hex, auto-hide is the default: the scope and caption appear during actual
transmission snapshots and simulation replay, then hide when activity ends.
None hides them throughout transmission. Manual Hex and Bits retain access to
the capture while idle. The dropdown remains available and is wide enough for
the new label; transport, capture data and surrounding control positions are
unchanged.

Both FLTK and Rev Release builds passed. All 23 shared GUI checks passed in
57.73 seconds, covering the default and manual choices, visibility during
generation/replay and after completion, retained captures, exact pending bits
and shared layout/binding boundaries. `git diff --check` passed. Native display
workflows were not rerun for this shared visibility change.

## Transmission generation scope — 15 September 2026

The first Console tab now presents the requested nine diagnostic stages plus
the actual framed Wire Plaintext input. Input, Data keystream and ciphertext
are adjacent; the pattern input, DSSS keystream and mapper result are adjacent.
Hex fits all 32 byte columns at minimum width. Bits adds aligned hexadecimal
and exact binary digits with horizontal scrolling. Partial bytes remain exact,
including the three-bit `001` dictionary encoding of `e`.

The capture comes from the actual source encoder, Data XOR and waveform
generator, independently of draft estimates. Data rows publish begun payload
symbols. Pattern rows sample eight actual mapper bytes at the first chip of
each of the first four symbols, exposing public repetition and changing private
streams. Preview reconstruction and filter lookahead cannot publish future
symbol starts. Each simulation replay frame owns its bounded capture; completion
retains the final capture, cancellation freezes it, and a new transmission clears
it. Transmitter admission and replay accounting include the diagnostic storage.

Private Pattern replaces the public template, so the Pattern XOR row is null;
the replacement bytes remain visible in Pattern Bitstream. Mapper inputs are
labeled separately from subsequent payload-dependent I/Q selection, pulse
shaping and carrier modulation. FHSS is not applied and is shown as unused.
All interval markers, MAC and FEC bits remain inside Data encryption. No wire
fields, padding, short-dictionary codes, receiver completion or pending-message
rules changed.

Validation:

- Release builds passed for FLTK (`build`) and Rev (`build-rev`).
- All 13 focused transport/live suites passed in 70.29 seconds, including
  independent dictionary vectors, exact short/raw endpoints, fixed framing,
  physical absence, sampled four-hour symbols and bounded live resources.
- After refining pattern sampling, `transfer`, `pattern_code` and `live`
  passed again, 3/3 in 13.74 seconds. New regressions independently check every
  encrypted marker/interval bit, actual Data and DSSS XOR operands, public
  repetition, private symbol addresses, exact traced/untraced PCM equality,
  preview nonpublication, retention bounds and workspace admission.
- The final shared GUI suite passed, 23/23 in 57.49 seconds. It includes
  first-tab declarations, Hex/Bits layout, actual per-poll transmission data,
  raw/escaped input, retained captures, pending reception and adapter boundaries.
- FLTK native workflow and document conformance passed. Adapter conformance
  reproduces the previously recorded Compression-page dictionary clipping at
  minimum size; that reference and its regression are unchanged.
- Rev native adapter, platform and 1x/2x coordinate conformance passed. Its
  concurrent workflow run missed the final replay-fraction threshold
  (`0.898306` against `0.9`); the unchanged workflow passed when run separately
  in 249.58 seconds.
- Final native FLTK screenshots were inspected at minimum and default sizes:
  all ten stages, all 32 Hex columns, exact `001`, three repeated public
  symbol-start groups, unused fourth-group placeholders and the full Dark
  choice are visible. Bits retains the same rows with horizontal scrolling.
- The final Rev native screenshot was inspected at default size with the same
  captured source, exact partial bits and repeated public pattern groups.
- `git diff --check` passed.

These checks exercise generated samples and native presentation. They do not
establish sound-card emission, physical-link operation or intercept probability.

## Mono transmit channel routing — 15 September 2026

The shared console now has a persistent **Mono** checkbox below **Audio device**,
enabled by default. ALSA and WinMM try stereo output, with mono fallback at each
existing sample-rate candidate. Enabled stereo output contains silent left PCM
and unchanged right PCM; disabling Mono duplicates the signal to both channels.
Mono-only endpoints use their sole channel in either mode. CLI hardware playback
uses the same default, with `--no-mono` to enable both stereo channels.

Changing the checkbox updates the next playback without restarting the receiver,
discarding pending reception, clearing plots or recalculating the draft. Channel
interleaving happens after resampling; modem bits, framing, source codecs,
physical-end detection, receive PCM and exported mono WAVs are unchanged.

New ALSA/WinMM stub regressions cover stereo preference, stereo-only and mono-only
devices, exact signed/clipped PCM on each channel, streaming frame counts,
partial ALSA writes, rate conversion and Windows double buffering. Existing
capture and error/cancellation tests remain. Shared GUI checks cover the default,
toggle propagation, busy/closing guards, all-page visibility and minimum-width
layout. Live/GUI regressions protect the receive clock, plots, prepared estimate
and ongoing hours-long symbol processing while output routing changes.

Validation:

- Release builds passed for FLTK (`build`) and Rev (`build-rev`).
- The full headless suite passed, 55/55 tests in 137.67 seconds, including the
  independent short-message/fixed-interval/physical-end/pending-progress tests,
  CLI integration and both audio backend contracts.
- After refining Mono updates to preserve reception, rebuilt both backends and
  reran `live`, `live_resources`, `gui_controller`, `gui_application` and
  `gui_layout`: 5/5 passed in 57.12 seconds.
- Rev self-check and native adapter/platform/coordinate conformance passed,
  5/5 tests on a private Xvfb display; the final rebuilt adapter conformance
  passed again. The FLTK default window was also captured
  and inspected: the checked Mono control fits below Audio device alongside
  the diagnostics.
- FLTK native document conformance passed. Adapter conformance reports that the
  existing Compression dictionary reference clips at minimum window size. Its
  text, Courier 12 font and 489-by-166-pixel frame match the pre-change source;
  the Mono layout does not alter that reference.
- `git diff --check` passed.

Windows audio was tested through the compiled WinMM stub on Linux. Physical
sound-card routing, including an IC-7100, was not tested.

## Message preservation contract and regressions — 15 September 2026

This change updates documentation and tests only. Runtime sources, protocol
constants, GUI implementation and build configuration are unchanged.
[Development requirements](development.md), also linked from root `AGENTS.md`,
now explicitly protect 1–16-byte dictionary text, exact binary messages, fixed
intervals, physical-absence completion and per-bit pending progress when each
bit may take hours. Current summaries now distinguish short dictionary content
from interval sources, describe all four GUI tabs and use the fixed format's
18.75% marker overhead relative to coded bits.

New regressions cover:

- `stream_receive`: malformed validity cells and LZMA2 chunk lengths remain
  uninterpreted through successive fixed intervals until physical completion,
  under a small local content quota. Underfilled intervals permit continuation
  and preserve leading/trailing zero bytes. Both checks cover all FEC profiles.
- `gui_application`: every pending prefix through dictionary, byte and interval
  boundaries updates one stable row without an expected length, premature
  decoding or completed-message actions. Missing-slot placeholders retain their
  visible notice.
- `gui_controller`: actual simulation progress snapshots match the displayed
  pending row on the same poll, across raw, dictionary and interval messages;
  copy/paste actions remain disabled while pending.

Existing regressions also ran for independent historical dictionary vectors,
the inclusive 16/17-byte split across key/FEC/compression settings, and generated
PCM containing three four-hour symbols. The latter verifies individual bit
drains and requires a complete absent four-hour symbol to finish reception.

Validation:

- `cmake --build build --parallel 2`: Release build passed, including FLTK.
- `ctest --test-dir build --output-on-failure -LE native_gui -j 2`: initially
  54/55 passed in 136.02 seconds, including all 23 headless GUI checks. The sole
  failure reproduced in the unchanged `attachment` test: its interval-quota
  fixture still used 16-byte text, which now correctly takes the short path.
- That fixture now uses 17-byte text and explicitly checks interval selection;
  its original quota rejection and nonpublication assertions are unchanged.
  Rebuilt `test_attachment`; focused CTest passed (1/1). All 55 headless tests
  therefore passed across the full run and this corrected-fixture rerun.
- `git diff --check` and links in the new contributor documents passed.

No native-window conformance or workflow run was performed: this environment has
no Xvfb/display server. Shared GUI tests and the native executable's headless
self-check passed; they do not establish native rendering or a physical link.

## Short-message GUI and inclusive 16-byte limit — 15 September 2026

The short dictionary now includes 16 source bytes. Shared transfer constants
set the transmit/receive/GUI limit to 16 bytes and at most 208 exact bits; text
starts using fixed coding intervals at 17 bytes. `quick brown` sends 70 bits;
`quick brown fox `, including its trailing space, sends 98. Explicit `010` sends
three bits and is interpreted as `t` after physical completion, with its exact
bits retained. Two-bit inputs remain raw without inferred characters or padding.

The Compression page now mirrors full short-text dictionary bits and accepts
up to 208 exact bits with an expected-text preview, replacing its four-bit entry
limit. Console byte-prefix editing retains its separate 128-bit limit.

The code-path audit found no separate current GUI encoder: FLTK and Rev share
the same controller, inspection model and transfer library. A stale
`build-native/datapump-gui` predated dictionary restoration despite showing the
same version number. The user reported normally launching `build/datapump-gui`,
could no longer reproduce the oversized layout, and suspected an older running
instance. Neither a stale process nor the stale alternative executable was
confirmed as the original cause. All three current GUI build targets were
rebuilt; a running instance must be closed and relaunched to load the new code.

Regression coverage now types `quick brown` character by character through the
shared application's production defaults (3.6 kHz rate, 1500 Hz carrier,
80 dB-Hz transmit/receive targets). It checks the actual Transmission document,
including replacing an older long draft while its estimate is running, clearing
pending details, page changes, raw `010` edits and returning to the same visible
text. Explicit Repeatable text remains visible and counts toward source size;
turning it off restores the exact short draft. End-to-end GUI simulations cover
70-bit text, two-bit raw reception, `010` interpreted as `t`, and copied/reused
dictionary bits. The full 208-bit input limit and rejected over-limit/invalid
edits are checked independently from Console's 128-bit limit.

Validation passed: Release builds in `build` (FLTK), `build-native` (FLTK) and
`build-rev` (Rev), plus the main FLTK and Rev headless self-checks; transfer and
stream-receive suites; all 12 CLI integration cases; six focused GUI suites
(layout, inspection, binary editor, application, adapter boundary, controller);
and the full shared GUI smoke workflow in about 96 seconds under its existing
300-second timeout. Targeted ASan/UBSan runs passed for transfer, stream receive
and short compression without diagnostics. `git diff --check` is clean.
GUI geometry and shared application paths were checked headlessly; no physical
audio link or native-window rendering was measured in this environment.

## Earlier fixed short dictionary restoration — 15 September 2026

The newer inclusive 16-byte limit and expanded Compression editor above supersede
this entry's original 15-byte limit.

Text of 1–15 source bytes again uses the original fixed dictionary. The exact-bit
encoder and decoder are restored without the former packed/length APIs or packet
parser. Text `e` sends `001`; an explicit Binary draft still transmits exactly
the entered bits. No marker, padding, length, FEC or MAC is added to either path.

Dictionary interpretation requires physical completion, complete canonical
tokens, no missing symbols, no recognized interval marker, at most 195 received
bits and at most 15 decoded bytes under the content quota. Pending observations
remain exact bits. CLI and GUI retain those bits alongside the completed text,
with no interval-validation or authentication claim and no received attachment.

Regression coverage pins the historical codebook and all 256 source bytes,
every token truncation boundary, canonical escapes, quotas, the 15/16-byte
threshold, optional encryption, exact airtime and the unchanged physical end
gate. The packed convenience API rejects partial bytes; it cannot silently
pad a dictionary stream or index beyond its packed buffer.

Validation passed:

- Release CLI and native GUI builds.
- All 14 affected headless suites: short compression, transfer, stream receive,
  regressions, CLI, attachment, pattern transfer, live, live resources, audio
  rates, GUI inspection, GUI controller, GUI application and native policy.
- The full shared GUI workflow, run serially, in about 108 seconds under its
  existing 300-second timeout.
- ASan/UBSan runs for short compression, transfer and stream receive, without
  diagnostics; `git diff --check` is clean.

The GUI checks exercised shared headless workflows, not native window rendering
or a physical speaker/microphone link.

## Earlier raw-message restoration — 15 September 2026

This entry describes the intermediate literal-byte behavior in `a495328`;
the dictionary restoration above supersedes that encoding choice.

Commit `4dd23c5` removed the automatic under-16-byte text bypass along with the
old packet/dictionary codecs. Explicit Binary/status transmission survived, but
the drainable receiver buffered fewer than 1,024 accepted bits until stream end.
That delayed pending presentation for precisely the few-bit, very slow use case.

Nonempty text of 1–15 source bytes then sent its exact MSB-first byte bits,
without markers, compression, padding, FEC or MAC. The dictionary was still
removed: text `e` was eight bits, while explicit Binary `001` was three bits.
Text of at least 16 bytes and all attachments retain fixed coding intervals.
The threshold affects transmission only and never ends a reception.

Receiver drains now expose accepted symbols below the chunk capacity. Regression
coverage includes:

- 1/3/15/16-byte thresholds across FEC, compression and keyed/public settings,
  including source quotas and matching raw waveform estimates.
- Pending `0` → `00` → `001`, stable row identity, no byte padding or premature
  completion, exact copied bytes and restored longer-message FEC selection.
- Actual generated PCM for three four-hour symbols at a 64 Hz sample clock,
  consumed in 4,096-sample blocks. Each accepted bit appears at its own endpoint
  within a 1 MiB receiver workspace. A subsequent wholly absent four-hour symbol
  completes reception; six seconds inside that symbol does not preempt it.
- Incremental chunks collected by physical stream identity, including interleaved
  hypotheses, timed gaps, carrier labeling, echo suppression and EOF flushes.

These generated-sample checks exercise long symbol coordinates and bounded
processing; they do not establish a measured acoustic or radio sensitivity.

Validation passed: the Release build, all 54 headless tests across the suite and
affected reruns, and the full shared GUI workflow with its existing 300-second
timeout. The physical suites include 29 receiver cases and 23 correlator cases.
Targeted ASan/UBSan runs passed for transfer/source reception, four-hour symbols,
incremental gaps, clock-rate hypotheses, carrier reporting and physical end.
Three tests requiring a native graphical display were not run; the native
executable built and its headless self-check passed.

## Reception, echo suppression and attachments — September 2026

The follow-up changes retain the fixed 128-byte interval format and the sole
six-second physical absence rule. Deterministic regressions now cover:

- A first symbol shifted by −28.125 Hz followed by a 1500 Hz stream: exact bits
  finish labeled 1500 Hz. A genuinely shifted stream remains labeled 1471.875 Hz.
- Duplicate overlapping carrier hypotheses in the clock correlator, with one
  physical stream emitted. Marginal isolated evidence admitted by the former
  `1e-8` significance is rejected by the new `1e-10` default. The stricter
  `1e-12` trial rejected short-pattern source fixtures and was not adopted.
- Exactly two seconds of independently generated suppression noise after the
  payload/filter tail. A weaker two-second delayed copy produces extra bits in
  the no-suppression control and exact bits with suppression. This is one
  deterministic channel fixture, not a bound on real-world echo delays.
- Actual sampled PCM with erased marker, data and final-parity symbols, including
  public shaped SF16/RS20 and keyed cases. Exact source recovery reports separate
  data/parity repairs and known versus missing bit coverage. A noisy keyed source
  also requires measurable RS recovery before successful source decoding.
- Short keyed streams surviving an epoch refresh before their first output chunk.
  An admitted receiver and a correlator with a drained bit buffer remain
  active until their physical search ends; an empty output buffer cannot make
  receiver retirement discard the six-second wait. Unconfirmed noise searches
  still retire within the receiver workspace budget.
- The explicit attachment prefix, exact file bytes and filename restoration,
  Repeatable disabled, ordinary binary/text excluded from the file list,
  post-end interpretation, bounded names and no released source on quota failure.

The Release build and all 54 headless tests pass, including a targeted rerun of
the application presentation fixture updated for the new gap/repair label.
Targeted ASan/UBSan checks passed for the codec, carrier recovery/correlator and
suppression waveform paths. Three native-window tests remain excluded because
no graphical display is available; the native executable and its self-check build
and pass. The full shared GUI workflow passes with the same 300-second budget as the
native workflow. It covers generated production keys, the formerly lost short
keyed raw message, attachment filename and exact saved bytes, ordinary message
copying, replay replacement/cancellation and fixed FEC for short source text.

## Earlier fixed-interval migration — September 2026

This entry records the initial migration; the short-dictionary restoration above
supersedes its removal of short-text encoding. Longer messages and attachments
use the fixed-interval format in [protocol.md](protocol.md).
Its only end rule is six seconds covered by consecutive fully scored failed
symbols; one failed symbol suffices at durations of six seconds or longer.
The migration removed the old packet parser, early completion callbacks,
transmitted lengths and short-text dictionary. FEC/MAC outcomes and codec ends cannot release
content before physical completion. EOF and resource limits remain interruptions.

Migration regression coverage includes fixed geometry and local metadata, RS errors
and erasures in data and parity, missing final bits, keyed address/phase recovery,
marker loss and attempt budgets, bounded source spooling, shared quotas, real
quiet capture tails, exact binary source recovery and cancellation. The receiver,
marker and codec suites have also been exercised with ASan/UBSan. Final migration results on 15 September 2026:

| Check | Result |
|---|---|
| Release build, including native FLTK executable | Passed |
| `ctest --test-dir build --output-on-failure -LE native_gui -j 2` | 52/53 passed initially; the remaining GUI application suite passed after migrating its obsolete under-16-byte FEC fixtures |
| Final headless suite status | All 53 passed, including the new stream receive and six-case live resource suites |
| Full shared GUI workflow, run serially | Passed: replay, keyed raw bits, exact file save, overwrite refusal, clipboard retention, cancellation and fixed FEC for tiny sources |
| RS/source codec, marker collector, FFT receiver, correlator and wrapper | ASan/UBSan runs passed without diagnostics; final fixed-six-second API cleanup also passed Release receiver suites |
| `git diff --check` and removed API scan | Passed; no packet parser, short dictionary codec, packet completion callback or configurable gap timeout remains in production code |

The graphical display could not be opened (`DISPLAY=:0`), so three native-window
conformance tests were excluded. The native executable built and its self-check
passed. The shared smoke initially missed a keyed raw reception while competing
with the full suite; its prescribed serial run passed. These checks do not
establish real-time reception throughput under arbitrary competing CPU load.

## Historical validation entries

The following dated entries describe earlier checkouts and their then-current
packet/dictionary behavior. They are retained as history, not the current wire
format, end policy or acceptance tests.

## Weak-symbol timeout and payload memory — 14 September 2026

The existing timeout remains six seconds of consecutive unconfirmed symbols,
evaluated at complete symbol boundaries with a minimum of two failed symbols.
A short gap preserves the admitted clock; exceeding that limit ends the active
span and trims its unconfirmed tail. Packet length does not extend the timeout.

The FFT receiver already releases expired tracks. The clock-window correlator
now also releases terminated spans' bit-vector allocations, including at EOF,
instead of retaining their empty capacity in the acquisition hypotheses.
Fixed acquisition scratch and bounded completed-message output remain available.
Regressions verify the default six-second boundary in both paths, no storage
growth or repeated completion during continued silence, and exact restoration
of the correlator's baseline workspace after draining completion. Independent
later reception retains its correct stream position.

Both full receiver suites passed in Release, and the full correlator suite
passed ASan/UBSan with no diagnostics (`detect_leaks=0`, halt on errors).
Focused live checks passed for long-symbol reception after idle, gap recovery
upgrading earlier short content, and long-packet processing/cancellation. The
application rebuilt successfully.

## Timed gaps before Reed–Solomon correction — 14 September 2026

Live reception, capture decoding and transfer simulation now preserve unknown
interior symbol slots on an admitted clock. Later independent evidence must
confirm their extent; unknowns add no score, cannot choose a private schedule,
and are trimmed from unconfirmed tails. Known bits retain their original Data
stream addresses. Packet recovery fills unknown plaintext slots with zero and
requires a recognized leading marker plus complete integrity validation.
CLI output and GUI signal rows disclose the number of inferred zero slots.

Twelve deterministic blanked-PCM cases cover public and encrypted compact
17-byte packets, with gaps in the marker, header and body. All eight RS20/RS60
cases recover the exact content with nonzero corrections; all four equivalent
FEC-off cases fail integrity. Low-level checks cover chunk independence,
gap expiry, bit/workspace limits, unsupported weak tails, independent later
starts and ambiguous stream phases. A complete SHA/MAC-validated packet closes
before a new gap; tests preserve two consecutive packets on the same clock,
including a first packet already repaired after a missing symbol. Completion
probes also check truncation, wrong keys, partial leading markers and highly
compressed packets under the actual configured content budget.

A live audio-adapter regression pauses capture inside a marker gap, observes
the completed raw span and an early short-text interpretation, then resumes
capture. The same signal row upgrades to the fully validated original packet
with three missing slots and nonzero RS corrections. A cancellation regression
waits for fresh measured plot points, since receiver evidence can advance the
snapshot sequence before the next plot publication.

Marker recovery now charges only observed bits to its `2^-84` ideal fair-bit
false-match budget and permits up to 80 contiguous deleted marker bits. Tests
cover distributed timed erasures, unknown trailing anchors, slot-trial costs,
the 102-known-bit versus 101-known-bit threshold on short inputs, and rejection
of competing marker endpoints. These are analytic-model checks, not empirical
measurements of extremely rare false matches. Marker bytes and cadence,
packet encoding, crypto streams and symbol evidence thresholds are unchanged.

All **53 tests that do not require a display** passed across the Release batch
and targeted runs, including the full live suite (121.66 seconds alone), all
27 CLI cases, both receiver paths and GUI-model checks. The three native GUI
interaction suites require an unavailable display. The native application
built successfully and its display-free self-check passed.

The four core suites (`boundary_sync`, `pattern_receiver`, `pattern_correlator`,
`pattern_transfer`) passed ASan/UBSan. Focused runs against the rebuilt
sanitized library also passed the packet-completion callback in both receiver
paths and consecutive-packet regressions. After the final live changes, the
audio-gap content-upgrade regression and complete correlator suite also passed
against the rebuilt sanitized library. Both sanitizers halted on errors;
leak detection was disabled. No sanitizer diagnostics were reported.

## Pulse shaping at unchanged payload rate — 14 September 2026

Pattern profiles with at least 16 complete chip times now apply a 25% RRC
pulse spanning 16 chips. The nominal 1,200 Hz profile retains its 600 chips/s
and original symbol durations; complete filter tails add 160 samples at 6 kHz
(26.7 ms) per burst. Shorter manual patterns and tone modes retain their
previous pulses. Both peers must use matching pulse-shaping settings.

A private 65,536-chip capture measured 742–746 Hz at 26 dB below the spectral
peak using Hann windows of 8,192–32,768 samples. Radial limiting reduced power
by 0.083 dB relative to the unlimited linear waveform, with 0.109% relative
mean-squared error. A capture including protected settling measured 744.9 Hz
with an 8,192-sample window; prefix-only and payload-only widths were 741.9 Hz
and 745.6 Hz. These finite software measurements do not certify an RF mask or
an adversary's detection time. Regular chip timing and burst edges remain
observable features. See [pulse shaping](modem.md#pulse-shaping).

Frozen pre-change Data ciphertext and fourteen private complex chip values
still match. Toggling pulse shaping also preserves the complete encrypted wire
bits for a fixed message ID. AES/HKDF, purpose keys, counter domains, XOR mixing,
bit alternatives and chip addresses are unchanged. The filter adds no chips or
public acquisition marker. Both receive paths fit shaped candidate patterns
against the original sample/bin observations, retaining their confidence
thresholds and timing-search rules. An independent raw-sample Gram calculation
checks both bit alternatives, including partial chips and rate error; unknown
neighbor-symbol tails and limiter distortion remain residual error.

Paired deterministic AWGN tests normalize both waveforms to the same received
C/N0 and use unchanged acceptance thresholds. They recover all 86 payload bits
per waveform across +26, +20, +6 and -6 dB-Hz scenarios, including fractional
sample origins and +/-5,000 ppm clock errors. Aggregate shaped/rectangular
evidence ratios are respectively 1.003 (exact timing, +26), 1.101 (+26,
+5,000 ppm), 1.122 (+20, -5,000 ppm), 1.238 (+6, 20-second symbols), and 1.088
(-6, 320-second symbols). Fractional timing and linear channel interpolation
can favor the smoother waveform. These fixtures guard against a substantial
confidence regression; they do not establish equal field error rates or
false-alarm calibration in correlated HF noise.

The fast FFT receiver's disjoint sample bins introduce a small additional
loss. An eight-seed +26 dB-Hz private comparison found an aggregate shaped/
rectangular score ratio of 0.949875 over all 48 payload symbols, including
unconfirmed candidates. In one marginal final symbol, the shaped score was 37.680 against an
unchanged 37.803 acceptance threshold, versus 40.091 for rectangular pulses;
the shaped result correctly left that last bit unconfirmed. The raw-sample
correlator recovered both complete bursts in that case, with aggregate scores
within 0.3%. Exact bin-averaged templates offered no consistent improvement
over the existing midpoint templates, so the implementation keeps their
bounded cost. All emitted bits were correct, with complete bursts in seven of
eight shaped cases and all eight rectangular cases. A permanent regression
checks the score ratio without forcing marginal bits past the confidence
threshold. No threshold was weakened to hide this measured difference.

All **53 tests that do not require a display** passed across the full Release
run and targeted reruns, including the live suite (122.49 seconds alone), the
26-test CLI suite, audio-rate conversion, crypto, receiver and GUI-model tests.
The three native GUI interaction suites could not run because this environment
has no display or Xvfb. The native GUI built and its display-free self-check
passed. Inspection accounts for pulse-tail airtime separately and identifies
its chip-space plots as input-chip design illustrations before shaping.

The four rebuilt ASan/UBSan suites (`pattern_code`, `pattern_receiver`,
`pattern_correlator`, `pulse_shaping`) passed in 339.25 seconds with both
sanitizers configured to halt on errors. LeakSanitizer was disabled for this
sandbox's ptrace restriction; no address or undefined-behavior diagnostics
were reported. The added fast-receiver confidence regression then passed a
separate rebuilt `pulse_shaping` ASan/UBSan run with the same settings.

## Public I/Q patterns and transmit history — 14 September 2026

Unkeyed public patterns now use circular I/Q values with varying amplitude and
phase, replacing the real-sign waveform. Their two complete bit templates still
repeat each symbol, so short public patterns retain a finite, potentially sparse
cloud. Private stream addressing and tone waveforms are unchanged.

The audio transmit plot now shows the transmitter's last 2,048 actual payload
chip values across GUI polls. It no longer replaces them with only the fresh
batch or measured samples between chip boundaries. The live regression verifies
exact emitted values, stability inside a chip, growth across updates, bounded
retention, and omission counts when the GUI stalls. Settling and independent
receiver-input simulation retain their measured views.

Short public symbols use sample-resolution FFT timing with an exact real
carrier-basis Gram fit. Evidence stays capped at the previous half-chip scale;
private timing and scoring are unchanged. Ninety cases cover 3/4/6/8/12/16-chip
public patterns, five sample offsets and three phases, preserving every bit and
the exact final endpoint. Initial admission may precede the true start by up to
two samples. Tests also cover shared projections with an arbitrary phase,
300-symbol wrong-key/noise captures, and weak pending tails before a later burst.
Disabling the pending-tail protection breaks both replacement interference
fixtures, confirming that they still exercise that protection.

All **51 configured CTest suites** passed across the final run and an isolated
live-suite rerun. The concurrent run passed 50 suites but exceeded the large live
transfer's wall-clock timeout while other build/verification work ran; the full
live suite then passed alone in **111.13 seconds**. All **26 CLI tests** passed.
The receiver and streaming-modem ASan/UBSan suites passed in **41.87 seconds**
with halt-on-error enabled and LeakSanitizer disabled for this sandbox's ptrace
restriction. The rebuilt native FLTK GUI passed its display-free self-check.

## Protected pattern-only waveform — 14 September 2026

The APSK transmitter/receiver, fixed training bytes, repeating spread template,
legacy whitening and post-encryption symbol padding have been removed. Every
selected non-tone key enables private pattern templates and Data encryption.
Tone clears the key and all private spreading at GUI, CLI, transfer and live
boundaries, including stale key lists and keyfile reloads.

Private templates now map eight mixed keystream bytes per chip to capped
circular I/Q noise with varying amplitude and phase. Settling combines Data
and all enabled private streams before the same mapping and chip cadence, in
separate preamble counter positions. FFT acquisition uses actual template
energy; the clock-window correlator retains its existing full Gram fit.
Pattern evidence remains the only acquisition and time/key alignment source.
Both peers must use this updated waveform.

The Release build passed all **51 configured CTest tests**, including sampled
channel/sample-rate, crypto/packet, pattern receiver/correlator, live session,
and GUI controller/inspection coverage. The independent CLI suite passed all
**26 tests**. After the final seeded-estimator memory-accounting correction
and obsolete-header cleanup, the three affected `pattern_transfer`, `tuning`
and `regressions` suites also passed. The native FLTK GUI and receiver benchmark
built successfully, and `datapump-gui --self-check` passed without a display.
Four focused ASan/UBSan suites (`pattern_code`, `pattern_receiver`,
`pattern_correlator`, `pattern_transfer`) passed in 33.33 seconds with both
sanitizers configured to halt on errors. LeakSanitizer was disabled because
this sandbox's ptrace environment prevents its shutdown inspection; no address
or undefined-behavior findings were reported.

The standalone extended controller smoke previously reached its 100-second
deadline during a later replay scenario; the default controller suite, including
production-key tone regressions, passed. This is not a completed interactive
hardware GUI certification.

Repeating the original `/tmp` waveform comparison with different messages and
keys (Data, Scrambler and DSSS enabled, prefix omitted, 1,920 samples at 6 kHz)
now changes every PCM sample. Maximum absolute squared-sample difference is
approximately **0.95946**, compared with **exactly zero** before the correction.
Permanent tests cover circular quadratures, variable amplitude, private stream
addressing/cache boundaries, each independent private layer, protected-prefix
mixing, exact bit counts and noise-only/wrong-key rejection. This validates
removal of that particular invariant; bandwidth, chip timing, capped amplitudes
and burst edges remain, and no measured interception probability is established.

## Partial byte-boundary marker recognition — September 2026

This section records the original 64-bit-loss, `2^-100` implementation and its
validation. The current [marker evidence threshold](protocol.md#marker-evidence-threshold)
permits up to 80 missing marker bits with a `2^-84` bound; the test results below
predate that adjustment.

Marker recognition now permits up to eight changed bits in a complete 192-bit
marker. Partial recognition permits one contiguous loss of 1 through 64 bits,
including a missing prefix, while preserving an exact final 32-bit anchor.
The acquired leading stream-symbol index can identify a lost initial prefix
after ordinary decryption; unkeyed input can also infer that prefix through the
bounded deletion search. Different passing endpoints reject recovery, while
equivalent paths sharing an endpoint use the strongest evidence. Starts remain
restricted to offsets 0 through 7 initially and -7 through +7 periodically.
Recovery still precedes deinterleaving, Reed–Solomon correction and packet
integrity checks. Recognized marker evidence suppresses dictionary fallback
for an invalid packet, and the acquired raw bits remain available.
When an acquired index fixes the surviving suffix's endpoint, its mismatch
budget also permits errors in the final 32 bits; inferred endpoints require
the exact trailing anchor.

The [analytic acceptance model](protocol.md#marker-evidence-threshold) charges
all searched slots, starts, deletion runs and mismatch patterns to a `2^-100`
false-match bound per recovery call under independent fair input bits. The
allowed mismatches decrease as surviving markers shorten or inputs grow.
This is a calculated model bound; software tests do not measure events at that
probability or establish a channel error rate. Distributed missing marker bits,
lost marker trailers, lost crypto alignment and whole missing data blocks
remain outside this recovery model. Packet integrity and authentication retain
their existing checks, and the wire marker, cadence and overhead are unchanged.

The validation results in the older byte-boundary section below predate this
partial-marker implementation and do not establish its test status.

For this implementation, the rebuilt Release `boundary_sync`,
`pattern_transfer`, `transfer`, and `gui_inspection` suites passed. Coverage
includes missing leading/interior marker runs, flipped bits, confidence-budget
rejection, text/file identification, and payload-byte errors with FEC off,
RS20, and RS60. Clear and keyed PCM captures with the first marker symbol
removed were acquired and decoded; the keyed receiver independently retained
the correct stream-symbol index. The boundary suite also passed ASan/UBSan
with halt-on-error enabled and leak detection disabled for the sandbox.
All 26 CLI tests passed. The Release executable and 183 source/document files
passed the runtime-marker storage scan at every bit phase; `git diff --check`
also passed.

Older entries below record their earlier implementation and test state; their
legacy waveform behavior and timing benchmarks do not describe this build.

## Periodic byte-boundary recovery — September 2026

Compact-packet pattern transport now inserts two copies of a runtime-derived
96-bit word before the encoded packet and after every complete 256 encoded
bytes, then encrypts the complete wire sequence, including markers, when a key
is selected. Recovery runs after
pattern acquisition and the unchanged whole-stream Data decryption. It matches
plaintext markers, normalizes plaintext intervals and removes markers before
deinterleaving, FEC and whole-packet integrity. The initial marker search uses
offsets 0 through 7 from the burst origin; a damaged initial marker consumes
its nominal 192-bit slot when available. Each periodic marker search is
restricted to seven bits either side of its expected slot, and normalization
retains a damaged interval's prefix while trimming or zero-filling its tail.
A recovered candidate permits one packet
at the existing burst origin, with exact extent and no inner-packet search or
retry of unstripped bytes or older packets without the initial marker. Exact
raw bits, text below 16 original bytes and byte packet APIs retain their
existing formats. Text of at least 16 bytes and every attachment use the initial
marker, including when the encoded packet is shorter than 256 bytes. Marker
overhead for `N` encoded bytes is `24 * (1 + floor(N / 256))` bytes.

Relevant checks are the 15/16-byte text threshold, short attachments, encoded
packets below 256 bytes and an exact final 256-byte block;
FEC-off and keyed/unkeyed round trips; inserted, deleted and changed plaintext
bits; damaged markers followed by intact markers; and rejection of nested or
trailing packet candidates. Source/executable scanning checks that even one
96-bit marker word is absent at every bit offset. Such a scan cannot guarantee its absence
from arbitrary transferred data or runtime memory dumps.

Before the initial marker was added, the six focused Release suites passed:
`boundary_sync`, `crypto`, `pattern_transfer`, `transfer`, `live`, and
`gui_inspection`. `boundary_sync`,
`crypto`, and `pattern_transfer` also passed with ASan/UBSan; LeakSanitizer was
disabled for that run. The pattern suite includes clear and encrypted PCM
round trips crossing a marker through the unchanged constellation decoder.
The optimized Release `pump` executable and 180 source/document files passed
the runtime-derived marker scan at all eight bit phases. `git diff --check`
also passed. These results describe the focused suites, not a full-suite run.

With the initial marker added, the five focused Release suites passed:
`boundary_sync`, `pattern_transfer`, `transfer`, `audio_rates`, and
`gui_inspection`. They cover the leading marker, bounded leading-bit recovery,
damaged aligned markers, encrypted packets, short dictionary/raw-bit bypass,
transmission estimates and audio sample-rate conversion.
All 26 CLI tests passed. The Release executable and 183 source/document files
also passed the runtime-marker storage scan at every bit phase, and
`git diff --check` passed.

These are transport-bit and software integration checks. They do not establish
recovery from whole missing blocks, an unknown absolute stream offset, lost
Data-stream/Scrambler/DSSS alignment or a slip in a tail with no later intact
marker. Pattern constellation decoding remains the sole authority for timing
and keystream alignment; recovery never trials offsets, resets counters or
reseeds streams. An intact marker never substitutes for packet SHA-256/HMAC
validation.

## Shared preamble keys and CTR pad — September 2026

The preamble now uses the same Data key and the same enabled Scrambler/DSSS
waveform keys and transmission epoch as the payload. Its only cryptographic
distinction is the fixed eight-byte ASCII `preamble` pad in the high half of
the AES-CTR counter; the low half contains the ordinary block offset. All
preamble-specific HMAC key derivations have been removed. Those derivations
were deterministic, and neither version generates random preamble keys.

Frozen Crypto vectors check unchanged payload output and the preamble counter
format at the beginning and end of the 64-bit byte-address range. All four
stream purposes have domain, epoch, chunk and random-access coverage. The
waveform checks recover the actual encrypted phase words against the selected
Data key, then verify each spreading layer against its existing payload key
with the preamble counter pad. Payload samples and positions remain unchanged;
the existing prefix-only rejection, full-prefix and lost-prefix cases remain
part of receiver validation.

All 48 Release CTest suites passed in 173.81 seconds. The four focused
ASan/UBSan suites (`crypto`, `pattern_code`,
`pattern_receiver`, `pattern_transfer`) passed in 19.61 seconds with leak
detection disabled.

## Preamble Data encryption — September 2026

Preamble noise bytes now pass through bytewise Data-purpose AES-CTR encryption
before I/Q mapping, followed by every enabled Scrambler and DSSS layer. The
selected key derives a dedicated hardware Data key; the prefix consumes no
payload keystream positions. At most four fixed 512-byte caches are allocated,
and the extra Data cache is included in the transmitter memory ceiling.

An analytic-waveform test recovers the transmitted phase words and verifies
their bytewise XOR against the dedicated Data stream across cache boundaries.
Chunked output with all three private layers preserves the waveform and PCM
headroom. A transmitter-only tone check verifies half-chip noise refreshes;
it adds no tone reception test and reserves no payload constellation points.
The receiver matrix retains its 32 existing cases and adds Data-only and
Data-plus-Scrambler-plus-DSSS preambles. These cases clear the preamble Data
seed at the receiver, reject prefix-only captures, and recover exact `001`
with the prefix present or entirely removed.

All 48 Release CTest suites passed in 165.38 seconds. The three focused
ASan/UBSan suites (`pattern_code`, `pattern_receiver`, `pattern_transfer`)
passed in 20.15 seconds with leak detection disabled. These software checks
do not measure physical low probability of intercept or external AGC behavior.

## Private hardware-noise preamble — September 2026

Hardware settling now uses independent circular Gaussian-derived I/Q noise,
with normalized mean power and bounded peaks. Any selected key supplies a
private noise seed, including Data-only configurations. Every enabled Scrambler
and DSSS layer also applies through a separate hardware derivation domain;
both layers affect the prefix when both are selected. At most three fixed
512-byte caches hold these streams, without consuming payload stream positions.

PCM checks vary the key, epoch, noise seed and each spreading seed independently.
They check both noise quadratures, peak headroom, mean power, chunk invariance,
payload independence and unchanged payload samples after the prefix. Thirty-two
receiver cases combine 64/128 chips, 100/1200 Hz, two epochs and all four
spreading-layer combinations. Prefix-only captures produce no acquired burst;
full and entirely removed prefixes both recover exact `001` under a 2 MiB
receiver ceiling.

Two nuisance-waveform regressions also prevent an admitted weak hypothesis or
its unconfirmed tail from blocking a much stronger later pattern. The captured
100 Hz CLI failure now recovers all 696 payload bits at sample 30,720 and
validates `independent clock`, using only pattern evidence to choose timing.
The final Release run passed all 48 CTest suites in 90.92 seconds. The three
focused ASan/UBSan suites passed in 21.09 seconds with leak detection
disabled. These finite software checks do not establish physical low
probability of intercept. The noise's rectangular half-chip updates have a
wider first-null spectrum than full-chip payload pulses; no enforced spectral
mask or physical AGC measurement is claimed.

## Binary pattern transport — September 2026

The hardware-settling follow-up rounds the five-second target to whole
sample-quantized payload-symbol durations, with ties upward. Duration checks
cover subsecond symbols, the ten-second boundary and hour-long symbols. Exact
three-bit PCM checks cover reception with the full prefix and with the entire
prefix removed, public and keyed spreading, and a listener whose local clock
starts after the keyed prefix with zero additional epoch-search radius.
Inspection and airtime estimates count settling separately from payload bits.
These are software checks; physical gain-control and muting behavior has not
been measured.

The follow-up also fixes weak candidates borrowing confidence from a later
individually strong symbol. An unadmitted prefix is discarded when that strong
symbol establishes a new burst; already admitted chains retain their pending
continuations. A retained failing 512-byte file recording previously produced
4,075 raw bits starting at sample 28,150. It now produces exactly 4,072 bits
starting at the actual payload boundary, sample 30,080, and validates the
original file. Both receiver paths have deterministic weak-prefix regressions.
No preamble recognition or packet validity enters the timing decision.

The consecutive text/file smoke workflow also exposed identical public
templates in plaintext and keyed Auto Pattern receive banks. The file's raw
symbol count was correct, but another bank could apply the wrong Data mask
while reporting the same pattern evidence. Auto Pattern and forced pattern
lengths now use private pattern fragments whenever a key is selected. The
key hypothesis is distinguished at pattern acquisition, without using packet
validity to select a key or adding any transmitted fields.

The completed follow-up Release run passed **48/48 CTest suites** in 95.69
seconds. The strict shared GUI smoke workflow also passed, including
consecutive plaintext text/file reception with receive keys loaded and exact
encrypted raw bits. Its existing deadline and acceptance checks were retained.
All six affected suites have passing ASan/UBSan coverage: pattern generation,
both correlation paths, transfer integration, tuning and live sessions. The
live coverage combines the full-run prefix with focused completion of the new
multiple-key case and the remaining cases. That new case initially reached
61% of a transmission before its 30-second test allowance expired under
instrumentation; it passed with a dedicated 90-second allowance and unchanged
content and memory assertions. Leak detection was disabled. The strengthened
long-correlator fixture was additionally linked against a
temporary copy of the pre-fix implementation and failed at its boundary
assertion, confirming that the regression exercises the corrected behavior.

The initial automatic transport used pattern evidence for
acquisition and exact burst endpoints. It preserves explicit legacy APSK
fixtures separately. That initial Release build passed **48/48 CTest suites**
in 78.05 seconds, including the CLI, shared GUI controller, live session,
resampling, packet, crypto and new pattern suites.

The initial **six focused ASan/UBSan suites** also passed (11.30 seconds): short
compression, pattern generation, both correlation paths, transfer integration,
and the exact-bit editor. Leak detection was disabled for this execution
environment; this is not a leak-sanitizer result.

New actual-PCM checks cover all eight possible three-bit messages without a
supplied bit count; dictionary text `e` occupying exactly three bits; unknown
start and carrier phase; wrong keys and noise-only captures; cropped keyed
streams; weak chips at -16 dB; and 1,536 received bits with 100 ppm clock error
and phase noise. Independent sound-card conversions preserve public and keyed
`001` and a downstream packet. A two-seed weak-channel regression recovers
three bits with the longer selected integration and rejects the shorter
integration in that same sampled channel. Packet integrity cannot discard
otherwise acquired raw bits or select their timing.

A deterministic live regression advances the receiver's injected clock from
`E + 0.90` to `E + 1.01` with a zero-second epoch-search radius and transmits
exactly `001`. It verifies that new epochs are admitted before the next PCM
block, without waiting a full elapsed second. This fixes an intermittent
keyed-burst failure exposed by the shared GUI smoke workflow. Previous epochs
remain available through their bounded symbol-completion interval.

The final toolkit-independent controller plus strict GUI smoke workflow also
passed, including key generation/loading, sampled encrypted text and files,
exact encrypted 32-bit raw recovery, replay replacement/cancellation, and
return to live plots. The smoke sequence now observes resumed live plots before
reconfiguring the key, which otherwise clears the completed replay's ID.

Long-symbol checks generate bounded prefixes and verify constant state during
a four-hour symbol, exact three-bit/12-hour transmitter estimates, cancellation,
and rejection of unaffordable clock windows. They do **not** run a complete
12-hour radio experiment. See the measured per-window memory and CPU costs in
[pattern constellation](pattern-constellation.md#streaming-long-symbol-correlation).
The scalar fallback is not real-time-capable for every admitted window on this
host. Carrier/rate coverage is finite, and physical oscillator coherence and
false-alarm behavior in colored interference remain uncalibrated.

The native FLTK application could not be rebuilt in this environment because
X11 development headers/libraries are missing (`Window` is undefined in the
FLTK platform headers). The toolkit-independent GUI application and controller
build and run. Earlier native GUI results below are historical and are not a
claim that this new waveform was verified through native windows here.

## Automatic regression signal policy

Automatic regression checks must not force `tone-N`, `auto-tone`, or
`SpreadingMode::tone` fixtures or require successful tone simulation/reception.
Use changing-sign fixed or seeded pseudorandom patterns to check differential
phase and amplitude measurements, acquisition, streaming memory and timing.
Keep measurable transitions in signal fixtures; changing a mode name while
leaving a constant one-chip code does not add pattern-transition coverage.
Mode-name parsing can still cover the complete supported option catalog.
Independent sine-wave checks of resampler/filter mathematics are not modem
tone-pattern reception tests.

Tone operation depends on narrower physical conditions, such as GNSS timing
synchronization, low frequencies, high symbol rates and suitable hardware.
Successful differential measurements with pseudorandom patterns exercise the
measurements needed for tone shifts under those conditions; automatic tone
loopbacks are not a requirement or evidence of general tone reliability.
Historical tone results below are retained as records, not current regression
requirements or hardware validation.

After replacing the forced-tone fixtures, all eight affected C++ suites passed:
`tuning`, `regressions`, `streaming_modem`, `transfer`, `live`, `audio_rates`,
`gui_inspection` and `gui_pattern_space`. All 23 CLI tests also passed, including
the 1,024-chip keyed simulation whose full PCM exceeds its batch memory limit.
Dense PCM checks retain exact pre-FEC bytes and require frequent measured phase
and amplitude changes with both fixed and seeded chip patterns.

The weak-channel fixture uses a seeded pattern planned at 24 dB-Hz, 6 dB of
acquisition margin, two channel seeds, ideal clocks and RS60; its shorter
integration control must fail in the same channel. This checks integration
behavior, not calibrated sensitivity at the planner's target. Long live-pattern
fixtures use a bounded 2 MiB DSP budget for their larger timing search bank.

## Synchronized message and binary editors

The Message/Binary source selector has been replaced by two views of the same
message payload. The binary editor updates the first sixteen bytes and preserves
the suffix. Tests cover byte order, whitespace, partial drafts, arbitrary byte
values, UTF-8 boundaries, escaped text and decoded payload limits. The controller
test also sends binary-edited `00 FF` through simulation and verifies that exact
payload after reception.

All 22 shared GUI tests and both FLTK/Rev native adapter suites pass. Native
keyboard checks exercise Ctrl+C/Ctrl+V, selection replacement and rejected
pastes. A real Rev keyboard exercise confirmed synchronization, suffix retention
and the sixteen-byte layout at default and minimum window sizes. FLTK now keeps
the selection until paste validation completes; Rev suppresses text events
generated by clipboard shortcuts.

The full shared smoke workflow stops at phase 8 with `Pending replay signal was
not presented`, before the binary-edit phase. An independent build of the exact
pre-change GUI sources at `feafb16b6f3e56e7ba685cbe22ec21fadd522ac4`, linked to the
same modem library, reproduces the identical failure.

## GUI abstraction audit and completion

The review traced native entry points and helpers, the public facade, control
and document declarations, layout, editor/record input, bitmap transfers,
platform-service dispatch, and CMake/CI dependencies. Three parallel reviews
covered controls, documents/layout, and boundary enforcement, followed by an
integration review and native regressions.

Remaining gaps were corrected: exported modem include paths; stale declared
callbacks after visibility, page or shutdown changes; duplicated UTF-8 edit and
record extent policy; Rev horizontal record scrolling and stale native glyph
measurement; duplicated document action identity/eligibility; unstable focus
when repeated document actions move; and zero/exhausted control allocations.
Native adapters now translate these shared decisions into toolkit operations.
The window title also comes from the shared declaration module.

The architecture check now runs in production builds, recursively checks
alternate source extensions and helper aliases, and rejects hidden toolkit or
backend-specific dependencies in shared code. Regression mutations cover both
forbidden dependencies and valid constructs that must remain accepted. All 17
public headers compiled individually as C++20 without modem/toolkit include
paths. A separate linked contract canary verifies that the application target
does not export modem include directories. A fresh `BUILD_TESTING=OFF` FLTK
configuration also passed its production boundary target.

Earlier Linux Release FLTK validation passed all 20 GUI tests, including the
simulated workflow, native adapter and document suites. The broader 45-test run passed 44 initially;
its live-simulation timeout passed when rerun alone (31.8 seconds). Native package
relocation and corruption checks passed. ASan/UBSan passed all 19 GUI checks other
than the separately validated workflow, with leak detection disabled for the
native toolkit environment. These include the actual FLTK widget/document tests.

Earlier Linux Release Rev validation passed all 22 GUI tests, including production
workflow, native adapter workflow, clipboard/platform checks and 1x/2x coordinates. The exact
profile was software OpenGL, `LP_NUM_THREADS=2`, and Xvfb at 2400x1800x24 with
96 DPI. The native probes also verify zero-area document rectangles, retained
action resizing and framebuffer extents from physical pixel endpoints. Rev
record measurements and geometry now avoid invalidating unchanged rows.
The default llvmpipe thread configuration repeatedly missed the final 50 ms
raw-replay symbol observation on this host; the bounded profile passed the same
unchanged assertions and is now used in the GUI-contract CI matrix. See
[Rev software rendering](rev-backend.md#software-rendering-and-validation).

The follow-up interface audit found and corrected additional gaps: repeated
ordinary declarations could overlap in relative rows; Rev could miss changes to
initially absent labels or shared geometry; typography and caption layers could
remain stale; service editors used different hardcoded limits; and FLTK dialogs
borrowed titles from temporary request storage. Layout identity and complete
geometry results are shared, caption layering comes from `ControlLayout`, and
service requests now declare input limits validated by the common queue. The
Rev file dialog no longer duplicates the controller's no-overwrite promise.
Both native suites use the same new layout lifecycle fixture and exercise
atomic prompt edits with a custom input limit.

Native conformance now runs separately from the production `gui_workflow` in
both backends. Rev previously repeated the entire shared workflow after its
widget probes; that second run repeatedly missed the short terminal raw-symbol
snapshot on this software-rendering host. A later production run reproduced the
same sampling sensitivity. All native probes remain, with the complete shared
workflow executed once through the production binary. The smoke's raw replay
check now accepts an unvisited tail only when completion returns live input,
reports dropped points, and delivers a completed raw signal. Packet replay still
requires observed received symbols, and timing, changing plots, pending reception
and exact raw-bit assertions remain. Deterministic `live` tests additionally
require received symbols in the terminal raw frame and verify skipped-terminal
point accounting at exactly three seconds. This does not establish a guarantee
that a slow native renderer displays every replay frame.

Final follow-up Linux Release validation passed **20/20 FLTK GUI tests** in
25.57 seconds and **22/22 Rev GUI tests** in 52.27 seconds. Both include the
production workflow and dedicated native conformance; Rev also includes actual
platform services and coordinates at 1x/2x. Builds and GUI workflows ran without
overlapping workloads, using the software-GL/Xvfb profile above. Both fresh
temporary directories were empty after success. The strengthened deterministic
`live` suite passed in 31.51 seconds. The final independent source review found
no remaining concrete application-ID decisions in either adapter, and the
production boundary guard and diff whitespace check passed.

The follow-up CLI-only contract/layout/application checks passed 9/9. Focused
ASan/UBSan checks passed 4/4, including FLTK native service title lifetimes and
input validation, with native-toolkit leak detection disabled. Successful smoke
runs now remove automatically created fixtures; explicit output directories and
failed-run evidence are retained. This prevents repeated GUI validation from
exhausting a temporary filesystem with large generated key fixtures.

The subsequent consolidation moved the remaining shared presentation
bookkeeping into `BindingState`, `RecordReconciliation` and
`DocumentPresentation`. Both adapters now consume common option retention,
record changes and document traversal/placement. Shared widget roles and chrome
preferences also cover disabled/focus/hover/selection colors, dialog content and
geometry, popup widths, tooltip timing, checkbox geometry, tab placement and
document content widths. Native glyph measurement, widget ownership, popup
screen fitting and file-browser controls remain toolkit responsibilities.

An independent source audit was performed after this consolidation and before
builds or regression execution. It caught missing bitmap-source and popup-
direction cache dependencies, dialog text measurement inferred from font size,
and native style precedence that could bypass shared disabled colors. These
were corrected and added to the shared/native conformance coverage. The boundary
guard additionally rejects new native RGB literals outside shared palette
conversion.

Final consolidation Release validation passed **24/24 FLTK GUI tests** in
27.10 seconds and **26/26 Rev GUI tests** in 62.28 seconds. Both include the
production workflow and native conformance; Rev also covers platform services
and 1x/2x coordinates. All **21 public headers** compile independently as C++20
without toolkit or modem include paths. Builds and native workflows ran
sequentially with the software-rendering profile above, and successful smoke
directories were empty afterward.

The native runs exposed two integration details. FLTK retained an unnecessary
hidden label for menus; it is now omitted, and the unchanged filtered-menu test
passes. Rev's probes assumed immediate event pumping delivered a paint. On X11,
the queued frame request may not have arrived yet, leaving the old geometry in
place. Tests now await the existing native paint counter, preserving their frame
counts and geometry/input assertions. Palette probes likewise inspect glyphs,
caret and selection after actual layout in both monochrome and color modes.
No production scheduling change was needed.

Visual comparison also caught a Rev checkbox adapter consuming its observable
change flag before the native checkbox could update, and missing disabled
styles on dropdown children. The adapter now observes without consuming that
flag, and maps disabled dropdown text, arrow and field colors explicitly.
Native tests assert unchecked/checked/unchecked paint and active/disabled/
re-enabled dropdown states in both palette modes. Both desktops use the shared
surface background role. Fresh isolated production-window captures were
visually checked for control state, disabled choices, layout, captions and
bitmap areas. The added FLTK surface assertion also passed in a final focused
native run; its production code was unchanged after the 24-test run.

The CLI-only consolidation checks passed **13/13** in 7.44 seconds. Focused
ASan/UBSan checks passed **15/15** in 8.46 seconds, including the new shared
retention/layout/chrome checks and actual FLTK widgets/documents; leak detection
was disabled for the native toolkit environment.

The maintenance scope remains features expressed with existing primitives.
New native primitive types, toolkit repairs and platform services still need
adapter implementations. These Linux runs do not establish Windows runtime
conformance or hardware audio validation.

## Rev backend and shared bitmap extraction

The optional Rev backend pins upstream `clean` at
`d73faa7759b5cfd30d592057790ab458568b569b`. Linux builds used Clang 19, Ninja,
and Mesa 25.0.7 llvmpipe (LLVM 19.1.7, OpenGL 4.5) on a private X11 display.
Controller/declaration, pixel format/tile replay, Unicode boundary and display-free
self-check suites passed. The shared controller smoke covered UTF-8 text, files,
exact leading-zero bits, stale drafts, pending reception and retained saves.
Rev rendered the same workflow, switched inspection pages and checked control
layout, UTF-8 editing, failed-paste selection preservation and modal focus.
Native clipboard tests covered UTF-8, empty versus failed reads, overlaps, stale
responses, incremental long selections and stalled-transfer limits/expiry.

The existing FLTK smoke and focused GUI/model suites passed after its plots were
moved to the shared producers. Bitmap tests also passed under ASan/UBSan. FLTK
plot output was inspected at ordinary and 200% display scale; Rev text wrapping,
Unicode glyphs, QR output and control layout were inspected visually.

A relocated Rev package passed dependency closure, inventory, CLI and full GUI
checks with its checkout, build trees, original installation, development
libraries and system fonts hidden. The path contained spaces and the environment
had empty `PATH` and `LD_LIBRARY_PATH`. Tampered and unrecorded files were rejected.
Host graphics drivers remain external dependencies.

Software OpenGL works but remains CPU-intensive: a 31-second smoke-plus-hold run
used 74 seconds user CPU after paint optimizations and a 10 Hz presentation cap,
versus 211 seconds before. Modem events and plot history remain polled at 25 Hz.
These measurements establish no low-power performance guarantee.
See [Rev backend](rev-backend.md) for build instructions and measurement details.
Windows, GCC 15+, hardware audio and the older Linux ABI release floor were not
validated by this Rev run; macOS/Metal is not integrated.

## Dark QR startup default

The preview now starts in Dark mode before its first draw, and the dropdown reads
that initial state. Every launch uses Dark red (Dark gray in monochrome), including
with an empty message. The dark background level increased slightly from 24 to 32;
Dim remains 64. The Release GUI rebuilt, and the QR suite plus all five focused
GUI/model suites passed.

## QR preview brightness

A dropdown above the QR preview selects Normal, Dim red, Dark red, or Off.
Monochrome output uses gray dimming levels. The selection survives message edits
and affects only the preview; Normal retains the original black-on-white rendering.
The Release GUI rebuilt, and the QR suite plus all five focused GUI/model suites
passed.

An ASan/UBSan rendering harness (with `detect_leaks=0`) exercised the actual App
dropdown callbacks at minimum, default, and enlarged window sizes. On a 24-bit
display, Normal matched the previous QR rendering pixel for pixel; dimming
preserved modules and quiet-zone geometry, and empty/error states stayed dim.
Off remained entirely black while editing. The adjacent waterfall was
unchanged in both color and monochrome. The minimum-size layout was inspected
visually.

On an 8-bit display without a suitable RGB visual, the actual App rendered only
grayscale, with gray dropdown labels and working brightness callbacks. Strict
waterfall pixel comparison was limited to 24-bit displays because colormap
allocation changes quantization on the 8-bit display.

## Muted color presentation

Color output now uses subdued cyan data text and traces, softer neutral labels,
and muted waterfall hues ending in off-white. The monochrome roles and waterfall
intensity/rendering path remain the same as the pre-color commit `924bd03`.

The Release GUI rebuilt and all five focused GUI/model suites passed. Both
`--color` and `--monochrome` passed the display-free self-check. An ASan/UBSan
rendering harness (with the existing `detect_leaks=0` setting) verified 108,500
clipped waterfall pixels across 218 intensity levels against the new palette,
neutral labels capped at 208, and the uniform RGB (144, 192, 184) data tint.
Its full 760-by-540 monochrome image was pixel-identical to the previous grayscale
render. On an 8-bit display without a suitable RGB visual, a color request still
produced entirely grayscale output. The muted rendering was inspected visually.

## Multihue waterfall

The waterfall now uses blue, cyan, green, yellow, orange and red between black
and white. Only the shared 256-entry lookup table and its descriptions changed;
the existing RGB row conversion, grayscale intensities, and control interface
remain unchanged.

All five Release GUI/model suites passed. An ASan/UBSan rendering harness
verified 89,050 clipped waterfall pixels across 213 intensity levels, distinct
blue/green/red regions, unchanged grayscale pixels against the previous render,
and unchanged uniformly tinted constellation points. The multihue output was
also inspected visually.

## Optional color presentation

Color is now enabled by default when an RGB visual is available. `--monochrome`
explicitly selects grayscale; `--color` restores color preference, with the last
switch taking precedence. The existing automatic grayscale fallback is unchanged.
The default/override update rebuilt successfully and passed the five Release GUI
model/self-check suites plus command-line override acceptance checks.

Release and ASan/UBSan builds succeeded, and all five focused GUI/model suites
passed in both configurations. `--color` is documented by `--help` and accepted
alongside the display-free self-check.

An isolated rendering harness exercised the actual FLTK fields, waveform,
constellation, and waterfall widgets under ASan/UBSan with the CI leak-detection
setting. Pixel comparison verified 89,050 clipped waterfall pixels over 213
intensity levels against the shared palette, and identical constellation-point
positions with one uniform tint. The initial cyan/mint palette had nondecreasing
channels; the later multihue palette above intentionally varies hue instead.
Both presentations were visually inspected. On an isolated 8-bit display without
a suitable RGB visual, a color request produced an entirely grayscale image.
The waterfall retains its Gray8 buffer and converts color rows only when enabled.
These checks exercise rendering and GUI policy; no physical-display or Windows
performance measurements were made.

## Single-backend configuration and monochrome GUI

The GUI rebuilt in Release and ASan/UBSan configurations. All five focused GUI
model/self-check suites passed in each configuration; all four toolkit-independent
GUI suites also built and passed in a fresh CLI-only configuration.

Fresh configuration accepted the default and explicit `fltk` selection and
rejected empty, multiple, and unimplemented selections. GUI-off configuration
skipped FLTK even with an unavailable backend value. The CLI-only executable
built and ran `--help` with shared OpenSSL: this host's separate static OpenSSL
installation lacks its required zstd link dependency without the existing local
development support setup. The dependency policy was not changed.

The Release GUI workflow passed on a private 1440-by-1100 virtual display,
covering keyfile operations, transmission, exact binary reception, clipboard/save,
replay, three tabs, resizing and complete diagram scrolling. Console and modem-flow
layouts were inspected visually. Dynamic dependencies contained no GL/EGL,
GTK/GLib, Cairo, or Pango library. `--help` and `--version` reported `fltk`.

The sanitizer workflow passed its behavior checks. An initial run with leak
detection enabled reported 41,319 bytes of Fontconfig allocations at shutdown;
the model/self-check suites passed with leak detection enabled outside the
sandbox. The GUI workflow was repeated using the existing CI setting
`ASAN_OPTIONS=detect_leaks=0`, retaining address and undefined-behavior checks.
After restoring scrolling for overflowing signal text, a later sanitizer run
timed out during the file-transfer stage despite the earlier completed workflows.
An unchanged-binary retry completed with exit status zero. The cause of the
intermittent timeout was not established. The final Release model/self-check
rerun passed all five suites; those checks do not cover this workflow failure.

This validates the current FLTK implementation, not a second adapter or MCU
deployment. The controller/declaration/bitmap extraction remains proposed work.
Windows, physical audio, and representative slow hardware were not exercised.

## Version 0.7.2 static full-pattern inspection

The standalone Constellations tab is removed. Modem flow now displays all legal
full-pattern symbols as phase/amplitude chip rows, a full-vector distance map,
and matched/off-pattern energy for legal, shifted, unused and mean-noise cases.
Every code position is inspectable through pagination; metrics always use the
entire actual symbol, including repeated periods and partial chips.

The new model tests first failed for the missing implementation. They then
compared every displayed chip against the previously compiled transmitter's
analytic preview and actual PCM, before rebuilding the modem with its extracted
shared code generator. This checks that sharing the generator preserves the
existing on-air sequences. Coverage includes every 2–6-bit APSK alphabet,
fixed/tone/keyed/DSSS modes, exact time-weighted distances, truncated and repeated
periods, antipodal timing ambiguity, public keyed illustrations, 16,384-chip
codes, long integrations and 30 MHz configurations.

The strengthened real-PCM acquisition test uses a 1,024-chip keyed pattern,
4APSK, -15 dB sample SNR, erased training and a 17-sample delay that changes the
carrier reference by 90 degrees. It measures per-chip Es/N0 below -7 dB, recovers
the complete authenticated packet without Reed-Solomon, and rejects the same
capture with a wrong code and a noise-only capture through finish.
This verifies acquisition within the receiver's bounded timing bank, without
hard chip decisions or a clean chip constellation. It does not establish
arbitrary clock/frequency tracking; accelerated matched-symbol simulation is
not used as evidence of blind chip acquisition.

* All **29 native CTest suites passed** across the full run and focused GUI
  rerun. The new keyed inspection fixture initially omitted its required key;
  the corrected fixture retains the production key requirement.
* All five GUI/model suites passed in Release (0.30 seconds) and under
  ASan/UBSan (2.28 seconds). The below-chip-noise PCM regression also passed
  under ASan/UBSan.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.2.
* The Release and ASan/UBSan GUI workflows passed with three tabs, current static-pattern model
  binding, unchanged live plots, three-second packet/raw replay and complete
  scrolling at minimum and expanded window sizes.
* Native visual checks covered fixed16, tone16, keyed128, all 64 APSK values,
  last-page access for 16,384 chips and a weak-signal example with -37.8 dB
  nominal chip Es/N0 and +10 dB integrated Es/N0. All four navigation buttons
  and static redraw invariance passed. The colour scale preserves distinct
  outer amplitude rings. Strict GUI and harness warning checks passed.

No wire format, keyfile or runtime dependency changed. Physical audio devices,
battery-state comparisons and hosted Windows/CI execution were not tested.
LeakSanitizer is disabled for this host's tracing environment; ASan and UBSan
remain enabled.

## Recorded 0.7.1 pattern constellation checks

The new Constellations tab shows the existing phase/amplitude observations beside
the full analytic pattern projection in modeled noise units. The new numerical
tests first failed because the projection did not exist, then passed with the
bounded model. They cover every supported alphabet, pairwise template distances,
fixed/keyed/tone equivalence, quantized integration time, sample-clock invariance,
unsnapped observations, invalid numeric inputs, hour-long integrations and 30 MHz
plans. A deterministic 16,000-trial Monte Carlo check uses the modem's actual AWGN
generator to verify the modeled radial and differential tangential variance.

* Native GUI Release: all **28 CTest suites passed** (54.72 seconds).
* After the final layout adjustment, all four focused GUI/model suites passed
  in Release (0.26 seconds) and under ASan/UBSan (1.93 seconds).
  The complete GUI source also passed strict warning checks with `-Werror`.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.1.
* The complete Release and ASan/UBSan GUI workflows passed on a private virtual
  display, including all four tabs during
  continuous reception and three-second replay, minimum/expanded resizing,
  exact observation projection, exclusion of unmatched I/Q, retained fresh-point
  batches and clearing replay points on return to live reception.
* The plot was visually inspected at the actual minimum 479×383 panel size
  for 8- and 64-symbol alphabets, using real `add_awgn` differential observations
  at moderate SNR and ideal-only high-SNR cases. Pixel margins preserve outer
  markers and labels, and the closest-pair labels remain separate in dense views.

The axes represent the analytic AWGN model, not a measured receiver likelihood.
The differential-noise ellipses use the weakest configured reference ring;
finite real-PCM quadrature covariance, clock drift and phase noise can differ
from that approximation. No modem, packet, keyfile or replay format changed;
0.7.1 remains compatible with 0.7.0. No runtime dependency was added.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this visualization release. LeakSanitizer is disabled for this
host's tracing environment; ASan and UBSan remain enabled.

## Recorded 0.7.0 framing checks

The new tests first reproduced mandatory RS on a tiny message and receiver
synchronization before rejection of a damaged short frame. The format removes
the magic byte and fixed-width body length, uses a variable compact header,
automatically disables all RS below 16 original bytes, and carries header and
body in one continuous symbol stream. Bounded provisional decoders verify
complete short frames before synchronization; initial phase hypotheses allow
reception without relying on header RS to repair the first symbol.

* Native GUI Release: all **28 CTest suites passed** (67.93 seconds).
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (49.38 seconds).
* Final Debug ASan/UBSan with `-O1`: all **27 CTest suites passed**
  (424.14 seconds), including the instrumented vendored codec, full streaming
  acquisition, transfer, live sessions, CLI, audio contracts and GUI models.
  The initial unoptimized nine-suite sanitizer run and the separate broader
  compact-bootstrap acquisition matrix also passed.
* Short provisional-reception tests passed under ASan/UBSan: corrupted complete
  packets never establish lock, subsequent valid packets recover, noise alone
  does not synchronize, and extremely long observations remain bounded.
* The complete Release and ASan/UBSan GUI workflows passed on the private virtual display,
  including automatic FEC Off below 16 bytes, restoration of the longer-message
  FEC choice, all three tabs, pending previews and exactly three-second replay.
  The compact transmission diagram and processing flow were visually inspected.
* Native ZIP and TGZ bundles passed fresh extraction into directories containing
  spaces, manifest and dependency checks, short/long compression simulations,
  and complete GUI workflows with package-search paths cleared. Their 42 ELF
  files have no GTK/GLib dependency; the local glibc requirement is at most 2.38.

The packet tests cover original-size boundaries 0/1/15/16/255/256, canonical
variable lengths, maximum metadata, every content kind, header correction up to
the actual parity budget, and malformed frames with otherwise valid integrity.
Receiver and transfer tests span two through six bits per symbol, keyed and
plain frames, missing training, continuous partial-bit boundaries, noisy gain
hypotheses, and recovery after a corrupted short body. Live tests preserve the
fresh-symbol limit across timing/gain changes and fit hours of simulated airtime
within a 1 MiB workspace.

A deterministic same-wire oracle confirmed that the old marginal-SNR CLI fixture
contains one to three actual bit errors in its now-uncoded six-byte message.
Those packets correctly fail integrity. The lifecycle fixture now uses the
healthy 3 dBm / -90 dB preset, retaining default crystal drift and phase noise;
noisy packet rejection is tested separately. Replay-start checks permit only a
new, empty, unverified frame-zero status and still forbid cancelled content.

Profiling the same PCM cases before and after nonthrowing acquisition probes
reduced two/three/four/five/six-bit trial times from
0.415/1.038/2.115/3.547/5.082 seconds to
0.179/0.211/0.278/0.531/0.732 seconds, with identical header decisions. Fixed-size
probe caches and conservative RS shape checks avoid repeated rejection work;
these measurements are local observations, not throughput guarantees.

The unoptimized Debug sanitizer live test reached its existing 60-second
computation deadline midway through multi-key reception. Sanitizer compilation
now uses `-O1`, retaining debug symbols, assertions, ASan/UBSan and frame pointers.
This also covers the already-instrumented vendored codec and standalone audio
tests. Normal Release, portable and MSVC compilation flags are unaffected.
The final full live suite passes with its original deadlines and assertions.

Both peers must use 0.7.0 framing. Raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.6.0 checks

* All **28 native/core CTest suites passed** across the full run and focused CLI
  rerun. The original CLI fixture assumed 65 KiB of repetition could not fit the
  repeat airtime policy; it now checks both compressed acceptance and explicit
  uncompressed rejection. The final CLI suite passed in 20.50 seconds.
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (37.70 seconds).
* All **nine focused ASan/UBSan suites passed** (254.87 seconds): packet,
  compact format, both compression codecs, transfer, inspection, plots, GUI
  policy and GUI self-check. The vendored liblzma C code was instrumented too.
* The compact-bootstrap acquisition suite passed under ASan/UBSan, covering
  two-ring gain aliases, noisy one-byte packets, supported constellation widths,
  missing-ring hypotheses, exact symbol padding and PCM boundaries.
* The complete GUI workflow passed in Release and ASan/UBSan on the isolated
  display. It covers text/file/raw reception, key generation, pending results,
  three-second replay, clipboard, file saves, tab switching and diagram layout.
  The final inspection also requires LZMA2 for its long text example. The flow
  and compact-bootstrap transmission views were visually inspected.

New tests first demonstrated the 72-byte bootstrap and short-window two-ring
acquisition failures, then passed with the 16-byte format and alternate gain
hypotheses. Packet tests cover four damaged header bytes, actual compression,
incompressible fallback, bounded original sizes, malformed lengths with valid
CRC/RS/SHA, and no transmitted dictionary identifier. Short-code tests cover
all bytes, canonical tokens/padding and bounded truncated previews. LZMA2 tests
include an independently generated `xz --format=raw --lzma2=preset=9e,dict=4KiB`
vector, strict stream endings, invalid controls, expansion limits, previews and
an enforcing scratch allocator. Vendored source hashes are checked by CTest.

The new compression dependency is pinned source compiled statically. No Python
or destination compression package is needed. Packet wire compatibility with
0.5.x is deliberately removed; raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.5.7 checks

* Native GUI Release: all **24 CTest suites passed** (28.92 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.86 seconds).
* All six focused ASan/UBSan suites passed: packet, transfer, inspection model,
  GUI plots, GUI policy and GUI self-check (128.84 seconds). LeakSanitizer was
  disabled for this host's tracing environment.
* The four Release GUI suites passed again after the final receiver-description
  correction. The complete GUI workflow passed in both Release and ASan/UBSan
  on the isolated virtual display, using simulated input.
* GUI workflow checks cover all three tabs during idle reception and replay,
  asynchronous RS20/RS60 and raw/packet model changes, invalid-input clearing,
  retained composition and receiver state, 1030×786 and 1400×1000 window sizes,
  and scrolling each diagram to its end and back. Packet and raw reception,
  clipboard handling, file saves and three-second replay remain covered.

The inspection tests were written before the model implementation. They verify
encoder-derived compression, keyed integrity, mandatory bootstrap parity with
body FEC Off, independent five-bit symbol padding, full and shortened RS20/RS60
blocks, physical byte totals, and omission of actual payload, metadata values
and key material. Receiver steps distinguish incoming-header selection from
outgoing settings. Raw diagrams add no packet overhead; hour-long and 30 MHz
plans remain bounded without generating waveforms. Strict C++20 warning checks
passed for the model and native diagram code.

The flow, transmission sequence and chosen constellation panels were visually
inspected in the running GUI. Packet, keyfile and waveform formats are unchanged.
No Python, GUI toolkit or other runtime dependency was added. Physical audio,
battery comparisons and hosted Windows/CI execution were not tested for this
GUI inspection release.

## Recorded 0.5.6 checks

* Native GUI Release: all **23 CTest suites passed** (28.21 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.58 seconds).
* New modem/transfer raw-reception cases passed under ASan/UBSan, including
  partial symbols, measured constellation points and arbitrary-bit-offset
  encryption. LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions (91.30 seconds), GUI
  plots, GUI policy and GUI self-check. The final GUI suites also passed in Release.
* The new end-to-end requirement first failed against the unchanged 0.5.5
  library: raw simulation produced no received bits at the presentation deadline.
  The corrected Release GUI workflow recovered encrypted `001`, displayed the
  pending and completed raw rows, and copied exactly those bits.
* The final GUI workflow also passed under ASan/UBSan, including a visible
  read-only FEC Off value in Binary mode and restoration of the saved packet
  setting. The completed `001` row and controls were visually inspected on the
  private virtual display, without using physical audio or the desktop clipboard.

Receiver tests span all supported constellation widths and one through seventeen
bits, preserve leading zeros, and check that altered/noisy observations change
decisions. Live tests cover pending-to-complete presentation at exactly 3000 ms,
no early computation results, keyed partial symbols with packet RS controls
selected but no on-air overhead, no fabricated packet/accuracy metadata,
cancellation, replacement, and an hours-long symbol within a 1 MiB DSP budget.

Raw simulation supplies nominal start timing, length and a carrier reference;
it does not implement blind real-audio raw discovery. Received raw bits have no
FEC or integrity check and can contain channel errors. Packet and raw transmitted
waveforms remain compatible with 0.5.5. Physical audio, battery comparisons and
hosted Windows/CI execution were not tested for this release.

## Recorded 0.5.5 checks

* Native GUI Release: all **23 CTest suites passed** (28.35 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.62 seconds).
* Focused raw streaming-modem and transfer tests passed under ASan/UBSan.
  LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 96.34 seconds.
* Native Release GUI workflow passed on the isolated virtual display, including
  exact encrypted three-bit transmission, explicit source selection and return
  to live plots. No physical audio device was used.
* The complete GUI workflow also passed under ASan/UBSan. The new binary editor,
  three-bit count and millisecond airtime were visually inspected on that display.

Physical audio, battery comparisons and hosted Windows/CI execution were not
tested for this release. Existing packet waveforms and keyfile formats are
unchanged; the GUI raw binary format is separate from legacy CLI DBPSK status.

## Binary transmission coverage

New regressions check leading-zero binary input, invalid input and explicit
source selection; exact unframed airtime for one through seventeen bits across
all supported constellation widths; partial final symbols that vary both phase
and amplitude; PCM/integrated agreement; and stream-key masking without extra
bits. The GUI workflow includes selected-key three-bit transmission beside a
retained message and attachment, with irrelevant packet controls disabled.

Live-session tests exercise the actual playback branch through a link-time audio
adapter, check its exact sample count, and simulate a one-symbol raw signal with
an injected presentation clock. They cover the three-second deadline, measured
input plots, no invented packet results, invalid-input nonmutation, cancellation,
return to live noise, and an hours-long symbol with a 1 MiB DSP workspace.

## Recorded 0.5.4 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 92.63 seconds. LeakSanitizer was
  disabled for this host's tracing environment.
* The new GUI workflow reproduced early verified reception against the previous
  backend, then passed with the timed event implementation. Text and file rows
  remain pending across earlier GUI polls; verified content and accuracy appear
  only when the three-second presentation completes.
* The workflow also replaces a replay after pending reception appears, cancels
  its replacement, and checks through the original deadlines that neither
  interrupted packet enters the inbox. A running pending row was visually
  inspected on the private Xvfb display.
* The complete timed GUI workflow also passed with ASan/UBSan. GUI checks used
  simulated input on the isolated display, with no physical audio device.

Deterministic presentation-clock tests cover the entire fixed-training/packet
timeline, no early browser or receipt events during computation, incremental
pending text, paired final signal and packet delivery at exactly 3,000 ms, and
duplicate-free snapshot reads. Delayed polling flushes due events in order
without extending the deadline. Two queued simulations each receive their own
three-second timeline. Cancellation, replacement and configuration discard
future results, including pending events made due before the next GUI poll.
A cancellation after the deadline preserves an already completed reception.

CPU cancellation is exercised during actual streaming progress. Failed decoding
never produces verified content. The hours-long-symbol simulation still uses
bounded CPU work and fits the 1 MiB DSP fixture, with fewer presentation frames
to accommodate preview text and result diagnostics. Packet content uses the
separate receive-content quota. Physical audio, battery comparisons and hosted
Windows/CI execution were not tested for this release.

## Recorded 0.5.3 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All five focused ASan/UBSan suites passed: packet, live sessions, GUI plots,
  GUI policy and GUI self-check. The Debug live suite took 77.75 seconds.
  LeakSanitizer was disabled for this host's tracing environment.
* The native Release GUI workflow passed on an isolated Xvfb display. Both text
  and file rows carried reception percentages, with exact data counters matching
  their verified packets. The labels were also visually inspected in the running
  window alongside the scrolling messages.
* The instrumented native GUI workflow also passed with ASan/UBSan. All GUI
  workflows used the isolated display and simulated input, without opening a
  physical audio device.
* The full standalone streaming suite passed. Targeted preamble cases passed
  under ASan, UBSan and float-cast-overflow instrumentation. All linked core
  translation units used the current diagnostics layout.
* Exact packet-bit accuracy tests passed in Release and ASan/UBSan, covering
  no-FEC packets, header/parity-only repairs, known bit flips in full and ragged
  interleaved blocks, compressed content, authenticated ciphertext corruption,
  and exclusion of trailing bytes. Failed validation never supplies accuracy.

Preamble fixtures cover clean training, half replaced by silence, missing training
replaced by silence or noise, 18 dB noise, capture beginning halfway through
training, leading silence, PCM/integrated agreement, and training retained across
a bootstrap using hour-long symbols. Coarse observations remain unknown. Very
large finite inputs exercise the numeric overflow guards. Every valid fixture
also checks that blind acquisition still returns the correct packet.

The new independent scanner has **11,520 bytes** of fixed storage per receiver
on this x86_64 build. Receiver admission and reported workspace include it.
Neither scanner storage nor runtime on a coarse integrated observation scales
with an hour-long symbol. Recognition remains a conservative diagnostic with
documented phase/amplitude thresholds, not a calibrated radio sensitivity test.

After the test workers finished, the generated-noise benchmark with thirteen
keyed epochs, three bits per symbol and a 6 kHz internal clock processed 2.66 times
real time. The preceding 0.5.2 comparison processed 2.32 times real time. These
short measurements on a shared host do not establish a speed improvement or
real-time guarantees for other profiles and machines.

Packet, keyfile and waveform formats remain compatible with 0.5.2. Physical audio,
battery comparisons and hosted Windows/CI execution have not been tested here.

## Recorded 0.5.2 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* Final live-session and GUI projection/policy/self-check suites passed again
  after adding slow-symbol labels and the cancelled-transmission serial guard.
* ASan/UBSan passed the streaming-modem suite with the new symbol-drain API,
  and all four final live/GUI suites. The standalone streaming translation unit
  used `-O1` and linked the instrumented core library; the final live suite used
  the normal Debug build and took 72.93 seconds. LeakSanitizer is disabled on
  this ptrace host.
* The native GUI workflow passed in Release and ASan/UBSan: production keyfile
  generation, text/file reception, clipboard and exclusive saves, chronological
  replay, replacement by a new transmission, Stop replay, and return to live
  waveform, waterfall and input constellation.

A separate injected monotonic presentation clock tests every 50 ms frame of a
60-frame replay and its exact three-second deadline. Repeated reads retain the
same frame. The tests verify early payload acquisition, fresh measured symbol
batches after lock, changing waveform/spectrum, and an end frame containing the
transmission rather than decoder-tail noise. A GUI that skips frames receives
their pending compatible symbol points together. A GUI that misses the deadline
returns directly to live input and reports the unseen points as omitted.

The new symbol-drain tests cover rotated received signals, unsnapped off-grid
measurements, differential coordinates, acquisition, repeated drains, ring
overflow counts, resets, and agreement between PCM and integrated transmission.
The live audio adapter checks empty intervals between slow symbols and that
cancelled TX points cannot return after live input resumes. Long simulated
symbols remain CPU-bounded and fit their existing 1 MiB test budget, reducing
replay capacity when necessary.

Packet/keyfile formats and transmitted waveforms remain compatible with 0.5.1.
These presentation changes do not add carrier tracking or remove the recorded
CPU limits below. Physical audio and battery-state comparisons have not been
performed for this release. Hosted Windows/CI execution remains unverified here.

## Recorded 0.5.1 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* All **22 ASan/UBSan CTest suites passed**, including the separate dense PCM
  boundary stress suite (302.93 seconds in Debug). LeakSanitizer is disabled
  because this host's ptrace environment prevents it from starting.
* The native GUI workflow passed in both Release and ASan/UBSan builds on a
  private Xvfb display. It covered production keyfile generation and selection,
  overwrite refusal, idle input, verified text/file reception, clipboard copying,
  exclusive saves, held simulation diagnostics and all plots returning live.

## Evidence for the 0.5.1 changes

Automatic audio uses `carrier = max(1500, 0.75 * bandwidth)` Hz and
`Fs = max(6000, ceil(4 * bandwidth))`. Nominal symbol timing still depends on
bandwidth; hardware clocks remain separately negotiated. Tests cover planning
from 1 Hz through 30 MHz, fractional bandwidths, manual CLI overrides and actual
PCM packets at the new carrier/rate combinations. A 100 Hz packet also crosses
four cascaded 300 Hz high-pass sections and separate 48/44.1 kHz conversions.

The received-tone regression exercises the actual capture converter at both
44.1 and 48 kHz with irregular input blocks. It checks sample values and fitted
I/Q against a known 1573 Hz tone at a 6 kHz logical rate, within 1e-5. The old
fractional-cycle I/Q fit fails this test. PCM acquisition now accumulates the
carrier projections over each candidate's exact chip and symbol boundaries,
then solves their Gram system. Dense 4/5/6-bit tone and pattern fixtures inspect
wire bytes before packet error correction, including fractional-cycle chips and
delayed, fragmented input. This focused fixture validates against the known
bootstrap to separate integration accuracy from blind-acquisition startup
aliases; normal transfer/live tests retain the real protected-bootstrap search.
Undelayed tones compare every bit. Delayed/patterned cases exempt only the first
symbol's unknown differential phase; its amplitude and all following bits must
match before FEC.
End-of-capture flushing remains bounded for hour-long
symbols. Simulation receivers reset before switching back to idle PCM.

The waveform now defaults to four carrier cycles, using captured guard samples
and a bounded 64-tap Blackman-windowed sinc for the line between measured sample
dots. Tests cover exact sample knots, DC gain, impulse response, linearity,
allocation bounds, and reconstruction error below 1e-4 through 0.4 times the
sample rate. The production-size plot was visually inspected on an isolated
display. Dense overviews continue to show raw extrema.

These are generated-signal, converter and software-driver tests. A physical
audio link and an AC-versus-battery comparison have not been measured here.

With other test/build workers stopped, the 6 kHz receiver bank with thirteen
keyed epochs processed generated noise at 2.94x real time for the automatic
three-bit profile and 8.33x for six bits. A continuous 1500 Hz carrier at amplitude
0.35 measured 8.33x and 0.68x respectively. The dense six-bit carrier case is a
known CPU limit: its ambiguous amplitude lattice keeps blind bootstrap searches
busy. These single-host measurements do not establish battery-state performance
or real-time operation for every signal/key-bank configuration.

## Recorded 0.5 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* Standalone GUI plot-projection tests and the actual-transmitter signal-view
  regression passed. Release whitening, FEC and transmit-history tests passed
  as part of the integrated suites.
* All eight focused AddressSanitizer/UndefinedBehaviorSanitizer suites passed:
  transfer, streaming modem, live sessions, signal view, GUI plot projection,
  GUI state policy, GUI self-check and the CLI integration suite. LeakSanitizer
  is disabled because this host's ptrace environment prevents it from starting.
* The Release GUI workflow generated and loaded a production 128 MiB keyfile
  with named keys while reception continued. It checked every selected
  key against its matching MAC, including names containing menu punctuation
  and a key named `None`, then completed text/file loopback, clipboard copying,
  exclusive saves, held simulation plots and live resumption. Final GUI policy
  and self-check suites passed again after the menu fix.
* The final GUI workflow also passed under ASan/UBSan. Its focused fixture uses
  two generated keys (`A|B` and `None`) and one admitted epoch; all ten menu-name
  edge cases remain in the real FLTK self-check. Drift-window acquisition is
  covered by the separate transfer/live sanitizer suites. The broader initial
  GUI fixture passed in Release but exceeded its sanitizer deadlines. The GUI
  test supports `--smoke-timeout 300`, also configured for CI.
* The local installed bundle passed an audit of all 42 ELF paths: no GTK/GLib
  dependency and a maximum required glibc version of 2.38.

Final TGZ and ZIP artifacts use `tools/verify-native-archives.cmake` to check
extracted inventories, native dependencies, isolated CLI/GUI commands and ABI
requirements. Historical measurements below apply to 0.4.

## Evidence for the 0.5 changes

The default 4.8 kHz internal clock and 900 Hz carrier produce **384 cycles in a
2,048-sample frame**. Drawing that entire frame into roughly 300 pixels aliases
a clean sine wave into apparent blocks. A numeric probe found maximum error of
3.3e-9 against the expected sampled sine, 2.1e-13 between contiguous and fragmented
TX reads, 8.9e-8 after conversion through a 48 kHz audio clock, and 6.4e-14 between
ideal accelerated preview and actual PCM. The carrier FFT level matched its
expected amplitude. The regression checks continuous frame phase, primary FFT
power and suppression of aliased square-wave harmonics by more than 70 dB.

The 0.5 waveform view defaulted to twelve carrier cycles, retained actual sample
values, and uses extrema when zoomed out. Waterfall tests check peak preservation,
a shared color scale, bounded history and clearing on frequency-axis changes.
Independent enumeration confirmed that all 1,025 original FFT-bin positions map
to the same display columns in live and compact review paths.

Public audio whitening reduces data-dependent constellation bias. In a structured
16APSK probe, empirical symbol-occupancy entropy increased from **3.640 to 3.956
bits/symbol**; its maximum is 4. A 64APSK regression with a 4 KiB zero-filled
payload visits every symbol and exceeds **5.97 bits/symbol**, for both keyed and
plain audio. These are finite-frame occupancy measurements, not added payload
entropy or a capacity measurement. Tests also cover the fixed protocol vector,
chunk/offset invariance, reversibility, unchanged training and raw packet formats,
and FEC correction/authentication across all five constellation sizes.

Transmit diagnostics retain a bounded chronological payload-symbol history.
Tests exercise ring overwrite, exclude training, compare PCM and accelerated
histories, and verify that partial observations of hour-long symbols do not
create duplicate points.

Audio peers require matching **0.5 modem settings**. Packet and keyfile formats
remain unchanged. Whitening is public and reversible; it does not conceal
repeated frames or guarantee low probability of intercept. Rectangular pulse
sidelobes remain. See [modem.md](modem.md) and [protocol.md](protocol.md).

## Recorded 0.4 baseline

These measurements were made on Linux x86_64 with GCC 14.2, CMake 3.31,
OpenSSL 3.5.7 and vendored FLTK 1.4.5. Version 0.4 passed 22 native Release,
19 Python-disabled CLI and 21 ASan/UBSan suites across integrated and focused
runs. The final live sanitizer suite took 97.27 seconds. LeakSanitizer was disabled
because this host's ptrace environment prevented startup.

The retained regression coverage includes 1 Hz–30 MHz bandwidth planning;
`max(64, ceil(4 * bandwidth))` internal clocks; bounded multistage resampling;
independent 48/44.1 kHz packet paths; default 100 ppm crystal error and 0.5 degrees
RMS phase diffusion per square root second; erased-training acquisition; adaptive
4/8/16/32/64-APSK; and exact byte, bootstrap and FEC boundaries. Conversion from
120 MHz to 64 Hz stays below 5 MiB. An ideal-clock packet with hour-long symbols
decodes, while its bad-crystal counterpart fails validation rather than assuming
impossible carrier coherence.

Continuous tests cover idle plots, authentication, multiple keys/epochs,
cancellation, consecutive messages and the two-second simulation review. All
three plots return to live reception afterward. A clock that jumps an hour per
query verifies that simulation preserves its admitted epochs through preparation
and decoding, then admits fresh epochs for the next burst. ALSA/WinMM fixtures
cover default discovery, partial I/O, rate conversion, cancellation and buffer
lifetimes. Cryptographic vectors, production keyfiles, QR, Unicode, content
bounds and exclusive saves remain covered.

The 0.4 native GUI workflow passed normally and under ASan/UBSan: provisional to
verified text, consecutive transmissions, exact UTF-8 clipboard copy, files-only
save listing, exclusive binary save, cache clearing and held/live plots. Its
layout was inspected on an isolated 1400×1100 display. This did not exercise a
physical audio link or compare AC and battery power states.

### 0.4 audio configuration and CPU measurements

A zero-PCM default-device probe negotiated these formats:

| Logical rate | Hardware rate | Converter passband | Playback workspace |
| ---: | ---: | ---: | ---: |
| 64 Hz | 48 kHz | 26.88 Hz | 118,348 B |
| 4.8 kHz | 48 kHz | 2,016 Hz | 119,296 B |
| 9.6 kHz | 48 kHz | 4,032 Hz | 120,256 B |
| 96 kHz | 48 kHz | 20,160 Hz | 233,792 B |

No sound was emitted and nothing was recorded. Passbands are converter
calculations, not measured analog response. Unsupported bands are rejected
before expensive audio processing; a high internal rate does not create an SDR
frontend or overcome a sound card's physical passband.

One synthetic 48 kHz → 4.8 kHz conversion processed one second in 15.42 ms,
with 647,624 bytes of workspace and 5.81 ms setup. Eight input chunks produced
exactly 4,800 samples. A separate generated-noise receiver benchmark measured
7.77× real time for automatic three-bit modulation and 13.74× for forced six-bit
modulation, using a 4.8 kHz clock and thirteen keyed epochs. Other build/test
workers were stopped. These single-host CPU measurements are not end-to-end
audio, battery-state or cross-machine performance guarantees.

### 0.4 delivery baseline and continuing limits

Relocation checks hide the original installation and exercise a copy in a path
with spaces, empty `PATH`/`LD_LIBRARY_PATH`, and invalid Python paths. They verify
dependency closure and reject modified or unrecorded files. Archive/ABI fixtures
cover TGZ/ZIP extraction, checksums, accidental GTK/GLib linkage and excessive
glibc requirements. The 0.4 package audit covered 42 ELF paths, found no GTK/GLib
dependency, and measured a maximum glibc requirement of 2.38.

GitHub Actions jobs have not run here. Windows binaries and the configured glibc
2.35 compatibility floor require a successful hosted workflow run. Local builds
on this newer host do not inherit that compatibility floor.

The accelerated channel remains a bounded matched-chip model. It has no
chip-clock recovery or carrier-tracking loop and does not demonstrate calibrated
sensitivity, arbitrary long encrypted-pattern acquisition, near-capacity
throughput, physical SDR operation, low probability of intercept, or Windows
driver reliability. See [offline-installation.md](offline-installation.md) for
bundle compatibility and copying requirements.

### Unsynchronized sampled simulation (2026-09-13)

Production simulation now sends receiver-clock PCM to the same blind acquisition
path as hardware audio. Earlier matched-observation sensitivity and constant-work
simulation measurements above describe the previous model. The matched-channel
API remains a low-level analytical test helper, outside the simulation transport.

The new channel tests verify arbitrary seeded carrier phase and fractional start
timing, positive and negative clock error, sample-identical results with one-sample
and 4096-sample reads, continuous oscillator/noise state during idle and later
bursts, and equivalence between analytic source PCM and hardware transmitter PCM
through fixed training and keyed spreading. Memory is bounded; CPU work now scales
with sample count. Long-symbol cases exercise cancellation instead of asserting
instant completion of hours of audio.

Transfer tests exercise independent receive epochs inside and outside the search
window, actual spreading correlation at weak sample SNR, and failed acquisition
under carrier incoherence. Live tests cover reception after idle noise and across
consecutive transmissions without a TX-triggered receiver reset. Raw-waveform
replays produce no timing/length-assisted received bits; blind raw discovery
remains unavailable. Very short packets may verify without a provisional browser
row, because transmission alone no longer creates a receiving event.

Release channel, modem, streaming-modem, transfer, regression and live tests
passed, as did the 22 CLI tests, GUI inspection/controller checks, controller
smoke workflow and native GUI self-check. These checks do not establish physical
hardware sensitivity, continuous clock tracking, fading or multipath performance.

AddressSanitizer and UndefinedBehaviorSanitizer checks passed for the final
channel suite and all live cases (the full-run prefix plus focused epoch and
remaining-case runs). LeakSanitizer is unavailable under this host's ptrace
sandbox, so those checks used `detect_leaks=0`. Automatic-epoch and idle-refresh
fixtures now use controlled local clocks: they test timestamp selection and an
explicit idle epoch advance without making sanitizer CPU throughput determine
key admission. Separate tests still require mismatched/out-of-window epochs to
fail. Production clock admission and retention are unchanged by these test fixes.

### GUI rate and carrier defaults (2026-09-14)

The GUI now defaults to Rate 3,600 Hz and Carrier 1,500 Hz. Carrier selection
participates in both transmit and receive planning before the existing pattern
confidence floor is evaluated. Rate changes restore that rate's recommended
carrier; other edits preserve an explicit choice. CLI defaults, cipher streams,
chip mapping, pulse shaping and acquisition thresholds remain unchanged.

The Release build passed all 53 non-interactive suites, including crypto/key
vectors, pattern acquisition, live transfer, CLI and GUI controller coverage.
New packet fixtures recover exact public and authenticated private payloads at
40 and 80 dB-Hz with unknown carrier phase/fractional start, an independently
offset receive epoch, ±100 ppm clock error and phase diffusion. The 80 dB-Hz
cases exercise the actual default 16-chip profile. New public/private PCM
round trips also pass through independent 44.1/48 kHz audio-card clocks.

Both FLTK and Rev passed the full simulated GUI workflow and native adapter
checks. FLTK document checks and Rev platform/clipboard plus 1x/2x coordinate
checks passed. The final Rate/Carrier row was visually inspected at 1180×866
and 1030×786 in both backends, with the shared layout/application checks rerun
after reserving full label widths. FLTK's 2x bitmap probe required an Xft-enabled
test build; the initial build without Xft could not apply the requested scale.

At equal received C/N0, translating the same 3,600 Hz rate waveform from a
2,700 Hz carrier to 1,500 Hz produced aggregate pattern-evidence ratios of
0.9911 public and 1.0206 private across the tested starts and ±100 ppm clocks.
These software-channel checks preserve the existing planner's confidence
expectations; they do not measure a physical radio/speaker passband or establish
field sensitivity, BER or probability of intercept.

### Shannon-Hartley capacity display (2026-09-15)

Shared GUI diagnostics now place the ideal Shannon-Hartley channel capacity
beside the gross modem bitrate. The calculation converts the accepted TX C/N0
in dB-Hz to linear SNR in the selected nominal bandwidth. CLI `estimate` exposes
the same calculation as `shannon_capacity_bps`. Very weak SNR uses `log1p` to
preserve precision, and unrepresentable capacity displays as `Unavailable`
(JSON `null`). Capacity is informational; modem planning, framing, payload
airtime and physical completion are unchanged.

The Release build and all 15 selected suites passed: `tuning`, `cli`,
`compression_short`, `transfer`, `stream_codec`, `stream_receive`, `attachment`,
`pattern_correlator`, `pattern_receiver`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_contract` and `gui_layout`.
Independent numerical cases cover C/N0 conversion, bandwidth changes, weak and
strong targets, and numeric overflow. Shared GUI checks cover setting updates,
RX/simulation independence and preservation of the last accepted value during
invalid edits. The 3,600 Hz / 60 dB-Hz CLI estimate reports 29,242.698314280708
bit/s while short text `e` retains its three transmitted bits.

This was a headless run; native window rendering and physical-link throughput
were not measured. Both adapters consume the shared diagnostic presentation.

### Pattern evidence reset and expiry (2026-09-15)

Clicking the Console Pattern evidence plot now clears retained observations.
Stable observation IDs prevent later polls or replay frames from restoring
cleared evidence; new observations can still appear with identical scores.
Each point expires when its presentation age exceeds six seconds, including
during idle input. Evidence receives its timestamp after the complete pattern
window is scored. Expired strong candidates cannot hide fresh evidence from
another receiver. Replay preserves IDs and uses first scheduled frame times.
This diagnostic bookkeeping is bounded and does not alter modem acquisition,
wire formats, physical completion or pending reception updates.

The Release build and all 20 selected suites passed: `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `live`, `live_resources`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_plots`, `gui_bitmaps`,
`gui_interactions`, `gui_contract`, `gui_adapter_boundary`,
`gui_boundary_regression` and `gui_bindings`. Deterministic new cases cover
the exact six-second boundary, separate observation ages, repeated polls,
clear persistence, fresh identical scores, completed long-symbol evidence,
receiver selection, replay timestamp preservation and bounded history.

Validation was headless; native pointer/rendering checks and physical audio
were not run. Both adapters use the existing shared bitmap click mechanism.

### Threshold-relative pattern evidence scale (2026-09-15)

The Console Pattern evidence plot now uses exponential axes relative to each
observation's actual single-symbol admission threshold. The threshold occupies
half of each axis with solid guides; twice-threshold occupies three quarters
with dashed guides. Strong outliers approach the outer edges without shrinking
this region. Both receiver engines attach the reference to diagnostic history;
live snapshots and replay retain it with the original observation identity and
age. Acquisition decisions, physical completion and pending-bit updates are
unchanged. Chain evidence and competing-pattern margins still participate in
admission, so the guides alone do not imply acceptance.

The Release build and all 18 selected suites passed: `compression_short`,
`transfer`, `stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `live`, `live_resources`, `gui_application`, `gui_controller`,
`gui_inspection`, `gui_binary_editor`, `gui_plots`, `gui_bitmaps`, `gui_contract`,
`gui_adapter_boundary` and `gui_boundary_regression`. Checks cover independent
threshold/twice-threshold pixel landmarks, per-candidate thresholds, outlier
stability, invalid and extreme numeric inputs, color/monochrome, sample aspect,
tiled repaint, reference capture, replay, expiry and clearing. A 241-by-221
pixel output from the actual shared renderer was also visually inspected.

Validation was headless; native windows and physical audio were not exercised.
Both GUI adapters consume the same shared renderer and captions.

### Log-evidence scale correction and sampled clicks (2026-09-15)

The initial threshold-relative scale above incorrectly divided scores that
were already logarithms. The display now derives relative inverse model
noise-tail evidence as `exp(score - threshold)` before compressing it onto the
axis. The threshold remains at 50%; the 75% guide now correctly means twice
its evidence (`threshold + ln(2)`), rather than twice its log score. Receiver
scoring, admission, observation retention and physical completion are unchanged.

A sampled damped carrier click in the streaming correlator produced score
18.2393 against threshold 25.5108, with no accepted bits. The initial display
placed this at 39.08% of the axis, despite only 0.000695 times the threshold's
inverse model noise-tail evidence. It now renders at the origin at the tested
pixel resolution. The FFT receiver fixture also retains weak click evidence
without accepting bits. Both fixtures separately recover the exact legal
payload `01`, whose evidence remains visibly separated from the clicks.

The Release build and the same 18 suites listed above passed. `gui_bitmaps`
now tests actual sampled clicks and valid patterns through both receiver
implementations and the shared renderer, along with additive log-evidence
landmarks, a ten-log-unit deficit, outlier stability and numerical extremes.
Validation was headless and does not calibrate physical click recordings or
establish universal impulse rejection. These remain model evidence scores;
competing-pattern margins and chain history also affect actual bit admission.

### Visible noise and signal score distributions (2026-09-15)

The preceding exponential relative-evidence transform is superseded: its
saturation collapsed ordinary noise and strong signals onto the origin and
outer endpoints. The plot now preserves native log-score variation below the
threshold and uses logarithmic interpolation above it. The threshold remains
at 50%, twice the log score at 75%, and stronger retained points share an
adaptive upper range with 5% headroom. Only scores above `2T` change position
when the upper range changes. Labels explicitly identify diagnostic log
scores; `2T` does not mean twice the probability or evidence. Receiver decisions,
framing, physical completion, pending bits and display retention are unchanged.

The Release build and all 18 suites listed above passed. The final expanded
`gui_bitmaps` suite also passed after strengthening its cloud-spread assertions.
Sampled Gaussian noise and repeated damped clicks remain unadmitted and occupy
separated interior positions below the reference. Twelve-bit signals decode
exactly at three noise levels in both receiver implementations; their retained
candidates span multiple separated display positions instead of three saturated
points. Independent synthetic cases cover zero, `T`, `2T`, multiple strong
scores, a shared upper range, outliers, invalid metadata and extreme finite
values. Existing monochrome, color, aspect and tiled repaint checks remain.

Actual shared-renderer output was visually inspected at 241-by-221 pixels for
noise/clicks, signals at varied noise levels and combined clouds from both
receivers. These are headless sampled fixtures, not native windows or physical
audio measurements.

### Recovery after late marker acquisition (2026-09-16)

An operator's live-audio RS60 capture contained 1,067 bits: the exact final
44 bits of the first alignment marker and 1,023 coded bits. It lacked 148
leading marker bits and the last parity bit. Independent RS60 checking found
no errors in the observed coded bits and restored the final bit to one.
Post-end raw LZMA2 decoding recovered the exact 55-byte source
`the quick brown fox jumps over the lazy dog lorem ipsum`.

A receiver probe compiled against the prior `transfer.cpp` left this exact
capture undecoded even after a supplied physical-end event. The same probe
against the updated implementation recovered the source. The initial bounded
fallback required a unique short marker suffix backed by sufficient fixed RS
evidence, after physical completion only; the broader search below supersedes
that suffix requirement. It preserves the configured source,
FEC and key settings, actual symbol addresses, diagnostic bit prefix, storage
quotas and ordinary marker detector. The protocol documents the combined
false-match bound; no source-format success supplies alignment evidence.

The independent capture regression checks every pending bit and its stable
identity, the exact recovered source and final-parity repair statistics.
Additional cases cover known data/parity errors, insufficient parity evidence,
timed unknown slots, unmarked input, incompatible local profiles and keyed
authentication at the correct and incorrect acquired coordinates. An
18-byte-error case is accepted; a 19-byte-error case remains algebraically
correctable but fails the stricter alignment evidence requirement.

The Release build and all 18 selected suites passed: `live_profiles`,
`live_receptions`, `live`, `live_resources`, `compression_short`, `transfer`,
`stream_codec`, `stream_receive`, `attachment`, `pattern_correlator`,
`pattern_receiver`, `gui_application`, `gui_controller`, `gui_inspection`,
`gui_binary_editor`, `cli`, `boundary_sync` and `boundary_marker_storage`.
The final expanded `stream_receive` suite also passed independently.

Validation uses the supplied received bits and generated fixtures. The original
audio recording was unavailable, so these checks do not establish why physical
acquisition missed the leading marker or reproduce a new hardware audio test.

### Comprehensive RS-assisted alignment and marker benefit (2026-09-16)

The post-end fallback now tests every leading coded start allowed by one marker
plus the seven-bit slip neighborhood, including an entirely absent marker.
Matching marker suffix bits add evidence without gating RS attempts. A unique
winner must meet the 144-bit candidate threshold, including correction and
erasure penalties; 38,600 charged hypotheses retain the fallback's below
`2^-128` random-input bound. Later retained starts can veto a winner but cannot
be accepted, preventing the search boundary from hiding shifted codewords.
The accepted decoded interval enters the existing statistics/quota/spool path
without a second RS decode. Physical completion and source interpretation
remain separate gates, and no transmitted bits were added or removed.

Generated variants recover the original capture with all 44 surviving marker
bits removed, a damaged marker tail, and a completely damaged 192-bit marker
at all eight supported leading bit phases. Tests reject ambiguous all-zero
words, including a 1,223-bit case where only one candidate falls inside the
accepted-start range, and preserve correct keyed addresses, pending prefixes,
unknown-slot rejection, local source settings and the capture-size bound.

Paired checks establish why the transmitted marker remains useful. With all
48 parity bytes absent but all 80 source-area bytes observed, the full marker
still permits recovery of the exact 55-byte text. Removing the marker leaves
RS with no parity evidence to establish alignment. A separate paired 18-error
case also passes with its 44 marker bits and fails without them under the
current conservative evidence calculation; that precise error cutoff is not
an information-theoretic RS limit.

Temporary Release probes performed five end-to-end receiver calls per variant.
The original, markerless and 18-error captures averaged approximately 8–10 ms
per call on this host. Both intact-codeword variants decoded in every call;
the 18-error variant decoded only with its marker evidence. The all-parity-
absent pair likewise decoded only with its full marker. These timings include
post-end source handling and are illustrative host measurements, not hardware
audio performance claims.

The Release build and all 18 compatibility suites listed in the preceding
entry passed with the comprehensive search and the final paired marker-benefit
regressions. Validation was headless; no new physical audio test was performed.

### Separate short and long GUI SNR targets (2026-09-16)

The shared GUI now has editable short and long target dropdowns, defaulting to
32 and 55 dB-Hz. Short text remains 1–16 source bytes inclusive; exact raw bits
use that same target. Longer text, empty byte sources and every attachment use
the long target. RX starts with both targets. Draft transitions update estimates,
inspection and diagnostics without replacing the running receiver bank.
Actual transmission selects its locally configured waveform before generation;
framing, short dictionary endpoints and observed-absence completion are unchanged.

The Release build and all 37 selected compatibility/shared GUI suites passed,
including every suite in the development contract. The final controller run also
passed added UTF-8, escaped-byte, incomplete-draft, raw-bit, 16/17-byte and tiny
attachment cases. A sampled live regression sends both profiles sequentially
through one independent receiver bank and checks exact wire bits, airtime,
decoded content and the uninterrupted sample clock. Final layout, application,
contract and adapter-boundary checks passed after presentation adjustments.

FLTK adapter/document conformance and Rev adapter/platform/1x/2x coordinate
conformance passed on private X displays. Rev screenshots at default and minimum
sizes verified complete target labels and dropdown buttons. The shared layout
adds a settings row while preserving the composition/history/plot areas; the
dictionary reference wraps into shorter rows. Rev's Clang build also required
moving an existing deduced-return helper above its first use, with no behavior
change.

The standard 300-second full native workflow timed out in phase 15 on both
backends. A diagnostic run showed steadily advancing generated samples; an
original single-target 32 dB-Hz comparison also progressed slowly. No workflow
assertions or repository timeout settings were relaxed. The production workflow
was rerun with its supported 600-second command-line allowance.
FLTK completed the full unchanged workflow successfully in 473.511 seconds,
including keys, text, files, binary editing, cancellation, retained saves, live
plots and page switching.
Rev's extended run failed after 200.830 seconds in phase 11: the last observed
replay fraction was 0.830509, below the existing 0.9 assertion, despite 11 changing
frames and pending reception. This matches the native late-poll/replay-frame
sensitivity already described in the Rev backend notes; review found no selected
transmit-configuration mismatch. The log alone does not distinguish rendering
delay from host contention. Native workflow coverage is therefore not a clean
pass for Rev. No new physical audio or Windows validation was performed.

### Carrier centers, QR preview size and waterfall resizing (2026-09-16)

Every Rate now offers its half-rate center carrier alongside the unchanged
recommended default, including 1.8 kHz for 3.6 kHz. Shared controller/application
coverage checks all ten presets, custom rates, selection on every page and
continued manual overrides.

The QR preview spans the composition and action rows, growing from 78 to 115
logical pixels at the default size and from 50 to 87 at the minimum. Width-aware
growth preserves airtime space in tall, narrow windows. Existing editor heights,
generation rows, received history and plot geometry remain unchanged. Layout
checks cover minimum/default, tall/narrow and short/wide windows and prevent
overlap with controls or captions; QR expansion and integer module rendering
remain unchanged.

Live waterfall rows previously occupied one backing pixel each even when the
plot exceeded the 160-row retention bound. Taller plots now scale that bounded
history to the available height, with missing startup history still blank and
bottom-aligned. Smaller plots retain the recent-row behavior. Pixel regressions
cover full/partial/empty histories, odd heights, horizontal scaling, shrinking
back and the unchanged overview mode; the enlarged-history case failed before
the renderer fix. No modem framing, symbol timing, source interpretation or
pending-reception behavior changed.

Release builds succeeded for FLTK and Rev. All development-contract suites
passed, and the final shared GUI run passed all 24 tests, including the QR,
layout, bitmap, overlay and application regressions. An earlier application
test used an intermediate layout build; rebuilding the final layout resolved
its button-width assertion without changing that assertion.

Native screenshots on a private 96-DPI X display checked both backends at
1180×909, 1030×829, 1030×1200 and 1920×1440, then restored the default size and
expanded the QR. The final minimum-width airtime and action labels fit, the
larger QR remained square, and the waterfall retained and scaled its observed
history. Rev's carrier popup visibly offered 1.5 kHz and 1.8 kHz. FLTK adapter
and document conformance passed, as did Rev adapter, platform and 1×/2×
coordinate conformance. Full native transmission
workflows, physical audio and Windows were not rerun for these shared UI fixes.

### Parallel iterative pattern search (2026-09-16)

FFT hypothesis scoring and clock-window fit accumulation now share persistent
workers, with all but one available CPU selected by default (11 on this host).
Mutable pattern caches are private to workers; trial counting, peak ties,
admission, missing slots and physical completion retain the original order.
Scratch is charged to the configured workspace only while processing. Exact
idle-memory comparisons also cover cache eviction, so scratch cannot displace
other key/epoch searches from the live receiver bank. No transmit format,
source interpretation or pending-bit presentation rule changed.

New serial/parallel comparisons check exact scores, candidate order, thresholds,
every pending/provisional event, diagnostics and constellation points across
public/private patterns, clock phases, rate/frequency banks, ties, noise, missing
slots, physical absence and workspace reductions. The existing template-cache
assertions and compact four-hour bounds remain unchanged. Executor coverage
checks every index exactly once, simultaneous workers including the automatic
default, reuse, nested/concurrent callers, and joining all work before reporting
the earliest indexed exception. Additional executor stress passed 60 runs across
one-, two- and twelve-CPU affinity masks.

The Release build, including FLTK, succeeded. All 17 development-contract suites
plus `search_parallel` passed with `ctest --test-dir build --output-on-failure
-j 2` and the contract's test-name filter extended for `search_parallel`
(18/18, 194.42 seconds). ThreadSanitizer passed the executor
and both receivers' focused parallel comparisons with executor, scorers,
PatternCode, Crypto and test sources instrumented; unrelated archive objects and
external dependencies were not instrumented. ASan/UBSan passed the correlator's
eight exact-comparison configurations and compact-memory regression. Leak checks
were disabled for that run because sandbox ptrace prevents LeakSanitizer from
operating. Native display workflows, physical audio and Windows were not rerun.

Final five-second generated-noise benchmarks measured 1.77–1.88x speedup over
one-worker scoring on the 12-logical-CPU Ryzen 5 PRO 5650U host. At 12 kHz
bandwidth and an 80 dB-Hz target, thirteen keyed epochs improved from 0.71x to
1.25x real-time processing. The one-epoch case improved from 8.81x to 16.55x.
Commands and the additional 1.2 kHz measurement are recorded in
[throughput](throughput.md#cpu-and-live-throughput).

### Search scheduling and simulation processing (2026-09-16)

FFT scoring now queues several hypotheses per worker, sharing each worker's
private transform and pattern state while retaining separate ordered results.
Immutable FFT stage constants replace repeated identical calculations. Serial
tracking reuses exact template and carrier phase values in existing acquisition
buffers. No search hypotheses, arithmetic reductions, admission order, idle
receiver footprint, wire format or physical-completion rules changed.

An independent temporary probe linked the previous committed receiver and the
final receiver against the same remaining library objects. All 5,933,472 bytes
of its per-poll traces matched exactly across three fixtures at one, three and
automatic workers. Fixtures cover split stream phases without optional caches,
short private nonorthogonal sample fits with phase/carrier hypotheses, and
midstream optional-cache eviction. Comparisons include pending prefixes, bursts,
candidate evidence, thresholds, status, idle memory, diagnostics, constellation
samples and repeated EOF handling. A separate old/new FFT probe found exact
equality for power-of-two transforms from 4 through 65,536 in both directions.
Executor tests also passed under one-, two-, four- and twelve-CPU affinity masks.

The new `benchmark_simulation` target exercises the actual live session,
including sampled channel generation, receiver-bank processing and replay
preparation. It verifies exact bits, zero missing symbols and observed-absence
completion, and excludes only the fixed three-second presentation replay.
Instrumentation attributed about 99% of the original simulation's processing
time to reception and under 1% to waveform/channel generation. The worker limit
therefore does not imply full CPU utilization; ordered tracking and admission
remain serial, and short scoring jobs incur synchronization overhead.

The Release build, including FLTK and both benchmark targets, succeeded. All
17 development-contract suites plus `search_parallel` passed (18/18, 162.98
seconds). ThreadSanitizer passed exact parallel progress, physical absence and
cache/workspace equivalence with the executor, both scorers, PatternCode,
Crypto and tests instrumented; remaining archive objects and dependencies were
uninstrumented. ASan/UBSan passed exact progress, physical absence, cache
equivalence and shared-projection/workspace checks with the FFT receiver and
tests instrumented; other objects were uninstrumented and leak checks were
disabled for the sandbox restriction. The new simulation benchmark additionally
verified exact one-bit and three-bit receptions with one and four CPUs available,
selecting one and three workers respectively. Native display workflows, physical
audio and Windows were not rerun.

Final uncontended simulation pairs, with run order reversed, measured
17.5172/17.3937 seconds for the previous committed receiver and
14.6487/14.6905 seconds for the updated receiver. This is about 16% less processing
time at 12 kHz, an 80 dB-Hz target, thirteen keyed epochs and 64 exact raw bits.
The corresponding generated-noise benchmark improved from 1.25429x to 1.57008x
real time, about 25% greater throughput. Both receiver versions used 11 workers
and the same remaining library objects. Commands, CPU-utilization observations
and measurement limits are in [throughput](throughput.md#cpu-and-live-throughput).

### Long-symbol live acquisition and progress (2026-09-17)

A generated-PCM reproduction of a public raw `0` at Rate 3,600 Hz, carrier
1,500 Hz and target -8 dB-Hz confirmed the reported simulation failure. The
internal clock is 14,400 Hz and each bit occupies 5,732,744 samples (398.107
seconds). The original full FFT path buffered 1,165.084 seconds before its first
acquisition pass. Feeding all 800.428 seconds of sampled transmission and
observed-absence tail from the +3 dBm/-170 dB, 100 ppm, 0.5 degrees/sqrt(second)
channel produced no candidates or bits. This reproduction used the full 1,914
carrier/clock alternatives with sufficient memory, not the local fallback.
The live GUI simulation does not call the offline receiver's EOF flush.

Long-symbol acquisition now uses a smaller FFT when its overlap leaves useful
coverage, caps the range of new starts per batch, and scores an initial range
after a complete symbol plus at most one second of new starts. Established
tracks score complete available symbols at input progress boundaries instead
of waiting for another acquisition batch. Separate tracking scratch keeps the
smaller FFT buffers safe. Transmission, hypothesis coverage, confidence gates,
exact bit prefixes and fully observed physical absence remain unchanged.

Entire acquisition batches may be skipped only when one retained accepted span
already excludes every start and carrier under the existing admission rules,
and no ready continuation can rotate that span out before admission. Their
trial penalties remain charged; rejected-overlap diagnostic records are not
generated. An independent temporary build with this optimization disabled
produced 336 byte-identical emitted events across long-symbol progress and
coupled-clock fixtures. Instrumentation confirmed 524 skipped batches covering
145,016 start positions. Bits, poll positions, identities, sample endpoints,
scores, carrier estimates, missing slots and completion flags matched.

Long-symbol continuation also distributes independent carrier fits across CPU
workers when their bounded private pattern caches fit the workspace. Timing
selection and the ordered score reduction remain serial. A new exact comparison
passes for one worker versus three and automatic workers, including public and
private patterns, changing private stream phases, nominal/coupled clock fits,
duplicate-carrier ties, pending prefixes, diagnostics, idle memory and physical
completion without `finish()`. The compute estimate retains a conservative
serial-rate tracking allowance rather than promising this parallel speedup.
The parallel continuation and continuous FFT progress fixtures also passed
ASan/UBSan with the complete library instrumented; leak detection was disabled
for the sandbox restriction.

New regression coverage uses continuous PCM without `finish()`: public `001`
appears as `0`, `00`, then `001`, including delayed starts, different chunk
sizes and shared carrier projections. Noise-only input stays unadmitted and
partial silence cannot complete a message. Separate asynchronous live capture
and sampled live simulation tests expose a pending one-bit row and complete
that same row only after a whole absent symbol. The new capture regression
failed before the fix and passed afterward. ASan/UBSan passed the new continuous
FFT progress, coupled-clock progress/absence and streamed-template regressions;
leak detection was disabled for the sandbox restriction.

The initial complete Release build and 20 selected development-contract suites
passed (344.08 seconds); the FFT receiver suite also passed independently.
After the final continuation change, the complete Release build and all 21
development-contract suites plus `search_parallel` passed (22/22, 662.52
seconds while sharing the host with the exact-profile probe). FLTK and Rev GUI
binaries were rebuilt successfully. `git diff --check` passed.
These are software checks, not a new physical microphone/speaker validation.

The first exact-profile test of the updated scheduler entered acquisition at
about 399.1 seconds of media but did not finish the full search within a
600-second wall-time limit. It used automatic workers, restricted to four CPUs
after about 95 seconds to share the host with regressions, and consumed 44m14s
of CPU at a stable 1,202,164 KiB RSS. Removing the input-buffering delay does not
establish real-time throughput for this large search. The simulation compute
estimate reflects the new geometry and cadence but remains an engineering
model, not a benchmark or runtime guarantee.

The public exact-profile first pass still requires 7,657 transforms of
2,097,152 complex points: one input transform plus a generated-template forward
transform and inverse transform for each bit and each of 1,914 hypotheses.
That is about 169 billion radix-2 butterflies, excluding template generation.
Parallel continuation cannot reduce this acquisition cost. Keeping both
transformed templates for the whole bank would take about 120 GiB, so the
bounded receiver streams them instead.

A longer probe of a frozen scheduler/pruning build, before continuation was
parallelized, confirmed exact acquisition with the full public-pattern bank.
It published pending raw `0` at media time 399.217778 seconds, after 1,469.328724
wall seconds (24m29s), with score 120.519907. It used the same +3 dBm/-170 dB
sampled channel, 100 ppm clock mismatch and phase diffusion, without a narrowed
carrier bank or EOF flush. Covered-batch skipping then let input advance to the
absent-symbol decision at media time 796.405 seconds. RSS fell from 1,202,216
KiB during acquisition to 312,896 KiB during serial continuation. The host was
also running the final regression checks during acquisition, so this is an
observed shared-host time rather than an uncontended throughput benchmark.
The probe reached its original 1,800-second wall limit (exit 124) while scoring
that absent symbol; it emitted no completion event. Total CPU time was
205m12.060s. This proves exact pending acquisition, not end-to-end completion
at this profile. The subsequently added parallel continuation is covered by
the exact serial/parallel and physical-end regressions above, but was not
rerun through this full-size acquisition. Physical audio and real-time
throughput at this setting remain unverified.

### Simulation computation stalls and progress (2026-09-17)

The reported stall at a varying near-final audio percentage occurs before
replay. `source_loop()` publishes generated-audio progress before a synchronous
receiver push; a large acquisition or continuation pass can therefore leave
that percentage unchanged. The finite tail loop and receiver search coordinates
advance, and the code audit found no unbounded completion loop. Weak input can
add work: without an admitted bit and its retained span, later overlapping
full-bank acquisition batches cannot be skipped. This is a computation-cost
explanation, not evidence that every reported wait is a confirmed decode failure.

Finite simulations now report audio percentage separately from elapsed
steady-clock wall time. Snapshot polls advance the elapsed display during DSP
work even if no additional audio has been processed. Tail scoring has its own
"Checking reception after transmission" stage. Completion/cancellation freezes
elapsed time, reconfiguration/new work resets it, and replay uses its separate
presentation clock. The noise-model success label and help explain that the
probability assumes completed computation and is not an empirical success rate
or deadline prediction. No probability penalty was invented from an unfinished
run, and no timeout or UI state can complete reception.

A bounded diagnostic used public raw `0`, 400-second symbols, 64 Hz sampling,
8 Hz bandwidth, 16 Hz carrier, 100 ppm mismatch and 0.5-degree phase diffusion.
All four seeds (1, 7, 19, 73) at -3 dB-Hz C/N0 and at a 50 dB stronger channel
produced exact pending `0` and one physical completion without `finish()`.
The weak scores were 132.79--192.88 against threshold 47.11; all eight cases
took about 1.03 seconds. This 26-alternative search has different timing and
trial penalties from the user's 1,914-alternative profile and does not establish
its reliability or throughput. A separate live-session regression covers four
strong and four weaker seeds, finite computation and replay completion, exact
strong receptions, elapsed-time updates during unchanged audio progress,
reconfiguration and cancellation.

Streamed long public-pattern FFT searches now optionally retain one exact pair
of unmodulated nominal-clock waveforms, then apply each carrier rotation in the
original arithmetic order. The cache is push-scoped, included in measured
workspace accounting, and eligible only when it preserves the affordable
physical worker count and has more reusable jobs than workers. Private and
time-scaled templates retain their generation path. An existing cache can be
reused during continuation; tracking does not allocate another copy.

A temporary 40-second public-pattern microbenchmark preserved the 14,400 Hz
sample rate, 3,600 Hz bandwidth, 1,500 Hz carrier and projection geometry. With
64 mixed nominal/coupled jobs and four workers, it measured 3.062497 seconds
uncached versus 2.460236 seconds cached, including 0.076760 seconds to build the
4,608,928-byte cache: about 20% less wall time. All 230,400 two-bit score outputs
matched byte for byte. This is a reduced local microbenchmark, not a measured
speedup or end-to-end completion at the user's full 1,914-alternative setting.
The prototype is `/tmp/pumpmodem-nominal-cache/bench40.cpp`, run as
`timeout 60 /tmp/pumpmodem-nominal-cache/bench40`.

New batch regressions compare exact cached/uncached scores for shaped and
unshaped public patterns, sample fits, partial final chips, extended observation
windows, different stream indices and mixed clock alternatives. Poisoned cache
values cannot affect time-scaled alternatives. Invalid geometry and cancelled
construction/scoring are rejected. The continuous long-symbol comparison also
exercises streamed public rows, serial versus parallel progress, idle workspace,
pending prefixes and observed physical completion.

The actual rebuilt library repeated the 40-second microbenchmark in both run
orders, also with byte-identical scores. Baseline-first measured 2.595165 seconds
uncached versus 1.997540 seconds cached including construction; cached-first
measured 2.502675 versus 1.938406 seconds. Both are about 23% less wall time.
Commands were `timeout 60 /tmp/pumpmodem-nominal-cache/bench_actual40` and the
same command with `reverse`. These runs followed the regression suite, without
another heavy workload dispatched in this task.

The complete Release build succeeded, both FLTK and Rev GUI binaries were
rebuilt, and all 21 development-contract suites plus `pattern_fft_batch` and
`search_parallel` passed (23/23, 225.81 seconds). Full-library ASan/UBSan builds
passed the FFT batch suite and the streamed public/private exact parallel
long-symbol progress fixture; leak checks were disabled for the sandbox
restriction. `git diff --check` passed. Native display workflows and physical
speaker/microphone reception were not rerun.

The final full-profile probe also completed successfully within its original
1,800-second limit, using a frozen copy of the rebuilt library and no other
heavy workload dispatched in this task. It sent public raw `0` with 3 dBm /
-170 dB, 3,600 Hz Rate, 1,500 Hz carrier, -8 dB/Hz target, 14,400 Hz internal
sampling, 100 ppm clock mismatch, 0.5-degree phase diffusion and noise seed 1.
The receiver retained all 1,914 alternatives under a 1,200 MiB workspace budget,
with automatic worker selection and no narrower fallback. One symbol was
398.107170553 seconds. It accepted exact pending `0` at 399.217777778 seconds
of input and 1,381.07810026 seconds of wall time (23:01), with evidence
120.519906936, identical to the earlier full-profile result. It then observed
physical completion at 796.547708333 seconds of input and 1,719.18909057 seconds
of wall time (28:39). The final 800.428263889-second input finished at
1,719.19319546 seconds, with one accepted bit and exactly one completion;
neither `finish()` nor an oracle supplied completion.

The largest sampled RSS was 1,195,220 KiB (about 1.14 GiB), falling to
312,948 KiB during continuation; CPU time was 15,872.917 seconds. The command
was `timeout 1800s /tmp/pump-exact-cache-parallel-053542`, with source, frozen
archive and output alongside it as `.cpp`, `.a` and `.log`. The archive SHA-256
was `c91e87b42f43a94b7eec2bcfbc0563342b864fee693d212d20b064edc1665f5b`.
This establishes successful pending and physical completion for this one public
seed, not the displayed model's population success rate, private-key behavior,
real-time throughput or a physical audio link. End-to-end computation remains
long at this setting; the roughly 23% reduced-benchmark saving is not a measured
full-profile speedup.

### Shared Link planner (2026-09-18)

The new native document tab shares the existing tuning, transfer, LPI and
receiver-workspace models across FLTK and Rev. Preview targets and link-budget
inputs remain separate from live settings until the operator applies a short-
or long-message target. Draft planning retains the existing exact wire count;
completion timing adds fully scored absence. No transport, framing, receiver
progress or physical-end behavior changed.

The dedicated `gui_link_planner` suite checks independent numerical anchors at
−8 and −23 dB-Hz, sample quantization, the one-second and one-day transitions,
fixed modes, bounded curves and duration overflow. Controller/application cases
cover preview isolation, explicit apply, prompt cancellation and invalid input,
exact short/raw/interval draft counts, stale-estimate withdrawal and asynchronous
estimate failure/recovery. Native document checks cover narrow and wide layouts,
actions, one general LPI warning, and identical full/tiled chart damage in RGB,
grayscale and monochrome. The existing application regression explicitly hides
the current-draft LPI advisory on this new tab while retaining it on every
pre-existing page.

Release builds succeeded with GCC/FLTK and Clang/Rev. All 21 development-contract
suites passed; the initial 29-suite run required the new-tab advisory expectation
above, then `gui_application` passed on rerun. Final focused shared GUI checks
passed 9/9 for FLTK and 8/8 for Rev, including the planner, application, document
layout and adapter-boundary checks. No physical radio/audio link was tested.

On an isolated Xvfb display, FLTK `gui_adapter_conformance` and
`gui_document_conformance` passed. The full `gui_workflow` initially reached its
unchanged 300-second smoke limit while other builds/tests ran; an isolated rerun
passed in 225.21 seconds without changing the workflow, receiver or timeout.
The compact planner was also inspected in native FLTK and Rev windows: headline
times, both charts, all four milestones and Apply actions fit the default view.
After final wording corrections, both Release builds and both
`gui_link_planner`/`gui_application` pairs passed again (2/2 per backend).

Rev's full native `gui_workflow` did not pass: under software OpenGL on Xvfb it
reached the unchanged 300-second smoke limit in phase 17 while transmitting
sampled audio to the independent receiver. Its screen was still reporting
simulation progress. This run does not establish a completed Rev workflow or
whether the timeout differs from the baseline; no timeout or assertion was
relaxed to claim success.
Rev's `gui_adapter_conformance`, `gui_platform_conformance`,
`gui_coordinates_1x` and `gui_coordinates_2x` all passed on the same private
display (4/4). `git diff --check` passed.

### Planner order, units and clock/RAM limit (2026-09-18)

Link planner now follows Console. Frequency labels use plain decimal Hz/kHz/MHz,
and the corrected LPI example selects +23 dB-Hz: 0.568889 seconds per bit,
5.853333 seconds sending one bit, and a modeled observer/receiver ratio of
4.33307. The −23 case remains independent long-duration regression coverage.

The clock milestone now requires both carrier-search coverage and the existing
simulation estimator's wide-search workspace support. It uses the selected DSP
byte allowance, reports its 25/50/75% metadata, and identifies Clock or RAM as the
limiting condition. Complete native projection-bin candidates avoid arbitrary
one-sample memory gaps; every returned target is independently rechecked. The
controller preserves full floating-point precision through prompting/applying
these sample-sensitive targets. No sampled receiver or wire behavior changed.

New regression cases cover 512 MiB versus 1 GiB at −8, fitting returned
milestones, 75% selection propagation, exact prompt/apply round trips, second-tab
order, +23 numeric anchors, and `4.375–10.625 kHz` endpoint formatting. Both
Release backends rebuilt successfully. Rev's eight focused shared GUI suites
passed. The updated FLTK window was visually checked on a private Xvfb display;
the navigation, controls, milestones and graphs fit the default window.
All 29 GCC/FLTK headless checks passed in 242.03 seconds, including every one of
the 21 development-contract suites. `git diff --check` passed. Native end-to-end
workflows and physical audio were not rerun for this planner-only correction;
the earlier Rev workflow timeout remains an unclosed validation limit.

### Shared link budget and empty-draft preview (2026-09-18)

Power, path loss and noise now lead Link planner, with a target margin or
shortfall and clock/RAM support in the headline. The Simulation selector is
Yes/No: No exposes the shared editable budget fields; Yes shows computation
time estimates. RX success remains visible and identifies the current draft's
transmit target, independently of the planner's preview target. Detailed model
assumptions and the supplied propagation references are collapsed by default;
the moonbounce reference uses the confirmed 220 dB path loss.

An empty text/raw draft uses a hypothetical single raw zero bit for GUI
estimates and cannot start ordinary transmission. Clearing it leaves the
composer empty. Empty attachments, exact nonempty short/raw encodings, fixed
intervals and physical completion retain their established behavior. Editing
the hypothetical budget with Simulation set to No does not reconfigure the
live receiver or discard pending data.

Both Release builds succeeded. All 29 GCC/FLTK headless suites passed in
220.17 seconds, including every development-contract suite. After the final
input-buffer handling and graph-spacing changes, the ten focused FLTK checks
passed again in 64.18 seconds, including `gui_controller`; Rev's nine focused
checks passed in 2.30 seconds. Added cases exercise character-by-character
power/noise/path entry, incomplete-input suppression through asynchronous
estimate completion, valid-input recovery, preset/dialog synchronization,
empty-draft send guards and uninterrupted pending reception.

Final native windows were inspected in FLTK at default and minimum sizes and
Rev at default size. Budget controls, both graphs and all four milestones fit
the default view. The minimum view retains the shared header and scrolls the
remaining planner content. Simulation No shows the editable budget fields;
Simulation Yes shows CPU/GPU estimates. The live-mode preview used a null audio
device, not a physical radio; its unpaced input can report receiver overruns.

Final FLTK native adapter/document conformance passed (2/2, 47.73 seconds).
Two earlier adapter runs failed the bitmap-overlay focus assertion. A diagnostic
run showed the original control enabled and focus restored at both window
sizes; the final uninstrumented test then passed with its original assertion
and timeout. No adapter or test change was retained, so those earlier failures
are recorded without claiming a diagnosed fix.

Rev native adapter/platform and 1×/2× coordinate conformance passed (4/4,
58.86 seconds). `git diff --check` passed. Full native transmission workflows
and physical audio were not rerun; the previously recorded Rev workflow timeout
remains an unclosed validation limit.

### Persistent link inputs and reference units (2026-09-18)

The shared top bar now owns the only visible power/path/noise controls, in both
Simulation Yes and No. Link planner starts with the link verdict and derived
received strength. Groundwave references now specify 180 dB path loss at 1 MHz
and 210 dB at 30 MHz, both at 150 miles. No timing, link-budget arithmetic,
wire encoding or receiver behavior changed.

Simulation-only CPU/GPU estimates use a separate 48-pixel row. With Simulation
No, shared geometry collapses that row and moves tabs, page frames and their
controls together; top-bar inputs and bottom modem settings stay fixed. Both
native adapters obtain page/tab/control geometry from the same application
facade. Regression coverage checks both modes, minimum/default/wide sizes,
visible/hidden transmit scope, header-label overlap and mode-switch round trips.

Both Release builds succeeded. The 17 focused GCC/FLTK GUI suites passed in
64.31 seconds, including controller, pending reception, planner, application,
layout, chrome, bindings, overlay and boundary checks. Rev passed the 16 matching
suites excluding the duplicated controller run in 12.08 seconds. Native windows
were visually inspected with Simulation Yes and No in both backends, including
the expanded header at minimum size; no controls overlap, and the planner has
no duplicate budget buttons.

Rev native adapter/platform and 1×/2× coordinate conformance passed (4/4,
58.39 seconds). FLTK document conformance passed; its adapter run reproduced the
previous bitmap-overlay focus failure. That fixture now waits for the draft's
Transmit target to become enabled before acquiring focus and before dismissing
the overlay, and explicitly checks initial focus acquisition. While the overlay
is open, readiness uses the underlying command state because the native
background is intentionally disabled. The original final restoration assertion
and suite timeout are unchanged; the new readiness waits are bounded.

After rebuilding the fixture, FLTK adapter conformance passed in 47.17 seconds.
`git diff --check` passed. Full native transmission workflows and physical audio
were not rerun for this presentation change; the previously recorded Rev
workflow timeout remains a validation limit.

### Sample-sensitive Clock/RAM navigation (2026-09-18)

Link planner's Stronger and Weaker actions now select checked clock/RAM fits,
normally about one dB apart, with a final-edge step when less than one dB
remains. The native target prompt and explicit application retain full numeric
precision. The main view has one short navigation hint; sample averaging and
the effect of rounded target labels are explained in the hidden model details.

The independent 1 Hz / 1500 Hz hobby-GPSDO fixture uses 4 GiB DSP RAM. Exact
−47 dB resolves to 18,973,665,962 samples and two-sample projection bins: clock
coverage fits, but RAM does not. The nearby 18,973,662,000-sample symbol uses
6,000-sample bins and fits both checks. Moving one sample to either side loses
that fit. These checks use the existing analytical receiver estimator; they
do not transmit a waveform or establish successful reception.

The search now checks projection-divisor alignments, clock-search caps,
automatic-profile transitions and FFT geometry events alongside a bounded
target sweep. Each offered endpoint is independently resolved and checked;
neither clock coverage nor memory use is assumed to be globally monotonic.
At 16 GiB, the 20,479,999,500-sample endpoint exposes an additional fitting
1,500-sample projection grid beyond the old 6,000-sample-aligned edge.

Both Release builds succeeded. An isolated 145-case probe covered practical
rates, all four oscillator presets, keyed and tone modes, profile transitions,
and the full bank-event case at 1 Hz / 1250 Hz. Every offered target passed an
independent profile resolution and receiver-estimator check, with finite bounds
and the correct navigation direction. The matrix took 1.93 seconds; its slowest
planner build took 35.6 ms on this host. Separate 0.01 Hz probes took about
40 ms. These are observed timings, not performance guarantees.

Native FLTK inspection at 1 Hz/GPSDO and the current 50% RAM allowance showed
the exact −47 dB RAM failure, a fitting Weaker selection, and the disabled
Weaker action at the discovered edge. The Clock/RAM milestone selected the same
precise value. Default and minimum window sizes remained legible without
overlap; minimum size scrolls to the milestones. The private display and
temporary capture harness were stopped afterward.

The initial Rev GUI run passed 15/16 suites; its new no-fit fixture used a
one-byte DSP budget, below the transfer API's 256 KiB minimum, so planning was
correctly unavailable before the intended RAM check. The fixture now uses a
valid 1 MiB budget and independently checks the fastest profile's RAM failure;
the one-byte input separately verifies rejection. The corrected planner suite
passed on GCC and Clang/Rev (3.48 seconds for the Rev CTest run). No production
behavior or existing compatibility assertion was changed for this correction.

The first broader GCC run was externally interrupted with SIGTERM after the
weak-signal suite passed; no cause was established. Its complete rerun passed
34/34 suites in 232.21 seconds, including all 21 development-contract suites
and 17 GUI suites (with four overlapping suites). The controller suite passed
in 64.12 seconds. Together with the corrected planner rerun, all 16 selected Rev
GUI suites passed. `git diff --check` passed. Full native transmission workflows
and physical audio were not rerun; the previously recorded Rev workflow timeout
remains an existing validation limit.


### Fitted target entry and phase-aware planner reception (2026-09-18)

Automatic short/long target entry below −20 dB-Hz now keeps an already fitting
value or chooses a checked nearby Clock/RAM fit, preferring weaker targets.
The check includes the unchanged companion target and deduplicated public/key
receive banks. Typed text remains editable while the label shows the effective
value; Enter and presets commit full precision without retuning. Explicit
planner Apply and manual RX lists keep their exact-input behavior. Tests cover
weaker and stronger fallback, sample-counter overflow recovery, shared bank
budgets, typing/commit eligibility, and unchanged exact short/raw wire counts.

Link planner now displays the preview's modeled RX probability and the existing
whole-symbol phase-coherence penalty. The estimator's probability formula and
production receiver are unchanged. Tests independently check the 2/e coherent
energy reference, zero-diffusion loss, transition cadences around 0.01 Hz,
GPSDO-model loss, and the distinction between a RAM fit and actual link power.
The preview uses one matching receiver and requires every wire bit correct;
the top-bar estimate retains the actual draft, receive bank and FEC model.

Both complete Release builds succeeded (GCC/FLTK and Clang/Rev). The initial
34-suite GCC run passed 33 suites; the numerical-anchor controller fixture
attempted planner Apply while deliberately invalid modem text was still active.
It now restores the original valid target before applying the exact anchor;
the invalid-edit preservation assertion and numerical anchor remain unchanged.
The corrected controller suite passed in 64.78 seconds. The final 16 other GCC
GUI suites passed in 9.77 seconds. Together, all 21 development-contract suites
and all 17 selected GUI suites passed, with four suites shared between them.

The final Rev selection passed 16/17 suites before its planner test's literal
expectation was updated to the refined explanation of relative-phase section
comparisons. That corrected planner rerun passed in 5.50 seconds; all 16 selected
Rev GUI suites and the simulation-estimate suite therefore passed. The existing
bitmap size-comparison compiler warning remains unrelated to these edits.

Native FLTK checks exercised real −60.5 keystrokes, Enter and preset callbacks.
The edit buffer stayed intact; commits displayed the exact fitted target. A
stale numeric-prefix validation notice found during inspection was fixed and
verified to clear after successful configuration while preserving unrelated
notices. At 1 Hz/GPSDO and this host's 50% RAM allowance, planner previews showed
above 99.9% RX at 170 dB path loss and below 0.1% at 214 dB, with phase loss near
18 dB visible in both. These are model outputs, not sampled reception results.
Default and minimum layouts were inspected; the private display exited normally.

A read-only receiver audit confirmed that the explicit adjacent-phase products
feed constellation diagnostics only. Production detection fits one common
complex amplitude/phase across a complete symbol; constant phase is tolerated,
but within-symbol phase wander is not independently tracked. The experimental
segmented detector sums section energies and does not test differential or
phase-state combining. Its results cannot establish a limit on those methods.
No multi-day PCM reception, physical RF link or full native transmission workflow
was tested. The previously recorded Rev workflow timeout remains a validation
limit. `git diff --check` passed.

### All-target fitting, RX curve and CPU pace (2026-09-18)

All automatic-mode GUI target editors now select checked Clock/RAM fits,
including short/long TX, manual RX lists and the independent native Planner
dropdown. TX and RX edits account for their companion targets and active
receive banks. Tests retain typed text until commit, verify accepted targets
against receiver support, and preserve exact wire counts and preview isolation.
The RX-list regression now checks the independently resolved usable targets
under its actual RAM budget instead of requiring the previously literal 6 dB
target, which can fall outside clock search. Fixed-mode geometry is unchanged.

The time graph overlays one-bit reception probability with its own percentage
axis. Tests check its probabilities independently, distinguish unavailable
regions from zero probability, retain a connected transition at 2 and 8 GiB,
and check draft independence and stable sampling across repaints, changed
keys/noise/phase/RAM and cache eviction. Costly sampling is bounded to twelve
additional evaluations; a checked fit within 0.001 dB can replace a tiny raw
rounding gap. Rendering tests cover all pixel formats, clipped damage, visible
isolated points and unbridged unsupported intervals.

The CPU indicator compares receiver-only work with incoming audio, including
the required fully scored absence tail, on the existing i9-13900H reference.
It reports processing seconds per audio second with green/yellow/red text and
colors. Expanded details retain total one-bit simulation CPU time. Tests verify
excluded synthetic-channel work, receive-bank scaling, tracking cost and draft
independence. Existing total CPU/GPU estimates, reception probability formulas,
LPI estimates, receiver scoring and framing are unchanged.

Both complete Release builds passed. The GCC shared GUI/estimate selection
passed 27/27 suites in 87.31 seconds; Clang/Rev passed 26/26 in 29.86 seconds.
Native FLTK adapter/document conformance passed 2/2; Rev adapter/platform and
1×/2× coordinate conformance passed 4/4 in 64.16 seconds. Default and minimum
windows were inspected in both backends: the target dropdown and CPU status
fit, and the RX transition is clearly visible beside the time curve. The
minimum window uses the existing document scroll. These are model and GUI
checks, not local CPU calibration or physical RF performance measurements.

The remaining 20 development-contract suites passed in 233.66 seconds;
together with the shared GUI/estimate selection, all 25 listed contract suites
passed, including independent short-wire vectors, four-hour sampled symbols,
physical completion and pending-prefix regressions. After clarifying the CPU
line as processing seconds per audio second, both builds and planner suites
passed again (15.51 seconds GCC, 17.73 seconds Rev), and all four window captures
were refreshed and checked. No physical audio/RF or full native transmission
workflow was rerun. `git diff --check` passed.

### Inline planner target and CPU graph (2026-09-18)

The Planner target now belongs to the scrollable planner document, in the same
row as Stronger/Weaker. A shared native text/choice control node reuses each
backend's ordinary bindings. Controls retain identity, edit buffers and cursor
selection across replacement or reordering; clipping clears input focus, and
removed controls release their native allocations outside active callbacks.
Native tests cover presets, Enter, forward/reverse Tab, hidden pages, viewport
and ancestor clipping, removal, and stale callbacks. A successful planner edit
now clears its own previous input error immediately without clearing a newer
unrelated notice.

A compact CPU graph sits beside the controls, using exactly the time graph's
target range. Its logarithmic ordinate is receiver processing seconds per
audio second, with a marked equal-pace limit and the selected preview point.
It reuses existing one-bit workload calculations without additional probability
trials. Tests independently check plotted CPU values, draft/cache invariance,
unsupported gaps, shared layout, all pixel formats and clipped damage repaint.
The CPU curve uses the existing reference processor model; no local benchmark
or receiver/LPI formula was introduced.

Both complete Release builds passed. GCC's shared GUI/estimate selection
passed 27/27 suites in 79.62 seconds. After the final label, vocabulary and
notice-recovery changes, its 11 affected shared suites passed in 14.86 seconds.
The FLTK heading-lifecycle fixture was corrected to find its visible extension
editor rather than the newly present hidden planner editor. A native clipping
regression also exposed a focus edge when attaching a retained editor beneath
an already hidden ancestor; the renderer now clears that ineligible focus.
The assertions were retained. Synthetic Tab events now provide their own text
instead of reusing stale event data; actual keyboard navigation was also tested.

The final Clang/Rev shared selection passed 26/26 suites in 32.56 seconds.
Final FLTK native adapter/document conformance passed 2/2 in 45.36 seconds.
Rev's native adapter suite passed with the inline-editor probes; platform and
1×/2× coordinate conformance passed 3/3 in 16.64 seconds. Real keyboard probes
also exercised partial typing, Enter, presets, forward/reverse Tab and scrolling.
Default/minimum layouts were visually inspected in both backends. The target
and step buttons align, the CPU graph fits beside them, and the existing
document scroll exposes lower graphs at smaller window sizes.

A final real-menu check exposed hover help overlapping the Rev preset popup.
Opening a popup now hides that help; the rebuilt native adapter suite and a
real hover-to-popup check both passed. Final default/minimum captures were
checked in both backends. No physical audio/RF or full native transmission
workflow was rerun for these view/control changes. `git diff --check` passed.

### Planner launch-command editor (2026-09-18)

The planner now includes a bounded multiline command editor beside
Stronger/Weaker and an explicit Load action. Startup and pasted settings share
one parser and canonical formatter. Commands use a relative executable name
for the host, retain exact numerical targets, and carry the link budget,
oscillator, waveform, rate, carrier and DSP percentage. The generated common
target uses the planner preview for both transmit profiles. Explicit short and
long overrides affect only their respective profiles. Pasting does not execute
a command, change settings or submit the draft; Load validates and applies the
settings with Simulation No. Ordinary polling retains uncommitted command text.

Controller checks cover generated-command round trips, quoted Windows paths,
partial imports, all requested settings, startup compatibility, malformed input,
and exact raw draft preservation. Full live-setting validation runs before any
field is committed: tests start a simulated session and verify that an
unsupported live bandwidth or channel level cannot partially replace settings.
The new validation entry point delegates to the existing live normalization;
receiver scoring, LPI calculations and all wire formats remain unchanged.

Native multiline controls retain caret/selection and use ordinary clipboard
operations. FLTK reserves Tab for navigation in the command field. Rev's
document-only multiline editor now scrolls glyphs, caret, selection and pointer
hit testing together, with bounded extents and wheel handoff at the edge.
Existing message editors retain their established behavior. Shared layout
checks cover widths from 220 through 1200 logical units, including command/Load
wrapping at narrow widths and the compact CPU graph below-right.

Both complete Release builds passed. The initial six GCC GUI suites passed in
73.50 seconds; 16 further shared GUI/estimate suites passed in 15.54 seconds.
The remaining 23 development-contract suites passed in 281.46 seconds; together
with gui_application and gui_controller, all 25 listed compatibility suites
passed. Rev's selected shared checks passed except an initially stale parser
binary; after rebuilding, parser and settings suites passed 2/2 on both
compilers. Parser cases include malformed signs, quotes, missing/unknown flags,
numeric bounds, frequency units and exact formatting.

Final parser/settings checks, including independent preview versus explicit
short/long overrides, passed 2/2 in 1.03 seconds with GCC and 1.28 seconds with
Clang/Rev. Rev native adapter/platform and 1×/2× coordinate conformance passed
4/4 in 92.87 seconds. Native fixtures explicitly wait for FLTK's deferred
scroll layout and supply an ordinary single-click interval for Rev hit testing;
their visibility, editing and scrolling assertions remain intact.

Final FLTK adapter conformance passed in 45.24 seconds. The unchanged document
suite passed five focused repetitions after one transient immediate-paint
assertion in the combined run (it had also passed the preceding two runs).
No document-renderer change or relaxed assertion was introduced. Both final
production binaries and all affected tests were rebuilt; `git diff --check`
passed.

Real windows in both backends verified copy/cut/paste, multiline editing,
Enter without transmission, Tab to Load, successful load with Simulation No,
normalization, and invalid-command recovery. Default and minimum windows were
inspected with the editor beside the target controls and no overlap. These are
GUI/model checks; no physical audio/RF or full native transmission workflow was
rerun for this settings feature.

### Compact planner command placement (2026-09-18)

The shared planner layout now aligns target controls, a 220–280 logical-pixel
command column and the CPU graph in one row at normal window sizes. Load sits
below the scrolling editor. Narrow documents pair the command and graph below
the controls, then stack them. Text size, command parsing, native editor behavior
and all modem models are unchanged.

Both GUI binaries rebuilt successfully. Existing planner geometry/plot and
launch-settings regressions passed 2/2 with GCC in 12.90 seconds and 2/2 with
Clang/Rev in 14.66 seconds. Layout assertions cover 220–1200 logical-pixel
documents, including compact-column alignment and non-overlap.
`git diff --check` passed. Protocol suites were not repeated for this
placement-only change.

Default and minimum windows were inspected in both backends: the three columns
fit, Load stays beneath the command, and the existing Apply buttons wrap when
needed. Native clipboard round trips, multiline Enter and Tab-to-Load focus
were also checked after the placement change.

### RX and observer warning colors; audio-link default (2026-09-19)

Available RX estimates below 80% and observer/receiver ratios below 8× now
use the shared red foreground in the planner and current-draft labels. Checks
use the unrounded estimates, so exactly 80% and 8× retain their usual color.
Pending, invalid and unavailable labels clear old warning tones; monochrome
keeps its established foreground. Both native adapters consume the same field
tone. The normal launch path-loss default is 120 dB; explicit launch settings
and the native smoke's established 60 dB fixture retain their behavior.

Planner regressions cover threshold boundaries, narrow/wide layouts, one/all-bit
estimates, limited RX references and hidden explanatory text. Historical 170 dB
numerical and probability-curve fixtures remain explicit, preserving their
independent anchors. Controller checks cover warning restoration and withdrawal.
A shared native fixture checks actual low, unavailable and high estimate labels
in color and monochrome on both backends.

Both complete Release builds passed. GCC's 28 shared GUI suites passed in
91.21 seconds; the selected 11 Clang/Rev shared suites passed in 106.63 seconds.
The remaining 21 development-contract suites passed in 290.03 seconds; with
the four shared GUI compatibility suites, all 25 required suites passed.
FLTK adapter/document conformance passed 2/2 in 53.16 seconds. Rev adapter,
platform and 1×/2× coordinate conformance passed 4/4 in 137.79 seconds, each on
a private X display. Default-size and minimum-size planner windows were captured
in both backends; the low RX percentage and observer ratio display in red
without overlap.

The hidden details and linked documentation describe a plausible reduction in
audible repetition with longer public patterns, including unencrypted use.
The 8× preference is not presented as a validated acoustic threshold or a
prediction of annoyance or interference complaints. No physical audio/RF or
controlled listening experiment was run for this presentation/default change.
`git diff --check` passed.

The subsequent wording refinement distinguishes perceived abruptness and
finite-pattern envelope/spectral structure from symbol-boundary repetition.
It also distinguishes statistical resemblance to noise from the observer's
ability to predict a public template. At the default geometry, both the
1,024-chip (4.33×) and 2,048-chip (8.52×) profiles already use the same pulse
shaping; the ratio assumes noise-like signals rather than testing their
randomness. Only explanatory text and its presentation assertions changed.
Both GUI binaries rebuilt, and the focused planner suite passed with GCC in
12.36 seconds and Clang/Rev in 14.51 seconds. No waveform, model calculation,
threshold, transport or receiver behavior changed in this refinement.

### Observer estimate error colors (2026-09-19)

Observer/receiver results outside the model range, unavailable results and
invalid-settings/error states now use red in the planner and current-draft
readout. Calculating remains neutral; valid ratios at or above 8× restore the
usual foreground. RX unavailable-state colors are unchanged.

Both GUI binaries rebuilt. Planner, controller and application checks passed
3/3 with GCC in 70.39 seconds and 3/3 with Clang/Rev in 74.51 seconds. Coverage
includes nonfinite planner ratios, encrypted/public out-of-range estimates,
invalid drafts/settings, pending state and restoration of a valid estimate.
The shared native fixture checks error and recovery colors in both color and
monochrome. FLTK adapter conformance passed in 49.90 seconds and Rev adapter
conformance passed in 68.57 seconds, each on a private X display.
`git diff --check` passed. This changes presentation only; modem behavior and
estimate calculations are unchanged.

### Independent Legacy radio-text modem (2026-09-19)

Legacy is a third Modem choice with a separate library, settings, audio session,
text presentation and waterfall. It implements BPSK31, BPSK125 and Olivia-4/2k
without importing the existing modem, transport, compression, crypto or link
models. Existing Robust/Fast DSP, wire algorithms, encryption and weak-signal/LPI
models were not edited. Shared changes are confined to mode hosting and a
read-only editor flag which defaults off for existing controls.

Independent interoperability checks passed both PCM directions against the
installed FLDigi 4.2.06 application for BPSK31 and BPSK125, and against the pinned
upstream FLDigi Olivia codec for four tones/2000 Hz. The PSK recordings were
reproduced byte-for-byte using an isolated configuration and fake audio backend.
Pinned recordings, hashes, independent wire vectors, provenance and optional
reproduction runners are retained under `tests/fixtures/legacy`. Olivia's
reference receiver used SyncThreshold=8; its default threshold also produced
startup/shutdown extras in the upstream-to-upstream control. These checks did
not use physical audio/RF links or establish fading-channel performance.

Legacy regressions cover immediate decoded/generated text, arbitrary PCM
chunks, partial-codeword and mid-conversation PSK acquisition, carrier changes,
noise and steady-carrier rejection, UTF-8 presentation, bounded snapshots and
waterfall retention, exclusive simplex audio, asynchronous cancellation and
mode ownership. Playback failure retains the draft; successful playback clears
only its submitted prefix, preserving new or replacement text. The sampled
regression channel uses the FLDigi LinSim 400–3400 Hz noise convention and a
fixed −25..+25 dB sweep in 5 dB steps. Its −5/+5 dB PSK125 failure/success
bracket is asserted separately. No production simulation path is added.

Both Release GUI builds succeeded. The final focused Legacy/Fast GUI and
Legacy DSP/session/simulation/isolation group passed 9/9. Shared GUI boundary,
layout, binding, editor, record and presentation checks passed; an additional
11 GUI/LPI checks also passed. The source-boundary checker was separately
challenged with forbidden old-DSP, crypto, Fast, simulation and reverse imports,
all correctly rejected. PSK and Olivia standalone ASan/UBSan checks passed with
leak detection disabled because LeakSanitizer is unsupported under this
sandbox's ptrace configuration.

FLTK native adapter/document conformance passed 2/2 in 54.93 seconds. Rev native
adapter, platform/clipboard and 1×/2× coordinates passed 4/4 in 171.34 seconds
on a managed private display with REV_SCALE=1 and two CPU cores. Native probes
cover the actual Legacy controls and read-only editor selection/copy/update
behavior. Default and minimum layouts were captured and visually inspected in
both backends without overlap. Windows runtime and physical radio/audio-link
validation remain separate requirements.

The existing development-contract, Fast and security regression group passed
39/40 suites. `differential_receiver_probability` exceeded CTest's 1500-second
limit while still computing; its completed unshaped calibration matrix passed.
That test, its core implementation and its binary's linked components do not
include Legacy changes. This run does not establish a complete calibration
pass, and no assertion or timeout was weakened.

The FLTK production GUI smoke passed with its supported `--smoke-timeout 600`
option. Its standard 300-second run timed out in phase 15; the original HEAD
GUI, rebuilt without Legacy and run on a separate private display, reproduced
the same phase-15 timeout after 300.408 seconds. The longer run preserved all
existing smoke assertions covering text, files, binary editing, cancellation,
retained saves, live plots, keys and page switching.

Rev's initial production smoke exceeded the 300-second budget in phase 11.
A 600-second run restricted to two CPU cores reached that phase's replay
assertion but displayed nine frames instead of the required ten. Its diagnostic
`dropped` counter refers to omitted plot measurements, not lost source text.
The final retry without CPU affinity restrictions passed that replay check,
then exceeded the 600-second budget in phase 17 while transmitting sampled
audio to the independent receiver (600.264 seconds total). Thus Rev native
control conformance passed, but this environment did not complete its full
production workflow within the tested budgets. No existing modem, replay or
smoke assertion was changed to accommodate these results. All private displays
were closed after validation, and `git diff --check` passed.

### Legacy transmission separators and controls (2026-09-19)

Legacy sessions now encode three leading LF characters and one trailing LF
around each submitted draft. The real transmitter callback echoes those same
characters into the transcript. The 32,768-byte draft limit is preserved by a
separate codec allowance for the four added characters, and successful playback
commits only the original draft byte count. Cancel stops queued or active TX,
retains the draft and newly edited text, then resumes RX after audio closure.
The same button reads Transmit, Cancel or Cancelling as appropriate. Ctrl+Enter
in the draft starts TX; repeated shortcuts during TX do not cancel it. Existing
Robust/Fast modem code and keyboard behavior are unchanged.

Both Release GUI executables rebuilt. The focused Legacy/Fast, shared GUI,
boundary, short dictionary and transport checks passed 20/20 in 77.65 seconds.
Sampled session tests decode the exact separators in all three modes, including
consecutive transmissions; cancellation covers maximum-sized drafts and partial
audio emission without a committed draft. Live GUI tests cover cancellation,
draft edits during TX, RX resumption and exclusive audio ownership. Native
shortcut/control conformance passed on FLTK (53.18 seconds) and Rev (38.22
seconds), using private displays that were closed afterward. No native adapter
implementation changed. `git diff --check` passed. The earlier full-workflow
and calibration timeout limits recorded above were not rerun for this scoped
follow-up.

### Fast input diagnostics before synchronization (2026-09-19)

Fast's constellation previously remained empty until a valid marker admitted
payload symbols, even when its waveform and waterfall showed captured audio.
The RX panel now shows a separate, bounded history of actual matched-filter
input I/Q before the first payload symbols, explicitly labeled unsynchronized
with its automatic display scale. Payload observations still switch to the
existing equalized constellation. Recent PCM RMS/peak and clipping indications
help distinguish signal level from synchronization. The new tap has no feedback
into modem acquisition, symbol decisions, coding or physical completion.

Both Release GUI executables rebuilt. The focused Fast, shared GUI, boundary,
short dictionary and transport group passed 23/23 in 70.14 seconds. The final
expanded `fast_telemetry` suite also passed (0.71 seconds): independent FM and
acoustic tone responses check actual uncorrected I/Q, fractional display cadence,
callback chunk independence, bounded storage and immutable frames. Collecting
or throwing input observers leave all soft evidence and receiver progress
identical. The live GUI regression shows real low-level input without admitting
payload, exposing a file or manufacturing completion, then preserves the plots
on cancellation; its successful sampled transfer still produces exact source
bytes and an equalized constellation. Shared pixel checks cover weak-input
scaling, source separation and retained snapshots.

Native adapter conformance passed on FLTK (51.76 seconds) and Rev (69.89
seconds), using a private display that was closed afterward. No native adapter
implementation changed. `git diff --check` passed.

No acquisition thresholds, wire framing or regular modem algorithms changed.
These software checks do not establish why the reported physical
speaker/microphone link failed to synchronize or qualify an acoustic channel.

### Fast expected-SNR controls and radio capacity profiles (2026-09-21)

Fast now has expected-SNR and symbol-rate selectors shared by both native
backends. Assumed SNR is referenced to the original channel bandwidth: cable
65..25 dB over 18 kHz, acoustic 13..−27 dB over 17.5 kHz, and radio
20..−20 dB over 2.4 kHz. Weak presets narrow bandwidth with unchanged modeled
total signal power. They do not measure or negotiate a link. Manual overrides,
per-channel retained choices, mode isolation and active-transfer freezing are
covered by shared GUI tests. Computed preset frequencies use a binary grid to
avoid last-bit math-library differences in the exact peer integrity context.

SSB/FM defaults now select capacity 64-QAM, LDPC 3/4, four frames, approximately
0.3% RS, compact source bytes, 2181.818 baud across 300–2700 Hz and markers every
four intervals. Estimated 50 MB public-file throughput is 8.70 kbit/s, versus
3.10/1.63 kbit/s for the previous SSB/FM defaults. Separate exact-default codec
tests cover public/keyed operation. Sampled 48 kHz radio cases cover 20 dB noise,
carrier offset and a static echo; these are not live RF measurements. Explicit
classic profiles and ordinary transport remain unchanged.

Capacity single-carrier rates below 1000 baud may use bounded multirate
processing. Tests recover clean bits at 5, 15, 100 and 500 baud, including the
dense cable constellation at 500 baud, and check one-baud memory bounds.
The actual −10 dB radio preset recovers a complete LDPC frame through sampled
noise over the original 2.4 kHz reference band. The same noise floor persists
through real trailing absence. Slow/sparse settings now emit enough silence
to score their complete marker/pilot observation windows; EOF and partial
silence still cannot complete reception. Nominal cable/radio and classic
tail durations remain unchanged.

A 104-frame production LDPC/QAM screen found dense half-rate QAM choices that
failed despite favorable information estimates. Auto excludes rate 1/2 above
16-QAM pending calibrated thresholds. The 36 dB cable replacement,
4096-QAM/LDPC 7/9, passes its four screened frames with 3.46% less estimated
throughput. All corrected cable menu points passed four frames each; these
small samples are not whole-file reliability estimates.

Both Release GUI executables and the CLI rebuilt. The final Fast/shared-GUI
group passed **26/26 in 168.82 seconds**. The final focused helper check also
passed after preserving explicit classic Auto timing. Independent checks
covered 1,604 SNR points with monotonic bulk throughput, bounded bandwidth,
canonical rate round-trips and invalid-input rejection. Rev's final self-check
passed. Complete native conformance passed on FLTK and Rev using private
virtual displays; checks include every new dropdown label and all four-line
detail variants at both minimum and default sizes. Native testing caught and
fixed overly long OFDM rate labels. Private displays were closed afterward.

[Integration logs](validation-data/fast/snr-presets-20260921/README.md),
[radio evidence](validation-data/fast/radio-capacity-20260921/README.md),
[LDPC screening](validation-data/fast/wire-snr-screen-20260921/README.md), and
[low-rate PCM evidence](validation-data/fast/low-rate-20260921/README.md)
record sources, noise definitions and limits. No new live audio/RF test or
physical margin guarantee is claimed.

The complete focused ordinary-modem development-contract group also passed
**29/29 in 1375.19 seconds**, including independent short/source vectors,
sampled physical-end and weak-signal tests, pending-reception GUI tests, and
the full `differential_receiver_probability` calibration. That long calibration
finished within its unchanged timeout; no assertions were relaxed. Final
`git diff --check` passed.

### Acoustic low-SNR premature-end recovery (2026-09-21)

Reproduced the reported acoustic Auto −10 dB failure on the default real
speaker/microphone devices: the old receiver ended around 46 seconds while
the 90.18-second signal continued. One failed pilot cleared a flag required
by all later pilots, so valid subsequent audio was counted as absent before
the next full marker, roughly 30.65 seconds away.

Capacity single-carrier reception now separates marker framing, current
physical presence and demapping quality. Bad groups preserve erasures without
disabling later pilots; coherent phase jumps recover common phase. A bounded
presence-only check of already scheduled marker quarters prevents a phase
change inside a long marker from falsely declaring absence at five baud.
It cannot acquire framing or weaken the whole-word/exact-sign admission test.
Slow-rate real silence tails cover the completed observation windows. Missing
intervals retain positions, and EOF/partial silence still cannot complete.

The acoustic single-carrier fallback also reused OFDM's amplitude despite
different normalization, causing an unintended 10.055 dB average output-power
increase. Its amplitude now preserves nominal OFDM PCM power. Measured
production PCM agrees within 0.007 dB across OFDM and both single-carrier
processing paths; a negative control restoring the old amplitude fails.

The identical original live recording now yields all eight intervals through
the corrected receiver and ends after transmission. A lower-level live trial
has two raw errors across 16,384 bits. A complete 60-byte live transfer then
passed all 64 intervals, matching the source SHA-256 after 714 LDPC bit
corrections. Physical completion occurred at 526.869 capture seconds; total
capture was 531.284 seconds. There was no PCM clipping, overrun or reported
device error. Final-build replay recovered the same source. System mixer
settings were not changed. These trials do not establish a whole-file success
probability or calibrated −10 dB reference-band sensitivity.

New regressions cover brief pilot fades, permanent phase steps, a damaged
scheduled marker, explicit erasure positions, later exact recovery, genuine
silence/noise completion, partial absence and EOF. The integrated Fast,
shared-GUI and selected ordinary compatibility group passed **31/31 in
343.95 seconds**. After the last runtime refinement, six affected suites
passed again in **37.16 seconds**. Both GUI backends and the CLI rebuilt.
No native adapter or regular modem algorithm changed; native rendering and
the unchanged long ordinary probability calibration were not repeated.

[Diagnosis](fast-acoustic-low-snr-recovery.md) and
[reproduction records](validation-data/fast/acoustic-low-snr-recovery-20260921/README.md)
include before/after data, complete-source fixtures, commands and recording
hashes. Large PCM recordings remain in `/tmp`; compact evidence is archived.

### Cable transmit-level calibration (2026-09-21)

The user's left headphone-to-microphone cable was tested through the default
physical devices and production S16 audio API. Twenty-six modem trials plus
eight tone levels totaled 656.4 seconds of capture. System mixer settings were
unchanged: output 95%, hardware Capture and Mic Boost both 0 dB. Nominal 18 kHz
cable trials swept amplitudes 0.10–0.40; raw trials also covered 1.8 kHz, 180 Hz,
and the radio-profile waveforms at 2.4 kHz and 240 Hz. No actual radio or RF path
was tested.

The 100,000-byte trials passed at 0.25–0.40 and failed at 0.10–0.20. Three
1,000,000-byte trials at 0.30, 0.35 and 0.40 all recovered exact SHA-256 values,
4,572 intervals and 144 LDPC frames each, but the two higher levels exceeded
digital full scale. The 0.30 fixture peaked at 0.8594. The existing 0.30 default
is retained. Settled generated RMS remained effectively constant across
narrowing, so no additional bandwidth scaling was added. The manually selected
1.8 kHz cable geometry accumulated more than the production FIFO allowance;
its offline calibration result is not a real-time reception qualification.

The new offline `tools/fast_known_evm.py` compares received symbols with saved
transmitted bits, retaining interval padding and rejecting missing observations.
Its seven independent mapping, geometry and misleading-EVM checks pass; the
existing eleven tone-analysis checks also pass. Independently calculated raw
bit-error counts agree with every raw probe. Noiseless controls show that the
narrowest residual plateau is largely modem DSP error. A separate sine-level
sweep measured only 0.00666 dB gain variation over a 30-fold range.

Only documentation and offline analysis tools/tests changed; no runtime,
transport or GUI behavior changed, so C++ and native GUI suites were not
repeated for this study. These are small calibration samples, not statistical
whole-file reliability measurements. [Findings](fast-cable-level-calibration.md)
and [reproduction evidence](validation-data/fast/cable-level-calibration-20260921/README.md)
include exact metrics, limitations and hashes of the retained recordings.

### Acoustic OFDM Auto 3/0 dB premature-end recovery (2026-09-21)

The user reported the fixed-coding-geometry error while transmission continued.
These presets use OFDM, unlike the previously corrected -10 dB single carrier.
Sampled production PCM reproduced premature ends after echo, delay and clock
changes: the strict acquisition-strength signature gated maintained tracking,
and eight failed blocks manufactured an apparent 6.144-second absence. Narrow
depth-eight presets also waited 36.864/74.496 seconds between channel refreshes.

Acquisition remains at 16/256 allowed sign errors; maintaining an established
block ordinal now permits 32/256, retaining independent verification and the
residual-energy guard. Pilot-only timing search expands from +/-6 to +/-24
samples without coarsening the 0.15-sample grid. Coding integrity, fixed physical
coordinates and six-second observed absence are unchanged. The documented
conditional maintenance probability is distinct from acquisition and from
accepted-byte integrity; the old acquisition aggregate claim is not applied
to the new maintenance threshold.

Automatic OFDM depth now shrinks with the number of data tones to retain the
nominal refresh cadence where the one-frame minimum permits. Auto 6/3/0 dB use
depth 4/2/1, with estimated public 50 MB throughput costs of 3.88/7.87/13.84%.
The nominal 13 dB setting is unchanged. Shared GUI depth choices now include 2,
and both endpoints must restart and reselect matching Auto settings.

Both original unmodified live baselines passed, so the user's exact physical
trigger was not observed. Adding a controlled 16-sample delay to the recorded
0 dB audio reproduces the error at 27.25 seconds with 26/508 intervals; final
receiver replay recovers exact bytes and all 508 intervals at 167.80 seconds.
Four fresh live runs with the new presets, 60 bytes and 20 KB each at 3/0 dB,
all recover exact source SHA-256 values with zero failed LDPC frames. No audio
errors or queue overruns occurred, maximum FIFO was 0.385 seconds, and system
volume settings were unchanged. Six live captures total 477.35 seconds; preset
labels are not claims of calibrated live SNR or statistical reliability.

New regressions independently preserve the original depth-eight failures and
check complete keyed 20 KB files at the new defaults, explicit erased positions,
corrupted acquisition training, noise-only input, partial absence, noisy physical
completion and EOF. The integrated Fast/shared-GUI/selected ordinary compatibility
group passes **33/33 in 311.33 seconds**, including the unchanged 92-case Fast
SNR matrix. Both Release GUI executables and the CLI rebuilt. No native adapter
or ordinary modem algorithm changed, so native rendering and the unchanged
long ordinary probability calibration were not repeated. A repository-local
temporary directory avoided the full host `/tmp` filesystem during tests.

[Diagnosis and tradeoffs](fast-acoustic-ofdm-recovery.md) and
[reproduction records](validation-data/fast/acoustic-ofdm-recovery-20260921/README.md)
preserve exact commands, captures' hashes, counterfactual prototypes and logs.

### Short acoustic minimum transfer time across Expected SNR (2026-09-22)

The separate `acoustic-short` channel now uses one minimum-airtime search across
its Expected SNR range. It selects the greatest modeled throughput within a
10.5-second minimum-transfer budget, or the shortest minimum when that budget
cannot be met. Sixty encoded bytes represent a minimal XZ source in both public
and keyed operation. Smaller convolutional coding cycles cover weak settings;
16,200-bit LDPC remains where it meets the target with better throughput. Other
channel profiles and the default 3 dB short selection retain their geometry.

At −6 dB over the original 17.5 kHz band, compact QPSK occupies about 1,104 Hz
and the generated minimum waveform takes 9.796 seconds, including the physical
end silence. The final −3 dB choice caps single carrier at 1,000 symbols/s to
keep a 5 ms echo inside its equalizer span and takes 9.513 seconds at minimum.
The 0/3/6/13 dB minima are 9.981/10.343/9.610/9.610 seconds. Larger messages need
more coding cycles; the existing 3 dB 2.5 KiB attachment still takes 11.794
seconds. Settings below −6 dB can exceed ten seconds even at minimum size.

Compact coding reuses the terminated K=7 trellis with outer GF(65536) correction
and whole-cycle SHA/HMAC verification. Its distinct profile context and bounded
geometry are limited to `acoustic_short`. A 256-symbol preamble, a marker with
64 fitting and 128 held-out verification symbols, and sixteen-symbol tracking
pilots reduce startup while retaining independent admission evidence. Only six
seconds of fully scored absence finish reception. EOF, cancellation, correction
and integrity success cannot expose source data; missing cycles keep their
positions. GUI coding/depth/parity fields report the actual selected code.

The ten-case −6 dB sampled suite and −3 dB public/keyed fixtures pass exact
recovery under fixed original-band noise density, a 0.30 echo at 5 ms and up to
±100 ppm drift. Controls cover EOF, partial absence, incomplete training,
corrupted marker verification and noise-only input. The 3 dB suite and 0/6/13 dB
transition fixtures also pass. Independent compact wire hashes and existing
LDPC/classic vectors remain. All 172 baseline reports for the four other
channel profiles are identical.

Both Release GUI backends and the CLI rebuilt. The selected Fast/shared-GUI
group passes all 40 checks across its initial run and a 92-case SNR rerun. The
initial SNR run timed out under overlapping calibration/UI load; the rerun kept
the same limits. Native adapter/document checks pass in FLTK, and adapter,
platform and both coordinate scales pass in Rev. The FLTK complete workflow passes on rerun in 268.93 seconds. Rev still misses
its existing three-second replay frame assertion when isolated; no complete Rev
workflow pass is claimed. Ordinary compatibility passes 28/29: the unchanged
differential receiver probability calibration reaches its 1,500-second CTest
timeout, including a brief pause used to isolate GUI timing. Its completed
portions are retained, but full calibration coverage remains unverified. No
assertions or unrelated runtime code were changed to accommodate these limits.

These are generated-audio and Linux GUI checks, not physical speaker/microphone
qualification or statistical file-success measurements. No audio hardware or
running user GUI was changed. [Profile behavior](fast-acoustic-short.md) and
[commands, sampled results and integration logs](validation-data/fast/acoustic-short-20260922/README.md)
record the limits and reproduction details.

### Developer mode layout after modem switching (2026-09-22)

FLTK and Rev now invalidate desktop layout when the page viewport, tab container
or tab presentation changes, independently of individual control rectangles.
Returning from Fast to Robust therefore restores the Robust tab row before
developer mode exposes additional tabs. Previously, retained Fast tab positions
could overlap one another and the Callsign, Grid and Repeatable row until a
window resize. Modem behavior, settings and message contents are unchanged.

The extended FLTK native regression fails against the original adapter and
passes with the fix. It establishes Fast geometry, returns to Robust, then
toggles developer mode repeatedly without resizing; default/minimum sizes,
both prior developer states, persistent controls and their children, tabs and
the page viewport are checked. Rev checks expected native placement in both
switching directions and after developer toggles. Shared binding tests cover
independent desktop geometry invalidation.

Both Release GUI executables rebuilt. The nine selected shared checks pass:
`gui_adapter_boundary`, `gui_boundary_regression`, `gui_layout`, `gui_bindings`,
`gui_application`, `gui_controller`, `gui_inspection`, `gui_binary_editor` and
`gui_overlay` (71.48 seconds together). FLTK adapter/document conformance passes
on a private 2400×1800 Xvfb display at 96 DPI (66.17 seconds); Rev adapter
conformance passes on a separate display with the same geometry (102.13 seconds).
The production FLTK simulation workflow also passes (260.50 seconds).
Rev platform and 1×/2× coordinate checks also pass. Its production workflow
still fails the previously recorded phase 11 three-second replay assertion
(12 frames, 11 changes, elapsed 3.096 seconds, fraction 0.898305); no assertion
was relaxed and no complete Rev workflow pass is claimed.
Temporary test files use repository-local build directories because host `/tmp`
is nearly full. These are Linux GUI checks; Windows rendering and physical
audio links are not qualified by this change.

### Restricted received text across all modems (2026-09-22)

Robust, Fast and Legacy now share a bytewise received-text boundary. Only ASCII
letters, digits and `,.@ -_/=` survive default presentation; each other byte
becomes one underscore without Unicode decoding or escape expansion. This
boundary covers native text, clipboard requests, received filenames, CLI
terminals/pipes/JSON and received-to-transmit pastes. CLI JSON deliberately
replaces `data_base64` with restricted `data_text`; explicit `rx --save PATH`
continues to save the original bytes. Local transmit input and its QR code keep
their existing character rules.

Shellcode mode starts false and is visible only with Developer mode enabled in
all three modems. Both permissions are required for printable ASCII
`0x20`–`0x7e`; control and non-ASCII bytes remain placeholders. Withdrawing either
permission filters received views, retained drafts and previous messages, clears
their native undo/redo histories, and invalidates pending clipboard requests
already handed to an adapter. Raw-bit pastes retain exact bits while their
decoded previews obey the active policy. A deliberate clear permits a fresh
local draft after discarding linked received editor histories.

Tests cover every byte value, default and Shellcode presentation, filenames,
copy/paste, raw-bit dictionary previews, revocation, stale editor callbacks,
previous messages, retained drafts behind attachments, exact saves, and native
Undo/Redo before the next application poll. Existing wire vectors, physical
absence completion assertions, source-decoding quotas and pending-bit progress
checks are unchanged.

Both Release GUI applications and the CLI rebuilt through `build.sh`. The
shared GUI group passes **33/33** with GCC/FLTK (125.24 seconds) and with
Clang/Rev (154.09 seconds). A final attachment-draft adjustment was followed by
fresh controller passes in both builds (83.85 and 94.38 seconds). Runtime tests
pass, as do the regular CLI's **29 cases** (58.27 seconds) and Fast CLI's
**24 cases** (31.26 seconds). The initial CLI test helper incorrectly requested
`--save` on `simulate`; it now performs an explicit `rx --save` of the generated
WAV and retains exact-byte assertions. An old Fast Unicode-filename expectation
was updated to the new per-byte placeholder policy.

The full compatibility group passes **30/30 across the main run and the CLI
rerun**. Its unchanged `differential_receiver_probability` calibration completes
in 1,187.36 seconds within the original 1,500-second timeout. No calibration,
physical-end or independent wire assertion was weakened.

On a private 2400×1800 Xvfb display at 96 DPI, final FLTK adapter/document tests
pass **2/2** (68.86 seconds). Rev platform and both coordinate-scale checks pass.
Its first adapter run fails the unchanged synthetic label-only document
clipping/focus assertion; an unchanged-binary rerun passes in 95.34 seconds.
No source or assertion was modified to make that rerun pass. The fixture can
receive delayed layout events, but this run does not establish the failure's
cause.

The final production workflows run separately after the heavy builds and
calibration finish. FLTK passes in **207.68 seconds**. Rev fails its previously
documented phase 11 three-second replay-frame assertion (elapsed 2.952 seconds,
9 frames, 8 changes, fraction 0.847458); no complete Rev workflow pass is claimed.
The timing assertion is unchanged and was not relaxed for this feature.

Reproduction logs are retained in the ignored `build/received-text-*.log` files.
These are generated-audio and Linux GUI checks, not Windows rendering or
physical speaker/microphone qualification.

## Stable simulation waterfall reference — 22 September 2026

The sampled simulation keeps nominal signal amplitude fixed and represents link
loss through noise amplitude. Its raw dBFS spectrum therefore changed the
waterfall background when only path loss or transmit power changed. The GUI now
uses a fixed display reference at the default −117 dBm received power and a
−100..0 dB color range. Signal peaks above that range saturate without rescaling
the background or retained history. Noise-density edits still change displayed
noise by the entered dB difference. Hardware retains its existing dBFS behavior.

The display offset travels with live and replay spectra. Receiver PCM, raw FFT
values, physical completion and message processing are unchanged. The caption,
tooltip and README distinguish the simulation display reference from hardware
full scale.

The new `gui_bitmaps` regression runs seeded channel PCM through the FFT and
color/grayscale renderers across 60–270 dB path loss, power edits and a 10 dB
noise-density edit. It checks signal attenuation, strong-signal saturation,
retained history, hardware switching and the real snapshot-to-bitmap path.
`live_resources` adds reference propagation through idle, transmission,
reconfiguration and replay, rejects nonfinite offsets, and independently
recomputes the unchanged raw FFT from retained waveform samples.

Both FLTK/GCC and Rev/Clang 19 passed all 33 headless GUI tests. A mutation that
removed the correction failed the new regression with “Simulation power/path
loss changed the receiver noise floor”; the correction was then restored.
The restored regression passes. The complete preservation contract passes
**30/30**, including the unchanged sampled differential calibration in
1,093.42 seconds (1,206.32 seconds for the complete group).

The initial FLTK production smoke exposed an older newline expectation: it
compared a space-flattened label against signal records that already preserve
LF. Its exact-content assertion now compares the permitted received text
without flattening newlines, consistent with the existing `gui_native_policy`
LF regression. No receive presentation behavior or timing assertion changed.
The initial adapter and document conformance cases both passed.
The corrected FLTK native group passes **3/3**: production workflow in
208.61 seconds, adapter conformance in 67.92 seconds, and document conformance
in 0.06 seconds on a private 2400×1800, 96 DPI Xvfb display.
Rev adapter/platform/1×/2× conformance passes **4/4** (87.21, 6.20, 5.18 and
5.17 seconds). Its initial workflow fails the previously documented replay
timing assertion, this time in phase 13: 2.867505 seconds, eight frames, seven
waveform changes and final fraction 0.864408. The assertion is unchanged.
An isolated retry of that same executable passes the full workflow in
296.91 seconds within the original time limit. Rev therefore passes all five
native cases across the group and retry, with the initial timing failure
retained as evidence of an intermittent issue.

Reproduction logs are retained under the ignored `build/waterfall-reference/`
directory. These are generated-audio and Linux GUI checks, not physical-link
or Windows qualification.

## Simulation waterfall noise and weak-signal contrast — 22 September 2026

The fixed reference above stopped path loss from recoloring noise, but its
100 dB range put default receiver noise near the middle of the palette. At
6 kHz sampling, seeded noise had a median palette index of 143; larger sample
rates raised it further because each FFT bin contained more noise power.
That bright background also reduced visible contrast for attenuated peaks.
The original color comparison accidentally requested a grayscale bitmap, so
it verified intensity invariance without exercising RGB output.

Simulation now uses a 40 dB display range: −50..−10 dB relative to the same
−117 dBm signal reference at 6 kHz sampling. The limits shift by
`10 log10(sample_rate / 6000)` at other sample rates. The default −164 dBm/Hz
receiver noise maps to dark blue, while noise-density edits still move the
background. Strong signals saturate without rescaling history. The full
frequency extent determines this adjustment so replay's four-bin peak
pooling does not change the reference. Hardware retains its original range.
Receiver samples, raw spectra and modem processing are unchanged.

The strengthened `gui_bitmaps` regression explicitly requests RGB and checks
that it differs from grayscale. It requires a dark-blue majority, stable
noise color across power/path-loss edits, retained-row pixel equality after
a strong signal, and identical noise pixels at 6 kHz, 14.4 kHz, 96 kHz,
4 MHz and 120 MHz sampling. It also checks matching live/replay limits.
Real seeded noise plus a narrow tone at 150 and 155 dB path loss is rendered
over 32 frames. Median carrier/background contrast is 66 and 34 grayscale
levels, respectively; the assertions require at least 45 and 25 levels plus
RGB blue-channel contrast. The default noise median is now palette index 41.

The initial calibration passes **33/33** headless GUI cases with both FLTK/GCC
and Rev/Clang 19 (110.63 and 123.34 seconds). Its FLTK native group passes
**3/3**, including the workflow in 232.23 seconds. A before/after comparison
from the production renderer,
using the same sampled noise and tones, is retained with its source in the
ignored `build/waterfall-calibration/` directory. It shows the previous
100 dB mapping beside the calibrated mapping; the false-color palette itself
is unchanged.

Review then identified an additional calibration case. Partial startup FFTs
have greater equivalent noise bandwidth than a complete Hann window, especially
at low explicit carrier/sample rates. The captured display gain now corrects
that bandwidth using the actual window length, including replay; raw FFT bins
are unchanged. Full windows retain exactly the original reference gain.
RGB regressions include the first 50 ms windows at 64 Hz, 400 Hz, 6 kHz and
14.4 kHz sampling. Noise-invariance coverage extends through 325 dB loss at
default power/noise, approaching the live session's supported sample-SNR limit.
The existing rejection of settings outside that limit is preserved, including
the unchanged atomic launch-settings regression.

All 33 shared GUI cases are verified on both backends across the final group
and focused runs. The bitmap fixture was adjusted from 330 to 325 dB so its
14.4 kHz sample rate stays within the existing live SNR limit; its intensity
and RGB assertions remain unchanged. `signal_view` passes, and all seven
`live_resources` cases pass, including the new startup/replay case and the
existing workspace, cancellation and reconfiguration checks. The low-rate
startup fixture uses a four-chip tone to keep frequency hypotheses inside
its narrow passband; no receiver validation was relaxed.

The final FLTK native group passes **3/3**: workflow in 275.72 seconds,
adapter conformance in 69.04 seconds and document conformance in 0.12 seconds,
on a private 2400×1800, 96 DPI Xvfb display.

The final Rev conformance cases pass **4/4**: adapter in 87.74 seconds,
platform in 6.20 seconds, and 1×/2× coordinates in 5.27 seconds each. Its
first workflow attempt hits the previously recorded phase-13 replay timing
failure: 2.848819 seconds, seven measured frames, six changes and final
fraction 0.898306. The timing assertion and application executable are
unchanged for the isolated retry.

That retry also fails phase 13 after 138.56 seconds: 2.959657 seconds of
replay, eight measured frames, seven changes and fraction 0.898306. No complete
Rev workflow pass is claimed for the final calibration; the existing timing
assertion remains intact. The color calibration, startup/reference propagation,
raw-spectrum checks, FLTK native workflow and Rev conformance checks pass.
Final logs and the renderer comparison are retained under the ignored
`build/waterfall-calibration/` directory. The complete 30-case preservation
contract recorded above was run for the initial reference change; this
display-only calibration follow-up uses the focused GUI, signal and live
resource checks without repeating the unchanged detector calibration.
