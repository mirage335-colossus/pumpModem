# FLTK source provenance

Data Pump is based in part on the work of the [FLTK project](https://www.fltk.org/).
The native desktop application statically links FLTK 1.4.5. The complete upstream
source is included in `fltk/`; CMake does not download any dependencies.

- Repository: [fltk/fltk](https://github.com/fltk/fltk)
- Release: [release-1.4.5](https://github.com/fltk/fltk/releases/tag/release-1.4.5) (April 25, 2026)
- Archive: [release-1.4.5.tar.gz](https://codeload.github.com/fltk/fltk/tar.gz/refs/tags/release-1.4.5)
- Archive SHA-256: `7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea`
- Documentation: [manuals and migration guides](https://www.fltk.org/documentation.php)
- License: [fltk/COPYING](fltk/COPYING), LGPL with FLTK's static-linking exceptions.

Version evidence: [fltk/CMakeLists.txt](fltk/CMakeLists.txt) and
[FL/Enumerations.H](fltk/FL/Enumerations.H) both specify **1.4.5**; the upstream
README headings still say 1.4.4.

There are no Data Pump modifications to the vendored source. Build options live
in [cmake/NativeGui.cmake](../cmake/NativeGui.cmake). The application uses the
static base widget library;
OpenGL, Wayland, Cairo, Pango, SVG loading, printing, and FLTK development tools
are disabled. Linux uses X11/Xft (also usable through XWayland); Windows uses the
native Windows backend. These operating-system interfaces are distinct from a
separately installed GUI toolkit or interpreter.

For upgrades, replace the source from a pinned release, update the archive hash
and bundled-library versions below, and review the migration guide and CMake
options. Rebuild on Linux and Windows, run the `gui_*`, `packaging_support`, and
`native_relocation` CTest checks, and check interactive GUI behavior.

## Libraries bundled inside FLTK

These versions come from FLTK's [bundled-library inventory and upgrade guide](fltk/documentation/src/bundled-libs.dox).
They are included with the pinned FLTK source, with FLTK's own build files and
patches. Data Pump disables NanoSVG and Wayland/libdecor; the bundled image
codecs are selected for the optional `fltk_images` target, which the GUI does
not currently link.

| Component | Bundled version/revision | Upstream locations |
| --- | --- | --- |
| IJG JPEG | [9f](fltk/jpeg/jversion.h) | [Website](https://ijg.org/), [9f archive](https://www.ijg.org/files/jpegsrc.v9f.tar.gz) |
| libpng | [1.6.44](fltk/png/png.h) | [Website](https://libpng.org/pub/png/libpng.html), [repository](https://sourceforge.net/p/libpng/code/ci/libpng16/tree/) |
| zlib | [1.3.1](fltk/zlib/zlib.h) | [Website](https://zlib.net/), [repository](https://github.com/madler/zlib) |
| NanoSVG | [FLTK tag `fltk_2023-12-02`, commit `7aeda550a84c15680f7e55867896c3906299dffb`](fltk/nanosvg/README.txt) | [Original repository](https://github.com/memononen/nanosvg), [FLTK fork](https://github.com/fltk/nanosvg) |
| libdecor | Commit `f7cd7ffd` (2025-01-21), as recorded in FLTK's inventory | [Website/repository](https://gitlab.freedesktop.org/libdecor/libdecor) |

Prefer updating these through a FLTK release. For independent updates, follow
the bundled-library guide: preserve FLTK's build files, JPEG/PNG/zlib symbol
prefixes, and NanoSVG patches; update zlib before libpng. Retain each component's
license notices in its headers and accompanying LICENSE or README files.
