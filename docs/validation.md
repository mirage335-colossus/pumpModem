# Local validation record — version 0.2

Linux x86_64, GCC 14.2, CMake 3.31, OpenSSL 3.5.7 and vendored FLTK 1.4.5.
Python is optional developer test tooling; it is not installed with the application.

* Native GUI Release: all 18 CTest suites passed. The CLI suite contains 19
  integration tests, including production 128 MiB keyfiles, encrypted WAV
  transfers, terminal escaping, exclusive saves and full-capacity packet pipes.
* AddressSanitizer + UndefinedBehaviorSanitizer Debug: all 17 suites passed.
  The final live/tuning revisions and dependent CLI/GUI suites were rerun and
  passed. LeakSanitizer is disabled because this host's ptrace environment
  prevents it from starting; no ASan or UBSan errors were reported.
* CLI-only Release with Python discovery explicitly disabled: all 15 C++/CMake
  suites passed, including relocation. Dependent suites were rerun after the
  final live/tuning revisions.
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
* Continuous engine: all ten cases pass, covering idle plots, pending/final
  identity, consecutive messages, encryption and named keys, epoch refresh,
  cancellation, bounded long tones and three keys with 13 candidate epochs each
  at the largest keyed template under the default DSP budget.
* Audio: ALSA fixtures cover duplex default discovery, same-card conversion at
  96 kHz, explicit-device failure, partial writes and streaming buffer lifetimes.
  WinMM fixtures compile the actual Windows branch and check queued buffers,
  cancellation, cleanup and underrun during a slow generation callback.
* Actual device configuration: this machine's discovered analog default opened
  at both 48 and 96 kHz using the new fallback. The probe submitted **zero PCM
  samples** and recorded no microphone input. No physical audio transfer or
  Windows driver reliability claim follows from this check.

The native GUI simulation smoke workflow checks changing plots/waterfall,
pending-to-verified events, two consecutive transmissions without a simulation
cooldown, exact UTF-8 clipboard text, text exclusion from the file list, exclusive
binary save, cache clearing and receive resume. The application layout was
visually inspected on an isolated 1400×1100 display. The final workflow passed
in both Release and ASan/UBSan builds.

The version 0.2.0 native installation was copied to a directory containing
spaces and the original installation made unavailable. Its CLI simulation,
GUI self-check and full GUI simulation workflow passed with an empty `PATH`,
invalid Python paths and an empty `LD_LIBRARY_PATH`. The verifier confirmed
the file inventory and application-library dependency closure, and rejected
both modified and unrecorded files.

An independent generated-PCM benchmark processed 7.509 seconds of 48 kHz noise
in 5.000 seconds of wall time (1.502× real time) with one key and 13 epoch
candidates after warmup. It does not benchmark every combination of bandwidth,
keys or spreading settings. To repeat the same workload without audio hardware:

```sh
cmake --build build --target benchmark_receiver
./build/benchmark_receiver
```

Cryptographic frozen vectors, keyfile tamper/permission cases, Unicode handling
and QR matrices remain covered by the automated suites. Independent ZXing QR
decoding was performed for the unchanged QR implementation during version 0.1;
that external decoder was not added as a runtime dependency.

Live acquisition has finite carrier, timing and epoch hypotheses. Multi-day
clock drift, arbitrary long encrypted-pattern start times, calibrated sensitivity,
near-capacity throughput and native Windows hardware remain outside this local
validation. See [modem.md](modem.md) and [requirements.md](requirements.md).
