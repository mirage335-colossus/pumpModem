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

**Link planner** is the second tab, immediately after **Console**. It previews a
target independently of the live transmit/receive targets, with **Time per bit**, **Observer / receiver
time**, send time and an earliest completion estimate. The two logarithmic
graphs follow actual sampled automatic-profile steps. Native buttons edit the
target through a numeric prompt, move to a stronger or weaker usable target,
select the −8/+23 examples,
or select the one-bit/second, one-day/bit and combined clock/RAM search edges.
Milestones are derived from the current rate, carrier, pattern and key geometry;
fixed modes omit target-dependent boundaries. The selected receiver check uses
one matching receive profile and the configured oscillator/DSP allowance.
The clock/RAM milestone must satisfy both carrier coverage and wide-search
workspace availability under the selected RAM percentage, including 75%.
Its reason identifies the limiting condition. Band edges use plain decimal
Hz or kHz; ordinary audio frequencies never use scientific notation.
**Stronger** and **Weaker** select targets that pass both checks, normally about
one dB apart, skipping clock/RAM gaps. A final usable edge remains reachable
when it is less than one dB away. Buttons are disabled when no usable target is
found in that direction. Exact target precision survives selection and Apply;
rounded labels must not change sample timing. The hidden model details explain
why adjacent symbol lengths can have very different RAM requirements.

The initial preview is one exact raw bit at −8 dB-Hz. **Use current draft** uses
the accepted transfer estimate's exact wire-bit count, including the short
dictionary endpoint or fixed-interval markers/FEC. An edited draft immediately
withdraws its stale planner estimate. Pending, failed and unsupported drafts
retain **Plan 1 bit**. All timing includes the existing transfer waveform
overhead; completion additionally requires complete absent symbols covering six
seconds. The displayed finish time excludes processing delay.

**Use target for short messages** and **Use target for long messages** explicitly
apply the preview through the existing target/RX-list update. All other planner
actions preserve the draft and modem geometry. Shared power/path/noise edits
update estimates without restarting reception in live mode. Preview
recalculation is bounded in sample count and independent of transmission time;
it generates no waveform, runs no receiver and starts no worker or transmission.
Immutable documents and plots are cached across unchanged presentation polls.

The planner opens with a verdict comparing the actual link budget to the
selected target and checking clock/RAM support. Its power/path/noise input
controls appear once, in the shared top bar. The **Simulation** dropdown offers
only **Yes / No**, and the editable budget dropdowns stay visible in both modes.
CPU/GPU computation estimates appear only with **Yes**; RX success is estimated
in either mode. The CPU/GPU row collapses with **No**, moving the tabs and page
up without moving the top-bar inputs or bottom modem settings. Power presets
run from 100 W to 1 µW.
Typing preserves the entered text; incomplete or invalid values show **Check
link inputs** instead of a stale RX or planner result. Valid edits and presets
restore estimates using the shared accepted budget.
The initial budget is +3 dBm, 170 dB loss and −164 dBm/Hz
noise, yielding −167 dBm received and −3 dB-Hz actual C/N0. These inputs affect
the actual budget separately from the target-driven timing and the existing
normalized observer comparison. Propagation names do not supply unverified
path-loss presets. **Model limits and references** retains the SSB/FT8 and rough
scenario references, oscillator caveats, and normalized LPI assumptions in
[link planning](link-planner.md).

Empty text/raw drafts remain empty and cannot start normal transmission. Their
GUI airtime, inspection, RX and LPI estimates use one raw `0` bit at the short
target and explicitly identify a preview. This does not change byte-API empty
sources or attachment framing. Clearing a draft does not repopulate it with a
default message. Nonempty short/raw input and pending reception are unchanged.

The tab uses one general LPI warning: **LPI is not guaranteed. See model limits.**
Its private-pattern hypothetical state is a short qualifier. While this tab is
selected, the persistent current-draft LPI advisory is hidden so it cannot be
confused with the preview; it remains unchanged on every other tab.

**Transmit noise** appears beside the transmit controls on Console and
Compression. It starts the regular encrypted pattern modulation with fresh
temporary keys and dummy bits, independently of draft/attachment validity or
the selected saved key. Valid modem settings and an idle transmitter are
required; key loading disables startup. **Stop noise** cancels it, and the mode
line shows elapsed time. Drafts, prepared inspection, previous-message history
and saved keys remain intact. Simulation runs continuously without a completion
replay. Automatic transmission capture stays hidden during noise; manual Hex/Bits
show an explanatory caption and no retained temporary keystream diagnostics.

