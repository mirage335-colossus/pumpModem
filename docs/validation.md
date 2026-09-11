# Local validation record — version 0.5

The application and portable runtime are native C++. Python remains optional
developer test tooling and is not installed with the application.

## Completed 0.5 checks

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

The waveform view now defaults to twelve carrier cycles, retains actual sample
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
