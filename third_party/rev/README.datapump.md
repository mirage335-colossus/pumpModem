# Rev source used by DataPump

Repository: https://github.com/ryanpmcguire/Rev
Branch: `clean`
Commit: `d73faa7759b5cfd30d592057790ab458568b569b`

This directory contains the dependency closure of the desktop element and 2D
rendering modules used by the DataPump adapter. Media capture, network transports,
and the 3D application stack are omitted. The selected module names are recorded
in `UPSTREAM.json`. Files under `source/` retain their upstream module APIs, with the compatibility fixes listed below.

The upstream checkout at this commit contains no top-level license or license
grant for Rev itself. This provenance record is not a license grant. Permission
and attribution requirements for distributing Rev must be resolved with its
owner before distributing a Rev-enabled DataPump release. Existing notices in
the included GLM and NanoSVG dependency headers are retained.

DataPump uses system GLEW and FreeType, rather than the upstream prebuilt binary
libraries. `external/glew/glew.h` is a compatibility include for system GLEW.
Upstream's default Arial resource is replaced during staging with DejaVu Sans Mono (stored as `DejaVuSans.ttf`);
the font's redistribution notice is in `resources/DejaVu-LICENSE`. No upstream
Arial binary is included.

The build runs `tools/prepare_rev.py` to stage modules in the binary directory
and embed their literal shader, icon and font resources. This replaces upstream
CMake's outdated `src`/`Resources` paths and source-tree resource generation.
All output belongs to the build tree; installed applications need no source
checkout or asset directory at runtime.

Local fix: `Elements/Text.ixx` also destroys its owned selection/cursor line primitive.

Local compiler and Linux compatibility fixes:

- Include `<algorithm>` in DirtyFlag and Element, and `<cstdint>` in WinEvent.
- Complete the Linux NativeWindow methods already used by the shared Window:
  `nativeFrameless`, EWMH `setIcon`, and root-relative `getClientPos`.
- Linux mouse events now carry physical screen coordinates (`x_root/y_root`),
  matching Win32 and `Window::screenToLocal`; the native layer no longer applies
  a second DPI division. Wheel events refresh their own pointer location before
  dispatch, and DPI changes invalidate layout through `onResize`.

Build requirements: CMake 3.28+, Ninja, Clang 19 (verified), Python 3,
OpenGL 4.4 or OpenGL 4.3 with `GL_ARB_buffer_storage`, GLEW, FreeType, and Linux X11/XRandR/Xext
headers and libraries. GCC 14 currently hits an internal compiler error while
serializing `Rev.NativeWindow`; use Clang for this pinned revision. Windows
OpenGL/MSVC integration is included but has not been executed in the Linux
verification environment. macOS/Metal is rejected explicitly by the adapter
build until integrated and tested.

The GL loader and hardware/software drivers are supplied by the destination
computer and excluded from the portable library collection. GLEW, FreeType and
ordinary desktop dependencies are still collected. Mesa software rendering
must expose the required modern OpenGL capability; there is no standalone Rev
software renderer in this selected source snapshot.

Both GL platform entry points validate shader and buffer-storage capabilities before Canvas creates graphics buffers.

GLM's dual licensing is documented at https://github.com/g-truc/glm/blob/master/copying.txt.
The MIT option is reproduced in `external/glm/LICENSE` (retrieved 2026-09-12);
Rev's copy omitted the upstream GLM top-level license file. NanoSVG's notice is
also copied verbatim from its included header into `external/nanosvg/LICENSE`
so both notices accompany installed binaries. These dependency notices do not
supply a license for Rev itself.

Local text correctness patches:

- `external/rev_utf8.hpp` decodes Unicode scalars while retaining original byte
  offsets; layout, selection, hit testing and editing keep the same byte index
  contract used by DataPump strings.
- `Core/Font.ixx` creates glyph atlas pages lazily for Unicode text. The text
  primitive carries each glyph's rectangle and UVs instead of assuming a fixed
  ASCII uniform-buffer table; its OpenGL and Metal shaders match that layout.
- `Elements/Text.ixx` moves and selects on UTF-8 boundaries and supports vertical,
  Home and End caret movement. It reflows wrapped text after the actual flex
  width is known and uses the resolved text color for a visible caret.
  `Window.ixx` exposes the Home/End key names.
- The text primitive retains glyph geometry while content, font and positions
  stay unchanged; caret and color updates do not rebuild every glyph instance.

The standalone `test_rev_utf8` target covers decoding, invalid sequences,
original byte ranges, and deletion boundaries without requiring a display or
C++ modules.

Local native input patches keep X11 mouse positions in physical screen pixels
until `Window` converts them to client logical coordinates. Native scale changes
invalidate the layout, and wheel events refresh their pointer position before
hit testing. X11 wheel notches use the same 120-unit delta as Win32 so scrolling
advances 60 logical pixels per notch at either DPI scale. The display-dependent
`test_rev_coordinates` target checks moved windows, 1x/2x scaling, caret hits,
wheel targeting and actual scroll distance through native event injection.
On Windows, native `setSize` converts client extents to the outer frame at the
window's DPI, and DPI changes publish the new scale before synchronous resize
events can apply minimum sizes. These Windows paths still need runtime testing
on Windows.

Local paint optimization: `Graphics/Primitives/Rectangle/Rectangle.ixx` skips
color submissions for rectangles with zero opacity or no visible fill, border
or shadow. Stencil submissions remain unchanged, so transparent layout
containers continue to clip their children. This avoids unnecessary software
OpenGL work for the many containers that exist only to arrange controls.

Local X11 event-loop fix: `NativeWindow.lnx.ixx` limits each `pumpEvents` call
to its initial pending event count. Frames requested while drawing cannot keep
that batch running indefinitely, so animations yield to application polling,
model updates and shutdown. Native record/focus probes and the shared GUI smoke
exercise this alongside normal input delivery.

Local inherited-state/layout fix: `Element::cascadeStyle` propagates hidden,
disabled and opacity state through hidden subtrees and restores parent-size
eligibility when they are shown. `Window::calcFlexLayouts` rebuilds its visible
queues before resetting dimensions, then reapplies inherited state. Newly
revealed descendants therefore receive fresh size resolution instead of stale
measurements from before their parent was hidden.

Local nested-clipping fix: `Window.ixx` unwinds every exited stencil ancestor
when drawing a later sibling or unrelated control. Popping only one level
retained a deep document child's clip and hid later cards and persistent
controls. This supports the generic document renderer's per-node overflow
clipping without changing the shared layout contract. The Rev adapter suite
checks actual rendered pixels across nested clips, sibling cards and a control
outside the document.
