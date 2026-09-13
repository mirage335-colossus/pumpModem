# Shared GUI contract

FLTK and Rev implement the same application interface. Both compile the shared
`datapump_gui_application` library and consume its field state, page/control
declarations, structured records, documents, bitmap sources and launch/workflow
code. The contract is implemented in `src/gui/ui_contract.hpp`,
`src/gui/ui_document.hpp` and `src/gui/bitmap.hpp`.

`Application` is the only application-facing facade used by native adapters.
It exposes generic field/action/service operations and presentation snapshots,
including resolved control labels, menu options and their availability.
Controller, modem models, workers and bitmap factories are private; the public
header dependency tree contains only toolkit-neutral GUI headers.

The maintenance rule is: an ordinary feature using existing primitives changes
shared declarations and application mapping only. No backend reads modem state
or contains application-specific field/command/page switches. Native adapters
may differ in glyph metrics, popup appearance/placement and platform dialogs, while
preserving the same content, values, availability and actions. `open_upward` is
a preferred popup direction subject to native screen fitting; FLTK retains its
platform popup placement policy.
Shared widget colors and application-owned chrome preferences are also defined
once, in `theme.hpp` and `chrome_layout.hpp`. Native contrast, glyph metrics,
screen fitting and host file-chooser internals remain toolkit mechanisms.

## Controls and state

The persistent **DSP workspace** choice selects 25%, 50% (default), or 75% of
available RAM. Its display includes the resolved MiB/GiB ceiling. The value is
sampled at controller startup and when the percentage changes; other modem edits
keep the same ceiling. Both native adapters use this shared control. Live DSP
and transfer estimates receive the same budget, independently of the 256 MiB
received-message/file cache.

| Vocabulary | Meaning |
| --- | --- |
| `Kind::label` | Literal or bound readonly native text. |
| `Kind::action` | Invoke a declared command; optional grouping into a named native menu. |
| `Kind::toggle` | Edit a boolean field. |
| `Kind::choice` | Select a stable option ID from a closed set. |
| `Kind::text` | Edit UTF-8 text, with optional presets and explicit submission. |
| `Kind::list` | Stable-ID structured records composed from native text cells. |
| `Kind::bitmap` | Display an immutable opaque pixel source and shared native captions. |
| `PageDefinition` | Shared page ID, title, tab size and control/document content kind. |

`Control` binds a field, command or bitmap and describes its shared placement,
help, font size, menu label, persistence, submit policy, record activation and
optional pointer/wheel actions. Application behavior is not inferred from its
label or from a particular widget type. Ordinary controls use stable binding
identity; `instance` distinguishes intentional repeated bindings on one page.

Control declarations have a construction lifetime. Their storage, referenced
strings and the declaration span must remain valid while the native view exists.
Keep each control's kind, single-line/multiline class, binding association,
parent/page/persistent scope and menu membership fixed during that lifetime.
Changing that structure requires constructing a new native view from the updated
shared declarations. Adding controls or changing their classes in shared source
before constructing or restarting the view does not require adapter changes.
This interface does not promise arbitrary live replacement of the declaration
array. The standalone reference interface uses handles and generations to make
structural replacement explicit.

Within an existing structure, shared presentation and behavior metadata update
on each presentation pass. This includes labels, layout, fonts, help, input byte
limits, submit and pointer/wheel commands, list row height, empty-list text,
tail following and record activation policy. Adding or removing an optional
behavior uses the same retained native control; its initial absence does not
disable later updates. These updates remain silent. A byte-limit change governs
subsequent user edits and does not truncate authoritative model text.

`control_binding.hpp` provides the control groups consumed by both adapters and
the layout engine. Menu identity combines the declared menu ID, its page or
persistent scope, and `instance`. Persistent menus group across page values;
ordinary menus on different pages remain separate. Only the first declaration
in each group creates a native control and consumes layout space.