Finite simulation reports generated **audio** percentage separately from elapsed
wall time. Elapsed time continues updating on UI polls while receiver scoring
holds the audio percentage unchanged. Once TX audio ends, the mode line says
**Checking reception after transmission** while complete-symbol absence is
processed. Completion or cancellation freezes the elapsed value; a new run or
reconfiguration resets it. Replay time is separate. None of these presentation
states declares a received message complete. **RX success (noise model)** is
conditional on completed receiver computation, not a measured success rate or
the probability of finishing within a deadline.

The persistent **Mono** toggle below **Audio device** starts enabled. Transmit
audio uses the right channel of a stereo output, with silence on the left;
mono-only outputs use their sole channel. Turning Mono off sends the same audio
to both stereo channels. It follows the audio device control's availability
during transmission. Changing it preserves the running receiver, pending bits,
plots and airtime estimate; the selected routing applies to the next playback.
Simulated samples and exported WAV framing are independent of this choice.

The persistent **Oscillator model** dropdown below the Simulation estimates
offers **Free-running crystal** (the unchanged default), **GPSDO: hobbyist XO
(no oven)**, **GPSDO: TCXO (no oven)** and **GPSDO: OCXO**. A neighboring label
shows the selected effective TX/RX clock mismatch in ppm and phase diffusion
in degrees per square-root second. These are illustrative residual scenarios,
not measured specifications for products: GPS lock does not establish phase
coherence, and low-cost non-oven oscillators retain more short-term instability
than the OCXO scenario. The settings do not control physical clock hardware.
Both sampled simulation and the RX/CPU/GPU advisory model consume the same
selected values. Changing the selection invalidates the old estimate, preserves
the draft and exact wire format, and follows the Simulation dropdown's busy
lock. Other modem edits retain the choice; invalid or stale choices do not
reconfigure a running or closed session.

The persistent **LPI relative observation** advisory below the oscillator shows
the unkeyed energy detector's total observation relative to the receiver's
one-bit design reference, at **90% detection / 1% false alarm per known window**.
Both listeners are normalized to **18 dB Es/N0 for one receiver symbol**,
the existing uncalibrated pattern-planning reference. A ratio of N:1 means N
total observer bit durations versus one receiver duration; additional durations
are `max(0, N - 1)`. This does not mean one accepted bit out of N transmissions
and does not establish a calibrated reception threshold. Keyed
pattern transmission enables the private pattern. With encryption off, including
tone experiments, the same advisory assumes encrypted private patterns at the
current sample, chip and symbol timing at that normalized reference. Its second
line starts with
**Warning: encryption off; hypothetical only**, including when numerical results
are unavailable. Actual public patterns and tones can be easier to detect and
are not described by these figures. No key or waveform setting is changed.
Numerical estimates apply only at normalized in-band SNR of -10 dB or lower.
The model assumes equal C/N0 for both listeners, a known band and on-air window,
and stationary Gaussian noise of known power. Reference C/N0 is derived as
`18 - 10 log10(symbol_seconds)` dB-Hz. Simulation on/off, channel power,
attenuation, noise figure and oscillator presets do not enter the comparison.
TX targets affect it only when they change chip or symbol geometry; local RX
target choices do not change it.

Flow and Transmission inspection share the advisory and its assumptions.
Transmission fields show the receiver reference, observation bandwidth,
reference in-band SNR and noise rise, total and additional observer bit durations,
and full draft airtime relative to the normalized detection duration, including
settling, pulse tails and suppression. This exposure ratio is neither an actual
link-budget estimate, a probability nor a safe traffic quota. Hypothetical
exposure uses the current draft's duration; it does not select an encrypted
automatic profile or re-encode for authentication.
The scenario is also explicit in inspection fields and model details.
Ratios retain fractional durations because an energy detector can accumulate
evidence within a modem symbol. Repeated traffic accumulates exposure, encryption
does not reduce power or interference, and there is no guaranteed hidden traffic.
Invalid drafts/settings and recalculation withdraw stale advisory numbers. See
[the LPI model](lpi-estimates.md) for its equations and limitations.

The persistent **DSP workspace** choice selects 25%, 50% (default), or 75% of
available RAM. Its display includes the resolved MiB/GiB ceiling. The value is
sampled at controller startup and when the percentage changes; other modem edits
keep the same ceiling. Both native adapters use this shared control. Live DSP
and transfer estimates receive the same budget, independently of the 256 MiB
received-message/file cache.

