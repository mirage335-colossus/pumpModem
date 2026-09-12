# Proposed minimal GUI contract

Status: the single-backend build option and shared monochrome FLTK theme are
implemented. The semantic declarations and alternative adapters below remain a
design draft; the application currently uses FLTK. The common API declares semantic controls:
choices/dropdowns, tabs, text fields, actions, and lists. Adapters map them to
existing backend widgets. Bitmap output is a separate path for plots and images.

The maintenance rule is: adding or moving an ordinary control changes its
declaration and, for new functionality, its state/action mapping. It does not
require reading or editing a backend. Keep the vocabulary small and keep its
meaning consistent across desktop and compact layouts. Share declarations,
application state, validation, and actions once. Reuse each backend's existing
widget behavior; do not duplicate it in application code or a universal widget
renderer. Only an adapter for a target without a suitable widget library needs
its own small widget implementation.

## Declaration vocabulary

Use compiled, typed C++ declarations with stable helper signatures. No runtime
markup parser, generic property dictionary, or toolkit-specific properties are
needed. One short screen declaration should show its hierarchy and bindings.

| Kind | Meaning |
| --- | --- |
| Label | Read-only text. |
| Action | Invoke a named application operation. Usually a button. |
| Toggle | Edit a boolean value. |
| Choice | Select one stable option ID from a closed set. |
| Text | Edit UTF-8 text; single/multiline and optional preset suggestions. |
| List | Select a record by stable ID; activation is a separate action. |
| Bitmap | Display a named graphical source; pixel displays map samples 1:1. |
| Row / Column | Lay out children in declaration order. |
| Tabs | Display one named page; pages have IDs and titles. |
| Scroll | Provide vertical scrolling for one child. |

## Consistent presentation

Every backend follows the same early-computing instrument-panel style: black
backgrounds, grey reference marks, bright data, monospaced labels, square borders,
compact spacing, and explicit text status. The intensity roles are defined once
in `src/gui/theme.hpp`; adapters map them through existing widget styling. A
monochrome terminal may use stippling or reverse video where grey is unavailable.
Focus and selection remain clearly visible through the backend's usual behavior.

The style applies to controls as well as plots, without recreating controls in a
shared framebuffer. No decorative motion, glow, shadows, or animated ticker is
required. A waterfall advances only to display signal history. QR codes retain
black modules on a white background. Signed data needs an explicit midpoint or
other distinguishable marks and a legend; status never depends on color alone.
Physical glyph shapes and native popup details may differ while retaining these
presentation rules. A control declaration does not repeat styling properties.

## Screen declaration

An illustrative declaration, not an implemented API:

```cpp
page(Page::console, "Console",
  column(
    row(
      choice(Field::source, "Source", Options::sources),
      text(Field::bandwidth, "Bandwidth",
           suggestions(Options::bandwidths))),
    text(Field::message, "Message", multiline(6)),
    row(
      action(Command::transmit, "Transmit"),
      action(Command::cancel, "Cancel TX")),
    bitmap(PixelSource::waveform, grow(1))))
```

Reordering children moves controls. Bindings identify application values and
commands; labels are literal text. A keyfile command menu is a presentation of
Actions, not a Choice whose selected value unexpectedly executes a command.
Ordinary control identity derives from its page, binding kind, and field/command/
source ID. Do not repeat a separate widget ID for the same binding. Only a repeated
binding within one page needs an explicit stable instance suffix. CLI settings
reuse field metadata, defaults, and parsers independently of page identity.

## Behavior that every adapter preserves

1. **Stable identity.** Controls derive stable IDs from their bindings as above;
   options, pages, and list records have explicit IDs. Labels, positions, and
   array indices never identify application values.
   Duplicate IDs in their declared scope are errors. Literal labels must not be
   interpreted as toolkit menu paths, accelerators, or formatting syntax.
2. **State and input.** Application state is authoritative. Applying state never
   emits an input event. User events are delivered serially to the controller:
   Activate, Toggle, Select, Edit, Submit, and SelectPage. Select reports a
   committed selection change. Repeating the current selection need not emit an
   event; acknowledgement or retry is a named Action, rather than a requirement
   to emulate selection gestures that a backend's widget does not report.
3. **Collections.** Replacing options or records preserves selection by ID when
   possible; otherwise selection is empty until the controller chooses a value.
   No implicit selection of the first item. Disabled items cannot be newly
   selected by user input; an existing selection or controller update is allowed.
