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
| `application.hpp` | Backend-facing facade; controller, workers and bitmap producers are private to its implementation. |
| `ui_contract.hpp` | Control, page, field, command, record and service vocabulary. |
| `ui_document.hpp`, `bitmap.hpp` | Generic document nodes and opaque `BitmapSource` pixel handles, without domain factories. |
| `control_binding.hpp` | Declaration-order control groups and menu identity by page or persistent scope, plus instance. |
| `document_layout.hpp` | Document flow, remaining widths, margins, padding, clipping and nested equal heights; adapters supply native glyph measurements only. |
| `control_interactions.hpp`, `record_interactions.hpp` | Pointer double-click identity, wheel command repetition, record keyboard navigation, selection and activation eligibility. |
| `text_policy.hpp`, `utf8_policy.hpp` | Atomic UTF-8 edit policy and declaration-specific byte limits. |
| `theme.hpp`, `presentation_palette.hpp` | Shared RGB values and semantic text/document tone and fill resolution. |
| `screen_console.cpp` | Page titles, controls, bindings, menus, help, submit/activation/gesture policies. |
| `desktop_layout.hpp`, `control_layout.hpp` | Desktop geometry and label/editor/preset/caption placement in logical units. |
| `controller.cpp` | Authoritative drafts, validation, settings, workers, commands, key/file state, reception and eligibility. |
| `record_presentations.hpp` | Signal/file records, including frequency, status, reception quality, text, tone and activation eligibility. |
| `inspection_page.hpp` | Inspection section order, cards, tables, pagination and native text around plot snapshots. |
| `bitmap_sources.hpp`, `plot_render.cpp` | Shared snapshots, captions, error overlays, invalidation and pixel producers. |
| `application.cpp` | Control/menu presentation and dispatch, validated edits/presets, launch parsing, lifecycle, submission/record dispatch, document caching and common self-check/smoke orchestration. |
| `gui_smoke.cpp` | The same application workflow checks for both native backends and the headless harness. |

`backend_fltk.cpp` and `backend_rev.cpp` iterate the same control/page definitions
and apply the same controller state and computed rectangles. Their document
renderers consume `ui::DocumentNode` without knowing what an inspection model is.
Adapters call `Application` through fields, commands, service messages and
presentation snapshots; no public controller or bitmap-producer access is
available.
`Application::control()` resolves bound labels and availability;
`Application::menu()` filters hidden entries and preserves their declaration IDs.
Menu selection, action activation and presets return through shared dispatch,
which rechecks eligibility and applies the control's edit constraints.
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
primitive requires generic support in each adapter once. Ordinary combinations
of existing metadata, including multiline presets, gestures on any control,
enabled choices with effective labels and padded bitmap documents, are covered
by common conformance fixtures. Toolkit bugs, platform
services and native rendering differences likewise remain adapter work.

A `Control` stores behavior as named commands and ordinary metadata. Adapters
never switch on a particular application field, command, bitmap or page. A
Choice's optional `display_text` reports an effective value without changing its
saved selected ID, for example FEC being off for binary or tiny input. Key labels
are literal UTF-8; FLTK-specific escaping stays in its menu adapter.
`control_binding.hpp` groups a menu only with declarations in the same scope and
with the same `instance`: ordinary menus belong to a page, while persistent
menus share one scope regardless of their declarations' page values. Layout uses
the same grouping, so additional menu entries do not allocate another widget or
consume row space. Reusing a menu ID on another page or instance needs no adapter
special case.

The application polls reception and captures plot history at 25 Hz while native
state/plot presentation runs at 10 Hz. Native input can repaint immediately.
Pointer double-click timing, wheel command repetition and record keyboard
selection/activation use shared policy. Adapters translate keys and pointer
coordinates, then focus and reveal the returned native row. Text tones for
records, captions and documents, along with document fills, resolve to RGB in
`presentation_palette.hpp`; native code only converts those values for drawing.
Both use immutable document and bitmap identities so unchanged polling does not
rebuild text trees or upload textures. Moving between pages and resizing cannot
restart reception. File/prompt/clipboard services use request IDs and deferred
results; a pending Save retains its bytes independently of inbox changes.

## Verification and maintenance guardrails

`gui_adapter_boundary` recursively checks native entry points and their local
header dependencies against the public contract/native helper boundary. It
rejects domain headers, application-ID decisions in native adapters, toolkit
headers in the public contract, native calls from shared feature code, and
obsolete parallel GUI sources. Generic public helpers are checked for hidden
application-ID decisions too. Its regression suite introduces domain includes,
field/command/menu references, aliases and `using enum` imports, whitespace-split
references, and toolkit dependencies in public or shared feature code; each
forbidden dependency must be rejected. Legal sentinel aliases remain accepted.
`gui_contract` separately compiles the public interface
without modem or toolkit include/link dependencies and checks that controller,
producer and model access are unavailable, along with shared tone/fill values.

`tests/gui_extension_fixture.hpp` and `tests/document_geometry_fixture.hpp` supply
ordinary extensions unchanged to both native suites. They exercise added
controls/actions, multiline presets with byte limits, filtered menus with
separate page/instance scopes, effective choice labels, generic gestures,
record cells, relative document widths/margins, wrapped actions and opaque bitmap
rectangles with padded strides and multiple formats. `gui_document_layout` and
`gui_interactions` test geometry and event policy without a toolkit, including
record arrow navigation, disabled rows, Space/Enter, double-click identity and
activation-on-selection. `gui_application` covers control/menu presentation,
dispatch, preset validation and persistent menu grouping. Native tests
verify that those descriptions reach real widgets and dispatch callbacks.
Rev also checks rendered pixels when leaving deeply nested clips, so later
document siblings and persistent controls remain visible while overflow stays
clipped.
Rev's native probes are compiled only into `test_rev_adapter`; production GUI
sources no longer include domain-bearing test fixtures.

The CI GUI-contract matrix builds **both FLTK and Rev** and runs the same shared
suite and simulation workflow, plus their native conformance tests on a private
display. Register display-dependent tests explicitly with
`-DDATAPUMP_TEST_NATIVE_GUI=ON`, then run:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 2400x1800x24 -dpi 96' \
  ctest --test-dir build-gui --output-on-failure -L gui
```

The option defaults to OFF so ordinary CTest and CLI-only builds remain usable
without a display. Native tests include clipboard, editor focus/selection,
record history, document resizing and Rev coordinates at 1x and 2x. Windows
runtime testing remains a separate platform requirement; Linux success does
not establish Windows validation.

See [GUI contract](gui-contract.md) for the vocabulary and
[Rev backend](rev-backend.md) for its pinned source, toolchain and software-GL
requirements. FLTK remains the default and uses software drawing without an
application OpenGL context. Each build selects exactly one adapter.
