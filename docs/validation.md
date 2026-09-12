# Local validation record — version 0.5.6

The application and portable runtime are native C++. Python remains optional
developer test tooling and is not installed with the application.

## Completed 0.5.6 checks

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
