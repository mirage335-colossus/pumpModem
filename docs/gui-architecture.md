# GUI architecture

FLTK and Rev are native adapters for one shared GUI application. Both link
`datapump_gui_application`, defined once in `cmake/GuiShared.cmake`. Neither
adapter constructs a second application controller, chooses modem settings,
formats signal data, or defines inspection sections. The former FLTK
`main.cpp`, live widgets and procedural inspection implementation have been
removed.

## Ownership

| Shared module | Responsibility |
| --- | --- |
| `ui_contract.hpp` | Control, page, field, command, record and service vocabulary. |
| `ui_document.hpp`, `bitmap.hpp` | Generic native document nodes and opaque pixel transfer. |
| `screen_console.cpp` | Page titles, controls, bindings, menus, help, submit/activation/gesture policies. |
| `desktop_layout.hpp`, `control_layout.hpp` | Desktop geometry and label/editor/preset/caption placement in logical units. |
| `controller.cpp` | Authoritative drafts, validation, settings, workers, commands, key/file state, reception and eligibility. |
| `record_presentations.hpp` | Signal/file records, including frequency, status, reception quality, text, tone and activation eligibility. |
| `inspection_page.hpp` | Inspection section order, cards, tables, pagination and native text around plot snapshots. |
| `bitmap_sources.hpp`, `plot_render.cpp` | Shared snapshots, captions, error overlays, invalidation and pixel producers. |
| `application.cpp` | Launch parsing, lifecycle, submission/record dispatch, document caching and common self-check/smoke orchestration. |
| `gui_smoke.cpp` | The same application workflow checks for both native backends and the headless harness. |

`backend_fltk.cpp` and `backend_rev.cpp` iterate the same control/page definitions
and apply the same controller state and computed rectangles. Their document
renderers consume `ui::DocumentNode` without knowing what an inspection model is.
Adapters own native widget construction, text measurement, focus/caret behavior,
scroll containers, menu escaping, event translation and platform services.
Bitmap widgets receive opaque snapshots; they do not interpret measurements.

Both toolkits retain native text and controls. A signal row is not rasterized
into a bitmap or flattened into one string. Stable record IDs preserve selection
and widget identity when old signals are removed or records are reordered.
Incoming records follow the newest row while the reader is already at the tail;
reviewing history keeps the reader's scroll position. Signal frequency, status,
preamble/data measurements, message text, completion tone, help, empty state and
copy actions are shared presentation policy.

## Adding functionality once

1. Add the application's field/command binding and its state/action mapping in
   `ui_contract.hpp` and `controller.cpp` when needed.
2. Add or edit the control declaration in `screen_console.cpp`. Put its label,
   help, submission, activation and optional native gestures there. Use a shared
   desktop slot, or the generic ordered-row layout for a new arrangement.
3. For richer content, add record cells in `record_presentations.hpp` or generic
   document nodes in `inspection_page.hpp`. Put new image production and captions
   in shared bitmap code.
4. Test the shared behavior and run the same workflow in both backend builds.

These edits require no adapter changes when they use existing primitives. A new
primitive requires generic support in each adapter once. Toolkit bugs, platform
services and native rendering differences likewise remain adapter work.

A `Control` stores behavior as named commands and ordinary metadata. Adapters
never switch on a particular application field, command, bitmap or page. A
Choice's optional `display_text` reports an effective value without changing its
saved selected ID, for example FEC being off for binary or tiny input. Key labels
are literal UTF-8; FLTK-specific escaping stays in its menu adapter.

The application polls reception and captures plot history at 25 Hz while native
state/plot presentation runs at 10 Hz. Native input can repaint immediately.
Both use immutable document and bitmap identities so unchanged polling does not
rebuild text trees or upload textures. Moving between pages and resizing cannot
restart reception. File/prompt/clipboard services use request IDs and deferred
results; a pending Save retains its bytes independently of inbox changes.

## Verification and maintenance guardrails

`gui_adapter_boundary` rejects concrete application IDs or domain presentation
includes in either adapter and rejects reintroducing the obsolete parallel GUI
sources. Both backend profiles contain only their adapter/platform source lists;
the common library owns application sources.

`tests/gui_extension_fixture.hpp` supplies an ordinary added control/action, an
additional record cell and a document field/action to native adapter tests. It
is shared unchanged by both backends. This checks that an existing primitive can
be extended above the interface and reach native widgets and dispatch callbacks.

`gui_application`, `gui_controller`, `gui_layout`, `gui_inspection_page` and the
bitmap/state tests exercise the common implementation without a toolkit. Native
adapter tests additionally cover literal menu labels, silent text updates,
stable record identity, effective choice labels, focus, clipboard transfer and
native document layout. Display-dependent tests run on an isolated display.
The shared simulation smoke runs through the actual GUI executable in each
build. Windows runtime testing remains a separate platform requirement; Linux
success does not establish Windows validation.

The migration was checked on Linux with 11 shared/model tests, both executable
self-checks, both native adapter suites and the expanded simulation workflow in
both GUIs. Visual checks cover the default/minimum console and inspection pages.
Rev's input regression checks moved windows, caret placement, wheel targeting
and scale changes at 1x and 2x. Parent-page visibility tests cover native document
measurement and retained signal history across hide/show transitions.

See [GUI contract](gui-contract.md) for the vocabulary and
[Rev backend](rev-backend.md) for its pinned source, toolchain and software-GL
requirements. FLTK remains the default and uses software drawing without an
application OpenGL context. Each build selects exactly one adapter.
