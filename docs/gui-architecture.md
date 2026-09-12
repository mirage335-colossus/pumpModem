# GUI architecture review

The recommended approach is an application-specific semantic interface, built
around the existing FLTK controls first. Keep one compiled backend, reusable
application state/actions, a short screen definition, and shared bitmap plot
producers. Match the grayscale instrument-panel presentation and optional color
accents across adapters through widget styling. Do not implement a general-purpose
widget toolkit.

The build selector, shared presentation roles and bitmap producers, semantic
screen declarations, toolkit-free controller and opt-in Rev adapter are
implemented. Rev uses the shared controller and declarations. Existing FLTK plot
widgets use the shared pixel producers while retaining native captions and
interactions; FLTK's controller and procedural screen migration remain.
See [Rev backend](rev-backend.md) for the pinned `clean` revision, build profile
and validation boundaries. The design below includes the extraction already
completed for Rev and the remaining migration work.

## What the project already separates

| Existing code | Reuse |
| --- | --- |
| `include/datapump/live.hpp`, `src/live.cpp` | Session commands, audio/modem workers, event and plot snapshots. No GUI objects. |
| `src/gui/state.hpp/.cpp` | Inbox, signal records, exact binary input, transmission cooldown, replay policy. |
| `src/gui/inspection_model.hpp/.cpp` | Structured processing lanes, fields, sections, constellations, and transmission estimates. |
| `src/gui/plot_data.hpp` | Width-bounded waveform reconstruction, peak-preserving reduction, common-scale spectrum history. |
| `include/datapump/tuning.hpp`, `src/tuning.cpp` | Shared modem planning and presets. |

The main coupling is in `src/gui/main.cpp`: about 1,360 lines originally combined
widget creation, fixed coordinates, settings, input buffers, async preparation,
reception updates, platform services, and several hundred lines of smoke tests.
`settings()` and `message()` read widgets directly. `update_controls()` combines
application eligibility with toolkit calls. Merely wrapping widget construction
would leave application state dependent on FLTK.

## Small, independently compiled modules

Current and planned ownership:

| Module | Responsibility |
| --- | --- |
| `ui_contract.hpp`, `bitmap.hpp` | Small IDs, control records, state/service records, pixel blocks. No toolkit, crypto, or modem headers. |
| `screen_console.cpp`, `screen_inspection.cpp` | Ordered declarations and bindings; inherit the shared style. |
| `controller.hpp/.cpp` | Authoritative values, commands, validation, selection policies, availability, and notices. |
| `controller.cpp` preparation workers | Debounce, workers, cancellation, and revisioned results; a separate `preparation.cpp` remains optional. |
| `plot_render.cpp` | Shared monochrome raster generation and optional private point/segment helpers. |
| `backend_rev.cpp`, `rev_platform.cpp` | Rev elements, event translation, presentation, bitmap transfer, and native services. |
| `main.cpp`, `bitmap_fltk.hpp` | Existing FLTK widgets/controller and shared bitmap transfer; semantic migration remains. |
| Future `gui_options.cpp` | Launch settings using shared parsers and validation. |
| `gui_smoke.cpp` | Existing workflow checks separated from production control flow. |

Use plain compiled records and a few functions; avoid a deeply templated builder,
runtime markup interpreter, arbitrary property map, or dynamically loaded plugin
ABI. Put sizeable drawing implementations in `.cpp` files rather than headers.
This makes screen edits local, limits recompilation, and lets AI read the small
contract plus one screen and its relevant action instead of the whole GUI.

One event sink and one source of authoritative state are sufficient. Creation is
retained; updates change existing controls without resetting focus, selection,
editor scroll, or cursor. An adapter owns its widget mapping and suppresses
callbacks during programmatic updates. Display changes must never restart the
receiver. All workflow validation stays in the controller.

Use the existing toolkit to implement Choice, Text, Tabs, and other semantic
controls. Compose existing facilities inside an adapter when an exact widget is
missing. Only a primitive-only target that cannot use a suitable library needs a
private miniature widget implementation. The common screen has no hit testing,
popup drawing, text caret handling, or backend branches.

## Behavior that must survive extraction

- Preserve inactive message/binary editors. Only the selected source is parsed
  or transmitted; exact bits and leading zeros are meaningful.
