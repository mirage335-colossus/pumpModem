# Local validation record — version 0.7.2

The application and portable runtime are native C++. Python is optional test
tooling for FLTK/CLI builds and required to embed Rev resources at build time;
it is not installed with the application.

## GUI abstraction audit and completion

The review traced native entry points and helpers, the public facade, control
and document declarations, layout, editor/record input, bitmap transfers,
platform-service dispatch, and CMake/CI dependencies. Three parallel reviews
covered controls, documents/layout, and boundary enforcement, followed by an
integration review and native regressions.

Remaining gaps were corrected: exported modem include paths; stale declared
callbacks after visibility, page or shutdown changes; duplicated UTF-8 edit and
record extent policy; Rev horizontal record scrolling and stale native glyph
measurement; duplicated document action identity/eligibility; unstable focus
when repeated document actions move; and zero/exhausted control allocations.
Native adapters now translate these shared decisions into toolkit operations.
The window title also comes from the shared declaration module.

The architecture check now runs in production builds, recursively checks
alternate source extensions and helper aliases, and rejects hidden toolkit or
backend-specific dependencies in shared code. Regression mutations cover both
forbidden dependencies and valid constructs that must remain accepted. All 17
public headers compiled individually as C++20 without modem/toolkit include
paths. A separate linked contract canary verifies that the application target
does not export modem include directories. A fresh `BUILD_TESTING=OFF` FLTK
configuration also passed its production boundary target.

Earlier Linux Release FLTK validation passed all 20 GUI tests, including the
simulated workflow, native adapter and document suites. The broader 45-test run passed 44 initially;
its live-simulation timeout passed when rerun alone (31.8 seconds). Native package
relocation and corruption checks passed. ASan/UBSan passed all 19 GUI checks other
than the separately validated workflow, with leak detection disabled for the
native toolkit environment. These include the actual FLTK widget/document tests.

