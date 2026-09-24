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
| `application.cpp`, `fast/controller.*`, `fast/screen.*`, `legacy/*` | Shared mode host and independent Fast/Legacy state and screens. Existing engines remain polled; native service routes retain owner/generation and idle audio ownership prevents overlap. Legacy owns its auto-listen/simplex text workflow. Regular waveform and pending-reception policy remain in the existing controller/service. |
| `ui_contract.hpp` | Control, page, field, command, record and service vocabulary. |
| `ui_document.hpp`, `bitmap.hpp` | Generic document nodes and opaque `BitmapSource` pixel handles, without domain factories. |
| `control_binding.hpp` | Declaration-order control groups and menu identity by page or persistent scope, plus instance. |
| `binding_state.hpp` | Resolved native control roles, effective option availability, deferred option identity, complete layout invalidation and bitmap revision retention. |
| `document_layout.hpp` | Document flow, remaining widths, margins, padding, clipping and nested equal heights; adapters supply native glyph measurements only. |
| `document_actions.hpp` | Document action identity, inherited availability and eligibility for restoring focus after a document replacement. |
| `document_presentation.hpp` | Immutable document ownership, rendered child traversal, action association and local/absolute placement with inherited allocation and availability. |
| `control_interactions.hpp`, `record_interactions.hpp` | Pointer double-click identity, wheel command repetition, record keyboard navigation, selection and activation eligibility. |
| `overlay.hpp`, `screen_overlay.hpp` | Shared overlay composition, key bindings, background access, service priority and focus-restoration policy; the expanded QR definition. |
| `record_scroll.hpp` | Record tail detection, scroll retention across data and viewport changes, and revealing selected rows. |
| `record_reconciliation.hpp` | Retained record identity, order, changed/added/removed rows, selection and availability. Native lists keep widget handles and glyph measurements. |
| `service_queue.hpp` | Serial platform-service requests, completion identity, declared input validation and cancellation of current/queued work during shutdown. |
| `chrome_layout.hpp` | Dialog wording/input shape and geometry, popup sizing preferences, tooltip metrics/placement, checkbox and empty-record geometry. |
| `text_policy.hpp`, `utf8_policy.hpp` | Atomic UTF-8 replacement proposals, caret/selection boundaries and declaration-specific byte limits. |
| `theme.hpp`, `presentation_palette.hpp` | Shared widget states (border, hover, focus, selection, disabled and dialog colors), semantic text/document tones and fills. |
| `screen_console.cpp` | Window/page titles, controls, bindings, menus, help, submit/activation/gesture policies. |
| `desktop_layout.hpp`, `control_layout.hpp` | Desktop geometry, label/editor/preset/caption placement, and record cell/content extents in logical units. |
| `controller.cpp` | Authoritative drafts, validation, settings, workers, commands, key/file state, reception and eligibility. |
| `record_presentations.hpp` | Signal/file records, including frequency, status, reception quality, text, tone and activation eligibility. |
| `inspection_page.hpp` | Inspection section order, cards, tables, pagination and native text around plot snapshots. |
| `link_planner_model.*`, `link_planner_page.*` | Bounded link previews, native planner controls and labels, logarithmic plot snapshots and model details. |
| `launch_command.*` | Settings-only command parsing and formatting shared by GUI launch and the planner's copy/load editor; no process execution or shell expansion. |
| `bitmap_sources.hpp`, `plot_render.cpp` | Shared snapshots, captions, error overlays, invalidation and pixel producers. |
| `application.cpp` | Control/menu presentation and dispatch, validated edits/presets, launch parsing, lifecycle, submission/record dispatch, document caching and common self-check/smoke orchestration. |
| `gui_smoke.cpp` | The same application workflow checks for both native backends and the headless harness. |

`backend_fltk.cpp` and `backend_rev.cpp` iterate the same control/page definitions
and apply the same controller state and computed rectangles. Their document
renderers consume `ui::DocumentNode` without knowing what an inspection model is.
Adapters call `Application` through fields, commands, service messages and
presentation snapshots; no public controller or bitmap-producer access is
available.
`Application::control()` resolves labels and availability for every control kind;
`Application::menu()` filters hidden entries and preserves their declaration IDs.
Menu selection, action activation and presets return through shared dispatch,
which rechecks eligibility and applies the control's edit constraints.
Choice/list selection, toggles and gestures also return with their declaration;
the facade rechecks visibility and availability at dispatch time. Gesture commands
must occur in that declaration. Native state from an earlier frame cannot bypass
these checks.
Adapters own native widget construction, text measurement, focus/caret behavior,
scroll containers, menu escaping, event translation and platform services.
Bitmap widgets receive opaque snapshots; they do not interpret measurements.

Legacy's upper transcript is a generic `Control.read_only` multiline editor:
it remains selectable and scrollable while rejecting edits and paste. The
flag defaults to false, preserving all existing editors. Native adapters share
the same Legacy declarations, text presentation and waterfall snapshots.

