# Portable releases

For the routine release entry point, follow [RELEASE](../RELEASE):
**Actions → _Publish new Latest release → Run workflow**.

The feature-complete application version is **001_00**. The CLI, GUI and build
information use that display version, and an ordinary release dispatch defaults
to the label `v001_00`. CMake, portable archive roots and distribution packages
use its numeric equivalent `1.0.0`; the Windows manifest uses `1.0.0.0`.
Version identity does not change the publication and certification requirements
below. See the [requirements matrix](requirements.md) for the implemented scope
and the remaining automatic HF frequency-tuning work.

The [release workflow](../.github/workflows/release.yml) builds and publishes
six portable application bundles: separate **FLTK** and **Rev** builds for Linux
x86_64, Linux aarch64 and Windows x64. Each new bundle contains its selected GUI,
the CLI, `datapump-tui` and `datapump-fb` (`.exe` on Windows),
the self-contained HTML/Wasm page under `share/datapump/web/wasm/`,
required application libraries and notices. Backend names appear in both the
download filename and extracted directory, so the two installations can coexist.
Unpack the whole archive and keep `bin/`, `lib/` and `share/` together. Nothing needs to
be copied into the system `/lib` directory. Linux uses `.tar.gz`; Windows uses
`.zip`. Compatible distributions share the same binary. Linux bundles also include
`datapump-worker` and its inherited-pipe browser adapters; Windows uses the
standalone browser page for HTML operation.

Publication and extensive testing are separate operations. Publication checks
build success, both package formats, checksums, relocation, CLI/self-check
operation, dependency closure and the Linux ABI ceiling. It then publishes the
six selected downloads, copies of the reusable build dependencies and a signed
flat APT repository with **certification pending**. The separate
[certification workflow](../.github/workflows/certify.yml) later tests the exact
published downloads and attaches immutable reports to that same release. The
long preservation contract and GUI tests remain intact in that workflow.

## Publish a new Latest release

