# Local validation record — version 0.3

Linux x86_64, GCC 14.2, CMake 3.31, OpenSSL 3.5.7 and vendored FLTK 1.4.5.
Python is optional developer test tooling; it is not installed with the application.

* Native GUI Release: all 20 CTest suites passed. The CLI suite contains 19
  integration tests, including production 128 MiB keyfiles, encrypted WAV
  transfers, terminal escaping, exclusive saves and full-capacity packet pipes.
* AddressSanitizer + UndefinedBehaviorSanitizer Debug: all 19 suites passed.
  LeakSanitizer is disabled because this host's ptrace environment
  prevents it from starting; no ASan or UBSan errors were reported.
* CLI-only Release with Python discovery explicitly disabled: all 17 C++/CMake
  suites passed, including relocation.
* Adaptive constellations: all five profiles preserve equal average power and
  Gray adjacency. Tests cover 25 raw PCM transfers across byte/bootstrap
  boundaries, 26 noisy blind transfers (including erased training, unknown gain,
  encryption, planned noise/drift and 16-byte protected-header corruption),
  13 outer-ring/gain-alias regressions and 150,000 seeded differential decisions
  at the specified noise/drift margin. These tests support the implemented design
  margin, not a calibrated packet error rate or capacity optimum.
* Independent rates: nominal modem timing is invariant across 44.1/48/96/192 kHz
  internal sample clocks. Fragmented resampler streams preserve phase, amplitude,
  duration and bounded workspace, suppress out-of-band aliasing and handle EOF.
  Packet tests traverse different 44.1/48/96 kHz transmit/receive clocks, including
  64-APSK, plus 24 kHz bandwidth through 88.2/96 kHz clocks with sufficient passband.
* Streaming/transfer: forced tone 128/1024 operation remains eligible with an
  8 MiB DSP workspace when full PCM would exceed the batch budget; live tone
  1024/16384 roundtrips run in a 1 MiB workspace. Automatic integration exceeds
  the former 16,384-chip ceiling, and training stays exactly five seconds.
* Weak-channel model: fixed-ID messages at target C/N0 of -20 and -60 dB-Hz
  decode with RS60 across seeds 1, 17 and 29 using the planned long integration.
  The shorter 40 dB-Hz plan fails at the same noise levels. These are ideal-carrier,
  matched-despreading integrated-AWGN tests, not measured radio sensitivity.
* Physical PCM: 24 kHz bandwidth at 96 kHz sampling, a fractional 12,731.375 Hz
  carrier and 137-sample delay roundtrip exactly. A separate tone-128 test removes
  all training, substitutes noise and decodes with body FEC off. A streaming
  encrypted 1,024-chip pattern test also recovers with obscured training, nearby
  sample delay and -5 dB sample AWGN, without allocating a complete waveform.
* Generic raw modem: binary and ciphertext-like training, delay, noise, a 3 Hz
  static carrier offset, independent keyed spreading, WAV validation and active
  cancellation pass. The generic known-training carrier search is distinct from
  the streaming blind packet receiver's finite timing search.
* Packet/resource regressions: trailing noise cannot discard a valid packet;
  content at the configured capacity works with every FEC mode; batch and
  streaming feasibility are independent; huge simulated idle intervals remain
  cancellable. The bootstrap prefilter preserves every single-byte mutation
  and 900 randomized 16-byte corruption trials while rejecting over 99% of
  independent noise headers. Frozen RS vectors and correction trials still pass.
* Continuous engine: all eleven cases pass, covering idle plots, pending/final
  identity, consecutive messages, encryption and named keys, epoch refresh,
  cancellation, bounded long tones and three keys with 13 candidate epochs each
  at the largest keyed template under the default DSP budget, plus a two-second
  simulation review with continued background reception and retained RX points.
  Preview tests compare reconstructed recent samples against actual PCM,
  including short dense symbols, keyed spreading and very long integration.
* Audio: ALSA fixtures cover duplex default discovery, same-card conversion at
  96 kHz, explicit-device failure, partial writes and streaming buffer lifetimes.
  WinMM fixtures compile the actual Windows branch and check queued buffers,
  cancellation, cleanup, slow callbacks, rate negotiation and suppression of ACM
  conversion so the application's own rate converter is used.
* Actual device configuration: this machine's discovered analog default opened
  with both 48 and 96 kHz logical streams while the hardware stayed at 48 kHz.
  The latter reported a 20.16 kHz usable converter passband and 233,680 bytes of
  audio workspace. The probe submitted **zero PCM samples** and recorded no
  microphone input. No physical audio transfer or Windows driver reliability
  claim follows from this check.

The native GUI simulation smoke workflow checks changing plots/waterfall,
pending-to-verified events, two consecutive transmissions without a simulation
cooldown, exact UTF-8 clipboard text, text exclusion from the file list, exclusive
binary save, cache clearing, frozen simulation review and receive resume while
the accumulated constellation persists. The application layout was
visually inspected on an isolated 1400×1100 display. The final workflow passed
in both Release and ASan/UBSan builds.

The relocation suite copies the installation into a path containing spaces and
makes the original unavailable. CLI simulation and GUI self-check run with an
empty `PATH`, invalid Python paths and an empty `LD_LIBRARY_PATH`. Inventory and
native-library closure are verified; modified and unrecorded files are rejected.
The final 0.3 GUI simulation workflow also passed in the relocated installation
with these isolated environment settings.
Both TGZ and ZIP archives were extracted and passed inventory, native-library
closure and isolated command checks; the extracted ZIP also passed the GUI
simulation workflow.

Final generated-PCM benchmarks processed 6.187 media seconds in 5.000 wall
seconds for 16-APSK (1.237× real time) and 7.381 media seconds in 5.001 wall
seconds for 64-APSK (1.476× real time), with one key and 13 epoch candidates after
warmup, following the dense gain-fit optimization. They do not benchmark every
combination of bandwidth, keys or spreading settings. To repeat the workload
without audio hardware:

```sh
cmake --build build --target benchmark_receiver
./build/benchmark_receiver
./build/benchmark_receiver 6
```

Cryptographic frozen vectors, keyfile tamper/permission cases, Unicode handling
and QR matrices remain covered by the automated suites. Independent ZXing QR
decoding was performed for the unchanged QR implementation during version 0.1;
that external decoder was not added as a runtime dependency.

Live acquisition has finite carrier, timing and epoch hypotheses. Multi-day
clock drift, arbitrary long encrypted-pattern start times, calibrated sensitivity,
near-capacity throughput and native Windows hardware remain outside this local
validation. See [modem.md](modem.md) and [requirements.md](requirements.md).
