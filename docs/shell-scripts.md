# Shell scripts used by Data Pump

`build.sh` supports Dash and traditional SVR4-style Bourne shell. Local checks
use Dash and the official Heirloom Bourne shell 050706; this does not imply
original V7 shell support or an obsolete operating-system toolchain. Modern
external utilities, CMake and the application's documented build dependencies
are still required. Heirloom uses external `printf`, so that utility must also
be on `PATH`.

The APT and CMake-installation CI helpers retain their existing modern POSIX
`/bin/sh` requirement and original implementations. Their network, installation
and hosted-runner behavior is outside the local build-wrapper portability checks.

## Maintained scripts

| Script | Purpose | Interpreter and external tools |
| --- | --- | --- |
| [`build.sh`](../build.sh) | Select the build profile, configure/build, run test groups and verify local packages | Bourne/Dash `/bin/sh`; CMake, CTest, `dirname`, `printf`, shell builtin physical `pwd`; `sed`/`cut`/`expr` for compiler/cache/SDK options; optional Python for capacity detection or browser package verification |
| [`tools/ci-apt.sh`](../tools/ci-apt.sh) | Signed distribution mirror configuration and bounded APT fetch recovery | Modern POSIX `/bin/sh`, unchanged; Debian/Ubuntu APT, dpkg, coreutils, sed and grep |
| [`tools/install-release-cmake.sh`](../tools/install-release-cmake.sh) | Explicit CI installation of the pinned release CMake tool | Modern POSIX `/bin/sh`, unchanged; Linux, curl, SHA-256 tools, tar, coreutils and `mktemp` |
| [`tools/diagnose-linux-rev-graphics.sh`](../tools/diagnose-linux-rev-graphics.sh) | Bounded capture of Linux Rev graphics/loader diagnostics; already compatible and unchanged | Bourne/Dash `/bin/sh`; Linux `timeout`, `glxinfo`, `readelf`, Python 3 and an existing application package |

There is no `editor.sh` or `fork.sh` in the Data Pump checkout. Those are separate
software-foundation tools. Build/test entry points such as `COMPILE-web` are
command references rather than executable shell scripts.

Portable syntax alone is insufficient: the build wrapper also preserves argument
boundaries, uses the invocation directory rather than an inherited stale `PWD`,
handles SDK paths beginning with `-`, clears positional arguments explicitly, and
avoids changing `IFS` through a traditional-shell builtin. Compiler argument text
is inspected literally and never evaluated. Optional capacity-probe failures
fall back to one compile job.

## Generated launchers and package recipes

[`tools/apt-release.py`](../tools/apt-release.py) owns the installed `/bin/sh`
launchers. [`tools/distro-release.py`](../tools/distro-release.py) reuses these for
Arch/Gentoo recipe extras, and [`tools/arch-release.py`](../tools/arch-release.py)
reuses them for native pacman packages. There are up to twelve, depending on the
interfaces included in the portable release:

| Installed launchers | Purpose |
| --- | --- |
| `datapump-fltk`, `datapump-rev` | Native GUI |
| `datapump-cli-fltk`, `datapump-cli-rev` | Command-line modem |
| `datapump-tui-ncurses`, `datapump-tui-ncurses-rev` | Terminal frontend |
| `datapump-fb-sdl`, `datapump-fb-sdl-rev` | Software framebuffer frontend |
| `datapump-worker`, `datapump-worker-rev` | Inherited-pipe worker |
| `datapump-html`, `datapump-html-rev` | Open the exact installed browser page through `xdg-open` |

The existing executable launchers use `exec ... "$@"` without `set -u`. This
preserves no arguments, one explicit empty argument and literal spaces or
metacharacters on the supported Dash/Heirloom baseline. Their original bytes,
package formats and verification remain unchanged. HTML launchers open the fixed
installed page and intentionally forward no extra page URL.

Generated `PKGBUILD` and Gentoo ebuild files use their package managers' Bash
languages (including arrays and `local`). They are not standalone `/bin/sh`
entry points. GitHub Actions inline commands likewise run in the explicitly
selected runner shell, normally Bash on Linux and PowerShell on Windows; see
[the workflows](../.github/workflows).

## Historical and upstream scripts

The fifth tracked `.sh` outside vendored source is the explicit Bash capture
[`docs/validation-data/bookworm-offline-20260930/online.sh`](validation-data/bookworm-offline-20260930/online.sh).
It records the dated Bookworm validation procedure and uses `pipefail`.
Its original bytes and interpreter are retained as historical evidence.

Seventeen upstream shell scripts/templates are retained under `third_party/`.
Their original interpreter/provenance remains authoritative; the project's
Bourne/Dash guarantee does not extend to these upstream implementations:

| Source | Scripts/templates |
| --- | --- |
| [`third_party/fltk`](../third_party/fltk) | `CMake/macOS-bundle-wrapper.in`, `autogen.sh`, `config.guess`, `config.sub`, `documentation/convert_doxyfile`, `documentation/make_header`, `documentation/make_pdf.in`, `fltk-config.in`, `fluid/documentation/convert_doxyfile`, `fluid/documentation/make_header`, `fluid/documentation/make_pdf.in`, `install-sh`, `misc/update_config_scripts`, `src/xutf8/utils/case.sh`, `src/xutf8/utils/non_spacing.sh`, `src/xutf8/utils/tbl_gen.sh` |
| [`third_party/xz`](../third_party/xz) | `src/liblzma/validate_map.sh` |

Prepared source SDKs also include Buildroot's upstream `relocate-sdk.sh`, checked
and run by [`tools/build-sdk.py`](../tools/build-sdk.py). Its implementation comes
from the exact prepared SDK, rather than a tracked Data Pump shell generator;
use that SDK's interpreter/dependency contract. Generated build directories and
untracked backup trees are artifacts of their source generators, not additional
maintained entry points.

## Recheck compatibility

The portability checks are opt-in local fixtures. The manual filename is outside
ordinary test discovery, CMake registration and workflow selections; the existing
`test_build_wrapper.py` remains unchanged.

```sh
DATAPUMP_TEST_SHELL=/bin/dash python3 -B tests/manual_shell_compatibility.py
```

Repeat with the path to Heirloom Bourne `sh` in `DATAPUMP_TEST_SHELL`. A missing
or non-executable selected interpreter fails the check. The runner reuses the
47 original mock-wrapper cases and adds six portability cases for capacity
failure, unusual whitespace, stale `PWD`, leading-dash SDK paths and physical
source/SDK paths. It uses isolated CMake/CTest stubs and SDK metadata fixtures,
without application builds, signing, downloads, installations or GUI launches.
These checks characterize the changed shell parsing, paths, argument forwarding
and exit handling; they do not qualify compilers, packages, drivers or a hosted
CI environment. Syntax checks and ShellCheck supplement the local behavior checks.
