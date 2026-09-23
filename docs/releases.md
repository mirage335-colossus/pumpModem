# Manual portable releases

The [release workflow](../.github/workflows/release.yml) builds and publishes
three portable application bundles: Linux x86_64, Linux aarch64 and Windows x64.
Each contains the FLTK GUI, CLI, required application libraries and notices.
Unpack the whole archive and keep `bin/` and `lib/` together. Nothing needs to
be copied into the system `/lib` directory. Linux uses `.tar.gz`; Windows uses
`.zip`. Compatible distributions share the same binary.

Publication and extensive testing are separate operations. Publication checks
build success, both package formats, checksums, relocation, CLI/self-check
operation, dependency closure and the Linux ABI ceiling. It then publishes the
three selected downloads with **certification pending**. The separate
[certification workflow](../.github/workflows/certify.yml) later tests the exact
published downloads and attaches immutable reports to that same release. The
long preservation contract and GUI tests remain intact in that workflow.

## Dispatch inputs

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | Empty | Use the CMake version (`0.7.2`) as `v0.7.2`, or supply a label such as `v001_00`. |
| `experiment` | Checked | Title exactly `experiment`; GitHub prerelease; never Latest, even after certification passes. |
| `publish` | Checked | Publish after all three packages pass basic checks. Uncheck to retain a draft with its assets for inspection. |
| `linux_baseline` | `bookworm-sdk` | Source SDK/glibc 2.36 for x86_64, or `ubuntu-22.04`/glibc 2.35. ARM64 always uses the Ubuntu 22.04 baseline. |

Leave `experiment` checked for builds users needing assurance should avoid.
An ordinary release uses its version/date tag as the title. It becomes Latest
only after a successful separate certification; publication alone does not
promote it. Hosted certification is limited to the tests listed below and
cannot establish physical-device compatibility.

One `America/Chicago` timestamp supplies every platform's tag and filename:
`VERSION-YYYY-MM-DD-HHMMCDT` or `VERSION-YYYY-MM-DD-HHMMCST`, such as
`v001_00-2026-09-22-0252CDT`. Exact source commit and workflow identity are also
recorded. A colliding tag fails; tags are never moved and assets never clobbered.
A failed build/upload may leave a draft and reserved tag for inspection.
Dispatch with a fresh timestamp after correcting the problem; cleanup of a
failed draft/tag is a deliberate maintainer operation.

## Run with GitHub CLI

