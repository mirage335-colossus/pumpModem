# Manual portable releases

The [release workflow](../.github/workflows/release.yml) builds and publishes
six portable application bundles: separate **FLTK** and **Rev** builds for Linux
x86_64, Linux aarch64 and Windows x64. Each contains its selected GUI, the CLI,
required application libraries and notices. Backend names appear in both the
download filename and extracted directory, so the two installations can coexist.
Unpack the whole archive and keep `bin/`, `lib/` and `share/` together. Nothing needs to
be copied into the system `/lib` directory. Linux uses `.tar.gz`; Windows uses
`.zip`. Compatible distributions share the same binary.

Publication and extensive testing are separate operations. Publication checks
build success, both package formats, checksums, relocation, CLI/self-check
operation, dependency closure and the Linux ABI ceiling. It then publishes the
six selected downloads and a signed flat APT repository with **certification pending**. The separate
[certification workflow](../.github/workflows/certify.yml) later tests the exact
published downloads and attaches immutable reports to that same release. The
long preservation contract and GUI tests remain intact in that workflow.

## Dispatch inputs

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | Empty | Use the CMake version (`0.7.2`) as `v0.7.2`, or supply a label such as `v001_00`. |
| `experiment` | Checked | Title exactly `experiment`; GitHub prerelease; never Latest, even after certification passes. |
| `publish` | Checked | Publish after all six platform/backend packages pass basic checks. Uncheck to retain a draft with its assets for inspection. |
| `linux_baseline` | `bookworm-sdk` | Source SDK/glibc 2.36 for x86_64, or `ubuntu-22.04`/glibc 2.35. ARM64 always uses the Ubuntu 22.04 baseline. |
| `arm_runner` | `ubuntu-24.04-arm-l` | ARM64 host: organization L or H tier, or standard `ubuntu-24.04-arm`. See [runner selection](#runner-selection-and-build-parallelism) for all three architecture selectors. |
| `source_release` | Empty | Build normally when blank. Otherwise reuse that complete release's six archives to create a new APT experiment; application/SDK builds are skipped. |
| `package_check` | `none` | Read-only checks of a published `source_release`: `all`, `apt`, `arch` or `gentoo`. Creates no release and rebuilds no application or SDK. |

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

Prepare the Linux SDK and Windows dependencies once, or after changing the
corresponding recipe:

```sh
gh workflow run sdk-base.yml --ref main -f source=auto -f jobs=0 -f publish=true
gh workflow run sdk-base.yml --ref main -f platform=windows -f source=auto -f publish=true
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
application archive for the desired platform and GUI backend from the release page linked in the run summary.
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
Existing schema-1 releases retain their original three FLTK assets; schema-2
releases retain all six backend-specific assets and a checksummed `warning.log`.
Both remain readable. Schema-3 releases also require the signed APT assets
and Debian installation checks described below. New schema-4 releases add signed
Arch/Gentoo recipe hashes and require native recipe installation checks. Upload and certification check each archive's root
and shipped build information, so relabeling an FLTK package as Rev is rejected.
Certification of a new release requires coverage of every declared backend.

For the Ubuntu 22.04 x86_64 baseline, set `linux_baseline=ubuntu-22.04`.
Clear `experiment` for an ordinary release. `publish=false` still reserves a
tag and uploads to a draft; it is no longer an Actions-artifact rehearsal.
PRs changing automation run helper tests without creating releases or tags.

## Debian installation from GitHub Releases

The release assets form a flat APT repository: four `.deb` packages (FLTK and
Rev, each for `amd64` and `arm64`), `Packages`/`Packages.gz`, signed `InRelease`
and `Release.gpg`, and their support metadata. No package pool, generated index
or signing key is committed to Git. GitHub Pages and an additional server are
unnecessary. The packages wrap the existing portable payloads without compiling
the application or SDK again.

`datapump-fltk` and `datapump-rev` can be installed together. They keep their
complete private `bin/`, `lib/` and `share/` trees under `/opt/datapump/fltk/`
and `/opt/datapump/rev/`, with application-menu entries and these commands:

| Package | GUI | CLI |
| --- | --- | --- |
| `datapump-fltk` | `datapump-fltk` | `datapump-cli-fltk` |
| `datapump-rev` | `datapump-rev` | `datapump-cli-rev` |

The same `.deb` files target Debian 12 Bookworm and newer glibc-based Debian
systems, and Ubuntu 24.04 and newer. The installation matrix covers Bookworm,
Debian 13 Trixie, Ubuntu 24.04 and Ubuntu 26.04 on both `amd64` and `arm64`.
ARM64 additionally targets Ubuntu 22.04 because that archive's baseline is
glibc 2.35. The normal Bookworm-SDK AMD64 archive requires glibc 2.36, so its
package correctly refuses installation on Ubuntu 22.04 (glibc 2.35). An explicit
`linux_baseline=ubuntu-22.04` build can provide an AMD64 package with the lower
baseline; this is a new build choice, not a change to existing archive bytes.
Future distribution versions are expected to remain compatible while these
ABIs and dependency names remain available; they are not automatically certified.

The package manager supplies each distribution's own audio plugins, fonts and
graphics drivers. Dependencies use names shared by Debian and Ubuntu; the
host's ALSA plugins resolve the appropriate `libasound2`/`libasound2t64` package.
Ubuntu needs its normal `universe` component enabled for `libasound2-plugins`.
See the upstream [Ubuntu ALSA package](https://packages.ubuntu.com/noble/libasound2-plugins)
and [Ubuntu 22.04 glibc baseline](https://packages.ubuntu.com/jammy/libc6). A compatible
ChromeOS Linux container or VelvetOS installation can use the matching Debian
architecture; packaging does not establish physical Chromebook audio/graphics
compatibility or change Rev's OpenGL requirement.

Choose a published release with APT assets. The repository's signing key,
configured on 23 September 2026, has primary fingerprint
`8C3DD4A727C83B93374C993B1F94BC4CEC2DF307`. Bootstrap the public key and source
file from that exact tag, replacing `RELEASE_TAG` below. Check this fingerprint
against a trusted copy of the maintainer's documentation before initial setup.

The current package experiment is
[`v001_00-2026-09-23-1200CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-23-1200CDT),
including the Arch and Gentoo recipes below.
It reuses the six archives built from application commit `88fb87b`.
[Debian/Ubuntu installation checks](https://github.com/mirage335-colossus/pumpModem/actions/runs/35892588927)
passed in all nine environments above; these are separate from full
application certification. As of
23 September 2026, no regular release has qualified for Latest, so use its
explicit tag when opting into this experiment.

```sh
tag=RELEASE_TAG
fingerprint=8C3DD4A727C83B93374C993B1F94BC4CEC2DF307
base="https://github.com/mirage335-colossus/pumpModem/releases/download/$tag"
download_dir=$(mktemp -d)
curl --fail --location "$base/datapump-archive-keyring.gpg" -o "$download_dir/datapump.gpg"
actual=$(gpg --batch --show-keys --with-colons "$download_dir/datapump.gpg" |
  awk -F: '$1 == "fpr" {print $10; exit}')
test "$actual" = "$fingerprint" || { echo 'Signing fingerprint mismatch' >&2; exit 1; }
curl --fail --location "$base/datapump.sources" -o "$download_dir/datapump.sources"
sudo install -d -m 0755 /etc/apt/keyrings
sudo install -m 0644 "$download_dir/datapump.gpg" /etc/apt/keyrings/datapump.gpg
sudo install -m 0644 "$download_dir/datapump.sources" /etc/apt/sources.list.d/datapump.sources
rm -rf "$download_dir"
sudo apt-get update
sudo apt-get install datapump-fltk
# Optional second GUI, with its own CLI and private libraries:
sudo apt-get install datapump-rev
```

The bootstrap commands need `curl` and `gpg`. Keep the installed keyring for
future updates; key replacement is a separate maintainer operation. The source
file uses `Signed-By: /etc/apt/keyrings/datapump.gpg` and `Suites: ./`.
For a regular release its URI is
`https://github.com/mirage335-colossus/pumpModem/releases/latest/download/`, so
normal `apt-get update` and `apt-get upgrade` follow qualified releases.
For an **experiment**, the supplied source file stays pinned to that exact
release tag. Installing it is an explicit opt-in and does not switch to Latest.

Package indexes point to immutable versioned release URLs, including when the
index itself was fetched through Latest. Versions include the project version,
UTC build timestamp and workflow identity, so APT upgrades remain ordered
across the CDT/CST clock change. Publication alone never changes Latest: a
regular schema-4 release must pass full certification, including APT and native
Arch/Gentoo recipe checks. Older schemas remain readable and certifiable, but
cannot replace the current update channel without all of its delivery assets.

### Maintainer signing configuration

Configure a dedicated APT signing key before dispatching a release. Store the
ASCII-armored private key as the repository Actions secret
`DATAPUMP_APT_SIGNING_KEY`, and its full primary fingerprint as the repository
Actions variable `DATAPUMP_APT_SIGNING_FINGERPRINT`. The CI key must support
unattended signing without an interactive passphrase. Keep its backup outside
the checkout; only the exported public key belongs in release assets.

```sh
gh secret set DATAPUMP_APT_SIGNING_KEY --repo mirage335-colossus/pumpModem \
  < /secure/path/datapump-apt-private.asc
gh variable set DATAPUMP_APT_SIGNING_FINGERPRINT --repo mirage335-colossus/pumpModem \
  --body FULL_PRIMARY_FINGERPRINT
```

The workflow fails before application builds when signing configuration is
missing. Signing uses a temporary private key file and keyring, verifies the
configured fingerprint, and removes the temporary material afterward. Every
Debian payload is checked against its original portable archive; signed index
and package hashes join the release's final checksum inventory before upload
and publication. Existing release assets are never overwritten.

### Package an existing release without rebuilding it

The existing release entry point accepts a complete release, including a
finalized draft, and calls the [APT workflow](../.github/workflows/apt-release.yml)
to create a **new experiment**:

```sh
gh workflow run release.yml --ref REF -f source_release=SOURCE_RELEASE_TAG \
  -f publish=true -f linux_runner=ubuntu-latest-h -f arm_runner=ubuntu-24.04-arm-h
gh run watch RUN_ID --exit-status
```

The six original archive byte streams stay unchanged. New metadata records
their source revision, original tag/inventory hash and packaging-tool revision.
The new Git tag identifies the packaging-tool revision; `source_sha` identifies
the application revision whose binaries were reused. Certification builds the
recorded application revision and checks both identities.
The old release and its reports remain intact. `publish=false` leaves the new
release as a draft after signature/payload checks; public APT installation
cannot run against a draft. The default `publish=true` publishes the experiment
and then tests actual GitHub `apt-get update`, installation of both backends,
installed-file hashes and bounded CLI/GUI self-checks across the Debian/Ubuntu
matrix above. Native Arch and Gentoo recipe installation checks run separately. Its runner dropdowns contain the larger L/H tiers only.
When called through `release.yml`, H selections are preserved and other runner
selections use L; Windows and baseline selectors do not trigger builds.
This path always creates an experiment, regardless of the ordinary release's
`experiment` checkbox. An optional `version` overrides its display label;
otherwise the original label is retained. Once registered on the default
branch, `apt-release.yml` can also be dispatched directly with
`source_tag=SOURCE_RELEASE_TAG`. The existing `release.yml` entry point permits
testing this path from an unmerged branch without registering a new workflow.

For a package-manager setup fault, select just that manager against the existing
published tag. This keeps passing distribution checks and application binaries
out of the retry:

```sh
gh workflow run release.yml --ref REF -f source_release=RELEASE_TAG \
  -f package_check=gentoo -f linux_runner=ubuntu-latest-h
```

`package_check=apt` runs the Debian/Ubuntu matrix; `arch` and `gentoo` select one
native recipe check, and `all` checks every package format. The default `none`
retains normal build/repackage behavior. Package checks neither publish assets
nor grant certification or Latest status. If recipe bytes change, publish a new
experiment first; never overwrite an existing signed recipe asset to make a
retry pass.

During packaging development, run the affected helper tests first. After the
candidate is complete, run the full Debian/Ubuntu and native recipe installation coverage
without repeating unchanged application or SDK builds. These checks do not
certify the application: follow publication with `certify.yml`, `devfast=false`,
for the new tag. Certification includes the same APT coverage for schema-3 and newer releases,
plus Arch/Gentoo recipe checks for schema 4, and binds the report to all
published hashes.


## Arch Linux and Gentoo binary recipes

Schema-4 releases also carry `datapump-arch-recipes.tar.gz`,
`datapump-gentoo-overlay.tar.gz` and `distro-packages.json`. Generated recipes
remain release assets; no overlay or package repository is added to the Git
layout. Each backend has a separate `datapump-fltk-bin` or `datapump-rev-bin`
package. They fetch the exact versioned portable archive, verify its hashes,
and install the same private payload and commands as the Debian packages.
Neither recipe compiles the application or SDK, strips binaries, or substitutes
system libraries for bundled files. Both backends can coexist.

Arch recipes include `PKGBUILD` and `.SRCINFO` for `x86_64` and `aarch64`;
Arch Linux itself supports x86-64, while the latter recipe targets Arch Linux
ARM. Gentoo uses an EAPI-8 local overlay with `~amd64`/`~arm64` testing keywords,
architecture-specific archive manifests and host dependencies. These require
a glibc-based system; musl profiles and 32-bit ARM are outside these binaries'
ABI scope. Native frontend installation checks cover Arch/Gentoo x86-64;
the same ARM64 payloads receive the Debian/Ubuntu matrix checks above.

Recipe archives are bound by SHA-256 in `apt-repository.json`, which is covered
by the release's signed `InRelease`. Before executing either recipe, verify
the release assets using the same trusted public fingerprint as APT. The
verification commands need `curl`, `gpg`, `gpgv`, `sha256sum` and Python 3;
minimal Debian 13 systems may need the separate `gpgv` package installed:

```sh
tag=RELEASE_TAG
base="https://github.com/mirage335-colossus/pumpModem/releases/download/$tag"
mkdir "datapump-recipes-$tag" || exit 1
cd "datapump-recipes-$tag" || exit 1
for asset in datapump-archive-keyring.gpg InRelease apt-repository.json \
  datapump-arch-recipes.tar.gz datapump-gentoo-overlay.tar.gz distro-packages.json; do
  curl --fail --location "$base/$asset" -o "$asset" || exit 1
done
actual=$(gpg --batch --show-keys --with-colons datapump-archive-keyring.gpg |
  awk -F: '$1 == "fpr" {print $10; exit}')
test "$actual" = 8C3DD4A727C83B93374C993B1F94BC4CEC2DF307 || exit 1
gpgv --keyring "$PWD/datapump-archive-keyring.gpg" --output Release InRelease || exit 1
awk '/^SHA256:/{hashes=1;next} hashes && $3 == "apt-repository.json" {print $1 "  " $3}' \
  Release | sha256sum --check --strict || exit 1
python3 - <<'CHECK' || exit 1
import hashlib, json
from pathlib import Path
expected = json.loads(Path('apt-repository.json').read_text())['distribution_assets']
assert set(expected) == {'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz', 'distro-packages.json'}
for name, digest in expected.items():
    if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Checksum mismatch: {name}')
CHECK
```

On Arch, extract the verified recipe archive and run `makepkg` as an ordinary
user. Standard `base-devel` packaging tools are needed, but the recipe performs
no application compilation:

```sh
tar -xzf datapump-arch-recipes.tar.gz
cd datapump-arch-recipes/datapump-fltk-bin
makepkg -si
# For Rev, use the adjacent datapump-rev-bin directory.
```

On Gentoo, install the verified overlay in a version-specific directory and
register it locally. The main Gentoo repository must already be available:

```sh
tar -xzf datapump-gentoo-overlay.tar.gz
sudo mkdir -p "/var/db/repos/datapump-bin-$tag" /etc/portage/repos.conf
sudo cp -a datapump-gentoo-overlay/. "/var/db/repos/datapump-bin-$tag/"
printf '[datapump-bin]\nlocation = /var/db/repos/datapump-bin-%s\nmasters = gentoo\nauto-sync = no\n' "$tag" |
  sudo tee /etc/portage/repos.conf/datapump-bin.conf
sudo mkdir -p /etc/portage/package.accept_keywords /etc/portage/package.license
printf 'media-radio/datapump-fltk-bin\nmedia-radio/datapump-rev-bin\n' |
  sudo tee /etc/portage/package.accept_keywords/datapump-bin
printf 'media-radio/datapump-fltk-bin DataPump-Bundled\nmedia-radio/datapump-rev-bin DataPump-Bundled\n' |
  sudo tee /etc/portage/package.license/datapump-bin
sudo emerge --ask media-radio/datapump-fltk-bin
# Optional second backend:
sudo emerge --ask media-radio/datapump-rev-bin
```

`DataPump-Bundled` preserves the archive's component notices together; it does
not grant new rights or describe every bundled component as CC0. Read the
included license file before accepting it. Gentoo may build missing **host**
dependencies according to local Portage settings; CI requires binary host
packages and fails if they are unavailable, so it cannot silently start a long
source build. The disposable Gentoo CI container uses an official desktop
profile and the official x86-64-v3 binhost after checking runner CPU support;
the generic binhost currently lacks the required ALSA/PulseAudio bridge.
This choice applies only to CI's host dependencies and does not raise the
application archive's CPU baseline or alter users' Portage configuration.
Updating means verifying the new release's recipe archive and
repeating `makepkg -si`, or selecting its new overlay directory and running
`emerge --update`. These local recipes are not submitted to AUR or Gentoo's main
repository, and are not an automatically synchronized package feed.

## Rev display warnings

The release's `warning.log` records a known presentation limitation: Rev may
refresh its replay/waterfall display below the target cadence. This is advisory,
not a build or certification failure. It is a known-limit notice, not a claim
that publication measured the current build's framerate.

When an extensive GUI smoke observes a cadence miss, it emits
`WARNING REV_REPLAY_CADENCE:` with elapsed time, frame/change counts, displayed
fraction and phase. Portable verification preserves these warnings in CI logs,
and certification reports link the release's checksummed warning notice.
Missing physical observations, premature completed content, changed pending
identity, wrong source/bitmap data and other correctness checks remain failures.
Do not tune the framerate or repeat long suites just to eliminate this advisory.

Rev also requires a working desktop OpenGL driver (4.4, or 4.3 with
`GL_ARB_buffer_storage`). That is a runtime requirement, distinct from a cadence
warning. The FLTK bundle remains available for systems lacking that capability.

## Diagnose a branch before full validation

Native CI (`ci.yml`), SDK qualification (`sdk.yml`) and certification
(`certify.yml`) expose a manual boolean `devfast`, default **false**. Leave it
unchecked for the existing full suites, including calibration and compatibility
matrices. `devfast=true` is temporary diagnosis while resolving a bug or feature.
For a focused Legacy investigation, dispatch either entry point, replacing
`REF` with the branch or tag containing the candidate:

```sh
gh workflow run ci.yml --ref REF -f devfast=true
# Alternative entry point; release_tag may be omitted in this mode:
gh workflow run certify.yml --ref REF -f devfast=true
```

These commands run the same small Linux/Windows diagnostic against the selected
branch commit. They compile only its Legacy controller/session fixture with
available cores and repeat `gui_legacy_poll` and `gui_legacy_live` three times
serially, stopping on failure. They skip SDK/application builds, calibration, the general test matrix
and packaging, and use neither Actions artifacts nor cache storage. The source
SHA is printed in the run summary. Dispatch again after pushing a fix; a rerun
of an older run still uses that run's original revision.

Native CI additionally accepts `diagnostic=windows-rev` with `devfast=true`.
It runs the small Win32 event-pump regression, reuses the Windows base, compiles
the Rev GUI and runs a bounded headless self-check. The event regression uses a
small nonactivating window to exercise real paint messages. Use this
selection for Windows Rev compiler/event-loop diagnosis before another
build of all six packages. It does not certify graphics or publish release assets.

`devfast` never downloads a published application, issues a certification
report, changes a release or grants Latest status. Full certification continues
to bind the release's recorded source and published hashes, even when the
dispatch branch has newer code. Publish a new version/date release when a code
fix must be included in the binaries being certified.

See [local focused commands](building.md#focused-development-diagnostics).
For temporary investigation commits, `[skip ci]` can suppress automatic
push/PR matrices while manual dispatch remains available. Once the candidate is
complete, continue with full source CI and applicable SDK qualification on that
same revision, using `devfast=false` or unchecked:

```sh
gh workflow run ci.yml --ref REF -f devfast=false
# Also run SDK/toolchain and copied-bundle coverage when relevant to the change:
gh workflow run sdk.yml --ref REF -f devfast=false
gh run list --workflow ci.yml --limit 5
gh run list --workflow sdk.yml --limit 5
gh run watch RUN_ID --exit-status
```

Wait for the relevant full runs to complete successfully and address any
regressions they expose. **Do not finish bug or feature work with only a fast
pass.** Full manual dispatch after a `[skip ci]` commit provides this validation;
there is no need to launch duplicate push/PR runs solely to remove the marker.
Alternatively, omit the marker and use the normal full automatic runs. Record
the tested commit and run IDs in either case.

For a release task, follow successful source validation with publication and
full certification of the correct published source:

```sh
# When the fix needs new binaries, publish a new version/date release first:
gh workflow run release.yml --ref REF -f experiment=true -f publish=true
# After publication completes, use its actual new tag:
gh workflow run certify.yml --ref REF -f release_tag=RELEASE_TAG -f devfast=false
gh run list --workflow certify.yml --limit 5
gh run watch CERTIFICATION_RUN_ID --exit-status
```

Publication remains separate from certification. Existing binaries and prior
reports stay immutable; selecting a newer `REF` cannot apply a source fix to an
older release. A diagnostic pass does not qualify either release, and an
experimental release remains a prerelease and never Latest even after full
certification passes.

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
The separate native and SDK/Rev regression workflows retain small application
artifacts for one day for CI inspection and copied-binary checks on other hosts.
Those are not durable releases.

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
the release pipeline now defers to separate certification. Native CI and SDK
qualification also retain extensive regression coverage. A cold SDK build remains
an occasional maintenance task, not work repeated for each application release.
These timings are observed hosted-runner results, not guarantees.

## Runner selection and build parallelism

Manual portable release, native CI, SDK qualification, base maintenance and
certification workflows expose x86-64 Linux and Windows runner dropdowns.
Portable release, native CI and certification also expose `arm_runner` for
their ARM64 jobs and diagnostics. The same input names work through GitHub CLI:

| Input | Choices | Default |
| --- | --- | --- |
| `linux_runner` | `ubuntu-24.04`, `ubuntu-latest-m`, `ubuntu-latest-l`, `ubuntu-latest-h` | `ubuntu-24.04` |
| `arm_runner` | `ubuntu-24.04-arm-l`, `ubuntu-24.04-arm-h`, `ubuntu-24.04-arm` | `ubuntu-24.04-arm-l` |
| `windows_runner` | `windows-2022`, `windows-latest-l`, `windows-latest-h` | `windows-2022` |

The larger labels are the runners configured by `mirage335-colossus`. Select one
explicitly for x86-64 builds; automatic push/PR x86-64 jobs keep their standard
defaults. Manual ARM64 selection defaults to L and reaches reusable diagnostics,
release builds and certification checks. Small helper, metadata and report jobs
stay on standard runners. Each architecture has its own selector: an x86-64
label cannot replace an ARM64 host. Existing Linux baseline containers, SDK
recipes and portable ABI ceilings remain unchanged.

The organization created Ubuntu 24.04 ARM64 pools in its existing
larger-runner group, which permits public repositories. Both pools were
confirmed Ready on **2026-09-23**. Their configured capacities and GitHub's
published prices checked that day are:

| ARM64 label | CPUs | RAM | SSD | Maximum concurrent jobs | USD/minute per running job |
| --- | ---: | ---: | ---: | ---: | ---: |
| `ubuntu-24.04-arm-l` | 8 | 32 GB | 300 GB | 15 | $0.014 |
| `ubuntu-24.04-arm-h` | 32 | 128 GB | 1200 GB | 10 | $0.050 |

GitHub bills larger runners for job execution, including in public repositories,
rounding each job up to whole minutes; idle configured pools have no execution
charge. Choose H when its shorter build time justifies its higher per-minute
cost. See [runner specifications](https://docs.github.com/en/actions/reference/runners/larger-runners)
and [current prices](https://docs.github.com/en/billing/reference/actions-runner-pricing).
Both pools are ARM64, not 32-bit ARM.

Check access and actual resources with the short capacity diagnostic before
starting expensive work on a newly configured runner:

```sh
gh workflow run ci.yml --ref REF \
  -f devfast=true -f diagnostic=runner-capacity \
  -f linux_runner=ubuntu-latest-l -f windows_runner=windows-latest-l

gh workflow run ci.yml --ref REF \
  -f devfast=true -f diagnostic=arm-runner-capacity \
  -f arm_runner=ubuntu-24.04-arm-h

gh workflow run release.yml --ref REF \
  -f experiment=true -f publish=false -f linux_baseline=bookworm-sdk \
  -f linux_runner=ubuntu-latest-h -f arm_runner=ubuntu-24.04-arm-h \
  -f windows_runner=windows-latest-h
```

Use the intended source branch as `--ref`. The capacity diagnostic records the
selected label, actual runner, architecture, available CPUs and memory. It
compiles small existing production fixtures: Legacy on the Linux Ubuntu 22.04
baseline and Rev modules with MSVC on Windows. It does not build an SDK, create
release assets or claim certification. A queued job has not demonstrated runner
access; the organization must grant this repository access to the selected label.
The ARM-only diagnostic checks the selected ARM64 pool without scheduling x86-64
or Windows jobs. Validate the new L and H pools directly; do not repeat the
smaller standard-runner checks merely to compare capacity. Reuse existing
evidence and proceed to the applicable full checks on the selected larger hosts.

Compilation uses `nproc` on Linux and `NUMBER_OF_PROCESSORS` on Windows. The
build wrapper's `--build-jobs` lets SDK and certification builds use those cores
while test `--jobs` stays at two, or one for existing serial checks. Certifying
older immutable releases whose wrapper lacks this option retains their original
two-job behavior. Windows jobs enable
[MSBuild MultiToolTask with a process limit shared across projects](https://devblogs.microsoft.com/cppblog/cpp-build-throughput-investigation-and-tune-up/)
so source files within a project can compile concurrently without multiplying
the CPU limit for every project. Module dependencies and link steps still impose
serial work; larger runners do not guarantee a particular elapsed time.

The [six-package H-runner verification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35882420441)
passed on 2026-09-23 and retained an experiment draft. Windows FLTK/Rev whole
jobs took **2m15s/2m32s**, with existing base restore in **15s/20s**; the preceding
standard-runner publication took **5m43s/6m49s** for those jobs. ARM64 FLTK/Rev
took **3m30s/5m03s**. These are observed results, not a speed guarantee or full
certification. See the [validation record](validation.md#larger-runners-and-independent-compilation-concurrency--23-september-2026)
for source/run identities and the separate certification limitations.

Base reuse remains the first optimization. Cold SDK builds use available cores
when base maintenance's `jobs=0`; normal application builds continue retrieving
the exact existing base instead of rebuilding it. Larger runners do not change
`devfast=false` defaults, test assertions or release certification requirements.

## Windows dependency base

Select `platform=windows` in [Maintain base SDK](../.github/workflows/sdk-base.yml).
Its [Windows build workflow](../.github/workflows/windows-base.yml) preserves a relocatable, precompiled dependency bundle in the same `base`
release. Its [recipe](../third_party/build-support/windows-base.json) pins
vcpkg and the shared union of static OpenSSL, GLEW and FreeType dependencies
for both FLTK and Rev, including Debug and Release configurations. The runner
already supplies Visual Studio/MSVC and the Windows SDK. The cold step
compiles these third-party dependencies; it does not rebuild or redistribute
Microsoft's compiler or SDK.

The [runner toolchain selector](../tools/select-windows-toolchain.ps1) prefers
VS2022 when installed. The current larger Windows images supply VS2026; on
those images it selects the installed v143 14.44 tools and the VS2026 CMake
generator, which requires CMake 4.2 or newer. It does not silently adopt the
newer default toolset. The default `windows-2022` image continues using VS2022.
Run logs identify the selected generator, toolset and linker version.
This selection leaves the existing base recipe identity and assets unchanged;
its hosted compile results must still be checked before claiming qualification.

Ordinary Windows release, certification and full CI jobs download, verify and
relocate the exact recipe through [the helper](../tools/windows-base.py).
They fail with maintenance instructions if it is missing, rather than starting
an implicit vcpkg build. The dependency handoff uses release assets, with no
Actions cache or artifact storage. Application compilation and the selected
tests still run normally.

Certification checks out the published source and current workflow tooling into
separate directories. Its current runner-discovery wrapper invokes the released
source's dependency helper and recipe, preserving that release's base identity
while accommodating the selected runner image. Releases predating that helper
retain the current-tooling fallback. The application source, published archives
and report hash bindings remain those of the release under certification.

Maintenance uses `source=auto` to reuse the exact bundle or build it when
missing, `source=base` to require reuse, and `source=rebuild` for an explicit
cold build. `publish=true` preserves the bundle, matching source inputs,
checksums and build provenance; `publish=false` leaves durable storage alone.
Cold dependency compilation uses the available runner cores. Existing recipe
assets are immutable: an upgrade needs a new recipe identity, and rebuilding
an existing recipe does not authorize replacing its bytes.

The consuming MSVC compiler/linker must be the same version or newer than the
one recorded for the bundle, within the supported v143 toolset. Installation
checks the linker version. These static dependencies are built without LTO;
compiler-specific LTO objects would require tighter toolset matching. Reuse
removes repeated dependency compilation, but does not guarantee a particular
workflow duration or replace application validation.

Hosted consumers reduced dependency setup from about six minutes per backend
to **13 seconds for FLTK** and **25 seconds for Rev** in the corrected
six-package release. The complete Windows jobs took **5m43s** and **6m49s**,
respectively, including fresh application compilation and packaging checks. A
separate reuse-only maintenance run completed its Windows job in **44 seconds**,
including a fresh static compile/link/run probe, with dependency compilation
and upload skipped. See the [validation record](validation.md) for those exact
runs. Long regression/calibration work belongs to the separate certification
workflow and does not indicate that dependencies were rebuilt.

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
has six user bundles. Neither option covers every historical Ubuntu LTS.

The release inventory names the application downloads
`DataPump-TAG-linux-x86_64-BACKEND.tar.gz`,
`DataPump-TAG-linux-aarch64-BACKEND.tar.gz` and
`DataPump-TAG-windows-x86_64-BACKEND.zip`, where `TAG` is the shared version/build
label and `BACKEND` is `fltk` or `rev`. It also includes release notes, metadata,
`warning.log` and `SHA256SUMS.txt`. Compiled SDKs and their corresponding source archives are retained once per
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
environment and retain the glibc 2.35 ceiling. Native Rev builds use signed,
pinned LLVM 19 packages and a small pinned Ninja bootstrap for C++ modules;
application libraries still come from that Ubuntu baseline. SDK Rev builds use
the existing SDK directly. Windows uses the runner's installed MSVC v143 tools
under Visual Studio 2022 or 2026 and its Windows SDK, static C/C++ runtimes and
the verified
[Windows dependency base](#windows-dependency-base). Rev additionally links its
static GLEW and FreeType dependencies from that same bundle.
Linux portable Clang builds using libstdc++ link their C++ runtime statically,
as GNU builds do, and keep its archive symbols private. This prevents an older
bundled C++ runtime from blocking newer host graphics drivers. The bundle still
retains the static runtime's license notices and the same glibc ceiling.
Generic CPU targets avoid requiring the particular build runner's instruction
set extensions. GitHub documents the available
[native ARM64 and Windows runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## Compatibility checks and scope

The separate certification workflow runs the full build, contract, GUI and
packaging selections on Linux, and the contract, GUI and packaging selections
on Windows, for each published GUI backend. It also tests both backends' published
archives on the hosts below.
Read the attached report before describing a particular release as tested.

| Bundle | Build baseline | Copied-archive compatibility jobs |
| --- | --- | --- |
| Linux x86_64, `bookworm-sdk` | SDK, glibc 2.36 | Debian 12 and 13; Ubuntu 24.04 and 26.04; Arch Linux |
| Linux x86_64, `ubuntu-22.04` | Ubuntu 22.04, glibc 2.35 | Debian 12 and 13; Ubuntu 22.04, 24.04 and 26.04; Arch Linux |
| Linux aarch64 | Ubuntu 22.04, glibc 2.35 | Debian 12 and 13; Ubuntu 22.04, 24.04 and 26.04 |
| Windows x64 | MSVC v143 with static CRT | Selected Windows x64 hosted runner and archive relocation; the default is `windows-2022` with VS2022 |

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
| Gentoo | Matching x86_64/aarch64 bundle or binary ebuild on a compatible glibc installation with X11/XWayland. Native recipe checks cover x86_64; musl configurations are outside this release format. |
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

For the default VS2022 toolchain, Microsoft documents the ability to build
desktop applications
for [Windows 10 and 11](https://learn.microsoft.com/en-us/visualstudio/releases/2022/compatibility?view=vs-2022).
The default `windows-2022` runner uses Windows Server 2022. Organization runner
labels may select another image; consult that run's image/toolchain logs.
Windows 10 and Windows 11 client installations are not directly tested by this
workflow.

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
runners. Rev display cadence is measured and reported as a warning; pending
progress, content correctness and cancellation checks remain mandatory.
Certification tests the selected published format on each compatibility host;
publication still verifies both generated package formats.

The Windows live-profile capture fixture requests 1 ms timer resolution for
its existing 1 ms sleeps and restores it on exit, following Microsoft's
[timer API contract](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).
Coarse timer rounding could otherwise leave fixture audio undelivered after
30 seconds even with an empty decoder queue. The same audio chunks, 30-second
deadline and all receiver assertions remain in place. Windows runs the full
calibration after the other selected tests to report platform failures sooner.