Earlier Linux Release Rev validation passed all 22 GUI tests, including production
workflow, native adapter workflow, clipboard/platform checks and 1x/2x coordinates. The exact
profile was software OpenGL, `LP_NUM_THREADS=2`, and Xvfb at 2400x1800x24 with
96 DPI. The native probes also verify zero-area document rectangles, retained
action resizing and framebuffer extents from physical pixel endpoints. Rev
record measurements and geometry now avoid invalidating unchanged rows.
The default llvmpipe thread configuration repeatedly missed the final 50 ms
raw-replay symbol observation on this host; the bounded profile passed the same
unchanged assertions and is now used in the GUI-contract CI matrix. See
[Rev software rendering](rev-backend.md#software-rendering-and-validation).

The follow-up interface audit found and corrected additional gaps: repeated
ordinary declarations could overlap in relative rows; Rev could miss changes to
initially absent labels or shared geometry; typography and caption layers could
remain stale; service editors used different hardcoded limits; and FLTK dialogs
borrowed titles from temporary request storage. Layout identity and complete
geometry results are shared, caption layering comes from `ControlLayout`, and
service requests now declare input limits validated by the common queue. The
Rev file dialog no longer duplicates the controller's no-overwrite promise.
Both native suites use the same new layout lifecycle fixture and exercise
atomic prompt edits with a custom input limit.

Native conformance now runs separately from the production `gui_workflow` in
both backends. Rev previously repeated the entire shared workflow after its
widget probes; that second run repeatedly missed the short terminal raw-symbol
snapshot on this software-rendering host. A later production run reproduced the
same sampling sensitivity. All native probes remain, with the complete shared
workflow executed once through the production binary. The smoke's raw replay
check now accepts an unvisited tail only when completion returns live input,
reports dropped points, and delivers a completed raw signal. Packet replay still
requires observed received symbols, and timing, changing plots, pending reception
and exact raw-bit assertions remain. Deterministic `live` tests additionally
require received symbols in the terminal raw frame and verify skipped-terminal
point accounting at exactly three seconds. This does not establish a guarantee
that a slow native renderer displays every replay frame.

Final follow-up Linux Release validation passed **20/20 FLTK GUI tests** in
25.57 seconds and **22/22 Rev GUI tests** in 52.27 seconds. Both include the
production workflow and dedicated native conformance; Rev also includes actual
platform services and coordinates at 1x/2x. Builds and GUI workflows ran without
overlapping workloads, using the software-GL/Xvfb profile above. Both fresh
temporary directories were empty after success. The strengthened deterministic
`live` suite passed in 31.51 seconds. The final independent source review found
no remaining concrete application-ID decisions in either adapter, and the
production boundary guard and diff whitespace check passed.

The follow-up CLI-only contract/layout/application checks passed 9/9. Focused
ASan/UBSan checks passed 4/4, including FLTK native service title lifetimes and
input validation, with native-toolkit leak detection disabled. Successful smoke
runs now remove automatically created fixtures; explicit output directories and
failed-run evidence are retained. This prevents repeated GUI validation from
exhausting a temporary filesystem with large generated key fixtures.

The subsequent consolidation moved the remaining shared presentation
bookkeeping into `BindingState`, `RecordReconciliation` and
`DocumentPresentation`. Both adapters now consume common option retention,
record changes and document traversal/placement. Shared widget roles and chrome
preferences also cover disabled/focus/hover/selection colors, dialog content and
geometry, popup widths, tooltip timing, checkbox geometry, tab placement and
document content widths. Native glyph measurement, widget ownership, popup
screen fitting and file-browser controls remain toolkit responsibilities.

An independent source audit was performed after this consolidation and before
builds or regression execution. It caught missing bitmap-source and popup-
direction cache dependencies, dialog text measurement inferred from font size,
and native style precedence that could bypass shared disabled colors. These
were corrected and added to the shared/native conformance coverage. The boundary
guard additionally rejects new native RGB literals outside shared palette
conversion.

Final consolidation Release validation passed **24/24 FLTK GUI tests** in
27.10 seconds and **26/26 Rev GUI tests** in 62.28 seconds. Both include the
production workflow and native conformance; Rev also covers platform services
and 1x/2x coordinates. All **21 public headers** compile independently as C++20
without toolkit or modem include paths. Builds and native workflows ran
sequentially with the software-rendering profile above, and successful smoke
directories were empty afterward.

The native runs exposed two integration details. FLTK retained an unnecessary
hidden label for menus; it is now omitted, and the unchanged filtered-menu test
passes. Rev's probes assumed immediate event pumping delivered a paint. On X11,
the queued frame request may not have arrived yet, leaving the old geometry in
place. Tests now await the existing native paint counter, preserving their frame
counts and geometry/input assertions. Palette probes likewise inspect glyphs,
caret and selection after actual layout in both monochrome and color modes.
No production scheduling change was needed.

Visual comparison also caught a Rev checkbox adapter consuming its observable
change flag before the native checkbox could update, and missing disabled
styles on dropdown children. The adapter now observes without consuming that
flag, and maps disabled dropdown text, arrow and field colors explicitly.
Native tests assert unchecked/checked/unchecked paint and active/disabled/
re-enabled dropdown states in both palette modes. Both desktops use the shared
surface background role. Fresh isolated production-window captures were
visually checked for control state, disabled choices, layout, captions and
bitmap areas. The added FLTK surface assertion also passed in a final focused
native run; its production code was unchanged after the 24-test run.

The CLI-only consolidation checks passed **13/13** in 7.44 seconds. Focused
ASan/UBSan checks passed **15/15** in 8.46 seconds, including the new shared
retention/layout/chrome checks and actual FLTK widgets/documents; leak detection
was disabled for the native toolkit environment.

The maintenance scope remains features expressed with existing primitives.
New native primitive types, toolkit repairs and platform services still need
adapter implementations. These Linux runs do not establish Windows runtime
conformance or hardware audio validation.

## Rev backend and shared bitmap extraction

The optional Rev backend pins upstream `clean` at
`d73faa7759b5cfd30d592057790ab458568b569b`. Linux builds used Clang 19, Ninja,
and Mesa 25.0.7 llvmpipe (LLVM 19.1.7, OpenGL 4.5) on a private X11 display.
Controller/declaration, pixel format/tile replay, Unicode boundary and display-free
self-check suites passed. The shared controller smoke covered UTF-8 text, files,
exact leading-zero bits, stale drafts, pending reception and retained saves.
Rev rendered the same workflow, switched inspection pages and checked control
layout, UTF-8 editing, failed-paste selection preservation and modal focus.
Native clipboard tests covered UTF-8, empty versus failed reads, overlaps, stale
responses, incremental long selections and stalled-transfer limits/expiry.

The existing FLTK smoke and focused GUI/model suites passed after its plots were
moved to the shared producers. Bitmap tests also passed under ASan/UBSan. FLTK
plot output was inspected at ordinary and 200% display scale; Rev text wrapping,
Unicode glyphs, QR output and control layout were inspected visually.

A relocated Rev package passed dependency closure, inventory, CLI and full GUI
checks with its checkout, build trees, original installation, development
libraries and system fonts hidden. The path contained spaces and the environment
had empty `PATH` and `LD_LIBRARY_PATH`. Tampered and unrecorded files were rejected.
Host graphics drivers remain external dependencies.

Software OpenGL works but remains CPU-intensive: a 31-second smoke-plus-hold run
used 74 seconds user CPU after paint optimizations and a 10 Hz presentation cap,
versus 211 seconds before. Modem events and plot history remain polled at 25 Hz.
These measurements establish no low-power performance guarantee.
See [Rev backend](rev-backend.md) for build instructions and measurement details.
Windows, GCC 15+, hardware audio and the older Linux ABI release floor were not
validated by this Rev run; macOS/Metal is not integrated.

## Dark QR startup default

The preview now starts in Dark mode before its first draw, and the dropdown reads
that initial state. Every launch uses Dark red (Dark gray in monochrome), including
with an empty message. The dark background level increased slightly from 24 to 32;
Dim remains 64. The Release GUI rebuilt, and the QR suite plus all five focused
GUI/model suites passed.

## QR preview brightness

A dropdown above the QR preview selects Normal, Dim red, Dark red, or Off.
Monochrome output uses gray dimming levels. The selection survives message edits
and affects only the preview; Normal retains the original black-on-white rendering.
The Release GUI rebuilt, and the QR suite plus all five focused GUI/model suites
passed.

An ASan/UBSan rendering harness (with `detect_leaks=0`) exercised the actual App
dropdown callbacks at minimum, default, and enlarged window sizes. On a 24-bit
display, Normal matched the previous QR rendering pixel for pixel; dimming
preserved modules and quiet-zone geometry, and empty/error states stayed dim.
Off remained entirely black while editing. The adjacent waterfall was
unchanged in both color and monochrome. The minimum-size layout was inspected
visually.

On an 8-bit display without a suitable RGB visual, the actual App rendered only
grayscale, with gray dropdown labels and working brightness callbacks. Strict
waterfall pixel comparison was limited to 24-bit displays because colormap
allocation changes quantization on the 8-bit display.

## Muted color presentation

Color output now uses subdued cyan data text and traces, softer neutral labels,
and muted waterfall hues ending in off-white. The monochrome roles and waterfall
intensity/rendering path remain the same as the pre-color commit `924bd03`.

The Release GUI rebuilt and all five focused GUI/model suites passed. Both
`--color` and `--monochrome` passed the display-free self-check. An ASan/UBSan
rendering harness (with the existing `detect_leaks=0` setting) verified 108,500
clipped waterfall pixels across 218 intensity levels against the new palette,
neutral labels capped at 208, and the uniform RGB (144, 192, 184) data tint.
Its full 760-by-540 monochrome image was pixel-identical to the previous grayscale
render. On an 8-bit display without a suitable RGB visual, a color request still
produced entirely grayscale output. The muted rendering was inspected visually.

## Multihue waterfall

The waterfall now uses blue, cyan, green, yellow, orange and red between black
and white. Only the shared 256-entry lookup table and its descriptions changed;
the existing RGB row conversion, grayscale intensities, and control interface
remain unchanged.

All five Release GUI/model suites passed. An ASan/UBSan rendering harness
verified 89,050 clipped waterfall pixels across 213 intensity levels, distinct
blue/green/red regions, unchanged grayscale pixels against the previous render,
and unchanged uniformly tinted constellation points. The multihue output was
also inspected visually.

## Optional color presentation

Color is now enabled by default when an RGB visual is available. `--monochrome`
explicitly selects grayscale; `--color` restores color preference, with the last
switch taking precedence. The existing automatic grayscale fallback is unchanged.
The default/override update rebuilt successfully and passed the five Release GUI
model/self-check suites plus command-line override acceptance checks.

Release and ASan/UBSan builds succeeded, and all five focused GUI/model suites
passed in both configurations. `--color` is documented by `--help` and accepted
alongside the display-free self-check.

An isolated rendering harness exercised the actual FLTK fields, waveform,
constellation, and waterfall widgets under ASan/UBSan with the CI leak-detection
setting. Pixel comparison verified 89,050 clipped waterfall pixels over 213
intensity levels against the shared palette, and identical constellation-point
positions with one uniform tint. The initial cyan/mint palette had nondecreasing
channels; the later multihue palette above intentionally varies hue instead.
Both presentations were visually inspected. On an isolated 8-bit display without
a suitable RGB visual, a color request produced an entirely grayscale image.
The waterfall retains its Gray8 buffer and converts color rows only when enabled.
These checks exercise rendering and GUI policy; no physical-display or Windows
performance measurements were made.

## Single-backend configuration and monochrome GUI

The GUI rebuilt in Release and ASan/UBSan configurations. All five focused GUI
model/self-check suites passed in each configuration; all four toolkit-independent
GUI suites also built and passed in a fresh CLI-only configuration.

Fresh configuration accepted the default and explicit `fltk` selection and
rejected empty, multiple, and unimplemented selections. GUI-off configuration
skipped FLTK even with an unavailable backend value. The CLI-only executable
built and ran `--help` with shared OpenSSL: this host's separate static OpenSSL
installation lacks its required zstd link dependency without the existing local
development support setup. The dependency policy was not changed.

The Release GUI workflow passed on a private 1440-by-1100 virtual display,
covering keyfile operations, transmission, exact binary reception, clipboard/save,
replay, three tabs, resizing and complete diagram scrolling. Console and modem-flow
layouts were inspected visually. Dynamic dependencies contained no GL/EGL,
GTK/GLib, Cairo, or Pango library. `--help` and `--version` reported `fltk`.

The sanitizer workflow passed its behavior checks. An initial run with leak
detection enabled reported 41,319 bytes of Fontconfig allocations at shutdown;
the model/self-check suites passed with leak detection enabled outside the
sandbox. The GUI workflow was repeated using the existing CI setting
`ASAN_OPTIONS=detect_leaks=0`, retaining address and undefined-behavior checks.
After restoring scrolling for overflowing signal text, a later sanitizer run
timed out during the file-transfer stage despite the earlier completed workflows.
An unchanged-binary retry completed with exit status zero. The cause of the
intermittent timeout was not established. The final Release model/self-check
rerun passed all five suites; those checks do not cover this workflow failure.

This validates the current FLTK implementation, not a second adapter or MCU
deployment. The controller/declaration/bitmap extraction remains proposed work.
Windows, physical audio, and representative slow hardware were not exercised.

## Version 0.7.2 static full-pattern inspection

The standalone Constellations tab is removed. Modem flow now displays all legal
full-pattern symbols as phase/amplitude chip rows, a full-vector distance map,
and matched/off-pattern energy for legal, shifted, unused and mean-noise cases.
Every code position is inspectable through pagination; metrics always use the
entire actual symbol, including repeated periods and partial chips.

The new model tests first failed for the missing implementation. They then
compared every displayed chip against the previously compiled transmitter's
analytic preview and actual PCM, before rebuilding the modem with its extracted
shared code generator. This checks that sharing the generator preserves the
existing on-air sequences. Coverage includes every 2–6-bit APSK alphabet,
fixed/tone/keyed/DSSS modes, exact time-weighted distances, truncated and repeated
periods, antipodal timing ambiguity, public keyed illustrations, 16,384-chip
codes, long integrations and 30 MHz configurations.

The strengthened real-PCM acquisition test uses a 1,024-chip keyed pattern,
4APSK, -15 dB sample SNR, erased training and a 17-sample delay that changes the
carrier reference by 90 degrees. It measures per-chip Es/N0 below -7 dB, recovers
the complete authenticated packet without Reed-Solomon, and rejects the same
capture with a wrong code and a noise-only capture through finish.
This verifies acquisition within the receiver's bounded timing bank, without
hard chip decisions or a clean chip constellation. It does not establish
arbitrary clock/frequency tracking; accelerated matched-symbol simulation is
not used as evidence of blind chip acquisition.

* All **29 native CTest suites passed** across the full run and focused GUI
  rerun. The new keyed inspection fixture initially omitted its required key;
  the corrected fixture retains the production key requirement.
* All five GUI/model suites passed in Release (0.30 seconds) and under
  ASan/UBSan (2.28 seconds). The below-chip-noise PCM regression also passed
  under ASan/UBSan.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.2.
* The Release and ASan/UBSan GUI workflows passed with three tabs, current static-pattern model
  binding, unchanged live plots, three-second packet/raw replay and complete
  scrolling at minimum and expanded window sizes.
* Native visual checks covered fixed16, tone16, keyed128, all 64 APSK values,
  last-page access for 16,384 chips and a weak-signal example with -37.8 dB
  nominal chip Es/N0 and +10 dB integrated Es/N0. All four navigation buttons
  and static redraw invariance passed. The colour scale preserves distinct
  outer amplitude rings. Strict GUI and harness warning checks passed.

No wire format, keyfile or runtime dependency changed. Physical audio devices,
battery-state comparisons and hosted Windows/CI execution were not tested.
LeakSanitizer is disabled for this host's tracing environment; ASan and UBSan
remain enabled.

## Recorded 0.7.1 pattern constellation checks

The new Constellations tab shows the existing phase/amplitude observations beside
the full analytic pattern projection in modeled noise units. The new numerical
tests first failed because the projection did not exist, then passed with the
bounded model. They cover every supported alphabet, pairwise template distances,
fixed/keyed/tone equivalence, quantized integration time, sample-clock invariance,
unsnapped observations, invalid numeric inputs, hour-long integrations and 30 MHz
plans. A deterministic 16,000-trial Monte Carlo check uses the modem's actual AWGN
generator to verify the modeled radial and differential tangential variance.

* Native GUI Release: all **28 CTest suites passed** (54.72 seconds).
* After the final layout adjustment, all four focused GUI/model suites passed
  in Release (0.26 seconds) and under ASan/UBSan (1.93 seconds).
  The complete GUI source also passed strict warning checks with `-Werror`.
* The CLI-only Release rebuilt with Python discovery disabled and reports 0.7.1.
* The complete Release and ASan/UBSan GUI workflows passed on a private virtual
  display, including all four tabs during
  continuous reception and three-second replay, minimum/expanded resizing,
  exact observation projection, exclusion of unmatched I/Q, retained fresh-point
  batches and clearing replay points on return to live reception.
* The plot was visually inspected at the actual minimum 479×383 panel size
  for 8- and 64-symbol alphabets, using real `add_awgn` differential observations
  at moderate SNR and ideal-only high-SNR cases. Pixel margins preserve outer
  markers and labels, and the closest-pair labels remain separate in dense views.

The axes represent the analytic AWGN model, not a measured receiver likelihood.
The differential-noise ellipses use the weakest configured reference ring;
finite real-PCM quadrature covariance, clock drift and phase noise can differ
from that approximation. No modem, packet, keyfile or replay format changed;
0.7.1 remains compatible with 0.7.0. No runtime dependency was added.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this visualization release. LeakSanitizer is disabled for this
host's tracing environment; ASan and UBSan remain enabled.

## Recorded 0.7.0 framing checks

The new tests first reproduced mandatory RS on a tiny message and receiver
synchronization before rejection of a damaged short frame. The format removes
the magic byte and fixed-width body length, uses a variable compact header,
automatically disables all RS below 16 original bytes, and carries header and
body in one continuous symbol stream. Bounded provisional decoders verify
complete short frames before synchronization; initial phase hypotheses allow
reception without relying on header RS to repair the first symbol.

* Native GUI Release: all **28 CTest suites passed** (67.93 seconds).
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (49.38 seconds).
* Final Debug ASan/UBSan with `-O1`: all **27 CTest suites passed**
  (424.14 seconds), including the instrumented vendored codec, full streaming
  acquisition, transfer, live sessions, CLI, audio contracts and GUI models.
  The initial unoptimized nine-suite sanitizer run and the separate broader
  compact-bootstrap acquisition matrix also passed.
* Short provisional-reception tests passed under ASan/UBSan: corrupted complete
  packets never establish lock, subsequent valid packets recover, noise alone
  does not synchronize, and extremely long observations remain bounded.
* The complete Release and ASan/UBSan GUI workflows passed on the private virtual display,
  including automatic FEC Off below 16 bytes, restoration of the longer-message
  FEC choice, all three tabs, pending previews and exactly three-second replay.
  The compact transmission diagram and processing flow were visually inspected.
* Native ZIP and TGZ bundles passed fresh extraction into directories containing
  spaces, manifest and dependency checks, short/long compression simulations,
  and complete GUI workflows with package-search paths cleared. Their 42 ELF
  files have no GTK/GLib dependency; the local glibc requirement is at most 2.38.

The packet tests cover original-size boundaries 0/1/15/16/255/256, canonical
variable lengths, maximum metadata, every content kind, header correction up to
the actual parity budget, and malformed frames with otherwise valid integrity.
Receiver and transfer tests span two through six bits per symbol, keyed and
plain frames, missing training, continuous partial-bit boundaries, noisy gain
hypotheses, and recovery after a corrupted short body. Live tests preserve the
fresh-symbol limit across timing/gain changes and fit hours of simulated airtime
within a 1 MiB workspace.

A deterministic same-wire oracle confirmed that the old marginal-SNR CLI fixture
contains one to three actual bit errors in its now-uncoded six-byte message.
Those packets correctly fail integrity. The lifecycle fixture now uses the
healthy 3 dBm / -90 dB preset, retaining default crystal drift and phase noise;
noisy packet rejection is tested separately. Replay-start checks permit only a
new, empty, unverified frame-zero status and still forbid cancelled content.

Profiling the same PCM cases before and after nonthrowing acquisition probes
reduced two/three/four/five/six-bit trial times from
0.415/1.038/2.115/3.547/5.082 seconds to
0.179/0.211/0.278/0.531/0.732 seconds, with identical header decisions. Fixed-size
probe caches and conservative RS shape checks avoid repeated rejection work;
these measurements are local observations, not throughput guarantees.

The unoptimized Debug sanitizer live test reached its existing 60-second
computation deadline midway through multi-key reception. Sanitizer compilation
now uses `-O1`, retaining debug symbols, assertions, ASan/UBSan and frame pointers.
This also covers the already-instrumented vendored codec and standalone audio
tests. Normal Release, portable and MSVC compilation flags are unaffected.
The final full live suite passes with its original deadlines and assertions.

Both peers must use 0.7.0 framing. Raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.6.0 checks

* All **28 native/core CTest suites passed** across the full run and focused CLI
  rerun. The original CLI fixture assumed 65 KiB of repetition could not fit the
  repeat airtime policy; it now checks both compressed acceptance and explicit
  uncompressed rejection. The final CLI suite passed in 20.50 seconds.
* CLI-only Release with Python discovery disabled: all **23 suites passed**
  (37.70 seconds).
* All **nine focused ASan/UBSan suites passed** (254.87 seconds): packet,
  compact format, both compression codecs, transfer, inspection, plots, GUI
  policy and GUI self-check. The vendored liblzma C code was instrumented too.
* The compact-bootstrap acquisition suite passed under ASan/UBSan, covering
  two-ring gain aliases, noisy one-byte packets, supported constellation widths,
  missing-ring hypotheses, exact symbol padding and PCM boundaries.
* The complete GUI workflow passed in Release and ASan/UBSan on the isolated
  display. It covers text/file/raw reception, key generation, pending results,
  three-second replay, clipboard, file saves, tab switching and diagram layout.
  The final inspection also requires LZMA2 for its long text example. The flow
  and compact-bootstrap transmission views were visually inspected.

New tests first demonstrated the 72-byte bootstrap and short-window two-ring
acquisition failures, then passed with the 16-byte format and alternate gain
hypotheses. Packet tests cover four damaged header bytes, actual compression,
incompressible fallback, bounded original sizes, malformed lengths with valid
CRC/RS/SHA, and no transmitted dictionary identifier. Short-code tests cover
all bytes, canonical tokens/padding and bounded truncated previews. LZMA2 tests
include an independently generated `xz --format=raw --lzma2=preset=9e,dict=4KiB`
vector, strict stream endings, invalid controls, expansion limits, previews and
an enforcing scratch allocator. Vendored source hashes are checked by CTest.

The new compression dependency is pinned source compiled statically. No Python
or destination compression package is needed. Packet wire compatibility with
0.5.x is deliberately removed; raw-bit and keyfile formats are unchanged.
Physical audio, battery-state comparisons and hosted Windows/CI execution were
not tested for this release. LeakSanitizer is disabled for this host's tracing
environment; address and undefined-behavior instrumentation remain enabled.

## Recorded 0.5.7 checks

* Native GUI Release: all **24 CTest suites passed** (28.92 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.86 seconds).
* All six focused ASan/UBSan suites passed: packet, transfer, inspection model,
  GUI plots, GUI policy and GUI self-check (128.84 seconds). LeakSanitizer was
  disabled for this host's tracing environment.
* The four Release GUI suites passed again after the final receiver-description
  correction. The complete GUI workflow passed in both Release and ASan/UBSan
  on the isolated virtual display, using simulated input.
* GUI workflow checks cover all three tabs during idle reception and replay,
  asynchronous RS20/RS60 and raw/packet model changes, invalid-input clearing,
  retained composition and receiver state, 1030×786 and 1400×1000 window sizes,
  and scrolling each diagram to its end and back. Packet and raw reception,
  clipboard handling, file saves and three-second replay remain covered.

The inspection tests were written before the model implementation. They verify
encoder-derived compression, keyed integrity, mandatory bootstrap parity with
body FEC Off, independent five-bit symbol padding, full and shortened RS20/RS60
blocks, physical byte totals, and omission of actual payload, metadata values
and key material. Receiver steps distinguish incoming-header selection from
outgoing settings. Raw diagrams add no packet overhead; hour-long and 30 MHz
plans remain bounded without generating waveforms. Strict C++20 warning checks
passed for the model and native diagram code.

The flow, transmission sequence and chosen constellation panels were visually
inspected in the running GUI. Packet, keyfile and waveform formats are unchanged.
No Python, GUI toolkit or other runtime dependency was added. Physical audio,
battery comparisons and hosted Windows/CI execution were not tested for this
GUI inspection release.

## Recorded 0.5.6 checks

* Native GUI Release: all **23 CTest suites passed** (28.21 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.58 seconds).
* New modem/transfer raw-reception cases passed under ASan/UBSan, including
  partial symbols, measured constellation points and arbitrary-bit-offset
  encryption. LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions (91.30 seconds), GUI
  plots, GUI policy and GUI self-check. The final GUI suites also passed in Release.
* The new end-to-end requirement first failed against the unchanged 0.5.5
  library: raw simulation produced no received bits at the presentation deadline.
  The corrected Release GUI workflow recovered encrypted `001`, displayed the
  pending and completed raw rows, and copied exactly those bits.
* The final GUI workflow also passed under ASan/UBSan, including a visible
  read-only FEC Off value in Binary mode and restoration of the saved packet
  setting. The completed `001` row and controls were visually inspected on the
  private virtual display, without using physical audio or the desktop clipboard.

Receiver tests span all supported constellation widths and one through seventeen
bits, preserve leading zeros, and check that altered/noisy observations change
decisions. Live tests cover pending-to-complete presentation at exactly 3000 ms,
no early computation results, keyed partial symbols with packet RS controls
selected but no on-air overhead, no fabricated packet/accuracy metadata,
cancellation, replacement, and an hours-long symbol within a 1 MiB DSP budget.

Raw simulation supplies nominal start timing, length and a carrier reference;
it does not implement blind real-audio raw discovery. Received raw bits have no
FEC or integrity check and can contain channel errors. Packet and raw transmitted
waveforms remain compatible with 0.5.5. Physical audio, battery comparisons and
hosted Windows/CI execution were not tested for this release.

## Recorded 0.5.5 checks

* Native GUI Release: all **23 CTest suites passed** (28.35 seconds).
* CLI-only Release with Python discovery disabled: all **19 suites passed**
  (31.62 seconds).
* Focused raw streaming-modem and transfer tests passed under ASan/UBSan.
  LeakSanitizer was disabled for this host's tracing environment.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 96.34 seconds.
* Native Release GUI workflow passed on the isolated virtual display, including
  exact encrypted three-bit transmission, explicit source selection and return
  to live plots. No physical audio device was used.
* The complete GUI workflow also passed under ASan/UBSan. The new binary editor,
  three-bit count and millisecond airtime were visually inspected on that display.

Physical audio, battery comparisons and hosted Windows/CI execution were not
tested for this release. Existing packet waveforms and keyfile formats are
unchanged; the GUI raw binary format is separate from legacy CLI DBPSK status.

## Binary transmission coverage

New regressions check leading-zero binary input, invalid input and explicit
source selection; exact unframed airtime for one through seventeen bits across
all supported constellation widths; partial final symbols that vary both phase
and amplitude; PCM/integrated agreement; and stream-key masking without extra
bits. The GUI workflow includes selected-key three-bit transmission beside a
retained message and attachment, with irrelevant packet controls disabled.

Live-session tests exercise the actual playback branch through a link-time audio
adapter, check its exact sample count, and simulate a one-symbol raw signal with
an injected presentation clock. They cover the three-second deadline, measured
input plots, no invented packet results, invalid-input nonmutation, cancellation,
return to live noise, and an hours-long symbol with a 1 MiB DSP workspace.

## Recorded 0.5.4 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All four focused ASan/UBSan suites passed: live sessions, GUI plots, GUI policy
  and GUI self-check. The Debug live suite took 92.63 seconds. LeakSanitizer was
  disabled for this host's tracing environment.
* The new GUI workflow reproduced early verified reception against the previous
  backend, then passed with the timed event implementation. Text and file rows
  remain pending across earlier GUI polls; verified content and accuracy appear
  only when the three-second presentation completes.
* The workflow also replaces a replay after pending reception appears, cancels
  its replacement, and checks through the original deadlines that neither
  interrupted packet enters the inbox. A running pending row was visually
  inspected on the private Xvfb display.
* The complete timed GUI workflow also passed with ASan/UBSan. GUI checks used
  simulated input on the isolated display, with no physical audio device.

Deterministic presentation-clock tests cover the entire fixed-training/packet
timeline, no early browser or receipt events during computation, incremental
pending text, paired final signal and packet delivery at exactly 3,000 ms, and
duplicate-free snapshot reads. Delayed polling flushes due events in order
without extending the deadline. Two queued simulations each receive their own
three-second timeline. Cancellation, replacement and configuration discard
future results, including pending events made due before the next GUI poll.
A cancellation after the deadline preserves an already completed reception.

CPU cancellation is exercised during actual streaming progress. Failed decoding
never produces verified content. The hours-long-symbol simulation still uses
bounded CPU work and fits the 1 MiB DSP fixture, with fewer presentation frames
to accommodate preview text and result diagnostics. Packet content uses the
separate receive-content quota. Physical audio, battery comparisons and hosted
Windows/CI execution were not tested for this release.

## Recorded 0.5.3 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* All five focused ASan/UBSan suites passed: packet, live sessions, GUI plots,
  GUI policy and GUI self-check. The Debug live suite took 77.75 seconds.
  LeakSanitizer was disabled for this host's tracing environment.
* The native Release GUI workflow passed on an isolated Xvfb display. Both text
  and file rows carried reception percentages, with exact data counters matching
  their verified packets. The labels were also visually inspected in the running
  window alongside the scrolling messages.
* The instrumented native GUI workflow also passed with ASan/UBSan. All GUI
  workflows used the isolated display and simulated input, without opening a
  physical audio device.
* The full standalone streaming suite passed. Targeted preamble cases passed
  under ASan, UBSan and float-cast-overflow instrumentation. All linked core
  translation units used the current diagnostics layout.
* Exact packet-bit accuracy tests passed in Release and ASan/UBSan, covering
  no-FEC packets, header/parity-only repairs, known bit flips in full and ragged
  interleaved blocks, compressed content, authenticated ciphertext corruption,
  and exclusion of trailing bytes. Failed validation never supplies accuracy.

Preamble fixtures cover clean training, half replaced by silence, missing training
replaced by silence or noise, 18 dB noise, capture beginning halfway through
training, leading silence, PCM/integrated agreement, and training retained across
a bootstrap using hour-long symbols. Coarse observations remain unknown. Very
large finite inputs exercise the numeric overflow guards. Every valid fixture
also checks that blind acquisition still returns the correct packet.

The new independent scanner has **11,520 bytes** of fixed storage per receiver
on this x86_64 build. Receiver admission and reported workspace include it.
Neither scanner storage nor runtime on a coarse integrated observation scales
with an hour-long symbol. Recognition remains a conservative diagnostic with
documented phase/amplitude thresholds, not a calibrated radio sensitivity test.

After the test workers finished, the generated-noise benchmark with thirteen
keyed epochs, three bits per symbol and a 6 kHz internal clock processed 2.66 times
real time. The preceding 0.5.2 comparison processed 2.32 times real time. These
short measurements on a shared host do not establish a speed improvement or
real-time guarantees for other profiles and machines.

Packet, keyfile and waveform formats remain compatible with 0.5.2. Physical audio,
battery comparisons and hosted Windows/CI execution have not been tested here.

## Recorded 0.5.2 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery disabled: all **19 suites passed**.
* Final live-session and GUI projection/policy/self-check suites passed again
  after adding slow-symbol labels and the cancelled-transmission serial guard.
* ASan/UBSan passed the streaming-modem suite with the new symbol-drain API,
  and all four final live/GUI suites. The standalone streaming translation unit
  used `-O1` and linked the instrumented core library; the final live suite used
  the normal Debug build and took 72.93 seconds. LeakSanitizer is disabled on
  this ptrace host.
* The native GUI workflow passed in Release and ASan/UBSan: production keyfile
  generation, text/file reception, clipboard and exclusive saves, chronological
  replay, replacement by a new transmission, Stop replay, and return to live
  waveform, waterfall and input constellation.

A separate injected monotonic presentation clock tests every 50 ms frame of a
60-frame replay and its exact three-second deadline. Repeated reads retain the
same frame. The tests verify early payload acquisition, fresh measured symbol
batches after lock, changing waveform/spectrum, and an end frame containing the
transmission rather than decoder-tail noise. A GUI that skips frames receives
their pending compatible symbol points together. A GUI that misses the deadline
returns directly to live input and reports the unseen points as omitted.

The new symbol-drain tests cover rotated received signals, unsnapped off-grid
measurements, differential coordinates, acquisition, repeated drains, ring
overflow counts, resets, and agreement between PCM and integrated transmission.
The live audio adapter checks empty intervals between slow symbols and that
cancelled TX points cannot return after live input resumes. Long simulated
symbols remain CPU-bounded and fit their existing 1 MiB test budget, reducing
replay capacity when necessary.

Packet/keyfile formats and transmitted waveforms remain compatible with 0.5.1.
These presentation changes do not add carrier tracking or remove the recorded
CPU limits below. Physical audio and battery-state comparisons have not been
performed for this release. Hosted Windows/CI execution remains unverified here.

## Recorded 0.5.1 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* All **22 ASan/UBSan CTest suites passed**, including the separate dense PCM
  boundary stress suite (302.93 seconds in Debug). LeakSanitizer is disabled
  because this host's ptrace environment prevents it from starting.
* The native GUI workflow passed in both Release and ASan/UBSan builds on a
  private Xvfb display. It covered production keyfile generation and selection,
  overwrite refusal, idle input, verified text/file reception, clipboard copying,
  exclusive saves, held simulation diagnostics and all plots returning live.

## Evidence for the 0.5.1 changes

Automatic audio uses `carrier = max(1500, 0.75 * bandwidth)` Hz and
`Fs = max(6000, ceil(4 * bandwidth))`. Nominal symbol timing still depends on
bandwidth; hardware clocks remain separately negotiated. Tests cover planning
from 1 Hz through 30 MHz, fractional bandwidths, manual CLI overrides and actual
PCM packets at the new carrier/rate combinations. A 100 Hz packet also crosses
four cascaded 300 Hz high-pass sections and separate 48/44.1 kHz conversions.

The received-tone regression exercises the actual capture converter at both
44.1 and 48 kHz with irregular input blocks. It checks sample values and fitted
I/Q against a known 1573 Hz tone at a 6 kHz logical rate, within 1e-5. The old
fractional-cycle I/Q fit fails this test. PCM acquisition now accumulates the
carrier projections over each candidate's exact chip and symbol boundaries,
then solves their Gram system. Dense 4/5/6-bit tone and pattern fixtures inspect
wire bytes before packet error correction, including fractional-cycle chips and
delayed, fragmented input. This focused fixture validates against the known
bootstrap to separate integration accuracy from blind-acquisition startup
aliases; normal transfer/live tests retain the real protected-bootstrap search.
Undelayed tones compare every bit. Delayed/patterned cases exempt only the first
symbol's unknown differential phase; its amplitude and all following bits must
match before FEC.
End-of-capture flushing remains bounded for hour-long
symbols. Simulation receivers reset before switching back to idle PCM.

The waveform now defaults to four carrier cycles, using captured guard samples
and a bounded 64-tap Blackman-windowed sinc for the line between measured sample
dots. Tests cover exact sample knots, DC gain, impulse response, linearity,
allocation bounds, and reconstruction error below 1e-4 through 0.4 times the
sample rate. The production-size plot was visually inspected on an isolated
display. Dense overviews continue to show raw extrema.

These are generated-signal, converter and software-driver tests. A physical
audio link and an AC-versus-battery comparison have not been measured here.

With other test/build workers stopped, the 6 kHz receiver bank with thirteen
keyed epochs processed generated noise at 2.94x real time for the automatic
three-bit profile and 8.33x for six bits. A continuous 1500 Hz carrier at amplitude
0.35 measured 8.33x and 0.68x respectively. The dense six-bit carrier case is a
known CPU limit: its ambiguous amplitude lattice keeps blind bootstrap searches
busy. These single-host measurements do not establish battery-state performance
or real-time operation for every signal/key-bank configuration.

## Recorded 0.5 checks

* Native GUI Release: all **23 CTest suites passed**.
* CLI-only Release with Python discovery explicitly disabled: all **19 suites
  passed**, including relocation checks.
* Standalone GUI plot-projection tests and the actual-transmitter signal-view
  regression passed. Release whitening, FEC and transmit-history tests passed
  as part of the integrated suites.
* All eight focused AddressSanitizer/UndefinedBehaviorSanitizer suites passed:
  transfer, streaming modem, live sessions, signal view, GUI plot projection,
  GUI state policy, GUI self-check and the CLI integration suite. LeakSanitizer
  is disabled because this host's ptrace environment prevents it from starting.
* The Release GUI workflow generated and loaded a production 128 MiB keyfile
  with named keys while reception continued. It checked every selected
  key against its matching MAC, including names containing menu punctuation
  and a key named `None`, then completed text/file loopback, clipboard copying,
  exclusive saves, held simulation plots and live resumption. Final GUI policy
  and self-check suites passed again after the menu fix.
* The final GUI workflow also passed under ASan/UBSan. Its focused fixture uses
  two generated keys (`A|B` and `None`) and one admitted epoch; all ten menu-name
  edge cases remain in the real FLTK self-check. Drift-window acquisition is
  covered by the separate transfer/live sanitizer suites. The broader initial
  GUI fixture passed in Release but exceeded its sanitizer deadlines. The GUI
  test supports `--smoke-timeout 300`, also configured for CI.
* The local installed bundle passed an audit of all 42 ELF paths: no GTK/GLib
  dependency and a maximum required glibc version of 2.38.

Final TGZ and ZIP artifacts use `tools/verify-native-archives.cmake` to check
extracted inventories, native dependencies, isolated CLI/GUI commands and ABI
requirements. Historical measurements below apply to 0.4.

## Evidence for the 0.5 changes

The default 4.8 kHz internal clock and 900 Hz carrier produce **384 cycles in a
2,048-sample frame**. Drawing that entire frame into roughly 300 pixels aliases
a clean sine wave into apparent blocks. A numeric probe found maximum error of
3.3e-9 against the expected sampled sine, 2.1e-13 between contiguous and fragmented
TX reads, 8.9e-8 after conversion through a 48 kHz audio clock, and 6.4e-14 between
ideal accelerated preview and actual PCM. The carrier FFT level matched its
expected amplitude. The regression checks continuous frame phase, primary FFT
power and suppression of aliased square-wave harmonics by more than 70 dB.

The 0.5 waveform view defaulted to twelve carrier cycles, retained actual sample
values, and uses extrema when zoomed out. Waterfall tests check peak preservation,
a shared color scale, bounded history and clearing on frequency-axis changes.
Independent enumeration confirmed that all 1,025 original FFT-bin positions map
to the same display columns in live and compact review paths.

Public audio whitening reduces data-dependent constellation bias. In a structured
16APSK probe, empirical symbol-occupancy entropy increased from **3.640 to 3.956
bits/symbol**; its maximum is 4. A 64APSK regression with a 4 KiB zero-filled
payload visits every symbol and exceeds **5.97 bits/symbol**, for both keyed and
plain audio. These are finite-frame occupancy measurements, not added payload
entropy or a capacity measurement. Tests also cover the fixed protocol vector,
chunk/offset invariance, reversibility, unchanged training and raw packet formats,
and FEC correction/authentication across all five constellation sizes.

Transmit diagnostics retain a bounded chronological payload-symbol history.
Tests exercise ring overwrite, exclude training, compare PCM and accelerated
histories, and verify that partial observations of hour-long symbols do not
create duplicate points.

Audio peers require matching **0.5 modem settings**. Packet and keyfile formats
remain unchanged. Whitening is public and reversible; it does not conceal
repeated frames or guarantee low probability of intercept. Rectangular pulse
sidelobes remain. See [modem.md](modem.md) and [protocol.md](protocol.md).

## Recorded 0.4 baseline

These measurements were made on Linux x86_64 with GCC 14.2, CMake 3.31,
OpenSSL 3.5.7 and vendored FLTK 1.4.5. Version 0.4 passed 22 native Release,
19 Python-disabled CLI and 21 ASan/UBSan suites across integrated and focused
runs. The final live sanitizer suite took 97.27 seconds. LeakSanitizer was disabled
because this host's ptrace environment prevented startup.

The retained regression coverage includes 1 Hz–30 MHz bandwidth planning;
`max(64, ceil(4 * bandwidth))` internal clocks; bounded multistage resampling;
independent 48/44.1 kHz packet paths; default 100 ppm crystal error and 0.5 degrees
RMS phase diffusion per square root second; erased-training acquisition; adaptive
4/8/16/32/64-APSK; and exact byte, bootstrap and FEC boundaries. Conversion from
120 MHz to 64 Hz stays below 5 MiB. An ideal-clock packet with hour-long symbols
decodes, while its bad-crystal counterpart fails validation rather than assuming
impossible carrier coherence.

Continuous tests cover idle plots, authentication, multiple keys/epochs,
cancellation, consecutive messages and the two-second simulation review. All
three plots return to live reception afterward. A clock that jumps an hour per
query verifies that simulation preserves its admitted epochs through preparation
and decoding, then admits fresh epochs for the next burst. ALSA/WinMM fixtures
cover default discovery, partial I/O, rate conversion, cancellation and buffer
lifetimes. Cryptographic vectors, production keyfiles, QR, Unicode, content
bounds and exclusive saves remain covered.

The 0.4 native GUI workflow passed normally and under ASan/UBSan: provisional to
verified text, consecutive transmissions, exact UTF-8 clipboard copy, files-only
save listing, exclusive binary save, cache clearing and held/live plots. Its
layout was inspected on an isolated 1400×1100 display. This did not exercise a
physical audio link or compare AC and battery power states.

### 0.4 audio configuration and CPU measurements

A zero-PCM default-device probe negotiated these formats:

| Logical rate | Hardware rate | Converter passband | Playback workspace |
| ---: | ---: | ---: | ---: |
| 64 Hz | 48 kHz | 26.88 Hz | 118,348 B |
| 4.8 kHz | 48 kHz | 2,016 Hz | 119,296 B |
| 9.6 kHz | 48 kHz | 4,032 Hz | 120,256 B |
| 96 kHz | 48 kHz | 20,160 Hz | 233,792 B |

No sound was emitted and nothing was recorded. Passbands are converter
calculations, not measured analog response. Unsupported bands are rejected
before expensive audio processing; a high internal rate does not create an SDR
frontend or overcome a sound card's physical passband.

One synthetic 48 kHz → 4.8 kHz conversion processed one second in 15.42 ms,
with 647,624 bytes of workspace and 5.81 ms setup. Eight input chunks produced
exactly 4,800 samples. A separate generated-noise receiver benchmark measured
7.77× real time for automatic three-bit modulation and 13.74× for forced six-bit
modulation, using a 4.8 kHz clock and thirteen keyed epochs. Other build/test
workers were stopped. These single-host CPU measurements are not end-to-end
audio, battery-state or cross-machine performance guarantees.

### 0.4 delivery baseline and continuing limits

Relocation checks hide the original installation and exercise a copy in a path
with spaces, empty `PATH`/`LD_LIBRARY_PATH`, and invalid Python paths. They verify
dependency closure and reject modified or unrecorded files. Archive/ABI fixtures
cover TGZ/ZIP extraction, checksums, accidental GTK/GLib linkage and excessive
glibc requirements. The 0.4 package audit covered 42 ELF paths, found no GTK/GLib
dependency, and measured a maximum glibc requirement of 2.38.

GitHub Actions jobs have not run here. Windows binaries and the configured glibc
2.35 compatibility floor require a successful hosted workflow run. Local builds
on this newer host do not inherit that compatibility floor.

The accelerated channel remains a bounded matched-chip model. It has no
chip-clock recovery or carrier-tracking loop and does not demonstrate calibrated
sensitivity, arbitrary long encrypted-pattern acquisition, near-capacity
throughput, physical SDR operation, low probability of intercept, or Windows
driver reliability. See [offline-installation.md](offline-installation.md) for
bundle compatibility and copying requirements.

### Unsynchronized sampled simulation (2026-09-13)

Production simulation now sends receiver-clock PCM to the same blind acquisition
path as hardware audio. Earlier matched-observation sensitivity and constant-work
simulation measurements above describe the previous model. The matched-channel
API remains a low-level analytical test helper, outside the simulation transport.

The new channel tests verify arbitrary seeded carrier phase and fractional start
timing, positive and negative clock error, sample-identical results with one-sample
and 4096-sample reads, continuous oscillator/noise state during idle and later
bursts, and equivalence between analytic source PCM and hardware transmitter PCM
through fixed training and keyed spreading. Memory is bounded; CPU work now scales
with sample count. Long-symbol cases exercise cancellation instead of asserting
instant completion of hours of audio.

Transfer tests exercise independent receive epochs inside and outside the search
window, actual spreading correlation at weak sample SNR, and failed acquisition
under carrier incoherence. Live tests cover reception after idle noise and across
consecutive transmissions without a TX-triggered receiver reset. Raw-waveform
replays produce no timing/length-assisted received bits; blind raw discovery
remains unavailable. Very short packets may verify without a provisional browser
row, because transmission alone no longer creates a receiving event.

Release channel, modem, streaming-modem, transfer, regression and live tests
passed, as did the 22 CLI tests, GUI inspection/controller checks, controller
smoke workflow and native GUI self-check. These checks do not establish physical
hardware sensitivity, continuous clock tracking, fading or multipath performance.

AddressSanitizer and UndefinedBehaviorSanitizer checks passed for the final
channel suite and all live cases (the full-run prefix plus focused epoch and
remaining-case runs). LeakSanitizer is unavailable under this host's ptrace
sandbox, so those checks used `detect_leaks=0`. Automatic-epoch and idle-refresh
fixtures now use controlled local clocks: they test timestamp selection and an
explicit idle epoch advance without making sanitizer CPU throughput determine
key admission. Separate tests still require mismatched/out-of-window epochs to
fail. Production clock admission and retention are unchanged by these test fixes.