The persistent **Rate** field defaults to `3.6 kHz`, with `18 kHz` also
available as a preset. The adjacent editable **Carrier** dropdown defaults to
`1.5 kHz`. Changing Rate resets Carrier to `1.5 kHz` for `3.6 kHz`, otherwise
to the existing `max(1500, 0.75 × rate)` Hz recommendation. The dropdown offers
the current rate's default carrier and its center frequency (half the rate),
including `1.8 kHz` for `3.6 kHz`. A manually entered carrier override
persists until the next rate change. Both fields configure transmit planning,
receive profiles, live audio, simulation and inspection together. Rate remains
the nominal timing parameter; it does not claim a measured spectral width.
Two editable **target SNR (dB-Hz)** dropdowns select transmit planning:
**Short ≤16 B** defaults to `32` for nonempty text of 1–16 source bytes inclusive
and explicit raw bits; **Long / file** defaults to `55` for longer text, empty
byte sources and every attachment. Both offer the same presets, including `32`
and `55`. The boundary counts source bytes, including UTF-8 and visible prefixes,
not displayed characters or dictionary bits. Draft edits select the corresponding
plan for estimates, inspection and transmission without restarting reception or
replacing its pending data. Noise transmission uses the short target.

Diagnostics show the gross modem bitrate followed by the **Shannon-Hartley
limit**, the ideal Gaussian-noise channel capacity in bit/s. It uses the accepted
draft's TX target C/N0 and nominal Rate as bandwidth; RX targets and simulation noise do
not select this estimate. It updates with accepted settings and retains the last
valid value while a setting contains an invalid draft. The diagnostic tooltip
explains the formula and distinguishes channel capacity from payload throughput.

The **RX targets (dB-Hz)** comma-list initially matches both TX defaults, `32, 55`.
Changing either TX SNR to a valid value replaces the RX list with both targets,
deduplicated when equal. The RX list can then be edited independently without
changing either TX SNR.
It trims and deduplicates valid entries, and resets the entire list to `32` on
invalid input. Its 512-byte edit limit and 750 ms normalization delay allow
comma/minus drafts while keeping native adapters free of parser logic. Receive
profiles hold the selected rate, carrier and pattern/tone mode fixed.
Competing profiles for the same received signal share one pending row. Stronger
supported pattern evidence may revise that row's bits; only the selected
profile's observed physical end can complete it. Native correlation scores from
different DSP paths are not directly comparable, so arbitration caps their
support at the duration of known, admitted symbols. Missing positions add no
support. A delayed weaker profile does not create another received message.
If a much longer symbol first becomes admissible after a shorter interpretation
has already completed, stronger overlapping evidence revises the original row
back to pending. Its obsolete content and copy/save eligibility are withdrawn.
When that evidence spans previously separate fragments, their rows and cached
content are retired under the oldest reception identity. Revision numbers keep
delayed old events from restoring an obsolete interpretation. Completed history
requires actual sample overlap; pending disjoint fragments must also follow the
longer symbol clock within the earlier reception's absence window. Unscored
long-symbol silence alone cannot join a later independently timed transmission.

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
selected Reed-Solomon preset (60% by default) remains saved
while a tiny message reports `Off (short dictionary)`.

A record has a stable ID, enabled/activation eligibility and ordered native text
cells. Cells carry literal text, relative logical rectangles, font size, semantic
tone and emphasis. Negative cell width means remaining row width minus that
amount. The list declaration supplies row height, empty/help text, tail-following
and activation policy. Signal records include frequency, status, preamble/data
quality and message text. Pattern-only rows show model log evidence rather than
dB or a calibrated confidence percentage. Complete discovered raw bits can be
copied without interval validation; provisional prefixes and file rows cannot.
Each newly accepted raw symbol updates the pending row without waiting for a
complete byte or coding interval. Eligible completed short streams show their
dictionary interpretation while retaining the exact raw bits. Whole raw
bytes use the editor's lossless escaped-byte representation, and a partial
final byte uses an exact-bit record. **Paste as message** loads text/byte records
into the composer without interpreting their display escapes as literal
characters. Binary shows the first 16 message bytes.

The Compression page mirrors the entire dictionary encoding of a 1–16-byte
Message draft and previews its expected text. Its separate exact-bit editor
accepts 1–208 bits, including leading zeros and incomplete codes; it preserves
the dictionary preview independently of byte packing. Editing that field
selects raw transmission. Console Binary retains its separate 128-bit edit
limit. Both fields represent the same active draft, so a later Message edit
returns to text encoding and a later exact-bit edit sends precisely those bits.
The transmission layout uses the same short-message limit as the transmitter
and receiver, including the 16-byte endpoint. Dictionary bytes are never rounded
up to transmitted whole bytes merely for display.

