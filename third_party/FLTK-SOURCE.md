# FLTK source provenance

Data Pump is based in part on the work of the FLTK project (https://www.fltk.org).
The native desktop application statically links FLTK 1.4.5. The complete upstream
source is included in `fltk/`; CMake does not download any dependencies.

- Upstream: https://github.com/fltk/fltk
- Release tag: `release-1.4.5` (April 25, 2026)
- Archive: https://codeload.github.com/fltk/fltk/tar.gz/refs/tags/release-1.4.5
- Archive SHA-256: `7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea`
- License: `fltk/COPYING`, LGPL with FLTK's static-linking exceptions.

There are no Data Pump modifications to the vendored source. Build options live
in `cmake/NativeGui.cmake`. The application uses the static base widget library;
OpenGL, Wayland, Cairo, Pango, SVG loading, printing, and FLTK development tools
are disabled. Linux uses X11/Xft (also usable through XWayland); Windows uses the
native Windows backend. These operating-system interfaces are distinct from a
separately installed GUI toolkit or interpreter.
