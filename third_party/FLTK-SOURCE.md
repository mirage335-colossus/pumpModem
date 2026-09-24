# FLTK source provenance

Data Pump is based in part on the work of the [FLTK project](https://www.fltk.org/).
The native desktop application statically links FLTK 1.4.5. Its source is
included in `fltk/`; CMake does not download any dependencies.

- Repository: [fltk/fltk](https://github.com/fltk/fltk)
- Release: [release-1.4.5](https://github.com/fltk/fltk/releases/tag/release-1.4.5) (April 25, 2026)
- Git commit: [a9b1113516ffd15fc7602a6d425a317df30f4720](https://github.com/fltk/fltk/commit/a9b1113516ffd15fc7602a6d425a317df30f4720)
  (peeled `release-1.4.5` tag).
- Immutable source: [tree](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720),
  [commit archive](https://codeload.github.com/fltk/fltk/tar.gz/a9b1113516ffd15fc7602a6d425a317df30f4720).
- Archive: [release-1.4.5.tar.gz](https://codeload.github.com/fltk/fltk/tar.gz/refs/tags/release-1.4.5)
- Archive SHA-256: `7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea`
- Documentation: [manuals and migration guides](https://www.fltk.org/documentation.php)
- License: [fltk/COPYING](fltk/COPYING), LGPL with FLTK's static-linking exceptions.

Version evidence: [fltk/CMakeLists.txt](fltk/CMakeLists.txt) and
[FL/Enumerations.H](fltk/FL/Enumerations.H) both specify **1.4.5**; the upstream
README headings still say 1.4.4.

Verified September 12, 2026: all **1,511 Git-tracked files** under `fltk/`
match the commit archive byte for byte at identical relative paths.
The five upstream files in [libdecor/build/](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/libdecor/build)
(`Makefile`, `fl_libdecor-plugins.c`, `fl_libdecor.c`, `fl_libdecor.h`,
`gtk-shell.xml`) are excluded from Git by the repository's `build*/` ignore
rule. Restore these from the pinned tree before enabling Wayland.

There are no Data Pump modifications to the vendored source. Build options live
in [cmake/NativeGui.cmake](../cmake/NativeGui.cmake). The application uses the
static base widget library;
OpenGL, Wayland, Cairo, Pango, SVG loading, printing, and FLTK development tools
are disabled. Linux uses X11/Xft (also usable through XWayland); Windows uses the
native Windows backend. These operating-system interfaces are distinct from a
separately installed GUI toolkit or interpreter.

For upgrades, replace the source from a pinned release, update the commit,
archive hash, and bundled-library versions below, and review the migration
guide and CMake options. Rebuild on Linux and Windows, run the `gui_*`, `packaging_support`, and
`native_relocation` CTest checks, and check interactive GUI behavior.

## Libraries bundled inside FLTK

The component links below pin the **actual copied source** to the FLTK commit
above, including FLTK's adaptations. The separate upstream commits identify
the original release baselines recorded in FLTK's
[bundled-library inventory and upgrade guide](fltk/documentation/src/bundled-libs.dox).
Data Pump disables NanoSVG and Wayland/libdecor; the bundled image
codecs are selected for the optional `fltk_images` target, which the GUI does
not currently link.

| Component / pinned FLTK source | Version | Full upstream baseline commit | Upstream locations |
| --- | --- | --- | --- |
| [IJG JPEG](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/jpeg) | 9f | No official Git repository documented; archive identity below | [Website](https://ijg.org/), [9f archive](https://www.ijg.org/files/jpegsrc.v9f.tar.gz) |
| [libpng](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/png) | `v1.6.44` | [f5e92d76973a7a53f517579bc95d61483bf108c0](https://sourceforge.net/p/libpng/code/ci/f5e92d76973a7a53f517579bc95d61483bf108c0/tree/) | [Website](https://libpng.org/pub/png/libpng.html), Git: `https://git.code.sf.net/p/libpng/code` |
| [zlib](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/zlib) | `v1.3.1` | [51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf](https://github.com/madler/zlib/commit/51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf) | [Website](https://zlib.net/), [repository](https://github.com/madler/zlib) |
| [NanoSVG](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/nanosvg) | FLTK tag `fltk_2023-12-02` | [7aeda550a84c15680f7e55867896c3906299dffb](https://github.com/fltk/nanosvg/commit/7aeda550a84c15680f7e55867896c3906299dffb) | [Original repository](https://github.com/memononen/nanosvg), [FLTK fork](https://github.com/fltk/nanosvg) |
| [libdecor](https://github.com/fltk/fltk/tree/a9b1113516ffd15fc7602a6d425a317df30f4720/libdecor) | Snapshot, 2025-01-21 | [f7cd7ffd88659ffb127b4c0ab2b74bdf7aa7ca47](https://gitlab.freedesktop.org/libdecor/libdecor/-/commit/f7cd7ffd88659ffb127b4c0ab2b74bdf7aa7ca47) | [Website/repository](https://gitlab.freedesktop.org/libdecor/libdecor) |

IJG publishes archives; FLTK records its repository as `N/A`. The downloaded
`jpegsrc.v9f.tar.gz` SHA-256 is
`04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b`.
All 64 shared JPEG files match that archive; the five extra files are FLTK
build/configuration files. Both NanoSVG headers match its fork commit.
All 16 shared libdecor source/license files match its commit; FLTK trims its
README and adds `build/`. zlib/libpng retain FLTK's adaptations, so use their
pinned FLTK subtrees when comparing the exact vendored copies.

Prefer updating these through a FLTK release. For independent updates, follow
the bundled-library guide: preserve FLTK's build files, JPEG/PNG/zlib symbol
prefixes, and NanoSVG patches; update zlib before libpng. Retain each component's
license notices in its headers and accompanying LICENSE or README files.