`Application::control()` resolves bound text, command labels, visibility and
eligibility. Native control labels consume shared presentation on each update,
including editor, choice and list headings. Bitmap titles and menu labels use
their specialized shared presentations.
`Application::menu()` derives each entry from that same presentation,
omits hidden entries, and enables the menu when at least one visible entry is
enabled. Returned option IDs retain declaration identity after filtering.
Adapters pass those IDs to `select_menu()`; shared dispatch rechecks the chosen
control's current visibility and eligibility. Adapters do not reconstruct menu
policy or map filtered indices to application commands.
`binding_state.hpp` resolves native control visibility, optional areas and
effective option availability once. It retains the displayed option IDs when
native popup replacement is deferred, and tracks complete geometry/font changes
and bitmap source identity/revisions for both adapters.
Native choice/list selection and toggles use the declaration-aware `select()`
and `toggle()` overloads. Pointer/wheel commands use `gesture()`, which checks
both the declared binding and current availability. These checks also apply to
callbacks queued before a control became hidden, disabled or closed, including
callbacks from a page that is no longer selected. Persistent controls continue
to accept input across page changes.

`FieldState` carries authoritative text/selection/boolean values, visibility,
eligibility, options and records. Options have IDs, literal labels and enabled
flags. `display_text` optionally shows an effective value without replacing a
saved selection or disabling an otherwise enabled choice. For example, the
selected Reed-Solomon preset remains saved
while a tiny message reports `Off (under 16 B)`.

A record has a stable ID, enabled/activation eligibility and ordered native text
cells. Cells carry literal text, relative logical rectangles, font size, semantic
tone and emphasis. Negative cell width means remaining row width minus that
amount. The list declaration supplies row height, empty/help text, tail-following
and activation policy. Signal records include frequency, status, preamble/data
quality and message text; files and unverified prefixes are not copyable as text.

## Behavior every adapter preserves

- Applying state is silent and preserves unchanged native widgets, focus, text
  cursor/selection, scroll and record identities. Removing or reordering records
  never transfers focus to a different record merely because it took an index.
  An explicit `FieldState::text_cursor_end_revision` increment collapses the
  editor selection at the text end without changing focus or emitting an edit.
  Each native editor consumes the revision once, even if text is unchanged;
  repeated presentation preserves subsequent user cursor movements. Zero does
  not request cursor movement.
- Selection uses IDs, never labels or native menu indices. Disabled entries
  cannot be newly selected. The controller chooses defaults and invalidation
  behavior; a native widget must not silently select the first entry.
- Text is valid UTF-8 with a byte limit. Reject invalid/over-limit edits with
  feedback; do not truncate them. Failed paste preserves selection. Presets are
  available on both single-line and multiline text controls and
  pass their option IDs to `Application::preset()`, which accepts enabled options
  and uses the same validated control edit path. Unknown or disabled presets
  cannot change text; a preset cannot bypass a control's byte limit.
  Enter/Ctrl+Enter/Shift+Enter submission is shared
  command policy; native adapters translate modifiers only. Shared dispatch
  rechecks visibility and eligibility before submission or record activation,
  including callbacks arriving after the control's state changed.
  Replacement text, byte boundaries and no-op/rejection outcomes are calculated
  by shared edit policy. Applying model text clamps both ends of a native
  selection to valid UTF-8 boundaries without emitting an edit callback.
- Declared click, double-click and wheel commands apply to every control kind.
  Adapters translate coordinates and wheel detents; shared interaction code
  chooses commands, recognizes double-clicks and caps coalesced wheel repetition.
  A declared pointer gesture consumes that press before native child handling;
  input without a declared gesture retains its ordinary native behavior.
- Record selection and activation are separate semantic operations. A
  declaration may activate an eligible row on selection, as for click-to-copy
  signals. `record_interactions.hpp` handles pointers and keyboard input for both
  adapters: Up/Down skip disabled rows without wrapping, Space selects, and Enter
  or a double-click requests activation. Activation still requires an eligible
  record; the declaration's activation-on-selection policy applies to keyboard
  and pointer selection alike. Hidden or disabled lists accept neither operation.
  Adapters only translate events and focus/reveal the returned row. The ordinary
  Copy/Save actions remain separately reachable.
- New records follow the tail only when the reader was already there. History
  review and long text remain accessible through native scrolling.
  `record_scroll.hpp` owns tail detection, position retention when records or the
  viewport change, and scrolling a selected row into view. Adapters supply
  native measurements and apply the returned position.
  Record rows share one horizontal content width. Fixed cells reserve their
  declared widths; flexible cells expand for their measured text and trailing
  inset. Adapters measure glyphs and apply that common extent to native scrolling.
  `record_reconciliation.hpp` owns stable IDs, declaration order, selection,
  availability and added/removed/changed rows. Native lists consume its change
  plan and retain only widget handles and native glyph measurements.
