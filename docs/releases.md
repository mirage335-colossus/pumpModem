# Manual portable releases

The [release workflow](../.github/workflows/release.yml) builds, tests and
optionally publishes a release from a manually selected Git revision. Every
release contains three user bundles: Linux x86_64, Linux aarch64 and Windows
x64. Each contains the FLTK GUI, CLI, required application libraries, notices
and build information. Linux uses `.tar.gz`; Windows uses `.zip`. There is no
separate download for every Linux distribution or Windows version.

## Dispatch inputs

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | Empty | Use the CMake project version, currently `0.7.2`, producing a `v0.7.2` prefix. A release label such as `v001_00` can be supplied instead. |
| `experiment` | Checked / `true` | Set the release title to exactly `experiment`, mark it as a GitHub prerelease, and never mark it latest. |
| `publish` | Unchecked / `false` | Build and test only; retain downloadable Actions artifacts without creating a release or tag. Check to publish after all required jobs pass. |
| `linux_baseline` | `bookworm-sdk` | Select the Linux x86_64 builder: the source SDK with glibc 2.36, or `ubuntu-22.04` with glibc 2.35. ARM64 always uses the native Ubuntu 22.04 baseline. |

Leave `experiment` checked for builds that users needing assurance should
avoid. Clearing it selects an ordinary release with the version/build label
as its title; it does not add physical-device certification.

The run generates one shared build timestamp in `America/Chicago`, including
the applicable daylight-saving abbreviation. Tags have the form
`VERSION-YYYY-MM-DD-HHMMCDT` or `VERSION-YYYY-MM-DD-HHMMCST`, for example
`v001_00-2026-09-22-0252CDT`. All platform jobs use the same label even if they
finish on different dates. The release records the exact source commit;
the label is not a replacement for that provenance. Existing tag or release
collisions fail rather than moving a tag or replacing assets. If publication
fails after reserving the tag or creating the draft, those may remain for inspection; the workflow never deletes or overwrites them on retry.
Dispatch again with a new build timestamp, or deliberately remove the failed
tag/draft after inspecting it before reusing its exact label.

## Run with GitHub CLI

Run these commands from a checkout whose GitHub remote is the intended
repository, or add `--repo OWNER/REPO`. The workflow must first exist on the
repository's default branch, and the account needs write access. A branch or
tag selected with `--ref` determines the source to build. See GitHub's
[manual dispatch documentation](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
and the [GitHub CLI reference](https://cli.github.com/manual/gh_workflow_run).

First exercise the complete build and validation without publishing:

```sh
gh workflow run release.yml --ref main \
  -f experiment=true -f publish=false -f linux_baseline=bookworm-sdk
gh run list --workflow release.yml --limit 5
gh run watch RUN_ID --exit-status
gh run download RUN_ID --dir release-artifacts
```

Replace `main` with the repository's intended branch and `RUN_ID` with the ID
reported for that dispatch. Inspect its logs and artifacts before choosing to
publish. A later dispatch is a new build with its own label.

Publish an experimental release with the requested version style:

```sh
gh workflow run release.yml --ref main \
  -f version=v001_00 -f experiment=true -f publish=true \
  -f linux_baseline=bookworm-sdk
```

For an ordinary release that also covers Ubuntu 22.04 on x86_64:

```sh
gh workflow run release.yml --ref main \
  -f experiment=false -f publish=true -f linux_baseline=ubuntu-22.04
```

Omitting `version` uses the CMake version. In the Actions web interface, the
same options appear under **Run workflow**, with checkboxes for `experiment`
and `publish`.

## Choose the Linux baseline

`bookworm-sdk` uses the pinned, relocatable
[source SDK](../third_party/build-support/README.md#source-sdk-for-the-bookworm-abi-baseline).
The release workflow calls the shared `sdk-build.yml` workflow, reuses
its recipe-keyed archive cache, then installs and verifies that SDK before
building the application. Its isolated target dependencies and glibc ceiling
are checked during packaging. The initial SDK targets x86_64 with glibc 2.36;
it does not supply an ARM compiler.

`ubuntu-22.04` builds Linux x86_64 natively against glibc 2.35, using the same
portable packaging checks. Select it when Ubuntu 22.04 compatibility matters.
It replaces the SDK-built x86_64 bundle for that release, so the release still
has three user bundles. Neither option covers every historical Ubuntu LTS.

The release inventory names the application downloads
`DataPump-TAG-linux-x86_64.tar.gz`, `DataPump-TAG-linux-aarch64.tar.gz` and
`DataPump-TAG-windows-x86_64.zip`, where `TAG` is the shared version/build label.
It also includes release notes, metadata and `SHA256SUMS.txt`. SDK releases also
include the compiled SDK and its corresponding source archive for developers;
these are separate from the three application bundles. The optional `version`
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

The workflow tests copies of the completed archives on the following systems;
it does not rebuild one binary per test distribution. Publication depends on
successful build, package and compatibility jobs. Read the run results before
describing a particular release as tested.

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

Pull requests changing release automation exercise the Ubuntu 22.04 build path
without publishing. Manual dispatch defaults to the source SDK path.

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
original deadlines. Compatibility jobs allow both archive formats to finish.

The Windows live-profile capture fixture requests 1 ms timer resolution for
its existing 1 ms sleeps and restores it on exit, following Microsoft's
[timer API contract](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).
Coarse timer rounding could otherwise leave fixture audio undelivered after
30 seconds even with an empty decoder queue. The same audio chunks, 30-second
deadline and all receiver assertions remain in place. Windows runs the full
calibration after the other selected tests to report platform failures sooner.
