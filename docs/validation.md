# Local validation record

Validated on Linux x86_64 with GCC 14.2, CMake 3.31, OpenSSL 3.5.7, and vendored
FLTK 1.4.5. This records tests actually run; Windows CI is configured but was not
run by this local environment. Python 3.13 is optional developer test tooling.

* Native GUI Release build: all 15 CTest suites passed, including native packaging
  and relocation. The optional
  Python CLI suite contains 17 integration
  tests and exercises production-size 128 MiB keyfiles, real waveform transfer,
  shared-clock search, invalid input, exclusive saves, and terminal boundaries.
* Native GUI AddressSanitizer + UndefinedBehaviorSanitizer Debug build: all 13
  application suites and the additional packaging-support fixture passed. The
  actual GUI smoke workflow also passed under the sanitizers.
  LeakSanitizer is disabled because ptrace prevents it from starting in the
  test environment. No sanitizer errors were reported.
* Reed–Solomon: frozen vectors and 300 deterministic randomized shortened-code
  correction trials, checked malformed bootstrap fields, invalid metadata,
  burst errors, and integrity/MAC rejection.
* Modem: clean/noisy binary loopbacks, arbitrary sample delay, fixed carrier
  offset, combined independently keyed DSSS/scrambler, ciphertext-like training,
  wide-bandwidth preset, pure-noise/wrong-training rejection, malformed WAVs,
  memory/finite-value bounds, and exact three-bit status.
* Crypto: named multi-key collections, complete key-set restoration, legacy file
  compatibility, encrypted names, invalid count/UTF-8/control rejection, and frozen independent vectors, purpose/time/key separation, random-access
  offsets, deliberate CTR reuse plus independent MAC rejection, production
  keyfile round trips, corrupted/wrong/missing pad/header/tag, exclusive creation.
* Short-message compression: fixed three-bit prefix vectors, arbitrary byte
  round trips, strict padding/truncation rejection and automatic smaller-form
  selection. Partial previews tested before the footer, including interleaving
  and all supported FEC modes.
* Tuning/repeatability: all named pattern/tone and simulation modes, C/N0 and
  noise-budget equations, exact estimate versus waveform duration, inclusive
  two-second content limit, one-byte slow exception, and configurable airtime
  policies allowing more than 64KiB.
* Continuous session: changing idle noise/spectrum/baseband plots, provisional
  text before verification with stable signal identity, consecutive packets,
  encrypted automatic epochs, cancellation/reconfiguration, bounded receive
  buffers and no delivery from unrecoverable noise.
* QR: frozen matrix tests and independent ZXing-C++ decoding of exported PBMs
  for short text, 500 ASCII characters, 500 four-byte Unicode characters, embedded
  NUL, markup payloads, and multilingual text. The decoder is a test-only tool.
* Windows audio contract: a fake WinMM backend compiles the actual Windows audio
  branch on Linux and checks continuous queued buffers, error handling, timeouts,
  and cleanup. This does not establish physical Windows device reliability.
* Real native FLTK GUI under an isolated Xvfb display: continuous noisy simulation,
  changing live plots/waterfall, pending text before final verification, normal
  Transmit control, exact UTF-8 clipboard copy, exclusive save, cache clearing,
  and automatic receive resume passed. The captured application was
  visually checked at 1180 by 830 pixels.
* A native CLI-only Release build with Python discovery explicitly disabled
  passed all 10 C++ suites. Its installed directory was moved to a path containing
  spaces; dependency closure, full inventory, and simulation passed with an empty
  PATH and no runtime environment setup.
* CMake packaging fixture: an executable depending on a shared library that in
  turn depends on another shared library was installed and relocated. Both
  indirect dependency resolution and isolated execution passed; modified files
  and unlisted additions were rejected by inventory verification.
* Complete native GUI installation relocated to a path containing spaces, with
  its original pathname removed. CMake verified the inventory and native
  dependency closure; the actual GUI workflow passed under Xvfb with an empty
  PATH and no interpreter or runtime setup.
* Native CPack TGZ and ZIP archives were generated, independently extracted, and
  verified for the complete inventory, native dependency closure, isolated modem
  execution, and GUI self-check. The archives are approximately 12 MiB each and
  are available in `build-native/releases/` with names beginning
  `DataPump-0.1.0-Linux-x86_64-native`.

No live audio/radio transmission, thermal receiver measurements, near-capacity
throughput measurements, multi-day integration, native Windows driver tests,
or independent external security audit was performed. See requirements.md for
the features that remain outside this reference implementation.

To repeat the native GUI smoke test on Linux with a display or Xvfb available:

```sh
xvfb-run -a ./build/datapump-gui --smoke-test
xvfb-run -a cmake -DBUILD_DIR="$PWD/build" -DGUI_SMOKE=ON -P tests/package_native.cmake
```