4. **Text.** Choice stays closed; editable presets belong to Text. A suggestion
   replaces the field's text and emits Edit only if the text changed. Text
   declares a UTF-8 byte limit;
   over-limit edits are rejected with feedback, never silently truncated. Edit
   reports text changes; Submit is explicit. Multiline fields support newline
   entry and submission through separately reachable actions. Enter/Ctrl+Enter/
   Shift+Enter bindings are backend conveniences where distinguishable, not
   requirements on terminal or MCU input. Validation and numeric/unit parsing
   live in shared application code.
5. **Layout.** Row/Column use ordered children, minimum sizes, standard gaps, and
   integer stretch weights. Backend font measurements inform minimum sizes.
   Declaration order also defines focus traversal. Hidden children occupy no
   space; disabled children retain space and produce no input actions. Tiny
   displays use an explicit compact layout; controls are not scaled below usable
   sizes. The application does not specify pixel coordinates for ordinary controls.
6. **Ownership.** Static declarations live for the UI lifetime. Dynamic update
   data is borrowed only during the update call; adapters copy anything retained.
   Event data remains valid for its dispatch call; the controller copies anything
   retained. Apply changes without recreating unchanged controls or resetting an
   editor's focus, cursor, selection, or scroll position. A changed text value may
   clamp positions to valid boundaries. Toolkit objects stay inside adapters.
7. **Capabilities.** Backend availability and profile requirements are checked
   before startup. An unsupported operation needs a declared alternative or a
   visibly unavailable action. Required controls cannot silently disappear.
   Preserve values and actions, not identical appearance or physical gestures.

Optional platform services such as file pickers and clipboard operations are
requested by application commands, outside the declaration vocabulary. Bitmap
sources must not become an escape hatch for implementing ordinary controls that
bypass this contract.

## Widget adaptation

An ordinary screen declaration never contains dropdown drawing, tab hit testing,
popup management, caret drawing, or toolkit-specific event handling. Those stay
inside the selected widget library and its thin adapter. A small composition of
existing controls is appropriate when there is no exact one-to-one equivalent.

| Backend | Implementation of semantic controls |
| --- | --- |
| FLTK / wxWidgets / LVGL | Corresponding library widgets and containers. |
| HTML / JavaScript | DOM controls such as `select`, `input`, `textarea`, and `button`; tab/page composition in the adapter. |
| ncurses | Menu/form facilities for selection and editing; small adapter composition for dropdown presentation and tabs/pages. |
| SDL | An existing widget library hosted by SDL, not an application-owned SDL widget engine. |
| Primitive-only MCU display | A compact widget implementation private to this adapter, only if no suitable existing library fits. |

