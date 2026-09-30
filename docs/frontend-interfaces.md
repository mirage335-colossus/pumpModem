# Terminal and framebuffer interfaces

Data Pump has three independent interactive interface choices. `datapump-gui`
uses FLTK or Rev. `datapump-tui` is a terminal application for SSH and local
consoles. `datapump-fb` presents a software framebuffer through SDL2; the same
framebuffer library can be embedded in an engine without SDL or a native window.

The terminal and framebuffer implementations are deliberately separate below
`Application`. The terminal projects shared application rectangles into cells and uses colored ASCII plot sampling. The
framebuffer uses pixel layout, software widgets, full-resolution plot sources and
pointer input. Neither implements modem functionality. Adding ordinary fields,
actions and documents to the shared declarations reaches both interfaces without
field-specific adapter code. Shared slot positions, declaration rows and stretch
weights also reach the terminal: a generic cell projection preserves geometric
reading order and relative widths, measures label height, and wraps neighbors
when their minimum usable cell widths do not fit. It retains the editor identity
through reflow. Terminal documents use the common document layout engine with
cell metrics, including declared padding, margins, heights and clipping; the
page itself participates in shared desktop placement. Narrow document rows wrap
when the next fixed width or minimum remaining width cannot fit. This adaptation
is deterministic and contains no modem-specific layout rules.

A new generic presentation primitive requires
support in each relevant renderer, as it does for FLTK and Rev.

## Building

On POSIX systems, install ncurses development files for the terminal application, and SDL2
development files for its framebuffer host. On Debian these are `libncurses-dev`
and `libsdl2-dev`. They are optional; ordinary GUI/CLI builds do not require them.
Windows uses its native console API for the TUI and SDL2 for the framebuffer;
the prepared Windows dependency recipe supplies SDL2. Build configuration never
downloads dependencies.

```sh
./build.sh --cli --tui --fb
./build/dev-cli-tui-fb/datapump-tui --simulation
./build/dev-cli-tui-fb/datapump-fb --simulation
./build.sh test frontends --cli --tui --fb
```

Omit `--cli` to also build the selected desktop GUI. `--backend fltk|rev` selects
only that GUI; it never selects TUI or framebuffer behavior. The corresponding
CMake options are `DATAPUMP_BUILD_TUI`, `DATAPUMP_BUILD_FB`,
`DATAPUMP_TUI_BACKEND=auto` and `DATAPUMP_FB_HOST=sdl`. `auto` chooses `ncurses`
on POSIX and `winconsole` on Windows; either may be selected explicitly on its
matching platform. Windows binaries are `datapump-tui.exe` and `datapump-fb.exe`.
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

The display has a mandatory ASCII fallback. QR images use fixed half-block glyphs
on capable terminals, preserving two module rows per terminal row and a complete
quiet zone. The ASCII fallback uses two character columns per module. If the
available area cannot hold every module, the plot asks for more space instead of
showing a misleading uniform image. Binary terminal QR rendering uses full
contrast; the explicit QR off setting still blanks the image.
Received text still uses the stricter
shared allowlist. Other untrusted display text is converted to literal cells;
terminal controls cannot be introduced through labels, paths, errors or messages.
Only the host renderer's fixed glyphs and terminal-control sequences can control
the terminal. The Windows host writes literal console cells. Correctly framed
POSIX bracketed paste is handled as a single text event.
The terminal protocol cannot authenticate an embedded paste-ending sequence;
the received-text policy removes such control bytes before copying or display.
POSIX framed pastes exceeding the host's 4 MiB limit are rejected completely, with a visible
reason and without changing the editor draft or selection.
Terminals without bracketed-paste reporting, including the Windows console host,
cannot distinguish pasted keys from typed keys; paste into an editor and use
explicit action controls.

The POSIX ncurses host queues output with a bounded nonblocking transport, so
slow SSH output does not stop application progress polling. The Windows host
writes a bounded surface through the native console API; blocked-reader behavior
through ConPTY remains unqualified. Shutdown restores terminal modes on normal
exit and handled termination signals. Abrupt process termination cannot run
cleanup. Plot painting can drop intermediate display frames; receiver state and
accepted pending bits retain their original shared behavior.

Waterfall and constellation cells retain producer colors where the terminal
supports them, with a grayscale ASCII fallback for monochrome terminals.
Waterfall downsampling requests a full retained extent and peak-pools in both
dimensions, preserving brief carriers between destination samples. This is a
generic producer capability, not a terminal-side modem interpretation. Other
plots use shared opaque bitmap producers and respect sample aspect ratio.

## Framebuffer MFD / AMPCD prototype

`datapump-fb` opens with MFD labels and clickable numbered keys along the
**right edge only**. Labels sit inward of their keys; the other three edges have
no bezel controls. Color is the default (AMPCD, Advanced Color Multi Purpose
Display); `--monochrome` selects grayscale, and `--no-mfd` removes the bezel.
`--mfd-buttons 3` selects three keys; the default is five keys total. Click a
numbered key or press F5 through F9 (F5 through F7 in three-key mode).

The fixed keys and changing, abbreviated labels follow the simulated-aircraft
MFD convention. The backend offers two banks of Console operating functions:

| Bank | Five-button behavior |
| --- | --- |
| TUNE | 1 changes bank; 2/3 choose a setting; 4/5 select the previous/next offered value, previewed by DEC/INC labels. |
| ACTIONS | 2/3 choose an action; 5 (EXEC) executes it. Button 4 is inactive. Browsing never executes an action. |

TUNE contains the modem selector, the selected modem's rate/profile, carrier,
SNR targets or squelch where available, and actual TX audio volume. Robust Rate
starts at 3.6 kHz. ACTIONS contains Transmit, Transmit noise where supported,
Cancel TX and Clear received where available. Abbreviations include TX, RX,
Exp SNR, Chan profile and Mdm. The header identifies the selected function and
its accepted value. Numeric presets are ordered numerically; other options keep
their declared order. Endpoints stop; the backend never invents a setting value.

