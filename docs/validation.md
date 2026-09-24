# Local validation record — version 0.4

Linux x86_64, GCC 14.2, CMake 3.31, OpenSSL 3.5.7 and vendored FLTK 1.4.5.
Python is optional developer test tooling and is not installed with the application.

## Executed checks

* Native GUI Release: all 22 CTest suites passed. The CLI suite contains 21
  integration tests, including production 128 MiB keyfiles, encrypted PCM
  transfers, terminal escaping, exclusive saves and full-capacity packet pipes.
* CLI-only Release with Python discovery explicitly disabled: all 19 C++/CMake
  suites passed, including relocation.
* The final idle-noise scheduling change also passed the live and GUI self-check
  suites. It paces empty simulated air to the display cadence while transmissions
  continue at CPU-bounded speed.
* All 21 AddressSanitizer/UndefinedBehaviorSanitizer suites passed across the
  integrated run and focused reruns after fixes. The final live suite passed
  in 97.27 seconds, including the deterministic epoch-jump regression.
  LeakSanitizer is disabled because this host's ptrace environment prevents it
  from starting.

## Signal and channel regressions

* Bandwidth planning covers 1 Hz through 30 MHz. The internal rate is
  `max(64, ceil(4 * bandwidth))` and the default carrier is `0.75 * bandwidth`.
  CLI checks verify 100 Hz → 400 Hz, 1.2 kHz → 4.8 kHz and 30 MHz → 120 MHz.
  A 4.8 kHz PCM16 WAV also preserves its logical clock in its metadata.
* Multistage conversion preserves phase, amplitude and exact finite-stream
  duration across fragmented input, suppresses out-of-band aliases, and keeps
  even 120 MHz → 64 Hz conversion below 5 MiB. A 100 Hz-bandwidth packet crosses
  independent 48/44.1 kHz audio clocks; 1.2/2.4/24 kHz packet cases also pass.
* The same sampled carrier fed as one buffer or irregular callbacks produces
  identical waveform, spectrum and constellation data. Absolute sample origins
  prevent callback-dependent phase resets. Hann normalization preserves signal
  level during startup; DC and Nyquist bins use the correct single-sided scale.
* Default simulation includes 100 ppm relative crystal error and 0.5 degrees
  RMS phase diffusion per square root second. Tests check signed clock warp,
  elapsed carrier phase, within-observation coherence loss, seeded phase-noise
  statistics, cancellation and bounded workspace. Plot polling does not change
  decoder randomness, and preview timestamps include interpolation lookahead.
* Ordinary impaired packets recover through both transfer and continuous APIs.
  A 30 MHz packet recovers with or without its training in the capture. Training
  requires 64 integrated observations; the complete small transfer stays below
  10,000 source observations. Protected-prefix validation work is bounded by
  training segments, timing/gain hypotheses and payload observations.
  Acquisition uses the protected bootstrap rather than a five-second age gate.
* A complete ideal-clock packet with hour-long symbols decodes; its counterpart
  with the default bad crystal fails validation. This checks that long integration
  does not silently assume carrier coherence that the oscillator cannot supply.
* Adaptive 4/8/16/32/64-APSK preserves equal average power and Gray adjacency.
  Existing byte/bootstrap boundary, noise, erased-training, unknown-gain,
  encryption, missing-ring and protected-header corruption cases remain covered.
  Receiver constellation history is a bounded sliding window; later symbols
  replace old points after the first 2,048 observations.
* The CLI uses the same automatic defaults as the GUI unless manual modem
  controls are supplied. A deterministic 24-message probe exposed three noisy
  loopback failures with the old implicit manual configuration at the new
  internal clock, and none with automatic planning. The CLI integration suite
  verifies both implicit automatic planning and explicit manual selection.
* Continuous-session tests cover idle plots, authenticated reception, epoch-bank
  refresh, consecutive messages, cancellation, 1 MiB long-tone workspaces,
  default oscillator impairments and the two-second simulation review. All three
  plots return to current reception after the review and keep changing afterward.
  A clock that jumps an hour on every query verifies that a simulated burst
  retains its admitted key epochs through preparation and decoding, and that
  consecutive bursts admit fresh epochs. Wall time still drives idle and real
  audio admission; simulation CPU speed does not expire an in-flight epoch.
