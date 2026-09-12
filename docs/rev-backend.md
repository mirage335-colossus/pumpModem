# Rev GUI backend

The opt-in `rev` backend uses the requested upstream `clean` branch, pinned at
[`d73faa7759b5cfd30d592057790ab458568b569b`](https://github.com/ryanpmcguire/Rev/tree/d73faa7759b5cfd30d592057790ab458568b569b).
FLTK remains the default. Select a toolkit when configuring a separate build;
there is no runtime toolkit switch.

## Build and run

Rev requires CMake 3.28+, Ninja, a C++23 standard library, and a compiler with
C++ module support. Clang 19 is the tested Linux toolchain. GCC 14 crashes while
serializing an upstream module and is rejected with an actionable configuration
error. GCC 15+ and the Windows/MSVC path still require platform testing; their
build paths do not establish a tested release. macOS/Metal is not integrated.
FLTK and CLI-only builds retain their C++20 requirements.

Linux needs OpenSSL 3, OpenGL, GLEW, FreeType, and X11/Xrandr/Xext development
files. Python 3 embeds the pinned shaders, SVG icons and monospaced DejaVu font
at build time; Python and source assets are unnecessary at runtime. Additional
Debian-style build packages include `libgl-dev libglew-dev libfreetype-dev
libxrandr-dev`. CMake never downloads dependencies.

```sh
cmake -S . -B build-rev -G Ninja \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DCMAKE_BUILD_TYPE=Release -DDATAPUMP_GUI_BACKEND=rev
cmake --build build-rev --parallel
ctest --test-dir build-rev --output-on-failure
./build-rev/datapump-gui --self-check
./build-rev/datapump-gui
```

`--simulation` starts without hardware audio. `--color` and `--monochrome` have
the same meaning as in FLTK; the last switch wins. QR brightness starts at Dark
and changes only that image. Rev currently uses a path-entry dialog for file
selection on the modem host. Existing files are never overwritten.

## Shared boundaries

`ui_contract.hpp` and the two `screen_*.cpp` files describe ordered controls,
stable IDs, application bindings and relative row widths. Rev maps these to
Text, Button, Checkbox, Dropdown and Box elements. Lists and tabs compose those
elements. `controller.cpp` owns drafts, validation, eligibility, exact binary
bits, the 120 ms estimate debounce, revisioned workers, keys and received files.
Silent state updates preserve existing editors, cursor and selection positions.
Native clipboard and asynchronous prompt services stay inside the adapter.
Pending Save requests retain their payload across inbox eviction.

`bitmap.hpp` specifies borrowed Gray8, MSB-first Mono1 and optional RGB24 pixel
rectangles. `bitmap_sources.hpp` maps application state to immutable sources;
`plot_render.cpp` produces waveform, waterfall, constellation, QR and inspection
graphics. Rev uses opaque RGB textures, nearest filtering and actual backing
pixels. FLTK's existing plot widgets use the same producers. FLTK's controller
and procedural screen construction remain to be migrated to the semantic API;
the shared pixel boundary is already exercised by both toolkits.

Local Rev text fixes preserve UTF-8 byte indices while rendering Unicode glyphs
and moving the caret across complete code points. Font coverage comes from the
embedded DejaVu font; missing glyphs use its visible replacement box. This is
not a shaping or font-fallback system. Original UTF-8 bytes are preserved for
transmission and copying. X11 clipboard transfers include long incremental
selections.

## Software rendering and validation

This Rev revision uses GLSL 4.30 and persistent mapped buffers. Startup requires
OpenGL 4.3 plus `ARB_buffer_storage`, or OpenGL 4.4+. Missing capabilities produce
an error before the rendering buffers are created. Software OpenGL is a valid
profile; a separate native software renderer remains future upstream work.

```sh
LIBGL_ALWAYS_SOFTWARE=1 ./build-rev/datapump-gui --smoke-test
LIBGL_ALWAYS_SOFTWARE=1 ./build-rev/datapump-gui --simulation
```

The implementation was exercised on Mesa llvmpipe (LLVM 19.1.7, OpenGL 4.5,
Mesa 25.0.7) in a private X11 virtual display. A full smoke workflow followed by
a 20-second hold at 1180 by 960 took 31 seconds wall time and 74 seconds user
CPU time. The initial 25 Hz paint path used 211 seconds; skipping invisible
rectangles and reusing glyph geometry reduced that to 161 seconds, and separating
presentation cadence reduced it to 74. Modem polling and plot-history capture
remain at 25 Hz while state/plot presentation runs at 10 Hz; native input can
repaint immediately. The same workflow without a display took
10.25 seconds wall time and 0.62 seconds user CPU time. Software rendering still
uses several CPU cores in this test; it needs further work for low-power hosts.
Measure CPU, memory, editing latency and reception continuity on the intended
hardware. Keep reception polling independent if reducing plot cadence.
Replacing the renderer should require no GL concepts in shared controls or
pixel producers.

`gui_bitmaps` checks formats/strides, borrowed-storage lifetime, whole versus
tiled replay, QR brightness, waveform peaks, I/Q scale and signed plots.
`gui_controller` checks controller/declaration and named-bitmap policy without a
display. `test_gui_controller --smoke` and `datapump-gui --smoke-test` cover
text/file/raw simulation, pending reception, clipboard request payloads, stale
drafts and retained saves. The latter also renders controls, switches pages and
checks control layout, UTF-8 editing, failed-paste selection preservation and modal
focus isolation/restoration. `test_rev_platform` checks actual clipboard round
trips, failed/overlapping reads, stale replies and bounded incremental selections
on a display. Automated GUI workflows use simulation;
hardware audio and the Windows adapter need device/platform testing. Existing
FLTK GUI smoke workflows remain regression checks.

The portable Linux package also passed the full GUI smoke after relocation to a
directory with spaces, with the checkout, build trees, original installation,
development libraries and system fonts hidden. Dependency closure, embedded
resources, CLI checks, package inventory and tamper checks passed. This checks
the packaged runtime on the same host, not compatibility with every Linux ABI.

## Maintenance and packaging

[Rev source notes](../third_party/rev/README.datapump.md) record the dependency
closure, omissions, repairs and notices. Upgrades change a deliberate commit
pin. Recheck local fixes against upstream when upgrading. Generated files remain
in the build directory; literal resources are embedded so relocation needs
neither the source checkout nor a particular working directory.

OpenGL dispatch libraries and graphics drivers belong to the destination system.
Packaging leaves them on the host while collecting ordinary application
libraries such as GLEW and FreeType. The pinned checkout contains no license
grant for Rev itself. Its distribution terms must be resolved with its owner
before distributing a Rev-enabled release; the provenance record does not
assign Rev a license.