- Key-file failure acknowledgement is an explicit action. It does not depend
  on a toolkit reporting re-selection of an unchanged choice.
- All input validation, transmit eligibility, preparation/revision checks,
  file/clipboard eligibility and retention live in the shared controller.

## Layout and documents

`desktop_layout.hpp` defines the desktop arrangement at 1180 by 866 logical
pixels, with a 1030 by 786 minimum. `control_layout.hpp` computes frame, label,
editor, preset, caption and footer rectangles once. Both adapters apply these
rectangles and convert logical to physical coordinates using their display
scale. Slot-free rows provide ordered stretch-weight placement for alternate
arrangements. A new desktop slot is shared layout work, not adapter work.
Zero stretch allocates zero width; large weights do not overflow the allocation.
When space is exhausted, shared editor, preset and bitmap subrectangles stay
nonnegative. Empty native allocations are not drawn and cannot take keyboard focus
or request synthetic bitmap pixels.

`ui::DocumentNode` is a generic tree of columns, rows, text, actions and bitmaps.
Nodes carry widths, optional fixed heights, margins/padding, semantic tone/fill,
emphasis, borders and equal-row-height intent. `document_layout.hpp` computes
all document rectangles. Toolkit callbacks measure glyph height; adapters apply
the result without implementing a second flow/layout algorithm.

Zero width fills the remaining row width or available column width; explicit
widths clamp to available space. Right margins reserve space before allocation.
Zero height grows to content; fixed heights include padding and clip overflow.
Fully clipped descendants lose allocation, focus and input eligibility even
when their own declared width and height are positive. Expanding the containing
clip restores eligible descendants through the same shared presentation path.
Equal-height rows allocate their inner height to auto-height children after each
child's top/bottom margins, recursively propagating final allocations into
nested rows. Fixed child heights remain authoritative. Native text and action
labels wrap inside the shared content rectangle; bitmap pixels use that same
inner rectangle, preserving their declared padding and border. A bordered node
reserves a minimum one-unit content inset; larger declared padding already
includes that inset. Dynamic content
owns the tree and produces an immutable
snapshot; adapters do not invent sections or legends. Both native document
renderers consume the same inspection tree and preserve action identity.
`document_actions.hpp` resolves inherited enabled state and focus restoration
for both adapters. A nonzero action `instance` is stable within its command and
survives insertion, reordering or removal of other actions; duplicate instances
are rejected. Zero retains declaration-order occurrence identity for static
documents. Removing or disabling the identified action clears its focus. The
containing native view must also accept input before an action can dispatch.
`document_presentation.hpp` owns immutable document descriptions, the rendered
child tree, action association and inherited availability. It emits both local
and absolute placements from the shared layout. The native renderers choose how
to retain or replace toolkit handles, then apply those placements without a
separate traversal or allocation policy.

`inspection_page.hpp` is application presentation code above this interface.
Changing a section, table, note or plot there reaches both backends. Ordinary
text, codeword bars and actions remain native elements, never whole-page bitmap
rendering. The same distinction applies to structured signal records.

`presentation_palette.hpp` resolves record/caption text tones, document tones and
document fills into the RGB values in `theme.hpp`. Both adapters convert the
resolved values to native colors. Normal, muted, data/accent and inverse text
therefore keep the same meaning in monochrome and color modes. An action with
no explicit fill uses the shared surface color; other unfilled document nodes
remain transparent. Adding or changing a semantic palette role is shared
presentation work.
Native widget states use the same `theme::WidgetRole` palette: border, hover,
focus, checked, selection, disabled text/background/border and dialog colors.
Adapters convert these values and apply native drawing/contrast mechanics.
Application-owned dialog labels/geometry, tooltip preferences, popup minimum
width, checkbox geometry and empty-record insets come from `chrome_layout.hpp`.
Both custom prompts consume the same description and input policy. FLTK's
file browser consumes shared title/action labels and retains native browse UI;
its popup loop retains native selected-row positioning and monitor fitting.

## Bitmap boundary

