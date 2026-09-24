# SNR preset integration checks — 2026-09-21

Baseline revision: `12f2aab`; tests ran against the working changes described
in [Fast SNR presets](../../../fast-snr-presets.md). No live audio or RF was
operated to qualify these presets.

- `fast-gui-tests.log`: all 26 final Fast and shared GUI suites passed,
  including the final canonical preset rates and streamed low-rate tests.
- `preset-helper-test.log`: final focused preset check after preserving
  explicit classic acoustic timing in the generic Auto-rate helper.
- `fltk-native.log`, `rev-native.log`: complete native adapter conformance
  on isolated virtual displays, including all new menu labels and nominal,
  lowest-SNR and Manual detail text at minimum and default window sizes.
- `preset-table.csv`, `geometry-review.log`: final 1,604-point independent
  geometry/monotonicity check and all channel menu settings. The reproduction
  source is [preset_geometry_review.cpp](../wire-snr-screen-20260921/preset_geometry_review.cpp).
  Its stored single-carrier baud column is inactive for OFDM profiles.

Both Release GUI executables and the CLI rebuilt. Rev's final self-check also
passed. The complete ordinary-modem development-contract group passed 29/29
in 1375.19 seconds, including the unchanged long probability calibration.
Native FLTK output includes existing ALSA/clip-stack diagnostics; the
test result is successful. This is software validation, not measured channel
margin or file-success probability.

Additional evidence is in the sibling [radio](../radio-capacity-20260921/README.md),
[decoder screen](../wire-snr-screen-20260921/README.md), and
[low-rate PCM](../low-rate-20260921/README.md) archives. Deliberate decoder-screen
failures led to excluding dense half-rate QAM from automatic selection.
