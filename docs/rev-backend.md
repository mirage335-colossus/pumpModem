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

`ui_contract.hpp` and `screen_console.cpp` describe ordered controls, stable IDs,
application bindings, shared desktop slots and grouped command menus.
`desktop_layout.hpp` supplies the same logical geometry to FLTK and Rev, including
the persistent header/identity row and modem/status footer around the tab viewport.
The shared default is 1180 by 866 logical pixels and the minimum is 1030 by 786.
Rev maps controls to Text, Button, Checkbox, Dropdown and Box elements. Lists and
tabs compose those elements. FLTK uses the same declarations and shared
application lifecycle with its own native widgets. `controller.cpp` owns drafts, validation, eligibility, exact binary
bits, the 120 ms estimate debounce, revisioned workers, keys and received files.
Silent state updates preserve existing editors, cursor and selection positions.
Structured signal rows, including frequency and reception quality, are defined
once in `record_presentations.hpp`; native list rows preserve stable IDs and
follow new arrivals only when already at the tail. Native clipboard and
asynchronous prompt services stay inside the adapter.
Pending Save requests retain their payload across inbox eviction.

`inspection_page.hpp` builds the inspection document from the shared `Inspection`
model. `backend_rev_document.hpp` renders its headings, numbered step/section
cards, notes, navigation and parameter rows with native Rev elements. Modem flow
follows the FLTK section order: processing lanes, chosen alphabets, full pattern
inspection, then preamble and integration notes. Transmission separates on-air
and logical packet sections, shows proportional data/parity codeword bars and
coding notes, and places the parameter table last. Card columns respond to the
page width; native text measurement supplies their heights. The pages scroll
inside the same tab viewport as FLTK. Their section structure is shared policy;
glyph metrics and wrapping remain toolkit-specific.

`bitmap.hpp` specifies borrowed Gray8, MSB-first Mono1 and optional RGB24 pixel
rectangles. `bitmap_sources.hpp` maps application state to immutable sources;
`plot_render.cpp` produces waveform, waterfall, constellation, QR and inspection
graphics. Rev uses opaque RGB textures, nearest filtering and actual backing
pixels. Inspection plots use named snapshots with explicit logical dimensions:
constellations and distance matrices stay square, chip rows retain their height,
and responsive chip navigation sits above the grid. Axis labels, explanations
and evidence values stay native text. FLTK consumes the same control declarations,
controller, bitmap presentation and document tree through its generic native
adapter. Both link `datapump_gui_application`; the previous separate FLTK
controller and inspection rendering path have been removed.

Local Rev text fixes preserve UTF-8 byte indices while rendering Unicode glyphs
and moving the caret across complete code points. Font coverage comes from the
embedded DejaVu font; missing glyphs use its visible replacement box. This is
not a shaping or font-fallback system. Original UTF-8 bytes are preserved for
transmission and copying. X11 clipboard transfers include long incremental
selections. Rev's current text primitive does not provide undo/redo history or
word-based Ctrl+Arrow and Ctrl+Backspace/Delete editing; those gestures currently
move or delete individual code points. FLTK retains its native editor behavior.

Native mouse events retain physical screen coordinates. Rev queries the current
client origin and converts to logical client coordinates once for hit testing
and caret placement. Wheel events update their target position even without a
preceding motion event. A scale change invalidates layout even when the physical
window size stays fixed. Linux and Windows wheel notches use the same 120-unit
delta. High-DPI startup centers the final physical window size on its display.

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
Mesa 25.0.7) in a private X11 virtual display. Before the shared desktop and rich
inspection layout changes, a full smoke workflow followed by a 20-second hold
at 1180 by 960 took 31 seconds wall time and 74 seconds user CPU time. This is a
historical measurement of the earlier layout, not a benchmark of the current
1180 by 866 desktop. The initial 25 Hz paint path used 211 seconds; skipping
invisible rectangles and reusing glyph geometry reduced that to 161 seconds, and separating
presentation cadence reduced it to 74. Modem polling and plot-history capture
remain at 25 Hz while state/plot presentation runs at 10 Hz; native input can
repaint immediately. The same workflow without a display took
10.25 seconds wall time and 0.62 seconds user CPU time. Software rendering still
uses several CPU cores in this test; it needs further work for low-power hosts.
Measure CPU, memory, editing latency and reception continuity on the intended
hardware. Keep reception polling independent if reducing plot cadence.
Replacing the renderer should require no GL concepts in shared controls or
pixel producers.