Use a checkout of the intended repository or add `--repo OWNER/REPO`. Workflows
must be registered on the default branch before normal manual dispatch; `--ref`
selects the build source. The account needs repository write access. See
[GitHub manual dispatch](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
and the [CLI reference](https://cli.github.com/manual/gh_workflow_run).

Prepare the SDK once, or after changing its recipe:

```sh
gh workflow run sdk-base.yml --ref main -f source=auto -f jobs=0 -f publish=true
```

Build and publish an experimental application release:

```sh
gh workflow run release.yml --ref main \
  -f version=v001_00 -f experiment=true -f publish=true \
  -f linux_baseline=bookworm-sdk
gh run list --workflow release.yml --limit 5
gh run watch RUN_ID --exit-status
gh release view RELEASE_TAG
```

Later, dispatch certification for that existing, published tag:

```sh
gh workflow run certify.yml --ref main -f release_tag=RELEASE_TAG
gh run list --workflow certify.yml --limit 5
gh run watch CERTIFICATION_RUN_ID --exit-status
```

Replace the uppercase placeholders with the recorded IDs/tag. Download the
three application archives from the release page linked in the run summary.
The helpers enumerate the dedicated, paginated release-assets API and stream
downloads by asset ID; they do not rely on an embedded release asset list.
Certification
checks out the release's exact source revision, regardless of the current tip
of `main`. It pins `SHA256SUMS.txt` before testing and refuses certification if
the source, tag, checksum inventory or published asset hashes change. It never
rebuilds or replaces the published downloads. Source unit tests compile from
the recorded revision; archive/GUI/CLI tests execute the released binaries.

Every completed certification records passed/failed status, required job
outcomes, source SHA and binary hashes in
`certification-RUN_ID-attempt-ATTEMPT.json` and `.md`. Reports are added to the
release, with links in its description. Missing, skipped, cancelled or failed
required jobs cannot grant a pass. Repeated runs retain prior reports. An
upload failure cannot promote a release. Drafts cannot be certified. Older ad-hoc releases without this workflow's
metadata, checksums and source-side certification tools are outside this path.

For the Ubuntu 22.04 x86_64 baseline, set `linux_baseline=ubuntu-22.04`.
Clear `experiment` for an ordinary release. `publish=false` still reserves a
tag and uploads to a draft; it is no longer an Actions-artifact rehearsal.
PRs changing automation run helper tests without creating releases or tags.

## Durable SDK storage and compilation time

The [base maintenance workflow](../.github/workflows/sdk-base.yml) owns the
[shared SDK producer](../.github/workflows/sdk-build.yml). It keeps compiled
SDKs, matching complete source archives and per-recipe checksum inventories in
the `base` release. This developer-only release is a prerelease, never Latest.
Old recipes remain available; an identical recipe is reused, and different
bytes under an existing recipe name are rejected. Each source archive preserves
the helper, recipe, pinned source downloads and bootstrap sources needed for
reconstruction.

`source=auto` retrieves the exact current recipe from `base`, building only if
absent. `source=base` requires and verifies durable reuse. `source=rebuild`
performs a cold replay from pinned sources; `publish=false` allows inspection
without changing durable storage. Cold rebuilds can produce different bytes;
existing recipe assets are immutable. Use a new recipe identity for an upgrade.
`jobs=0` uses `nproc`, the CPUs actually available to the runner, and passes that
count through Buildroot's internal parallelism. A positive number overrides it.
Application packaging also uses available runner cores; time-sensitive test
concurrency remains controlled independently.

Routine app builds download only the compiled SDK and checksum inventory. The
source archive stays in `base` for developers. Neither publication, certification
nor base maintenance uses Actions cache or artifact storage: platform builds
upload directly to a draft, and certification downloads the published assets.
The separate optional SDK/Rev regression workflow retains small application
artifacts for one day solely to test copies on other hosts. Those are not durable
releases. Existing unrelated CI artifacts are unchanged.

GitHub [limits each release asset to under 2 GiB](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases),
while documenting no limit on aggregate release size or download bandwidth.
The validated compiled SDK and source archives are approximately 233 MiB and
435 MiB respectively. This design adds certification assets after publication,
so it requires releases that allow later uploads; GitHub's optional
[immutable releases](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
prevent adding assets after publication. The helpers themselves never replace
existing application binaries or per-run certification reports.

The first successful split publication took **11m50s**, and the separate base
maintenance job reused and verified its SDK in **64 seconds** without compiling
it. The earlier **46m59s** application job included the extensive tests, which
now run only in certification. A cold SDK build remains an occasional maintenance
task, not work repeated for each application release. These timings are observed
hosted-runner results, not guarantees.

## Choose the Linux baseline

`bookworm-sdk` uses the pinned, relocatable
[source SDK](../third_party/build-support/README.md#source-sdk-for-the-bookworm-abi-baseline).
The release workflow downloads the exact recipe from the durable `base`
release, then verifies and installs that SDK before building the application.
A missing recipe fails with maintenance instructions; it never starts a cold
SDK build inside an application release. Its isolated target dependencies and glibc ceiling
are checked during packaging. The initial SDK targets x86_64 with glibc 2.36;
it does not supply an ARM compiler.

`ubuntu-22.04` builds Linux x86_64 natively against glibc 2.35, using the same
portable packaging checks. Select it when Ubuntu 22.04 compatibility matters.
It replaces the SDK-built x86_64 bundle for that release, so the release still
has three user bundles. Neither option covers every historical Ubuntu LTS.

The release inventory names the application downloads
`DataPump-TAG-linux-x86_64.tar.gz`, `DataPump-TAG-linux-aarch64.tar.gz` and
`DataPump-TAG-windows-x86_64.zip`, where `TAG` is the shared version/build label.
It also includes release notes, metadata and `SHA256SUMS.txt`. Compiled SDKs and their corresponding source archives are retained once per
recipe in `base`, separately from application releases. The optional `version`
input labels the release and archives; it does not rewrite the CMake project
version embedded in the application's `--version` output.

The default SDK bundle requires glibc 2.36 or newer and therefore does not
cover Ubuntu 22.04. A newer builder cannot gain an older ABI simply by setting
an audit limit. The destination supplies glibc and the ELF loader; those system
components are not copied into the bundle. Debian Bookworm's libc is
[glibc 2.36](https://packages.debian.org/en/bookworm/libc6). See
[the build guide](building.md#portable-releases) for the distinction between
the SDK and native baselines.

ARM64 builds run natively on a GitHub ARM64 runner in an Ubuntu 22.04 build
environment and retain the glibc 2.35 ceiling. Windows uses Visual Studio 2022,
the Windows SDK, static C/C++ runtimes and pinned static OpenSSL dependencies.
Generic CPU targets avoid requiring the particular build runner's instruction
set extensions. GitHub documents the available
[native ARM64 and Windows runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## Compatibility checks and scope

The separate certification workflow runs the full build, contract, GUI and
packaging selections and tests the published archives on the hosts below.
Read the attached report before describing a particular release as tested.

| Bundle | Build baseline | Copied-archive compatibility jobs |
| --- | --- | --- |
| Linux x86_64, `bookworm-sdk` | SDK, glibc 2.36 | Debian 12 and 13; Ubuntu 24.04 and 26.04; Arch Linux |
| Linux x86_64, `ubuntu-22.04` | Ubuntu 22.04, glibc 2.35 | Debian 12 and 13; Ubuntu 22.04, 24.04 and 26.04; Arch Linux |
| Linux aarch64 | Ubuntu 22.04, glibc 2.35 | Debian 12 and 13; Ubuntu 22.04, 24.04 and 26.04 |
| Windows x64 | Visual Studio 2022 with static CRT | Windows Server 2022 hosted-runner tests and archive relocation |

Linux package verification audits all shipped ELF libraries for the selected
glibc ceiling and checks relocation, checksums, CLI operation and the GUI
under a private display. These checks establish application/runtime evidence;
containers share the runner's kernel and do not emulate every target's
hardware, sound system or desktop session.

| Requested platform | Bundle and conditions |
| --- | --- |
| Debian 13 Trixie / Debian 12 Bookworm | Use the bundle matching the installed x86_64 or aarch64 user space. Both Linux baselines cover their glibc requirements. |
| Ubuntu / Ubuntu LTS | Match architecture and glibc floor. For Ubuntu 22.04 x86_64 choose a release built with `ubuntu-22.04`; the default SDK covers Ubuntu 24.04 and newer compatible releases. |
| Arch Linux | x86_64 bundle, with the selected ABI floor and a working X11/XWayland desktop. Rolling package updates may require renewed validation. |
| Gentoo | Matching x86_64/aarch64 bundle on a compatible glibc installation with X11/XWayland. Gentoo's musl configurations are outside this release format; Gentoo itself is not a compatibility-matrix job. |
| Velvet OS on Lenovo 100e Chromebook 2nd Gen | aarch64 bundle for the ARM64 Debian-based Velvet installation. Confirm the installed user-space architecture and glibc version; the model name alone is insufficient. |
| Raspberry Pi | aarch64 bundle for supported hardware running 64-bit Raspberry Pi OS with glibc 2.35 or newer. A 32-bit OS needs a separate 32-bit build that this workflow does not produce. |
| Windows 10 / Windows 11 | x64 bundle; Windows 10 version 1903 or newer is required by the application's Unicode-path behavior. This is not a native Windows ARM64 or 32-bit x86 build. |

[Velvet OS describes its target as Debian on ARM64](https://velvet-os.github.io/).
Its [Lenovo 100e hana device page](https://velvet-os.github.io/chromebooks/systems/oak/hana-100e-gen2.html)
reports software rendering without 3D acceleration; the shared FLTK frontend
avoids imposing the Rev frontend's OpenGL requirement. The workflow does not
test the physical Chromebook's audio devices or drivers.

Raspberry Pi's [architecture guide](https://www.raspberrypi.com/news/raspberry-pi-os-64-bit/)
distinguishes the original Pi/Zero's ARMv6, early Pi 2's ARMv7 and the ARM64-capable
Pi 3/Zero 2 W and later families. Check the current
[64-bit OS hardware list](https://www.raspberrypi.com/software/operating-systems/).
A 64-bit CPU with a 32-bit user space still needs a 32-bit application. Adding
ordinary Debian ARMv7 `armhf` binaries would not cover the original Pi/Zero's
ARMv6 hard-float user space. Hardware-specific Raspberry Pi audio remains
outside the hosted tests.

Microsoft documents Visual Studio 2022's ability to build desktop applications
for [Windows 10 and 11](https://learn.microsoft.com/en-us/visualstudio/releases/2022/compatibility?view=vs-2022).
The hosted runner uses Windows Server 2022. Windows 10 and Windows 11 client
installations are not directly tested by this workflow.

All Linux GUI users still need X11 or XWayland, fonts and appropriate system
drivers. Live audio depends on host devices, ALSA configuration and plugins;
Windows uses WinMM. Keep the complete extracted bundle, including its libraries
and notices, together. See [offline installation](offline-installation.md) for
local verification and runtime requirements.

Native Linux builders and compatibility jobs use checksum-pinned CMake 3.31.10
from Kitware for dependency inspection; SDK builds use their bundled CMake. Older CMake versions can lose inherited
executable RPATHs during recursive scans and falsely report conflicts with host
X11/font libraries. This is a host-tool upgrade: the application still compiles
against the selected glibc baseline, and host dependency rejection remains strict.
Release tests stop after the first failed suite for prompt feedback; successful
runs execute the full contract, GUI and packaging selections.

The hosted release configuration gives each otherwise-unbounded CTest case a
3600-second process limit instead of CTest's 1500-second default: the full
probability calibration exceeded that default on a hosted x86-64 runner. It
retains every sample and numerical assertion. Windows suites run serially to
avoid competing with the live/audio fixtures' own workers; their internal
progress deadlines remain unchanged.

The copied-archive GUI smoke uses the existing 600-second overall allowance.
It covers the complete text, attachment, interruption and replacement sequence;
the previous 300-second CI allowance expired late in that sequence on hosted
runners. Per-frame, pending-progress and cancellation checks retain their
original deadlines. Certification tests the selected published format on each compatibility host;
publication still verifies both generated package formats.

The Windows live-profile capture fixture requests 1 ms timer resolution for
its existing 1 ms sleeps and restores it on exit, following Microsoft's
[timer API contract](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).
Coarse timer rounding could otherwise leave fixture audio undelivered after
30 seconds even with an empty decoder queue. The same audio chunks, 30-second
deadline and all receiver assertions remain in place. Windows runs the full
calibration after the other selected tests to report platform failures sooner.