* ALSA and WinMM fixtures cover default-device discovery, low logical rates,
  rate negotiation, partial I/O, cancellation and queued-buffer lifetimes.
  Different partial-read sizes produce identical converted PCM. WinMM uses the
  application's converter rather than hidden ACM conversion.
  CLI TX, RX and status TX also reject an unsupported band after negotiating
  the fixture's 48 kHz endpoint; supported audio and wider WAV output still work.
* Cryptographic vectors, keyfile tamper/permission checks, content bounds, QR,
  Unicode, framing, Reed-Solomon correction and exclusive saves remain covered.

The native GUI simulation workflow passed with pending-to-verified text, two
consecutive transmissions, exact UTF-8 clipboard copy, files-only save listing,
exclusive binary save, cache clearing, held midpoint plots and live resumption.
The final GUI workflow also passed under AddressSanitizer/UndefinedBehaviorSanitizer.
The layout was visually inspected on an isolated 1400×1100 display. This did not
exercise the physical audio path or compare AC and battery power states.

## Actual device configuration and conversion cost

A zero-PCM probe opened the discovered analog default at these logical rates:

| Logical rate | Hardware rate | Converter passband | Playback workspace |
| ---: | ---: | ---: | ---: |
| 64 Hz | 48 kHz | 26.88 Hz | 118,348 B |
| 4.8 kHz | 48 kHz | 2,016 Hz | 119,296 B |
| 9.6 kHz | 48 kHz | 4,032 Hz | 120,256 B |
| 96 kHz | 48 kHz | 20,160 Hz | 233,792 B |

Every producer returned zero samples: no sound was emitted and nothing was
recorded. Passbands are converter calculations, not measured analog response.
An ALSA-stub session reproduces an unsupported 30 MHz audio request; the live
service now rejects such a request before starting an unusable audio upsampler.

One synthetic 48 kHz → 4.8 kHz conversion processed a second of input in
15.42 ms, with 647,624 bytes of workspace and 5.81 ms setup. Eight source chunks
produced exactly 4,800 samples. This measures conversion alone, not end-to-end
receiver capacity or performance on another machine.

With other build/test workers stopped, the generated-noise receiver benchmark
processed 7.77 times real time for the automatic three-bit profile and 13.74 times
real time for a forced six-bit profile. Both used a 4.8 kHz internal clock, 13
keyed epoch candidates and a five-second measurement after warmup. These are
single-host CPU measurements, not battery-state or physical-audio guarantees.

## Portable delivery and CI

The relocation suite copies the installation into a path containing spaces and
makes the original unavailable. It verifies native dependency closure and
commands with empty `PATH`/`LD_LIBRARY_PATH` and invalid Python paths. Modified
and unrecorded package files are rejected.

New archive/ABI fixtures check both TGZ and ZIP extraction, downloaded checksum
validation, rejection of accidental GTK/GLib dependencies and rejection of a
glibc requirement above the chosen ceiling. The workflow's YAML parses and its
Linux shell steps pass ShellCheck. The GitHub Actions jobs have not run here:
Windows binaries and the glibc 2.35 compatibility floor require a successful
hosted workflow run after push. Locally generated binaries use this newer host's
ABI and do not inherit the CI compatibility floor.
The local 0.4.0 installation passed an audit of all 42 packaged ELF paths with
no GTK/GLib dependency and a maximum required glibc version of 2.38.

The accelerated channel is a matched-chip model with bounded Wiener-phase
integration. It does not implement a chip-clock recovery or carrier-tracking
loop, nor demonstrate calibrated sensitivity, arbitrary long encrypted-pattern
acquisition, near-capacity throughput, physical SDR operation or Windows driver
reliability. See [modem.md](modem.md) and [offline-installation.md](offline-installation.md).