The bezel does not offer application-tab navigation or inspection views. It also
omits previous-message paste, attachments, Use text, clipboard/file operations,
waterfall clearing and zoom, simulation, link-budget power/loss/noise assumptions,
oscillator models, audio routing, QR brightness and key-management setup. These
remain available through the ordinary GUI or preconfiguration. Noise-floor
planning is separate from the retained Transmit noise operation. Changing banks
only changes bezel functions; it does not switch the application's tab.

With three keys, 1 cycles individual functions through TUNE and ACTIONS; 2/3
adjust a setting, and 3 executes an action. All three keys stay on the right.
This trades more navigation presses for fewer physical switches. Ordinary text
editing and occasional setup remain available. During help, a popup, a modal
application overlay or a host-service dialog, background operations are inactive.
Key 1 becomes BACK for help/popups/services or ESC for an overlay that declares
an Escape binding. It follows shared modal policy. A stale adjustment or action
is discarded if a refresh replaces its displayed binding or target value.
Labels and unused bezel areas never activate anything.

Below 480 by 320 pixels, or when a large selected font cannot fit the keys, the
bezel requests enlargement and disables its keys. The full GUI is **not** a
watch-size layout. Its normal panning/focus behavior remains available when the
area to the left of the bezel is smaller than the desktop layout.

This is a prototype for future display-hardware ports or simulated VR use,
not an Arduino modem implementation. Its other purpose is to demonstrate the
application's functionality through a consistent, reusable interface that other
applications can follow. All MFD configuration, rings, hit testing, page/function
selection, abbreviations and preset navigation belong to the framebuffer backend.
Shared declarations describe backend-neutral control purposes (operating
parameter, operation, setup, content, planning or presentation); they contain no
MFD flags, button positions, banks or abbreviated labels. The framebuffer chooses
which purposes to expose and invokes existing opaque, scoped callbacks, without
modem-field lookup, label-based routing or radio calculations. Ordinary application
maintenance requires no MFD-specific code. Setup menus and arbitrary text entry
remain outside the bezel's operating subset.

The intended derivatives are inexpensive embedded radios that seldom have a
touchscreen or keyboard/mouse: a watch-like news receiver, a configurable radio
used to retune from a new location, or a device with only two or three switches.
A physical two-button adaptation can use the same function-cycling pattern;
this prototype implements three and five. Typical Arduino-class projects would
implement only a high-SNR Fast Modem for wired links, a transmit-only Robust Modem
for sensor reports, or a specialized Robust Modem with narrowly limited listening
and transmitting, such as a slow mesh repeater. They generally lack the compute
for much of Data Pump's receive processing. A receiver/repeater may need no text
entry at all; otherwise occasional keyboard/mouse setup is reasonable.
Encryption and other deployment choices can be configured beforehand by command
line, ordinary GUI, firmware defaults or EEPROM in the derivative. Avionics-style
presentation does not imply suitability for fast-moving vehicles: these modems
intentionally lack Doppler tracking.

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

`framebuffer::Config` defaults to `mfd=true`, `mfd_buttons=5`, and `color=true`.
Set `mfd=false` for the unframed layout. An embedded host can call
`Runtime::press_mfd_button(number)` with the displayed 1-based button number,
then `update`/`tick`; this needs neither mouse coordinates nor a keyboard. Debounce
physical switches in the host and dispatch once per press.

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
The software rasterizer uses an antialiased DejaVu Sans Mono atlas, with a 13px
default font and shared native widget colors. Dropdown arrows and checkmarks are
drawn as geometry. Unsupported locally typed Unicode glyphs appear as placeholders
while text bytes remain in the model.
Framebuffer plots retain their pixel resolution and color; they are not ASCII art.
Waterfall startup leaves the unused history area blank and fills it progressively,
using the same viewport scale as FLTK and Rev.

The committed atlas comes from the existing Rev font resource (DejaVu Sans Mono
Book 2.37, despite its `DejaVuSans.ttf` filename). Its source checksum, FreeType
version and raster settings are recorded in `src/gui/framebuffer_font.hpp`;
`tools/generate-framebuffer-font.cpp` reproduces it offline. Ordinary builds and
embedded hosts need neither FreeType nor Rev. Framebuffer distributions include
`share/doc/datapump/framebuffer/DejaVu-LICENSE`; derived embeddings must retain
that notice with the atlas.

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

`./build.sh package --tui --fb` includes both executables and manuals in
the portable bundle. New release and package-manager builds enable both, and
verify their binary/manual inventory. Distribution wrappers place them under the existing
`/opt/datapump/fltk/bin/` or `/opt/datapump/rev/bin/` bundle. The FLTK package owns
`datapump-tui-ncurses` and `datapump-fb-sdl`; a coinstalled Rev bundle uses
`datapump-tui-ncurses-rev` and `datapump-fb-sdl-rev`. This package ownership does
not introduce an FLTK/Rev dependency into either frontend. Historical release
payloads acquire no new commands unless they contain the corresponding binaries.
Portable ncurses packages carry common Linux-console, xterm, screen and tmux
terminfo descriptions alongside the executable, retaining normal user/system
lookup for other terminal types. New SDK and Windows base recipes include the
frontend dependencies; older published recipes remain immutable and require
explicit base maintenance before building the new release configuration.

The `frontends` test group covers independent UI/renderer behavior and, when
selected, terminal PTY or Windows-console transport and SDL/headless launch.
CI schedules this group in independent jobs alongside the existing regressions,
including SDK and Windows builds. The existing shared GUI,
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