The [_Publish new Latest release workflow](../.github/workflows/_release-latest.yml)
coordinates the existing release stages in one manual dispatch. Its filename,
workflow name and run title start with an underscore for easy identification
in the pinned workflow list. Push the complete candidate before dispatching.
The workflow must be registered on the default branch; select the intended
branch in the Run workflow form. Configure the
[release signing secret and variable](#maintainer-signing-configuration) first.

[Local reusable workflow calls](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows)
use the same commit as the parent dispatch, so later branch changes cannot mix
source revisions between stages. The sequence is:

1. Check signing configuration and the release label; optionally maintain the
   selected Linux/Windows/Wasm dependency bases with `sdk-base.yml`.
2. Run full native regression (`ci.yml`) and SDK qualification (`sdk.yml`) in
   parallel, both with `devfast=false`.
3. Publish new ordinary binaries and packages with `release.yml`, using
   `experiment=false` and `publish=true`.
4. Pass that workflow's exact release tag to `certify.yml`, with `devfast=false`,
   to certify the published source and binary hashes and promote the release.
5. Read back GitHub Latest and verify its tag, release ID, source SHA, inventory
   SHA-256 and successful certification report for this run. A missing or
   unsuccessful required stage cannot produce a successful final verification.

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | Empty | Use the application version plus a timestamp, or provide an explicit release label. |
| `linux_baseline` | `bookworm-sdk` | Linux x86_64 baseline; `ubuntu-22.04` is also available. ARM64 keeps its Ubuntu 22.04 baseline. |
| `linux_runner` | `ubuntu-24.04` | Standard Linux x86_64 host; explicitly select an organization M/L/H pool when desired. |
| `arm_runner` | `ubuntu-24.04-arm` | Standard ARM64 host; explicitly select an organization L/H pool when desired. |
| `windows_runner` | `windows-2022` | Standard Windows x64 host; explicitly select an organization L/H pool when desired. |
| `maintain_base` | `none` | Explicitly maintain `linux`, `windows`, `wasm`, `both` (Linux/Windows), or `all` before qualification. `source=auto` reuses matching recipes and builds missing ones; existing assets are never overwritten. |
| `sanitizer_smoke` | Unchecked | Add the optional instrumented native desktop smoke. |
| `sanitizer_realtime` | Unchecked | Add timing-sensitive Fast RX cases to instrumented native CI. |

Runner selections are passed through to every applicable stage. Standard
GitHub-hosted runners are free for public repositories; private repositories
have account-specific allowances and billing. Larger runners are billed even
for public repositories ([GitHub billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions)).
See [runner selection](#runner-selection-and-build-parallelism)
for the pool labels, costs and concurrency policy. Standard hosts are the default;
heavy pools require explicit selection. Runner choice changes neither the full
qualification requirements nor the documented warning policies.

Leave `maintain_base=none` when the matching dependency recipes already exist.
Ordinary qualification and publication require those prepared recipes and fail
if they are missing; they do not start an implicit cold dependency build.
Selecting maintenance explicitly authorizes the existing `source=auto` base
workflow to prepare missing recipes before the other stages.

The previous Latest remains selected while the complete candidate is built,
published and certified. After all required delivery assets and successful
certification reports are attached, the existing certification workflow uses
one `gh release edit --latest=true --prerelease=false` update to pivot Latest.
It does not move tags, delete prior releases or copy replacement assets into
the old release. This is an atomic release metadata update, not a transaction
across multiple client download requests; package indexes point to immutable
versioned asset URLs. The final read-only check proves the expected release is
Latest when verification completes.

Runs through this entry point are serialized across branches without cancelling
an active release. Independently dispatched certification or manual promotions
can still change Latest; avoid them while this workflow runs. A competing
promotion observed by the final verifier fails the run instead of claiming
that the candidate is Latest. No workflow can prevent a later authorized
promotion after it finishes.

Review the linked certification report and any warnings in the successful run
summary, then record the tag, run ID and coverage limits in
[the validation record](validation.md). The standalone workflows below remain
available for diagnosis, experiments and resuming individual stages.

## Standalone publication dispatch inputs

| Input | Default | Meaning |
| --- | --- | --- |
| `version` | Empty | Use the application display version as `v001_00`, or supply an explicit release label. |
| `experiment` | Checked | Title exactly `experiment`; GitHub prerelease; never Latest, even after certification passes. |
| `publish` | Checked | Publish after all six platform/backend packages pass basic checks. Uncheck to retain a draft with its assets for inspection. |
| `linux_baseline` | `bookworm-sdk` | Source SDK/glibc 2.36 for x86_64, or `ubuntu-22.04`/glibc 2.35. ARM64 always uses the Ubuntu 22.04 baseline. |
| `arm_runner` | `ubuntu-24.04-arm` | ARM64 host: standard by default, or an explicitly selected organization L or H tier. See [runner selection](#runner-selection-and-build-parallelism) for all three architecture selectors. |
| `source_release` | Empty | Build normally when blank. Otherwise reuse that complete release's six archives to create a new APT experiment; application/SDK builds are skipped. |
| `package_check` | `none` | Read-only checks of a published `source_release`: `all`, `apt`, `arch` or `gentoo`. Creates no release and rebuilds no application or SDK. |

Leave `experiment` checked for builds users needing assurance should avoid.
An ordinary release uses its version/date tag as the title. It becomes Latest
only after successful separate hosted certification; documented runner capability
warnings do not disqualify it. Publication alone does not promote it. Hosted certification is limited to the tests listed below and
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

Publish and certify an ordinary Latest release with the default standard runners:

```sh
gh workflow run _release-latest.yml --ref main
gh run list --workflow _release-latest.yml --limit 5
gh run watch RUN_ID --exit-status
```

For heavy runners, select all three architecture hosts explicitly:

```sh
gh workflow run _release-latest.yml --ref main \
  -f linux_runner=ubuntu-latest-h -f arm_runner=ubuntu-24.04-arm-h \
  -f windows_runner=windows-latest-h
```

Add `-f maintain_base=both` only when explicit dependency base maintenance is
needed, or choose `linux` or `windows`. Leave `version` blank for the default
application version/timestamp label. The run summary supplies the new tag and
certification report; there is no manual tag handoff between stages.

The following standalone commands are alternatives for individual stages.

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
After a certification-harness correction, finish the older run before dispatching
full certification again for the same tag. For the final read-only check,
`tools/verify-latest-release.py` accepts `--certification-run-id` for that later
report; `--run-id` still identifies the original publication. The certification
ID defaults to the publication ID for the combined release workflow.
For schema-6 releases, source certification retrieves the retained dependencies
from that same release and verifies them against the pinned inventory. It does
not need the original `base` release. Older schemas retain the existing exact
recipe lookup in `base`; missing or corrupt schema-6 assets fail instead of
falling back to another release.

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
and Debian installation checks described below. Schema-4 releases add signed
Arch/Gentoo recipe hashes and native recipe installation checks. Schema-5
releases additionally require signed pacman repositories and verified Gentoo
update channels. New schema-6 releases also require the compiled Windows
dependency bundle, its complete sources and per-recipe checksum file. Releases
using `bookworm-sdk` require the corresponding Linux SDK triplet too. The final
`SHA256SUMS.txt` binds every retained dependency asset to the release. Older
metadata remains readable; this policy does not add assets to older releases.
New releases also declare `frontends: [tui, framebuffer]`. This requires both
binaries, their manuals and matching enabled build provenance in every archive.
Repackaging preserves this declaration and the original bytes. Older releases
without it retain their original checks and explicitly report that terminal and
framebuffer coverage is not claimed.
The optional schema-6 `web` inventory declares the Wasm browser and Linux worker
platforms. New publication enables it and requires the exact `wasm-sdk` dependency
triplet, HTML inventory, notices, full source commit and matching SDK recipe.
Native producers and the browser producer run in parallel; finalization verifies
both browser archive formats and merges the same payload into each native bundle,
regenerating its complete package manifest before signed package conversion.
Temporary native/browser producer outputs use Actions artifacts. Only completed
combined archives are uploaded to the reserved draft, once; existing assets are
never overwritten. Certification adds independent native-worker and Wasm source
jobs and a required `web-tests` aggregate. Real browser/device microphone,
playback and download behavior remains outside simulated-device qualification.
Upload and certification check each archive's root
and shipped build information, so relabeling an FLTK package as Rev is rejected.
Full qualification of a new release requires coverage of every declared backend.
The scoped Windows Rev WGL warning described below permits a green workflow with
explicitly incomplete graphics coverage. That known runner limitation does not
block certification or Latest eligibility for an otherwise qualified ordinary release.
The same policy applies to an exact typed GUI smoke workload limit with recent
sampled-transmission progress. Certification records it as `passed_with_warnings`,
retains `incomplete_smoke_coverage` by target and scope, and appends immutable
`certification-RUN_ID-attempt-ATTEMPT-smoke-warnings.json` and `.log` assets. These
are bound by SHA-256 to the main report and identify the exact source and binary
inventory. Incomplete smoke is never reported as a smoke pass. Stalls, crashes,
sanitizer errors, invalid evidence and external timeouts still fail. Experiment
releases remain prereleases and never replace Latest.

For the Ubuntu 22.04 x86_64 baseline, set `linux_baseline=ubuntu-22.04`.
Clear `experiment` for an ordinary release. `publish=false` still reserves a
tag and uploads to a draft; it is no longer an Actions-artifact rehearsal.
PRs changing automation run helper tests without creating releases or tags.

## Debian installation from GitHub Releases

New builds include UNIX manual pages in every portable archive. Distribution
packages install matching pages for `datapump-cli-fltk`,
`datapump-cli-fltk-fast` and `datapump-fltk` (or `rev`), including rewritten
examples and cross-references. Debian, pacman and Arch/Gentoo recipes retain
the same private portable documentation. See [manual pages](offline-installation.md#manual-pages)
for reading commands. Historical releases retain their original contents when
repackaged; adding manuals requires a new source build.

The release assets form a flat APT repository: four `.deb` packages (FLTK and
Rev, each for `amd64` and `arm64`), `Packages`/`Packages.gz`, signed `InRelease`
and `Release.gpg`, and their support metadata. No package pool, generated index
or signing key is committed to Git. GitHub Pages and an additional server are
unnecessary. The packages wrap the existing portable payloads without compiling
the application or SDK again.

`datapump-fltk` and `datapump-rev` can be installed together. They keep their
complete private `bin/`, `lib/` and `share/` trees under `/opt/datapump/fltk/`
and `/opt/datapump/rev/`, with application-menu entries and these commands:

| Package | GUI | CLI | TUI | Framebuffer |
| --- | --- | --- | --- | --- |
| `datapump-fltk` | `datapump-fltk` | `datapump-cli-fltk` | `datapump-tui-ncurses` | `datapump-fb-sdl` |
| `datapump-rev` | `datapump-rev` | `datapump-cli-rev` | `datapump-tui-ncurses-rev` | `datapump-fb-sdl-rev` |

The terminal and framebuffer commands are included in newly built releases.
Arch and Gentoo delivery uses the same private binaries, wrappers and manuals.
The TUI requires a terminal; the framebuffer application uses SDL2 to present
the independent software renderer. Neither requires the native GUI toolkit to
open its interface.
Native Linux producers prepare the pinned minimal SDL host explicitly, retaining
its source archive, license and build provenance in each package. This avoids
unused distribution SDL audio dependencies without relaxing runtime-closure or
static-compression checks. The Linux SDL2 software window uses X11/XWayland;
Linux SDK and Windows dependency recipes still come from the prepared bases.

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

The current ordinary Latest release is
[`v001_00-2026-09-30-0648CDT`](https://github.com/mirage335-colossus/pumpModem/releases/tag/v001_00-2026-09-30-0648CDT),
including signed Debian/Ubuntu, Arch and Gentoo update channels. Its six bundles
were built from application commit `dce9138` and passed full hosted certification
on standard GitHub runners. See the [validation record](validation.md) for its
exact binary hashes and documented Windows Rev graphics/physical-device limits.
The superseded 26 September package experiment and other old releases were
removed at the maintainer's request; their original reports and source tags are
retained in the [cleanup evidence](release-history/2026-09-30/README.md).

```sh
tag=RELEASE_TAG
fingerprint=8C3DD4A727C83B93374C993B1F94BC4CEC2DF307
base="https://github.com/mirage335-colossus/pumpModem/releases/download/$tag"
download_dir=$(mktemp -d)
curl --fail --location "$base/datapump-archive-keyring.gpg" -o "$download_dir/datapump.gpg" || exit 1
actual=$(gpg --batch --show-keys --with-colons "$download_dir/datapump.gpg" |
  awk -F: '$1 == "pub" {primary=1; next} primary && $1 == "fpr" {print $10; primary=0}')
test "$actual" = "$fingerprint" || { echo 'Signing fingerprint mismatch' >&2; exit 1; }
for asset in InRelease apt-repository.json datapump.sources; do
  curl --fail --location "$base/$asset" -o "$download_dir/$asset" || exit 1
done
gpgv --keyring "$download_dir/datapump.gpg" --output "$download_dir/Release" \
  "$download_dir/InRelease" || exit 1
python3 - "$download_dir" "$tag" <<'VERIFY' || exit 1
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1])
release_hashes = root.joinpath('Release').read_text().split('SHA256:\n', 1)[1].splitlines()
expected = [line.split()[0] for line in release_hashes
            if len(line.split()) == 3 and line.split()[2] == 'apt-repository.json']
manifest = root.joinpath('apt-repository.json').read_bytes()
if expected != [hashlib.sha256(manifest).hexdigest()]:
    raise SystemExit('Repository manifest checksum mismatch')
metadata = json.loads(manifest)
if metadata['repository'] != 'mirage335-colossus/pumpModem' or metadata['tag'] != sys.argv[2]:
    raise SystemExit('Unexpected repository or release tag')
if hashlib.sha256(root.joinpath('datapump.sources').read_bytes()).hexdigest() != metadata['sources_sha256']:
    raise SystemExit('APT source configuration checksum mismatch')
VERIFY
sudo install -d -m 0755 /etc/apt/keyrings
sudo install -m 0644 "$download_dir/datapump.gpg" /etc/apt/keyrings/datapump.gpg
sudo install -m 0644 "$download_dir/datapump.sources" /etc/apt/sources.list.d/datapump.sources
rm -rf "$download_dir"
sudo apt-get update
sudo apt-get install datapump-fltk
# Optional second GUI, with its own CLI and private libraries:
sudo apt-get install datapump-rev
```

The bootstrap commands need `curl`, `gpg`, `gpgv` and Python 3. They authenticate
the source configuration before installing it. Keep the installed keyring for
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
regular schema-5 release must pass full certification, including APT and native
Arch/Gentoo repository checks. Older schemas remain readable and certifiable, but
cannot replace the current update channel without all of its delivery assets.

### Maintainer signing configuration

Configure a dedicated release signing key before dispatching a release. The
existing APT secret also signs pacman packages/databases and the Gentoo channel
manifest; no additional secret is needed. Store the
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
The new release also copies the original release's retained SDK/dependency
triplets byte for byte. For older releases without complete retained copies,
repackaging resolves the recipe at the original application source revision and
retrieves that exact recipe from `base`. It fails when the original recipe
cannot be established or its assets are missing; it never substitutes the
packager's current recipe or rebuilds dependencies. A retained copy remains
available for later repackaging or bootstrapping after earlier releases are
deleted.
The old release and its reports remain intact. `publish=false` leaves the new
release as a draft after signature/payload checks; public APT installation
cannot run against a draft. The default `publish=true` publishes the experiment
and then tests actual GitHub `apt-get update`, installation of both backends,
installed-file hashes and bounded CLI/GUI self-checks across the Debian/Ubuntu
matrix above. Native Arch and Gentoo recipe installation checks run separately.
The runner dropdowns include standard and larger hosts. Calls through
`release.yml` preserve the selected Linux and ARM64 runners; Windows and
baseline selectors do not trigger builds.
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
plus Arch/Gentoo delivery checks for schema 4 and newer, and binds the report to all
published hashes.


## Arch and Gentoo update channels

Schema-5 releases add signed native pacman packages/repository databases and a
verified Gentoo overlay sync helper. Generated package trees and indexes live
in GitHub Release assets; the Git repository layout stays unchanged. Separate
FLTK and Rev packages can coexist. Installation preserves the portable archive's
private `bin/`, `lib/`, notices and shared files under `/opt/datapump/BACKEND`.
No application or SDK compilation is needed.

Bootstrap checks require exactly one primary key with the pinned fingerprint;
additional primary keys are rejected before trusting downloaded signatures.
Both channels use the same pinned signing fingerprint as APT:
`8C3DD4A727C83B93374C993B1F94BC4CEC2DF307`. Keep the trusted key installed;
automatic updates never replace that trust anchor. A regular release must pass
full certification before becoming Latest. An experiment requires an explicit
tag and never enters the Latest channel. Until a qualified regular schema-5
release exists, use a published experiment's exact tag for evaluation.

### Arch: normal pacman updates

Packages are provided for `x86_64` and `aarch64` (Arch Linux ARM). Set `base` to
Latest for normal updates, or to `.../releases/download/RELEASE_TAG` to stay on
one experiment. Bootstrap with the independently pinned public fingerprint:

```sh
base=https://github.com/mirage335-colossus/pumpModem/releases/latest/download
# For an experiment instead:
# base=https://github.com/mirage335-colossus/pumpModem/releases/download/RELEASE_TAG
curl --fail --location "$base/datapump-pacman-keyring.gpg" -o datapump-pacman-keyring.gpg || exit 1
actual=$(gpg --batch --show-keys --with-colons datapump-pacman-keyring.gpg |
  awk -F: '$1 == "pub" {primary=1; next} primary && $1 == "fpr" {print $10; primary=0}')
test "$actual" = 8C3DD4A727C83B93374C993B1F94BC4CEC2DF307 || exit 1
sudo pacman-key --init
sudo pacman-key --add datapump-pacman-keyring.gpg
sudo pacman-key --lsign-key 8C3DD4A727C83B93374C993B1F94BC4CEC2DF307
arch=$(uname -m)
case "$arch" in x86_64|aarch64) ;; *) exit 1 ;; esac
printf '[datapump-%s]\nSigLevel = PackageRequired DatabaseRequired\nServer = %s\n' "$arch" "$base" |
  sudo tee /etc/pacman.d/datapump.conf
```

Add `Include = /etc/pacman.d/datapump.conf` once at the end of
`/etc/pacman.conf`, then install either or both backends:

```sh
sudo pacman -Syu datapump-fltk-bin datapump-rev-bin
# Subsequent updates use the normal command:
sudo pacman -Syu
```

Both database and package signatures are required. Versioned package names
include the UTC build timestamp and workflow identity. Pacman requires package
basenames in its database, so it cannot use APT's relative path to immutable
release URLs. If Latest moves between fetching an index and a package, the
operation fails safely; retry **the full `pacman -Syu`** to refresh both. Do not
work around this with disabled signatures or a partial system upgrade.
Exact-tag repositories remain available for reproducible installation.

### Gentoo: verified overlay sync

Gentoo uses the release's EAPI-8 binary ebuild overlay. Install Python 3, GnuPG
(`gpg` and `gpgv`), curl and Portage first; the main Gentoo repository must be
available. The bootstrap below authenticates the updater and Portage adapter
before either is executed. Downloads after manifest verification use its exact
tag, even if Latest changes while the commands run.

```sh
base=https://github.com/mirage335-colossus/pumpModem/releases/latest/download
# For an experiment instead:
# base=https://github.com/mirage335-colossus/pumpModem/releases/download/RELEASE_TAG
mkdir datapump-gentoo-bootstrap || exit 1
cd datapump-gentoo-bootstrap || exit 1
for asset in datapump-archive-keyring.gpg datapump-gentoo-channel.json datapump-gentoo-channel.json.asc; do
  curl --fail --location "$base/$asset" -o "$asset" || exit 1
done
fingerprint=8C3DD4A727C83B93374C993B1F94BC4CEC2DF307
actual=$(gpg --batch --show-keys --with-colons datapump-archive-keyring.gpg |
  awk -F: '$1 == "pub" {primary=1; next} primary && $1 == "fpr" {print $10; primary=0}')
test "$actual" = "$fingerprint" || exit 1
gpgv --keyring "$PWD/datapump-archive-keyring.gpg" \
  datapump-gentoo-channel.json.asc datapump-gentoo-channel.json || exit 1
python3 - <<'VERIFY' || exit 1
import hashlib, json, re, urllib.request
from pathlib import Path
channel = json.loads(Path('datapump-gentoo-channel.json').read_text())
if channel['repository'] != 'mirage335-colossus/pumpModem':
    raise SystemExit('Unexpected repository')
tag = channel['tag']
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,100}', tag) or '..' in tag:
    raise SystemExit('Unsafe release tag')
base = f'https://github.com/mirage335-colossus/pumpModem/releases/download/{tag}'
for name in ('datapump-gentoo-sync.py', 'datapump-gentoo-portage-sync.py', 'datapump-gentoo-overlay.tar.gz'):
    data = urllib.request.urlopen(f'{base}/{name}', timeout=60).read()
    expected = channel['assets'][name]
    if len(data) != expected['size'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise SystemExit(f'Checksum mismatch: {name}')
    Path(name).write_bytes(data)
VERIFY
sudo install -Dm644 datapump-archive-keyring.gpg /etc/portage/datapump-release-keyring.gpg
sudo python3 datapump-gentoo-sync.py install --assets "$PWD" \
  --keyring /etc/portage/datapump-release-keyring.gpg --fingerprint "$fingerprint" \
  --repository mirage335-colossus/pumpModem
# For an experiment, add --tag RELEASE_TAG to that install command.
```

The public key uses its own keyring file outside `/etc/portage/gnupg`, which
Portage manages for official binary-package trust. The installer registers
`datapump-bin` with automatic sync enabled. The helper
requires a valid signature and matching overlay hash, rejects experiments on
Latest and older releases, and stages the entire overlay before replacing it.
A failed refresh preserves the previous overlay. The signed ebuild Manifest
then pins downloads to the immutable portable application archives.

Accept the two testing keywords and preserved component licenses after reading
`/var/db/repos/datapump-bin/licenses/DataPump-Bundled`, then install your selected backend:

```sh
sudo mkdir -p /etc/portage/package.accept_keywords /etc/portage/package.license
printf 'media-radio/datapump-fltk-bin\nmedia-radio/datapump-rev-bin\n' |
  sudo tee /etc/portage/package.accept_keywords/datapump-bin
printf 'media-radio/datapump-fltk-bin DataPump-Bundled\nmedia-radio/datapump-rev-bin DataPump-Bundled\n' |
  sudo tee /etc/portage/package.license/datapump-bin
sudo emerge --ask media-radio/datapump-fltk-bin media-radio/datapump-rev-bin
# Normal subsequent updates:
sudo emerge --sync
sudo emerge --ask --update --deep --newuse @world
# To refresh only this overlay:
sudo emaint sync -r datapump-bin
```

The helper does not update its own executable or replace its trusted key.
Repeat the verified bootstrap to upgrade the helper, or after a Portage/Python
upgrade relocates Portage's sync plugin directory. Gentoo may compile missing
**host dependencies** according to your Portage settings; these binary ebuilds
never compile DataPump or its SDK. CI uses only signed binary host dependencies.
Native Arch/Gentoo installation checks cover x86-64; helper/recipe fixtures
cover both architectures, and ARM64 application payloads also receive the
Debian/Ubuntu installation matrix. These binaries require glibc; musl and
32-bit ARM are outside their ABI scope.

### Pinned recipes for older releases

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
  awk -F: '$1 == "pub" {primary=1; next} primary && $1 == "fpr" {print $10; primary=0}')
test "$actual" = 8C3DD4A727C83B93374C993B1F94BC4CEC2DF307 || exit 1
gpgv --keyring "$PWD/datapump-archive-keyring.gpg" --output Release InRelease || exit 1
awk '/^SHA256:/{hashes=1;next} hashes && $3 == "apt-repository.json" {print $1 "  " $3}' \
  Release | sha256sum --check --strict || exit 1
python3 - <<'CHECK' || exit 1
import hashlib, json
from pathlib import Path
expected = json.loads(Path('apt-repository.json').read_text())['distribution_assets']
names = {'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz', 'distro-packages.json'}
assert names <= set(expected)
for name in names:
    digest = expected[name]
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
CI also uses Portage's `parallel-install` with the runner's available cores,
retains dependency ordering/merge locks, and disables per-package power-loss
disk syncs only inside this discarded container.
For these older/manual recipes, updating means verifying the new release's recipe archive and
repeating `makepkg -si`, or selecting its new overlay directory and running
`emerge --update`. These local recipes are not submitted to AUR or Gentoo's main
repository. Use the schema-5 channels above for automatic repository updates.

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

Windows certification treats only the exact coordinate-probe error
`[NativeWindow] Required WGL ARB extensions not available` with exit code 1 as a
hosted-environment warning. Compilation and all headless GUI/CLI, modem,
clipboard, package, relocation and calibration checks remain mandatory.
Only `gui_workflow`, `gui_adapter_conformance`, `gui_coordinates_1x`,
`gui_coordinates_2x` and the published Rev GUI smoke are omitted on that runner.
Unexpected output, different failures, crashes and timeouts still fail.

If all remaining checks pass, GitHub displays a green check and the report says
`passed_with_warnings`. A prominent summary, release notes and immutable
`certification-RUN-attempt-N-warning.log` record the missing coverage. Existing
release `warning.log` and earlier reports remain untouched. Such a result
certifies the release within the documented hosted-runner scope and permits an
otherwise qualified regular release to become Latest. It does not claim that
omitted Windows Rev graphics tests passed. Experiments remain prereleases.
The runner limitation is minor for build delivery, but a user machine with the
same limitation cannot launch the Rev GUI; that is a functional limitation,
with FLTK available as the alternative frontend.

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

The Legacy and certification `devfast` routes test source without downloading a
published application. The graphics diagnostic instead checks an existing
published archive. Neither route issues a certification report, changes a
release or grants Latest status. Full certification continues
to bind the release's recorded source and published hashes, even when the
dispatch branch has newer code. Publish a new version/date release when a code
fix must be included in the binaries being certified.

See [local focused commands](building.md#focused-development-diagnostics).
Automatic CI runs lightweight Legacy and helper checks on PRs and pushes to
`main`; SDK qualification and full native matrices are manual. A PR branch push
does not duplicate the PR run. For temporary investigation commits, `[skip ci]`
can suppress the automatic checks while manual dispatch remains available. Once the candidate is
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
Automatic green checks do not replace that full manual validation. Record
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

## Browser packages

Every new release bundle carries `share/datapump/web/wasm/datapump-wasm.html`.
Open that page in a compatible browser on Windows or Linux. Debian/APT, pacman,
Arch recipes and Gentoo expose `datapump-html` for FLTK-owned files and
`datapump-html-rev` for Rev-owned files, plus separate application-menu entries.
These launchers use `xdg-open` and the packages depend on `xdg-utils`; they install
no HTTP service. Browser audio still needs a supported secure context and ordinary
microphone permission. HTTPS static hosting is also supported. Actual browser and
physical audio-device qualification is separate from the automated tests.

Explicit Wasm base maintenance uses `sdk-base.yml` with `platform=wasm`; `all`
maintains Linux, Windows and Wasm recipes. `both` retains its historical meaning
of Linux plus Windows. Routine workflows fail if the exact Wasm base is missing;
select maintenance explicitly before the first web-enabled release. Preparation,
archive checks and installation/relocation commands are in the
[Wasm SDK guide](../third_party/build-support/wasm-sdk/README.md).

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

Base maintenance qualifies the archived SDK after installation/relocation.
Linux builds both FLTK and Rev with the relocated SDK, runs their native
conformance/self-check cases, the complete terminal/framebuffer group and an
X11 software-window probe. This checks the actual cold-built archive when
`source=rebuild`; a later application run would otherwise fetch the existing
published archive. Full GUI workflow, modem and calibration qualification stays
in the separate SDK/release workflows. Windows maintenance verifies the retained
source archive as well as the compiled dependency bundle before reuse.

Routine app builds download only the compiled SDK and checksum inventory.
Before finalizing a new binary release, publication copies the exact source
recipe's compiled dependencies, complete source archives and per-recipe checksum
files from `base` into that release. Every new release retains the Windows
dependency triplet; `bookworm-sdk` releases also retain the Linux SDK triplet.
Web-enabled releases retain the Wasm SDK binary/source/checksum triplet too.
Ubuntu-baseline releases do not claim to have used the Linux SDK. Each copy keeps
its original recipe filename and bytes and joins the release's final
`SHA256SUMS.txt`. Missing, incomplete or inconsistent dependencies prevent
publication. SDK compilation remains an explicit base-maintenance operation.

The retained assets cover the reusable SDK/dependency recipes required by the
binary build. Native Linux builders still use their documented distribution
toolchains and packages; Windows runners supply MSVC and the Windows SDK
separately. Microsoft's compiler and SDK are not redistributed. Dependency bases
use durable release assets, never an Actions cache. Web-enabled publication uses
short-lived Actions artifacts for independent native/browser producers, then
uploads verified combined archives to the draft. Certification downloads the exact
published assets and release-retained dependency recipes.
The separate native and SDK/Rev regression workflows retain small application
artifacts for one day for CI inspection and copied-binary checks on other hosts.
Those are not durable releases.

### Bootstrap from a surviving binary release

A schema-6 release contains enough retained assets to recover its reusable
dependencies even if the original `base` or an earlier binary release has been
deleted. The retrieval helper downloads the full binary/source pair and recipe
checksum file, verifies the release inventory and asset hashes, and names the
local checksum file `SHA256SUMS` for the existing install/verification helpers:

```sh
python3 tools/release-dependencies.py fetch --repo OWNER/REPO --tag RELEASE_TAG \
  --kind linux-sdk --directory restore-sdk
python3 tools/release-dependencies.py fetch --repo OWNER/REPO --tag RELEASE_TAG \
  --kind windows-base --directory restore-windows-base
```

Use the Linux command only for a `bookworm-sdk` release. The optional
`--binary-only` flag verifies the full pair, then retains only the compiled
archive and checksum for installation. Keep the full pair for restoration to
`base` or future reconstruction.
For older schemas the helper returns `found=false`; their existing base lookup
remains necessary.

For manual downloads, choose the exact recipe IDs in the release's asset
inventory. Download each compiled archive, matching source archive and per-recipe
checksum file together. Verify all three against the release's `SHA256SUMS.txt`,
retaining that inventory outside the isolated directories below.

For Linux, the triplet is `datapump-sdk-ID-linux-x86_64.tar.gz`,
`datapump-sdk-sources-ID.tar.gz` and `sdk-ID-SHA256SUMS.txt`. Put only those files
in `restore-sdk`, then rename the recipe checksum file to the local name
expected by the existing helper. Skip the `mv` command when using `fetch`, which
already performs this rename:

```sh
mv restore-sdk/sdk-ID-SHA256SUMS.txt restore-sdk/SHA256SUMS
python3 tools/sdk-release.py verify --directory restore-sdk
python3 tools/build-sdk.py install \
  --archive restore-sdk/datapump-sdk-ID-linux-x86_64.tar.gz \
  --destination /absolute/path/to/recovered-sdk
```

Use a checkout matching the retained recipe; `verify` checks the preserved
recipe and sources against that checkout. The installed SDK can then build the
application with `./build.sh --sdk /absolute/path/to/recovered-sdk`, subject to
its documented host baseline.

For Windows, the triplet is `windows-base-ID-x64-windows-static.zip`,
`windows-base-sources-ID.zip` and `windows-base-ID-SHA256SUMS.txt`. Put only those
files in `restore-windows-base` and use the matching source checkout:

```powershell
Rename-Item restore-windows-base/windows-base-ID-SHA256SUMS.txt SHA256SUMS
$toolchain = & ./tools/select-windows-toolchain.ps1
python tools/windows-base.py install --directory restore-windows-base `
  --destination "$PWD/build/recovered-windows-base" --linker-version $toolchain.LinkerVersion
```

Skip `Rename-Item` when using `fetch`. Windows installation accepts either the
full binary/source pair or the compiled archive alone, with the adjacent
`SHA256SUMS`; when the source archive is present it verifies that archive too.

Replace `ID` with the appropriate 20-character recipe identity and use new
installation destinations. Once downloaded, verification and installation use
local files. Keep the source archives for future reconstruction. Explicit base
maintenance can restore the same triplets using the existing
`sdk-release.py publish` and `windows-base.py publish` commands, with `SHA256SUMS` in each
isolated directory. Windows publication requires a published prerelease named
`base` to exist first. Existing recipe assets are compared and never overwritten;
restoration does not authorize a cold rebuild or alteration of surviving
release assets.

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

The combined `_Publish new Latest release` entry point, manual portable release,
native CI, SDK qualification, base maintenance and certification workflows expose
x86-64 Linux and Windows runner dropdowns. The combined entry point, portable
release, native CI and certification also expose `arm_runner` for their ARM64
jobs and diagnostics. The same input names work through GitHub CLI:

| Input | Choices | Default |
| --- | --- | --- |
| `linux_runner` | `ubuntu-24.04`, `ubuntu-latest-m`, `ubuntu-latest-l`, `ubuntu-latest-h` | `ubuntu-24.04` |
| `arm_runner` | `ubuntu-24.04-arm-l`, `ubuntu-24.04-arm-h`, `ubuntu-24.04-arm` | `ubuntu-24.04-arm` |
| `windows_runner` | `windows-2022`, `windows-latest-l`, `windows-latest-h` | `windows-2022` |

The larger labels are the runners configured by `mirage335-colossus`. Every
workflow default and automatic push/PR fallback uses standard GitHub-hosted
runners, including helper, metadata and report jobs. Heavy H pools require an
explicit runner selection. Agents should select them when expected to save
elapsed time; no separate user request is required for that choice. An ordinary
push or an omitted dispatch input does not opt into a paid pool.
Release repository repackaging, APT and Arch/Gentoo checks preserve the selected
hosts through their reusable workflows. Jobs do not automatically retry on a
different pool.
Each architecture has its own selector: an x86-64
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

Compilation uses the shared [CPU/RAM capacity selector](building.md#commands-and-profiles).
It reserves one usable logical CPU, with at least one job, and caps concurrency
using currently available memory. Detection uses the Python standard library and
optional OS probes with conservative fallbacks; no resource-monitor dependency
or download is required. The build wrapper's `--build-jobs` lets SDK and certification builds use that capacity
while test `--jobs` stays at two, or one for existing serial checks. Certifying
older immutable releases whose wrapper lacks this option retains their original
two-job behavior. Windows jobs enable
[MSBuild MultiToolTask with a process limit shared across projects](https://devblogs.microsoft.com/cppblog/cpp-build-throughput-investigation-and-tune-up/)
so source files within a project can compile concurrently without multiplying
the CPU limit for every project. CI sets `CL_MPCount` to the selected count as
well as CMake's project parallelism. Module dependencies and link steps still impose
serial work; larger runners do not guarantee a particular elapsed time.

The [six-package H-runner verification](https://github.com/mirage335-colossus/pumpModem/actions/runs/35882420441)
passed on 2026-09-23 and retained an experiment draft. Windows FLTK/Rev whole
jobs took **2m15s/2m32s**, with existing base restore in **15s/20s**; the preceding
standard-runner publication took **5m43s/6m49s** for those jobs. ARM64 FLTK/Rev
took **3m30s/5m03s**. These are observed results, not a speed guarantee or full
certification. See the [validation record](validation.md#larger-runners-and-independent-compilation-concurrency--23-september-2026)
for source/run identities and the separate certification limitations.

Base reuse remains the first optimization. Cold SDK builds use the CPU/RAM budget
when base maintenance's `jobs=0`; normal application builds continue retrieving
the exact existing base instead of rebuilding it. Larger runners do not change
`devfast=false` defaults, test assertions or release certification requirements.

## Windows dependency base

Select `platform=windows` in [Maintain base SDK](../.github/workflows/sdk-base.yml).
Its [Windows build workflow](../.github/workflows/windows-base.yml) preserves a relocatable, precompiled dependency bundle in the same `base`
release. Its [recipe](../third_party/build-support/windows-base.json) pins
vcpkg and the shared union of static OpenSSL, GLEW, FreeType and SDL2 dependencies
for both GUI backends and the framebuffer host, including Debug and Release
configurations. The Windows TUI uses the native console API. The runner
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

Ordinary Windows release and full CI jobs download the exact recipe from
`base`; schema-6 certification downloads the target release's retained copy.
They verify and relocate it through [the helper](../tools/windows-base.py).
They fail with maintenance instructions if it is missing, rather than starting
an implicit vcpkg build. The dependency handoff uses release assets, with no
Actions cache or artifact storage. Application compilation and the selected
tests still run normally.

Certification checks out the published source and current workflow tooling into
separate directories. Its current runner-discovery wrapper invokes the released
source's dependency helper and recipe to install the retained copy, preserving
the recipe identity while accommodating the selected runner image. The wrapper
passes the release tag and pinned inventory hash to the retrieval helper.
Older release schemas retain the base lookup, and releases predating the
dependency helper retain the current-tooling fallback. The application source,
published archives and report hash bindings remain those of the release under
certification.

Maintenance uses `source=auto` to reuse the exact bundle or build it when
missing, `source=base` to require reuse, and `source=rebuild` for an explicit
cold build. `publish=true` preserves the bundle, matching source inputs,
checksums and build provenance; `publish=false` leaves durable storage alone.
Cold dependency compilation uses the available runner cores. Existing recipe
assets are immutable: an upgrade needs a new recipe identity, and rebuilding
an existing recipe does not authorize replacing its bytes.

The frontend-enabled Linux SDK recipe adds wide ncurses, terminal descriptions
and SDL2; the Windows recipe adds SDL2. These produce new recipe identities.
Maintain both bases explicitly before the first full CI/release run using these
recipes (`maintain_base=both` in the combined release entry point, or the
corresponding base workflows). Prior recipe assets remain unchanged. A missing
new recipe is a maintenance prerequisite, never permission for routine jobs to
build dependencies implicitly.

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
`warning.log` and `SHA256SUMS.txt`, plus the complete SDK/dependency triplets
required by schema 6. These retain the exact recipe assets from `base` on each
binary release; see [durable SDK storage](#durable-sdk-storage-and-compilation-time).
The optional `version`
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
| Windows x64 | MSVC v143 with static CRT | Selected Windows x64 hosted runner and archive relocation; the default is `windows-2022`, using VS2022 with v143 |

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

For the supported VS2022/v143 toolchain, Microsoft documents the ability to build
desktop applications
for [Windows 10 and 11](https://learn.microsoft.com/en-us/visualstudio/releases/2022/compatibility?view=vs-2022).
The default `windows-2022` runner uses Windows Server 2022. An explicitly selected
organization H runner uses its configured image; consult that run's image/toolchain logs.
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
deadline and all receiver assertions remain in place. Full certification runs
calibration in independent jobs for every Linux and Windows source target,
alongside GUI, contract and published-package verification. Both scopes reuse
the same pinned source, dependency recipe and build configuration. Calibration
retains every section, seed, worker setting, assertion and process deadline;
each applicable calibration job must succeed before certification is recorded.
This duplicates only setup and the minimal calibration build, allowing its
roughly 20–40 minutes of computation on standard runners to overlap the other
checks; actual duration varies with the host.

Terminal and framebuffer regressions run in independent native Linux Release
and Debug jobs, a Windows job, and an SDK job. Releases declaring both interfaces
also require independent frontend certification jobs for each source target.
They run alongside the existing modem, native GUI, calibration and package jobs;
their tests are excluded from the core/native-GUI test selection. Each frontend
job runs its cases serially to preserve terminal input timing. Package producers
still include and verify both binaries and their runtime resources.