- Keep editable draft field text separate from the last valid modem settings.
  Invalid edits stay visible with feedback and disable relevant actions without
  resetting the running receiver or silently restoring old text.
- Keep raw completed bits distinct from verified packets and their copy/save
  eligibility. Monochrome appearance retains explicit status labels.
- Move FLTK ampersand/menu escaping out of `key_choice_labels()` in shared state
  and into the adapter. Keys and list items need stable identity independent of
  labels and menu indices.
- Make key/file defaults explicit controller policy. Today key loading selects
  the first entry; file refresh preserves selection or selects the newest item.
- Replace the current key-load-failure acknowledgement by reselecting an unchanged
  menu item with a named action. This avoids forcing every widget implementation
  to detect gestures that its normal change event does not report.
- Preserve the current 120 ms estimate debounce and rejection of stale results.
  Layout, tab changes, or applying state must not emit settings changes.
- Request file selection, prompts, clipboard, and folder opening through small
  platform services. Use request IDs and later result events rather than requiring
  nested event loops. An SSH path belongs to the modem host.
- Retain the payload behind a pending Save request even if reception evicts it
  from the inbox. The current save path copies it before entering a modal chooser;
  a future immutable handle can retain that protection with fewer copies.

## Plot and inspection migration

The bitmap contract describes pixels, not UI widgets. Rev exposes
zoom/reset/clear/pagination as ordinary declared controls. Shared renderers are
pure operations over immutable snapshots and support tiled repaint. FLTK's
`LivePlot`, `Waterfall`, and `PatternSpaceView` use those renderers while retaining
their existing mouse gestures and native navigation; migrating those interactions
to the declarations remains.

Inspection already supplies structured content. Present its prose and tables
through ordinary labels/lists, and rasterize only actual plots. Rasterizing the
entire inspection page would unnecessarily duplicate text rendering and make
terminal adaptation harder.

Point and straight-segment helpers can produce the early-computing appearance
inside `plot_render.cpp`; the backend still receives only pixel rectangles.
Waterfalls remain rows of intensity samples. All adapters use the same measured
scales and sign conventions. Terminal output maps plot samples into cells with
explicitly reduced fidelity, while controls remain native terminal text.

The grayscale basis uses shared intensity roles, monospaced fonts, flat
borders, stationary short signal rows, small white constellation marks, and a reusable
one-byte-per-pixel waterfall buffer. Signed pattern cells use a bipolar grayscale
and revised legends so removing hue does not remove sign. The Normal QR setting
retains its original contrast.
Native widgets continue to handle input and editing.

Color is enabled by default when supported. `--monochrome` selects grayscale;
`--color` re-enables color, and the last of these switches wins. Color gives data
field values, waveform traces, and every constellation point the same fixed muted
cyan tint, and softens neutral text through the adapter's text role. The original
grayscale roles and scalar intensities remain unchanged. Reference marks, status,
and signed pattern diagrams remain grayscale. The waterfall uses a
fixed muted multihue lookup table over the same Gray8 intensities: black, dark blue,
blue, cyan, green, yellow, orange, red, then soft off-white. Shared producers emit
bounded rows. At ordinary scale FLTK batches up to 16 rows per native image
operation; at high DPI it assembles a physical-pixel backing image. Rev copies
the result into an opaque RGB texture.

The QR preview has a separate brightness selection: Normal, Dim, Dark, or Off.
Dark is selected on every startup. Dim and Dark use backgrounds of RGB (64, 0, 0)
and (32, 0, 0) when color is enabled, or grayscale levels 64 and 32 otherwise.
Off paints the preview black. The choice persists across text edits without
changing the encoded matrix, module geometry, or other widgets. Normal restores
the original white background and gray border; dim previews omit the border to
avoid a bright frame.

Color rendering and the QR brightness choice use existing control kinds. The
implemented bitmap contract retains mandatory Gray8/Mono1 and adds only optional
packed RGB24. Its target capability defaults to false; shared producers emit
RGB24 only when color is enabled and supported. Other targets keep the original
gray/mono representation instead of desaturating false color. Use bounded rows
or tiles for RGB's extra transfer bytes; no palette management API is needed.

Long signal rows retain their existing scrolling so pending/unverified content
remains readable. Verified text and completed bits can still be copied in full.
A semantic detail view or scrollable text control should replace this overflow
presentation during extraction; the current signal widget has no detail view.

## Responsiveness and memory