This incremental pending view and the short/raw compose paths are
[development requirements](development.md), including when a symbol takes hours
or longer. GUI refactoring must preserve the next-poll update for each accepted
bit and must not interpret source content or mark a row complete before physical
absence establishes the end of reception.

## Behavior every adapter preserves

### Console transmission scope

The first tab includes ten aligned native-text diagnostic rows and an offset
header. The scope captures the active transmitter, independently of the draft
and its estimate. It retains at most 64 source bytes, 256 bits in each data
lane and 32 bytes in each pattern lane. ASCII alphanumeric source characters
keep their original positions; other source bytes display as dots. Empty cells
remain placeholders, and incomplete data bytes show their exact bit prefixes.
Hex view fits all 32 byte columns at minimum window width; Bits view adds
aligned binary digits and horizontal scrolling. A partial byte in Hex view
shows its bit count and the exact prefix beside the row label. Switching views
only redraws the retained capture. The display choice offers **None**,
**Hex, auto-hide**, **Hex** and **Bits**, with **Hex, auto-hide** selected by default.
Auto-hide shows the scope and its caption during transmission and simulation replay, then hides
them on completion, cancellation or failure. None always hides them. The format
choice remains available; Hex and Bits show the retained capture even while idle.
Whenever the scope is hidden, its area collapses: the signal and file browsers
move up and grow, fitting two more full signal entries, and all four plots grow
to fill the remaining space. Showing the scope restores its aligned rows and
the compact browsers and plots on the same presentation update.

Source and compressed-source previews come from the encoding used for that
transmission. Wire rows advance when payload symbol generation begins; pattern
rows sample the first chip of each of the first four generated payload symbols.
Preview reconstruction, settling
noise and suppression noise do not append payload evidence. Simulation replay
uses the capture saved with each frame; the source generator may read ahead of
the simulated receiver. The scope describes generation, not sound-card emission
or a measurement of intercept probability. The last capture remains available
after completion or cancellation and resets for the next transmission.

Input, XOR keystream and resulting bytes sit in adjacent rows. The additional
Wire Plaintext row exposes the complete pre-encryption framing input. The
compressed-source and transmitted-wire offsets describe different stages.
For interval sources, source coding/padding, authentication, FEC and the fixed
markers intervene. **Every wire bit, including each marker, is Data-masked when
encryption is enabled.** Transmitted Bytes therefore shows ciphertext from the
final modem input, never a clear marker beside encrypted content. The exact
short-dictionary and raw-bit endpoints remain unchanged.

Pattern rows show the actual eight input bytes at each sampled symbol start,
used to map circular I/Q amplitude and phase. Columns 00–07 belong to symbol 0,
08–0F to symbol 1, 10–17 to symbol 2, and 18–1F to symbol 3. This makes repeated
public template input visible beside the changing private input, without
retaining a duration-sized pattern. A private Pattern stream replaces the public template;
there is no public-template XOR keystream to display. Its replacement bytes
appear in Pattern Bitstream. DSSS, when enabled, is XORed into those bytes to
produce Transmitted Pattern Bitstream. The Pattern Keystream row explicitly
identifies the absent XOR operation. FHSS is not applied by this transmitter
and its row says so. These are local diagnostic values, not extra wire fields.
The pattern-byte rows are marked as mapper inputs: payload-dependent pattern
selection, pulse shaping and carrier modulation follow. The existing waveform
and transmitted constellation display the generated signal from those stages.

### Native interaction and presentation

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

`desktop_layout.hpp` defines the desktop arrangement at 1180 by 1048 logical
pixels, with a 1030 by 968 minimum. A persistent simulation row holds the
dropdown, modeled receive probability and reference CPU/GPU compute estimates.
The following persistent row holds the oscillator dropdown and selected model
values. A third persistent row gives the LPI advisory two full-width text lines.
The added rows preserve the existing composition, reception and plot allocations.
`control_layout.hpp` computes frame, label,
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

Live waterfall history retains at most 160 rows. Short plots show the newest
rows one per backing pixel; taller plots scale that bounded history to fill the
available height. Startup history remains bottom-aligned with blank space for
unobserved rows. Resizing does not clear or add measurements.