The planner command editor is an ordinary document-only multiline control.
Its shared declaration makes Enter insert a newline and Tab navigate to the
next control. Retained native editors preserve selection, caret and uncommitted
text across document updates. Adapters keep glyphs, selection, hit testing and
caret reveal in the same scrolled coordinates and pass a wheel event to the
containing document when the editor cannot scroll further.
The controller stages imported settings, including the derived link channel,
and preflights live-session validation before committing fields or configuration.
Parsing and validation errors therefore preserve both the existing settings and
the pasted command. Successful loads use the ordinary configuration path and
do not submit the message.

Full-window views use `OverlayDefinition.controls`, the same `Control` vocabulary
and native factories as the desktop. Edit the QR view in `screen_overlay.hpp`:
add a choice, editor, action, list, label or bitmap; set its placement and ordinary
bindings; and change `OverlayPolicy` for dismissal keys, keyboard routing,
background access or service priority. These changes require no backend edits.
The default QR view still contains just the QR bitmap and its error caption.
Its Escape binding and keyboard policy are shared data.

`Application::show_overlay()` owns an immutable definition and stamps every
control with a new surface generation. Native callbacks retain that definition;
the facade rejects input from a closed or replaced generation, including after
the same view is reopened. Native page and document actions use `navigate()` and
`dispatch()` so covered desktop content cannot bypass the same input policy.
`activate()` and `select_page()` remain programmatic shared-workflow entry points.
Native services report their active state; overlay policy can defer queued
services, while an already active service retains priority.

Native overlay code handles widget ownership, popup lifetimes, painting and
focus mechanics. Both adapters consume `overlay_layers()` and `overlay_key()`;
neither defines its own Escape command or assumes that an expanded view is only
a bitmap. `tests/overlay_fixture.hpp` supplies an identical bitmap, brightness
choice, editor, action and grouped menu to both native suites, including a
replacement that changes layout and key/service policy. `gui_overlay` verifies
the shared contract and stale-callback rejection without a toolkit.

Both toolkits retain native text and controls. A signal row is not rasterized
into a bitmap or flattened into one string. Stable record IDs preserve selection
and widget identity when old signals are removed or records are reordered.
Incoming records follow the newest row while the reader is already at the tail;
reviewing history keeps the reader's scroll position. Both adapters use shared
record scroll policy with native viewport and scroll measurements. Signal
frequency, status, preamble/data measurements, message text, completion tone,
help, empty state and copy actions are shared presentation policy.
One shared record extent calculation reserves fixed cells and expands flexible
cells to fit native glyph measurements. Both adapters expose the resulting width
through the list's horizontal scroll container.

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
saved selected ID, for example FEC being off for explicit raw-bit input. Key labels
are literal UTF-8; FLTK-specific escaping stays in its menu adapter.
`control_binding.hpp` groups a menu only with declarations in the same scope and
with the same `instance`: ordinary menus belong to a page, while persistent
menus share one scope regardless of their declarations' page values. Layout uses
the same grouping, so additional menu entries do not allocate another widget or
consume row space. Reusing a menu ID on another page or instance needs no adapter
special case.
Ordinary controls already in the declaration span use their actual declaration
identity for layout, so unbound labels and repeated bindings retain separate row
positions. A copied declaration uses its binding, instance and complete menu/
persistent/page scope. Give copied repeated bindings distinct instances.

Native control geometry is refreshed from `ControlLayout` as a complete value.
Label appearance/disappearance, row movement, footer allocation and bitmap
caption mode changes reach retained controls in both adapters. Native layout
invalidation does not enumerate a separate subset of shared geometry rules.
`BindingState` also owns the options actually shown by a native popup. A toolkit
may defer replacing them while its nested menu loop is running; callback indices
still resolve against those displayed IDs. Both adapters consume one resolved
binding presentation for visibility, optional areas and effective option state.
Tab positions also come from shared declaration-order geometry.
Document content widths share their margins and minimum; adapters supply only
space consumed by a native scrollbar. Both windows use the shared initial and
minimum dimensions without a separate application maximum in one backend.

Native appearance values live in `theme::WidgetRole`; changing border, hover,
focus, disabled, selection or dialog roles updates both adapters' mappings.
`chrome_layout.hpp` supplies application-owned prompt content, button wording,
dialog rectangles and popup/tooltip preferences. Native glyph measurement and
host screen fitting remain toolkit work. FLTK's native file chooser owns its
browse controls; the adapter supplies the shared request and action labels.

Both record widgets use `RecordReconciliation` to decide which IDs are retained,
inserted, removed, changed or reordered. Both document renderers use
`DocumentPresentation` for rendered structure, inherited availability, action
association and placement. FLTK can retain native widgets while Rev rebuilds a
changed immutable document; neither independently derives document structure or
geometry. Native ownership, focus calls and measurements remain adapter code.

