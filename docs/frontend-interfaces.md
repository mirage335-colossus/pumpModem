# Terminal and framebuffer interfaces

Data Pump has three independent interactive interface choices. `datapump-gui`
uses FLTK or Rev. `datapump-tui` is a terminal application for SSH and local
consoles. `datapump-fb` presents a software framebuffer through SDL2; the same
framebuffer library can be embedded in an engine without SDL or a native window.

The terminal and framebuffer implementations are deliberately separate below
`Application`. The terminal uses cell layout and ASCII plot sampling. The
framebuffer uses pixel layout, software widgets, full-resolution plot sources and
pointer input. Neither implements modem functionality. Adding ordinary fields,
actions and documents to the shared declarations reaches both interfaces without
field-specific adapter code. A new generic presentation primitive requires
support in each relevant renderer, as it does for FLTK and Rev.

## Building

Install ncurses development files for the terminal application, and SDL2
development files for its framebuffer host. On Debian these are `libncurses-dev`
and `libsdl2-dev`. They are optional; ordinary GUI/CLI builds do not require them.
Build configuration never downloads dependencies.

```sh
./build.sh --cli --tui --fb
./build/dev-cli-tui-fb/datapump-tui --simulation
./build/dev-cli-tui-fb/datapump-fb --simulation
./build.sh test frontends --cli --tui --fb
```

Omit `--cli` to also build the selected desktop GUI. `--backend fltk|rev` selects
only that GUI; it never selects TUI or framebuffer behavior. The corresponding
CMake options are `DATAPUMP_BUILD_TUI`, `DATAPUMP_BUILD_FB`,
`DATAPUMP_TUI_BACKEND=ncurses` and `DATAPUMP_FB_HOST=sdl`.
Use a new build directory when changing dependencies/toolchains.

For engine integration without a window library:

```sh
./build.sh --cli --build-dir build/framebuffer-library -- \
  -DDATAPUMP_BUILD_FRAMEBUFFER_LIBRARY=ON
```

An embedding CMake project can link `datapump_framebuffer`, which propagates its
C++20 requirement; its public header is
`src/gui/framebuffer.hpp`. This target links the common application but neither
ncurses nor SDL. The CPU renderer has no FLTK, Rev or graphics-API dependency.
The SDL host uses window surfaces with framebuffer acceleration disabled. A host
window system may itself composite its desktop; Data Pump does not create an
OpenGL context or issue OpenGL drawing commands.

Prepared SDKs must contain the selected frontend dependencies. Explicit external
paths are checked against the SDK root; a missing recipe requires explicit base
maintenance before SDK release qualification. No existing release/base archive
is modified by enabling these local targets.

## Terminal operation

Use Tab/Shift-Tab to move focus, arrows within editors/lists/choices, and Enter to
activate the focused control. F1 displays help; F2/F3 provide Ctrl/Shift-Enter,
and F4 opens presets when modified arrow keys are unavailable. Preset selection is separate from
editing, and Escape dismisses a popup without replacing the draft. Mouse clicks
and wheel events are available when the terminal reports them. The terminal must
have an appropriate terminfo entry; local Linux-console mouse support additionally
depends on ncurses/GPM availability. A remote Linux VT does not automatically
forward its local GPM daemon over SSH. Use `ssh -t` for an interactive session.
The modem uses the machine running the program, including that machine's audio.

The display has a mandatory ASCII fallback. Received text still uses the stricter
shared allowlist. Other untrusted display text is converted to literal cells;
terminal controls cannot be introduced through labels, paths, errors or messages.
Only ncurses and the host's fixed terminal-control sequences can control the
terminal. Correctly framed bracketed paste is handled as a single text event.
The terminal protocol cannot authenticate an embedded paste-ending sequence;
the received-text policy removes such control bytes before copying or display.
Pastes exceeding the host's 4 MiB limit are rejected completely, with a visible
reason and without changing the editor draft or selection.
Terminals without bracketed-paste reporting cannot distinguish pasted keys from
typed keys; paste into an editor and use explicit action controls.

Terminal output is queued with a bounded nonblocking transport. Slow SSH output
does not stop application progress polling. Shutdown restores terminal modes on
normal exit and handled termination signals. Abrupt process termination cannot
run cleanup. Plot painting can drop intermediate display frames; receiver state
and accepted pending bits retain their original shared behavior.

Waterfall downsampling requests a full retained extent and peak-pools in both
dimensions, preserving brief carriers between destination samples. This is a
generic producer capability, not a terminal-side modem interpretation. Other
plots use shared opaque bitmap producers and respect sample aspect ratio.

## Framebuffer and engine integration

`framebuffer::Runtime` owns one application instance and a separate pixel UI.
The minimal host constructs one runtime and calls `update(width, height, events)`:

```cpp
#include "framebuffer.hpp"
using namespace datapump::gui;
framebuffer::Runtime panel;
// In the engine's update loop; events use framebuffer pixel coordinates:
auto image = panel.update(width, height, events);
// Upload image->pixels using image->width, height and stride_bytes to one texture.
```

The returned frame is complete. Resizing, widgets, popups, dialogs and plots all
remain inside this one surface. The host does not need per-widget callbacks,
multiple textures, a damage protocol or any SDL interface. Immutable frames can
be kept alive until an asynchronous texture upload completes. Advanced hosts can
use separate `resize`, `input`, `tick` and platform-service calls instead. Keep
polling at least every 40 ms even when texture uploads are slower. Calls on one
runtime are serialized; immutable frames may be retained by another thread.

Frames declare dimensions, stride, RGBA byte order, revision and damage. A frame
handle owns its pixels across later redraws, resizes and runtime destruction.
A host that skips a revision must copy the full frame. `copy_frame` also supports
padded BGRA8888 and RGB565 little-endian destinations with checked extents.

A VR/game host maps ray intersections on its interface surface into pixel
coordinates, passes those as pointer events and uploads frame pixels to a texture.
The host owns its game loop, GPU API, texture lifetime and any physical display
flush. SDL types and window/event-loop ownership never enter the embedding API.
The initial software rasterizer uses a small ASCII font; unsupported locally typed
Unicode glyphs appear as placeholders while text bytes remain in the model.
Framebuffer plots retain their pixel resolution and color; they are not ASCII art.

Clipboard/folder services are explicit host requests, carrying the same revocable
permission token as the existing adapters. Text/path prompts remain bounded.
The minimal `update()` interface automatically declines unsupported services and
shows the reason inside the framebuffer. Only advanced hosts opting into these
services need to check validity immediately before performing a request and
return a completion. No received attachment is opened or executed automatically.
The terminal has no automatic OSC clipboard export.

A framebuffer abstraction does not imply that the entire modem fits a particular
Arduino. For example, a 320x240 RGB565 buffer alone takes 150 KiB. Engine/embedded
hosts must qualify their display memory, scheduling, audio and modem resources.

## Packaging and verification

`./build.sh package --tui --fb` includes both optional executables and manuals in
the ordinary portable bundle. Distribution wrappers place them under the existing
`/opt/datapump/fltk/bin/` or `/opt/datapump/rev/bin/` bundle. The FLTK package owns
`datapump-tui-ncurses` and `datapump-fb-sdl`; a coinstalled Rev bundle uses
`datapump-tui-ncurses-rev` and `datapump-fb-sdl-rev`. This package ownership does
not introduce an FLTK/Rev dependency into either frontend. Historical release
payloads acquire no new commands unless they contain the corresponding binaries.

The `frontends` test group covers independent UI/renderer behavior and, when
selected, terminal PTY transport and SDL/headless launch. The existing shared GUI,
wire-vector, physical-end, pending-bit, security, build and packaging checks remain
required. The architecture guard traverses the new implementation files and
rejects domain bindings below the facade or native-toolkit dependencies above it.
No test result for one frontend implies qualification of another host/platform.
The PTY fixture checks Linux-console terminfo and keyboard behavior with GPM
probing disabled: a pseudo-terminal cannot provide a physical VT/GPM session.
Physical console mouse integration requires separate qualification. On the
development host, enabling the probe without that device exposed a 10-byte
libgpm [console-name allocation](https://github.com/telmich/gpm/blob/master/src/lib/liblow.c)
retained across a failed probe and library unload;
the production adapter's GPM discovery remains enabled.

## Alternatives and replacement boundaries

ncurses was chosen for its conservative terminal coverage and optional local
[GPM mouse integration](https://invisible-island.net/ncurses/man/curs_mouse.3x.html).
[FTXUI](https://github.com/ArthurSonzogni/FTXUI) offers modern C++ components;
[Turbo Vision](https://github.com/magiblot/tvision) offers a larger widget toolkit
over ncurses on Unix. [Notcurses](https://github.com/dankamongmen/notcurses)
offers richer terminal graphics, with a larger terminal-capability surface to
qualify. None removes the need to test keyboard-only Linux consoles, SSH and
literal output handling. Replacing ncurses affects only the terminal side.

SDL2 is the initial framebuffer window/input host.
[MiniFB](https://github.com/emoon/minifb) is a smaller alternative; its OpenGL
option must be disabled for a software-only host. A direct Linux DRM/KMS or
fbdev host plus evdev/libinput, or a platform-native bitmap host, can implement
the same input/frame exchange. Game engines bypass all these window hosts.
[LVGL](https://github.com/lvgl/lvgl) and
[Nuklear rawfb](https://github.com/Immediate-Mode-UI/Nuklear/tree/master/demo/rawfb)
are alternatives to the widget/raster implementation, not requirements of the
embedding interface. The initial implementation uses project-owned software
widgets and rasterization, keeping the complete framebuffer under one owner.