The Console **Pattern evidence** plot places received P0 log scores on the
horizontal axis and P1 log scores on the vertical axis. It preserves the spread
of noise and signal candidates instead of converting their scores into a
saturating probability-like coordinate. Each observation uses its captured
single-symbol admission threshold `T`. The lower half of each axis shows native
log scores from zero to `T` linearly, keeping noise candidates visible. Solid
guides mark `T` at 50%; dashed guides mark `2T` at 75%. **2T means twice the
log score, not twice the evidence or probability.**

Above `T`, the display uses logarithmic interpolation. For `r = score/T`, the
position from `T` to `2T` is `0.5 + 0.25*log2(r)`. Above `2T`, it is
`0.75 + 0.2*ln(r/2)/ln(M/2)`, where `M` is the larger of four and the greatest
retained valid score/threshold ratio across both axes. This fits strong signals
into the upper region with 5% headroom, rather than piling them onto the edges.
Only this upper region adapts when strong points arrive or expire; the noise
region and both guides stay fixed. Ratio logarithms are evaluated without
overflow even for extreme finite scores and small positive thresholds.

The diagonal means equal P0/P1 scores. Distances describe these diagnostic log
scores, not a calibrated probability that a bit is correct. The display transform
changes no receiver decisions. The reference is not a complete admission rule: chain evidence and
the competing-pattern margin also matter. Missing or invalid threshold metadata
cannot produce a plotted point with an invented threshold.

Clicking the Console **Pattern evidence** plot clears its retained points.
Each observation disappears after six seconds, including while input is idle;
polling the same candidate does not refresh its age. New observations can appear
after a clear, even when their scores match an earlier point. The age starts
when a completed pattern window produces evidence, so a long symbol retains
its full scoring duration. Replay uses each observation's first scheduled frame
as its presentation time. Clearing and expiry affect diagnostic display only;
they do not reset acquisition, discard pending bits or complete a reception.

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

## Editing full-window views from shared code

`screen_overlay.hpp` defines the expanded QR view. `OverlayDefinition` contains
ordinary `Control` declarations. Both backends instantiate them through the
same factory used for desktop controls. For example, adding a brightness choice
requires only shared definition changes:

```cpp
Control brightness{Kind::choice, Field::qr_brightness};
brightness.instance = 2;
brightness.label = "Brightness";
brightness.placement = {.left=16, .top=32, .width=180, .height=28};
view.controls.front().placement.top = 80;
view.controls.push_back(brightness);
view.policy.keyboard = OverlayKeyboard::controls;
```

`placement` is relative to the app client area in logical units. Positive
`width`/`height` give fixed extents; zero fills the space between corresponding
insets. `anchor_right`/`anchor_bottom` anchor fixed extents to those edges. Shared
layout also supplies labels, presets, bitmap captions and borders. Controls paint
in declaration order; menu continuation declarations share one widget. Give
repeated controls distinct `instance` values; menu entries use the same menu and
instance when they should share a popup.

`OverlayPolicy.keys` maps logical key strokes and modifiers to commands.
Named keys are Escape, Enter, Space, Tab, arrows, Backspace and Delete, with
Ctrl/Shift/Alt modifiers. `Key::other` reports unrecognized keys and cannot be
bound as a shortcut; printable text goes through the ordinary editor path.
`keyboard=controls` sends unbound keys to eligible native controls; `consume`
discards them. Native popups and active services retain their own key handling.
`hide_background` controls desktop visibility independently of `block_background`
input eligibility. `services=above` allows queued services over the view; `defer`
postpones starting them until dismissal. Already active services retain priority.
`restore_focus` and `dismiss_on_page_change` are shared policy too.
`overlay_layers()` supplies effective visibility, eligibility and stacking order.

`show_overlay()` owns an immutable definition and assigns a fresh generation to
every control's `surface`; zero identifies desktop declarations. Handlers use
the same declared edit, choice, toggle, gesture, action, preset, record and submit
operations as the desktop. The facade rejects closed or replaced generations,
including stale menu callbacks. Native document actions use scoped `dispatch`
and native tabs use `navigate`; programmatic shared workflows retain `activate`
and `select_page`. Covered desktop callbacks cannot change shared state, and
disabling the desktop does not disable a field's separate overlay control.

Both native conformance suites consume `tests/overlay_fixture.hpp` unchanged.
It adds a brightness choice, editor, close action and scoped menu beside the
bitmap, then changes declaration order, geometry, labels and key/service policy.
`gui_overlay` tests the policies and generation lifetimes without a toolkit.
New combinations of existing controls belong in the shared definition; native
painting, widget ownership and event translation remain backend responsibilities.

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