`Session::snapshot()` currently drains reception events and copies current plot
vectors under its mutex. The GUI polls every 40 ms. Rev retains plot history at
that cadence and presents state/plots every 100 ms; native input repaints directly.
Throttling the entire poll
because plots are hidden or slow would also delay received content. Keep event
consumption independent of drawing cadence; eventually expose sequence-aware or
immutable plot snapshots to avoid redundant copies.

Plot snapshots now share immutable retained data, and producers preserve min/max
envelopes and bounded reconstruction instead of dropping arbitrary samples. Rev
defers hidden rasterization and reuses unchanged textures. Continue to bound
retained source data and reuse transfer buffers. A changed shared waterfall scale
must recolor its history consistently.

The spectrum history currently holds up to 256 by 160 doubles, about 320 KiB
before container overhead. Tile output alone does not make that an MCU-sized
model. History dimensions, content limits, and plot cadence need explicit profile
budgets. Waterfall scrolling can also change most display cells despite only one
new input row, so bound terminal output and coalesce obsolete plot frames while
preserving application events.

An MCU graphics backend does not port the modem engine: C++20 threads, OpenSSL,
the default 64 MiB DSP budget, and 128 MiB production key generation remain
separate constraints. A display client for a modem host is a distinct future
transport project, not part of this UI refactoring.

## Backend choice and build cost

Keep `DATAPUMP_GUI_BACKEND=fltk` as the default for current Linux/Windows desktop
packages. The toolkit is vendored, compiled without GL/Cairo/Pango/Wayland, and
already exercised by native and relocation tests. This Linux profile requires
X11 or XWayland; it is not a bare-framebuffer or Wayland-only solution.
[FLTK's platform documentation](https://www.fltk.org/doc-1.4/intro.html) supports
the broader portability rationale; the particular configuration is this project's.

`cmake/NativeGui.cmake` configures only the selected adapter. `GuiFltk.cmake` owns
FLTK source/link settings and its license. Empty, multiple, and unimplemented
backend selections fail configuration. `DATAPUMP_BUILD_GUI=OFF` removes GUI
dependencies while retaining backend-independent model tests. `--help` and
`--version` report the compiled backend; no runtime registry is needed.

Rev is the second adapter, built in a separate directory. Its retained controls
and texture path test semantic control and bitmap boundaries with a different
desktop toolkit. Software OpenGL is permitted for this profile and must be
measured on deployment hardware. ncurses remains an informative SSH candidate:
it tests semantics and keyboard operation without relying on pixel equality.
Its [menus](https://invisible-island.net/ncurses/man/menu.3x.html)
and [forms](https://invisible-island.net/ncurses/man/form.3x.html) supply existing
selection/editing behavior. Future framebuffer/MCU profiles can use LVGL; SDL
must be paired with an existing widget library. HTML uses DOM controls, with a
separate choice of hosting and communication mechanism.

A future wxWidgets build may need its own packaging dependency policy:
`tools/verify-linux-abi.cmake` currently rejects GTK/GLib for the FLTK package.
Do not weaken that existing check for an adapter that is not being compiled.

## Implementation sequence and acceptance

1. Implement one-backend configuration and shared presentation roles with optional
   color (done).
2. Extract authoritative state, workers, and platform requests (done for Rev),
   while preserving the existing FLTK workflows and tests.
3. Replace procedural construction/layout with small screen declarations and
   native-widget mappings; separate smoke orchestration (done for Rev).
4. Extract pure pixel producers with equivalent complete-image and tiled output
   (done for both). Migrate FLTK plot interactions to semantic controls.
5. Exercise Rev in a separate build (Linux/llvmpipe validated), migrate the remaining FLTK screen/controller
   code to the shared declarations, and refine the contract where real adapter
   differences require it. Consider ncurses afterward for SSH. Freeze the first
   contract after both desktop adapters use its full semantic path.

Keep current model, policy, waveform, acquisition, replay, and exact-bit tests.
Add focused checks for silent updates, stable item identity, explicit selection
defaults, stale async results, retained save payloads, and pure pixel replay as
those seams are extracted. Preserve Linux virtual-display smoke workflows,
Windows GUI/relocation checks, and packaged self-check. Reuse the core library
and compile only the selected adapter; verify ordinary screen edits do not
recompile DSP/crypto or require reading adapter internals.
