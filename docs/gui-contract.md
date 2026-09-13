# Shared GUI contract

FLTK and Rev implement the same application interface. Both compile the shared
`datapump_gui_application` library and consume its field state, page/control
declarations, structured records, documents, bitmap sources and launch/workflow
code. The contract is implemented in `src/gui/ui_contract.hpp`,
`src/gui/ui_document.hpp` and `src/gui/bitmap.hpp`.

`Application` is the only application-facing facade used by native adapters.
It exposes generic field/action/service operations and presentation snapshots.
Controller, modem models, workers and bitmap factories are private; the public
header dependency tree contains only toolkit-neutral GUI headers.

The maintenance rule is: an ordinary feature using existing primitives changes
shared declarations and application mapping only. No backend reads modem state
or contains application-specific field/command/page switches. Native adapters
may differ in glyph metrics, popup appearance/placement and platform dialogs, while
preserving the same content, values, availability and actions. `open_upward` is
a preferred popup direction subject to native screen fitting; FLTK retains its
platform popup placement policy.

## Controls and state

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
- Selection uses IDs, never labels or native menu indices. Disabled entries
  cannot be newly selected. The controller chooses defaults and invalidation
  behavior; a native widget must not silently select the first entry.
- Text is valid UTF-8 with a byte limit. Reject invalid/over-limit edits with
  feedback; do not truncate them. Failed paste preserves selection. Presets are
  available on both single-line and multiline text controls and
  emit ordinary edits. Enter/Ctrl+Enter/Shift+Enter submission is shared
  command policy; native adapters translate modifiers only.
- Declared click, double-click and wheel commands apply to every control kind.
  Adapters translate coordinates and wheel detents; shared interaction code
  chooses commands, recognizes double-clicks and caps coalesced wheel repetition.
  A declared pointer gesture consumes that press before native child handling;
  input without a declared gesture retains its ordinary native behavior.
- Record selection and activation are separate semantic operations. A
  declaration may activate an eligible row on selection, as for click-to-copy
  signals. The ordinary Copy/Save actions remain separately reachable.
- New records follow the tail only when the reader was already there. History
  review and long text remain accessible through native scrolling.
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

`ui::DocumentNode` is a generic tree of columns, rows, text, actions and bitmaps.
Nodes carry widths, optional fixed heights, margins/padding, semantic tone/fill,
emphasis, borders and equal-row-height intent. `document_layout.hpp` computes
all document rectangles. Toolkit callbacks measure glyph height; adapters apply
the result without implementing a second flow/layout algorithm.

Zero width fills the remaining row width or available column width; explicit
widths clamp to available space. Right margins reserve space before allocation.
Zero height grows to content; fixed heights include padding and clip overflow.
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

`inspection_page.hpp` is application presentation code above this interface.
Changing a section, table, note or plot there reaches both backends. Ordinary
text, codeword bars and actions remain native elements, never whole-page bitmap
rendering. The same distinction applies to structured signal records.

## Bitmap boundary

Shared code creates immutable `PlotSnapshot` producers and passes adapters an
opaque `BitmapSource` handle exposing only pixel painting. Domain constructors
for plots, QR codes and modem measurements are unavailable through that handle.
On repaint the adapter
provides actual backing-pixel dimensions, damage bounds, sample aspect ratio and
pixel capabilities. The source emits borrowed pixel rectangles through
`BitmapSink`; the adapter copies them synchronously or retains its own storage.
Rectangles
may span multiple rows, use padded strides and arrive in different supported
formats. Native transfer batching must not impose producer-specific block sizes.
Unpainted areas of a full replacement are black. Captions and painting receive
the same actual backing-pixel width, including high DPI.

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

`Application` polls controller/bitmap state at 25 Hz and requests ordinary
presentation at 10 Hz. Adapters pump native events, call `tick()`, apply returned
state and render. Shared code owns page state, launch parsing, document caching,
submission/record actions and smoke workflows. Both backends expose the same
flags, including `--simulation`, `--self-check` and the smoke options.

A build selects exactly one backend through `DATAPUMP_GUI_BACKEND=fltk|rev`.
CLI-only builds can still compile and test the shared GUI application without a
widget toolkit. New primitives or platform requirements need adapter work;
features expressed with this existing vocabulary do not.

See [GUI architecture](gui-architecture.md) for file ownership, extension tests
and the architectural regression guard.