Shared code creates immutable `PlotSnapshot` producers and passes adapters an
opaque `BitmapSource` handle exposing only pixel painting. Domain constructors
for plots, QR codes and modem measurements are unavailable through that handle.
On repaint the adapter
provides actual backing-pixel dimensions, damage bounds, sample aspect ratio and
pixel capabilities. The source emits borrowed pixel rectangles through
`BitmapSink`; the adapter copies them synchronously or retains its own storage.
Each paint retains the executing producer and its captured data until it returns
or unwinds, even if the receiver releases or replaces the source during delivery.
Replacement takes effect on subsequent paints.
Rectangles
may span multiple rows, use padded strides and arrive in different supported
formats. Native transfer batching must not impose producer-specific block sizes.
Unpainted areas of a full replacement are black. Captions and painting receive
the same actual backing-pixel width, including high DPI.

Native cache dimensions and clean flags commit only after pixel production and
upload succeed. A failed attempt preserves the previous texture and pending
work, so another paint attempt can retry the same source and dimensions. Native
replacement resources are allocated before releasing their predecessors.
Exceptions continue through the host's existing error handling; this contract
does not promise an automatic recovery dialog.

The pointer-based block interface requires producers to supply readable storage
for every declared row until the synchronous sink returns. Dimension and stride
validation cannot prove a raw pointer's allocation length. A bounded byte-view
interface should be used when allocation-length validation is required.

Supported formats are Gray8 (one intensity byte), MSB-first Mono1, and optional
RGB24 (three bytes R/G/B). Stride is explicit. Placement is 1:1 in actual drawable
pixels, including high DPI. There is no application drawing-command API, alpha
blending, arbitrary scaling or generic custom-widget callback. Shared producers
own axes, scale and pixel geometry; captions and explanations remain native text.

`BitmapSources` owns history, source invalidation and versions. `Application`
provides titles/captions/error-overlay tone without any source-specific decision
inside an adapter. Changing QR brightness affects only that bitmap and never
changes modem state. Monochrome and color are shared presentation preferences;
unsupported targets retain the grayscale path.

## Platform services and lifecycle

Commands request `open_file`, `save_file`, `prompt`, `clipboard` or `open_folder`
using a unique request ID and opaque strings. Native adapters return cancellation,
a value or an error. Dialogs must allow the shared polling loop to continue.
The controller retains data behind pending saves and performs exclusive writes;
platform dialogs never authorize silent overwrites.
`service_queue.hpp` provides the request queue and completion-ID checks used by
both adapters. `ServiceRequest::byte_limit` declares the input limit (32768 bytes
by default). Prompts use the same atomic UTF-8 editor validation as controls;
paths returned by native file selectors can also contain OS filename line breaks.
The shared queue validates completed input before returning it to the application,
including results supplied without an editor callback. Invalid input returns an
error without the invalid value. Cancelled requests and platform errors retain
their original meaning. Requests execute serially. Application shutdown cancels active
and queued requests, so closing a dialog cannot launch another queued operation.
Adapters own the native dialog and platform calls and close their active dialog
when shared queue state is cancelled.

`Application` polls controller/bitmap state at 25 Hz and requests ordinary
presentation at 10 Hz. Adapters pump native events, call `tick()`, apply returned
state and render. Shared code owns page state, launch parsing, document caching,
submission/record actions and smoke workflows. Both backends expose the same
flags, including `--simulation`, `--self-check` and the smoke options.

A build selects exactly one backend through `DATAPUMP_GUI_BACKEND=fltk|rev`.
CLI-only builds can still compile and test the shared GUI application without a
widget toolkit. Features expressed with this existing vocabulary require only
shared declaration, presentation and application changes. A new primitive needs
generic support in each adapter once; toolkit bugs, native rendering and platform
requirements also remain adapter work.

Shared extension fixtures exercise the same added controls, scoped/filtered
menus, validated presets, record cells and document geometry in both native
suites. They also change existing label visibility, row placement, font size,
footer allocation and bitmap caption mode without rebuilding native controls.
Both adapters consume the complete shared geometry; native invalidation must not
maintain a second list of which shared layout rules can change.
Toolkit-free tests cover grouping, dispatch, record interaction policy,
scrolling, service ordering/shutdown and semantic palettes. The boundary guard
also checks generic public helpers; its regressions reject application-ID
decisions hidden behind helpers or aliases
and dependencies that cross into a toolkit or application internals.

See [GUI architecture](gui-architecture.md) for file ownership, extension tests
and the architectural regression guard.