`gui_layout` checks the common default/minimum rectangles, resizing, alignment
and persistent controls outside the page viewport. `gui_inspection_page` checks
section order, responsive cards and chip windows, square plots, codeword
proportions and the final parameter table without a toolkit.
`gui_bitmaps` checks formats/strides, borrowed-storage lifetime, whole versus
tiled replay, QR brightness, waveform peaks, I/Q scale and signed plots.
`gui_controller` checks controller/declaration and named-bitmap policy without a
display. `test_gui_controller --smoke` and `datapump-gui --smoke-test` use the
same expanded workflow: production key generation/loading and failure recovery,
text/file/raw simulation, effective FEC settings, encrypted exact bits, pending
reception, replay cancellation/replacement, clipboard request payloads, stale
drafts and retained saves. They also verify shared structured signal fields,
replay timing and rendered measurement sources. The GUI smoke renders controls, switches pages and
checks control layout, UTF-8 editing, failed-paste selection preservation and modal
focus isolation/restoration. `test_rev_platform` checks actual clipboard round
trips, failed/overlapping reads, stale replies and bounded incremental selections
on a display. The updated `gui_layout`, `gui_inspection_page`, `gui_controller`
and `gui_self_check` tests passed, as did the full Rev smoke with shared desktop
rectangle assertions at 1x and 2x. Native click-and-type checks exercised the
visible message editor at both scales. The default and minimum-size console
and both inspection pages were visually checked on the private display.
A further full smoke passed at 1.2x while resizing to the minimum window size;
physical minimum dimensions round upward to preserve the logical minimum.

`test_rev_coordinates` is a display-dependent regression executable, deliberately
excluded from default CTest runs. On Linux it requires X11, an OpenGL-capable
display and the `libXtst.so.6` runtime (`libxtst6` on Debian-style systems). It
injects native pointer events to check moved window origins, physical/logical
positions, focus, UTF-8 caret boundaries, wheel targeting and actual scroll
distance at 1x and 2x. It
also dispatches a scale transition while the physical dimensions stay fixed;
this exercises invalidation without changing desktop DPI settings. The
`xvfb-run` example additionally needs Xvfb and `xauth`.

```sh
cmake --build build-rev --target test_rev_coordinates
# Run on an existing private X11 display:
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 ./build-rev/test_rev_coordinates 1
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 ./build-rev/test_rev_coordinates 2
# Or start an isolated display for either scale:
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 2400x1800x24 -dpi 96' \
  ./build-rev/test_rev_coordinates 2
```

These native coordinate checks passed on Linux at both scales, including moved
origins and scale transitions. The Windows event path and test are implemented
but have not been runtime tested, including the corrected decorated client-size
conversion and DPI-before-resize ordering. Automated GUI workflows use simulation;
hardware audio still needs device testing. FLTK now runs the same expanded
application smoke through the same `Application` lifecycle. Its separate native
adapter/document tests exercise toolkit input, records, clipboard and rendering.
The native extension fixture is shared unchanged by both adapters, and
`gui_adapter_boundary` prevents application bindings or parallel screen sources
from returning to either adapter.

Before the desktop/inspection alignment changes, the portable Linux package
also passed the full GUI smoke after relocation to a directory with spaces,
with the checkout, build trees, original installation,
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