HTML defines ordinary [form controls](https://html.spec.whatwg.org/multipage/form-elements.html)
and the [tabs pattern](https://www.w3.org/WAI/ARIA/apg/patterns/tabs/) supplies a
page interaction model. ncurses supplies [menus](https://invisible-island.net/ncurses/man/menu.3x.html)
and [forms](https://invisible-island.net/ncurses/man/form.3x.html); neither mapping
requires application code to rasterize its controls. The primitive-only widget
implementation is linked only for targets that need it.

## Bitmap output contract

The backend receives only opaque pixel rectangles. Shared application code turns
waveform, spectrum, constellation, or QR data into pixels. The backend needs no
knowledge of traces, axes, points, fonts, or diagrams. There is no drawing-command
API, alpha blending, scaling, interpolation, or general custom-widget callback.

Illustrative output interface:

```cpp
enum class PixelFormat { gray8, mono1 };
struct PixelBlock {
    unsigned width, height, stride_bytes;
    PixelFormat format;
    const unsigned char* pixels;
};
void blit(Id bitmap, unsigned x, unsigned y, PixelBlock block);
```

- Coordinates start at the bitmap's upper left, in its advertised sample grid.
  On pixel displays this is actual drawable pixels, including on high-DPI
  desktops, and `blit` replaces an in-bounds rectangle 1:1. A terminal adapter
  instead encodes the plot samples into character cells; physical pixel equality
  is not promised. This adaptation applies only to Bitmap, never to controls.
- `gray8` is one intensity byte per pixel: 0 black, 255 white. `mono1` packs
  eight pixels per byte, leftmost pixel in the most significant bit, 0 black,
  1 white. Rows start `stride_bytes` apart; unused end bits and padding are
  ignored. Stride is at least the packed row size and storage covers every row.
- Adapters accept both formats and convert to the display's native encoding.
  Monochrome output thresholds gray8 at 128. A monochrome bitmap source uses
  dotted white crosshairs to keep them visible instead of relying on grey.
- Pixel storage is borrowed until `blit` returns. The backend consumes or copies
  it before returning; asynchronous hardware transfers use backend-owned bounded
  storage or finish before return.
- On repaint, the backend supplies the actual bitmap width/height and damaged
  rectangle, sample aspect ratio, and whether output is monochrome. Shared code
  uses these to preserve I/Q geometry and choose visible reference marks, renders
  that region using one consistent plot snapshot, and emits one or more pixel
  blocks. Resize or exposure can request a complete repaint. Retain enough
  source/history to regenerate it. A terminal adapter requests damage aligned to
  complete character cells, or retains the other samples of partially updated
  cells. It declares its cell geometry instead of assuming square samples.

A pixel block can be the entire image, a tile, or one scanline. A permanent full
framebuffer and atomic whole-frame presentation are not requirements. For a
128-by-64 image, gray8 is 8 KiB, mono1 is 1 KiB, and one gray8 row is 128 bytes.
Tiling saves working memory; waterfall scrolling still requires transferring the
affected pixels unless the backend independently optimizes display scrolling.

The minimal shared bitmap producers are:

| Source | Pixels |
| --- | --- |
| Waveform | Black background, optional grey baseline, white trace/envelope. |
| Waterfall | Rows of grayscale intensities using a common signal-level scale. |
| Constellation | Black background, grey crosshairs, white point pixels. |

Keep measured values and a common I/Q scale when mapping points to pixels;
simplifying appearance must not normalize each point or erase amplitude meaning.
Titles, units, status, and inspection explanations use ordinary labels/lists.
Zoom, reset, clear, and diagram navigation use ordinary controls, keeping Bitmap
output-only. Shared code may rasterize additional static images through the same
pixel contract without adding backend methods.

## One backend per build

Candidate adapters are FLTK, wxWidgets, LVGL, ncurses, HTML/JavaScript, SDL with
an existing widget library, and a primitive-only MCU adapter. All expose the
same semantic contract. A browser adapter's hosting/transport is a separate
implementation decision; it does not introduce a second UI definition.

The implemented CMake cache option selects exactly one backend:

```sh
cmake -S . -B build-native -DDATAPUMP_GUI_BACKEND=fltk
cmake --build build-native --target datapump-gui
./build-native/datapump-gui --version
```

`fltk` is the default and currently the only implemented value. An empty,
multiple, or unavailable selection fails configuration with the available choice.
`DATAPUMP_BUILD_GUI=OFF` skips the backend and its dependencies altogether.
Backend-independent GUI model tests remain available in CLI-only builds.

`cmake/NativeGui.cmake` dispatches only the selected backend; its module owns its
toolkit configuration, sources, libraries, and notices. Each build directory
contains at most one GUI backend, including desktop builds. No runtime selector,
backend registry, or plugin loader is needed. `--help` and `--version` identify
the compiled backend. Future runtime settings can choose page, layout, or plot
cadence within that backend, but cannot switch toolkits.

Use one default backend for each supported platform/profile and separate build
directories for exceptional targets. Keep FLTK for the current desktop Linux and
Windows releases; ncurses is the proposed SSH profile, and LVGL is a candidate
for framebuffer/MCU targets. An SDL profile must name its accompanying widget
library. These alternatives are not currently buildable.

The initial supported rendering configuration should avoid application GL/EGL
contexts. For the SDL window-surface path, disable
[framebuffer acceleration](https://wiki.libsdl.org/SDL3/SDL_HINT_FRAMEBUFFER_ACCELERATION)
before acquiring the surface. The operating system's compositor/display path
remains a separate deployment concern.

GUI launch settings should reuse the same typed setting definitions, defaults,
and validators as the controls. Command-line initialization updates shared state;
it must not simulate clicks or implicitly activate Transmit. Introduce individual
setting switches when their shared bindings exist.

## First implementation boundary

Extract the existing FLTK controller and declarations first; preserve application
behavior while simplifying plots to bitmap sources and exposing their existing
zoom/reset/navigation operations through ordinary controls. Move the existing
keyfile-failure acknowledgement that relies on reselecting a choice to an explicit
Action. Then implement a second adapter and confirm that moving/adding a control
touches no adapter code.
Test stable selection after reorder/removal,
literal labels, silent state updates, explicit acknowledgement, text submission,
visibility, and unavailable backend handling. Check pixel formats, 1:1 placement,
buffer lifetimes, and identical complete-image versus tiled repaint output.
Freeze the first contract only after both adapters demonstrate the same semantics
in separate builds. The source review and staged extraction plan are in
[GUI architecture](gui-architecture.md).

Keep the declaration vocabulary and one complete example together in a short
public header. Separate the screen declarations, application action/state
mapping, and backend internals. A routine GUI edit should need the first two
plus that header; graphics implementation details should not enter its context.
