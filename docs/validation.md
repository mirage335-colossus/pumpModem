# Local validation record

Validated on Linux x86_64 with GCC 14.2, CMake 3.31, OpenSSL 3.5.7 and Python 3.13.
This records tests actually run; Windows CI is configured but was not run by this
local environment.

* Release build: all 10 CTest suites passed. The CLI suite contains 14 integration
  tests and exercises production-size 128 MiB keyfiles, real waveform transfer,
  shared-clock search, invalid input, exclusive saves, and terminal boundaries.
* AddressSanitizer + UndefinedBehaviorSanitizer Debug build: all 8 suites passed.
  LeakSanitizer is disabled because ptrace prevents it from starting in the
  test environment. No sanitizer errors were reported.
* Reed–Solomon: frozen vectors and 300 deterministic randomized shortened-code
  correction trials, checked malformed bootstrap fields, invalid metadata,
  burst errors, and integrity/MAC rejection.
* Modem: clean/noisy binary loopbacks, arbitrary sample delay, fixed carrier
  offset, combined independently keyed DSSS/scrambler, ciphertext-like training,
  wide-bandwidth preset, pure-noise/wrong-training rejection, malformed WAVs,
  memory/finite-value bounds, and exact three-bit status.
* Crypto: frozen independent vectors, purpose/time/key separation, random-access
  offsets, deliberate CTR reuse plus independent MAC rejection, production
  keyfile round trips, corrupted/wrong/missing pad/header/tag, exclusive creation.
* QR: frozen matrix tests and independent ZXing-C++ decoding of exported PBMs
  for short text, 500 ASCII characters, 500 four-byte Unicode characters, embedded
  NUL, markup payloads, and multilingual text. The decoder is a test-only tool.
* Windows audio contract: a fake WinMM backend compiles the actual Windows audio
  branch on Linux and checks continuous queued buffers, error handling, timeouts,
  and cleanup. This does not establish physical Windows device reliability.
* Real Tk GUI under an isolated Xvfb display: actual CLI simulation, receive
  display, QR update/render including the 500 Unicode limit, exact clipboard copy,
  explicit save, clear cache, and visible controls/plots. The screenshot in
  `build/gui-preview.png` is a capture of the running application.
* CMake install and CPack TGZ/ZIP generation completed successfully.
* Offline runtime: Linux bundle moved into a new directory containing spaces,
  original path removed, PATH emptied, host Python/Tk settings poisoned. Copied
  Python, Tcl modules, native-library origins, actual GUI/clipboard/QR/save workflow,
  CLI simulation, unchanged inventory, and deliberate corruption detection passed.
  The packaging helper has 10 unit tests; Windows PE parsing and recursive DLL
  collection have 14 fixture tests, with no native Windows execution claim.

No live audio/radio transmission, thermal receiver measurements, near-capacity
throughput measurements, multi-day integration, native Windows driver tests,
or independent external security audit was performed. See requirements.md for
the features that remain outside this reference implementation.

To repeat the optional GUI smoke test on Linux with Tk and Xvfb installed:

```sh
xvfb-run -a python3 tests/gui_smoke.py --pump build/pump
```