The application polls reception and captures plot history at 25 Hz while native
state/plot presentation runs at 10 Hz. Native input can repaint immediately.
Pointer double-click timing, wheel command repetition and record keyboard
selection/activation use shared policy. Adapters translate keys and pointer
coordinates, then focus the returned native row and apply the shared reveal
position. Text tones for records, captions and documents, along with document
fills, resolve to RGB in `presentation_palette.hpp`; native code only converts
those values for drawing.
Both use immutable document and bitmap identities so unchanged polling does not
rebuild text trees or upload textures. Moving between pages and resizing cannot
restart reception. File/prompt/clipboard services use request IDs and deferred
results; a pending Save retains its bytes independently of inbox changes.
`service_queue.hpp` serializes service requests and matches completion IDs for
both backends. The request also owns its input byte limit. Prompt editors use
shared atomic text validation and every successful input completion is validated
again by the queue, including native file chooser replies. Prompts remain
single-line; OS paths may contain line breaks. Changing these constraints is
shared contract work. Closing the application cancels active and queued requests;
adapters close native dialogs and cannot start another queued service.

## Verification and maintenance guardrails

`gui_adapter_boundary` recursively checks native entry points and their local
header dependencies against the public contract/native helper boundary. It
rejects domain headers, application-ID decisions in native adapters, toolkit
headers in the public contract, native calls from shared feature code, and
obsolete parallel GUI sources. Native RGB literals are rejected so palette
values must come from shared roles. Generic public helpers are checked for hidden
application-ID decisions too. Its regression suite introduces domain includes,
field/command/menu references, aliases and `using enum` imports, whitespace-split
references, and toolkit dependencies in public or shared feature code; each
forbidden dependency must be rejected. Legal sentinel aliases remain accepted.
The check runs before building `datapump_gui_application`, including builds with
`BUILD_TESTING=OFF`. It discovers nested sources and alternate C++ extensions,
normalizes comments and continued lines, resolves aliases across helper headers,
and rejects nonliteral dependencies and backend conditionals in shared features.
Public headers accept only approved GUI headers and standard-library includes.
This is a source architecture lint, not a general C++ parser or security boundary.
The application library links the modem implementation privately; adapters no
longer inherit its include directories. `gui_link_boundary` compiles the public
contract using that actual link dependency and rejects exposed modem headers.
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
activation-on-selection, along with record tail following, history retention and
selection visibility. `gui_application` covers control/menu presentation,
dispatch, stale submission callbacks, preset validation and persistent menu
grouping. Native tests verify that those descriptions reach real widgets,
refresh existing control labels and dispatch callbacks.
Additional cases cover stale choice/toggle/gesture callbacks, shared UTF-8 edit
proposals, record widths, zero/large stretch weights, exhausted control space,
and document action focus and availability after tree changes.
`gui_services` tests service ordering, completion identity, declared limits,
UTF-8 validation and shutdown cancellation without a toolkit. Both native suites
also exercise the same limited prompt edits and shared layout lifecycle fixture.
`gui_bindings`, `gui_record_reconciliation`, `gui_document_presentation` and
`gui_chrome` cover retained option identity during deferred native updates,
complete cache invalidation, stable record changes, rendered document traversal,
and shared service/control geometry and tooltip timing. Native probes check
the resulting popup widths, dialog rectangles, checkboxes and resolved colors,
including selection, hover and inherited disabled states.
Rev also checks rendered pixels when leaving deeply nested clips, so later
document siblings and persistent controls remain visible while overflow stays
clipped.
Rev's native probes are compiled only into `test_rev_adapter`; production GUI
sources no longer include domain-bearing test fixtures. Both native conformance
executables run their widget probes separately from `gui_workflow`, which runs
the complete shared smoke through the production executable. This keeps the
workflow's initial state and clock independent of native probe duration, and
avoids maintaining a second workflow lifecycle in an adapter's test code.

The CI GUI-contract matrix builds **both FLTK and Rev** and runs the same shared
suite and simulation workflow, plus their native conformance tests on a private
display. Register display-dependent tests explicitly with
`-DDATAPUMP_TEST_NATIVE_GUI=ON`, then run:

```sh
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a -s '-screen 0 2400x1800x24 -dpi 96' \
  ctest --test-dir build-gui --output-on-failure -L gui
```

The option defaults to OFF so ordinary CTest and CLI-only builds remain usable
without a display. Native tests include clipboard, editor focus/selection,
record history, document resizing and Rev coordinates at 1x and 2x. Windows
runtime testing remains a separate platform requirement; Linux success does
not establish Windows validation.
Successful smoke runs remove their automatically created temporary fixtures;
failed-run evidence and explicitly selected `--smoke-dir` output are retained.

See [GUI contract](gui-contract.md) for the vocabulary and
[Rev backend](rev-backend.md) for its pinned source, toolchain and software-GL
requirements. FLTK remains the default and uses software drawing without an
application OpenGL context. Each build selects exactly one adapter.
