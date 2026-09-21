# Acoustic OFDM recovery evidence, 2026-09-21

Baseline revision: `c91cbf2` (full hash in `revision.txt`). The baseline probe,
Fast library and OFDM receiver source were frozen before runtime edits.
[Diagnosis and resulting behavior](../../../fast-acoustic-ofdm-recovery.md)
distinguish live observations, deliberately impaired replay and sampled tests.

## Results and original inputs

- `results.md` and `results.json`: live and replay outcomes. Both original
  unmodified live 60-byte baselines passed; the user's exact live failure
  trigger was not captured. All four corrected live transfers passed.
- `baseline-snr{0,3}.*`: original production-audio probe JSON, progress log and
  exact argument array. `preset-{0,3}.json` records the original Auto settings.
- `fixed-snr*-b*.*`: new Auto settings, live command arguments and results for
  60-byte and 20,000-byte sources at both preset selections. All source SHA-256
  values match; no LDPC frame failed. `fixed-sweep.log` summarizes the runs.
- `environment.json`: default physical speaker/microphone devices, output 95%,
  microphone 27%, unmuted. The user confirmed the acoustic connection. Output
  was right-only; no mixer setting changed.
- `delay-snr0.impairment.json` and `impair_recording.py`: controlled replay
  alteration. At capture time 21 seconds, all following samples are delayed
  by 16 samples (0.333 ms); the small seam is filled with the boundary sample.
  Recording duration is unchanged, truncating only the last 16 tail samples.
  This is an artificial timing change in a real capture, not a live failure.
- `delay-snr0-baseline.json`: original receiver stops at 27.25 seconds with
  26/508 intervals and the reported geometry error. Final replay recovers all
  intervals and the source; unmodified recordings are replayed as controls.
- `raw-and-binary-sha256.json`: SHA-256, byte counts and paths for PCM, original
  literal TX bits, observed IQ and frozen probe/library binaries. Large inputs
  are not committed. They are retained under `build/diagnostics/acoustic-zero-three-20260921`;
  `/tmp/acoustic-zero-three-20260921` is a session symlink to that directory.
  The move avoided the host's nearly full `/tmp` RAM filesystem.

Six live captures totaled 477.35 seconds. These are preset trials on the
available acoustic channel, not measurements at a deliberately calibrated
0 dB/3 dB live SNR. The initial 3 dB recording includes one near-full-scale
captured sample yet completes; the records do not claim a perfectly quiet or
linear acoustic environment. All reported queued audio remains below the
production OFDM four-second allowance.

## Controlled tests

`controls/fast_ofdm_presence_repro.cpp` and its logs preserve the initial
actual-preset raw-cycle screen at the baseline revision. It adds white real
PCM noise with

```
variance = (amplitude / 4.5)^2 * 48000 / (2 * 17500 * 10^(expected_snr_db/10))
```

Noise continues through the entire tail. Changes introduced at 20 seconds
include an added five-millisecond echo, a permanent 16-sample delay and a
100 ppm clock-rate step. Noise-only, actual source cutoff and stationary echo
controls distinguish a continuing signal from real absence. The early scratch
harness cuts the delayed/clock-shifted signal at the original duration; the
final regression retains the full delayed ending and causal echo tail.

`tests/test_fast_ofdm_presence.cpp` is the maintained regression. Its original
failure fixtures explicitly pin depth eight, so Auto improvements cannot
silently shorten them. Before correction, all three changing-channel cases
fail early, while the negative/boundary controls pass. The corrected receiver
passes the historical cases. Additional current-Auto tests stream deterministic
keyed 20,000-byte files through real coding, OFDM, noise and the added echo;
they require exact authenticated bytes, all expected intervals, no failed LDPC
frame, no early source exposure and six seconds of observed absence.

`controls/fast_ofdm_coded_repro.cpp` and `ofdm-coded-*` logs record prototype
comparisons. With depth eight, the 0 dB/0.5-echo case remains decoding-limited
even after timing/presence recovery; a 0.45 echo passes. Depth one at 0 dB and
depth two at 3 dB recover exact 20 KB under the 0.5 echo. Prototype files are
preserved under `prototypes/` to distinguish wider timing, maintenance tolerance,
and a rejected approach that retained too many whole-block erasures. They are
experimental source, not separate shipped implementations.

`controls/fast-acoustic-capped-*` contains the independent preset estimator
audit. Public 50 MB throughput losses are 3.88%, 7.87%, 13.84% at 6/3/0 dB.
The keyed prototype estimates have slightly larger losses because a fixed
per-cycle authentication cost is more frequent. Do not mix those two tables.
No acoustic-menu throughput reversal was found on a 0.1 dB grid.

## Reproduction

Build and run from the repository root. The maintained regressions are offline:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_fast_ofdm_presence test_fast_presets fast_cable_probe -j 3
build/test_fast_ofdm_presence
build/test_fast_presets
```

To replay the deliberately delayed original 0 dB recording, retain its old
depth-eight geometry instead of resolving the new Auto depth:

```sh
build/fast_cable_probe --profile acoustic --capacity --mode codec --bytes 60 \
  --qam 4 --code-rate 2/3 --depth 8 --ofdm-fft 32768 --ofdm-prefix 4096 \
  --ofdm-pilots 16 --ofdm-low 500 --ofdm-high 4895.8012542724609 \
  --amplitude .4 --tail 9 \
  --replay /tmp/acoustic-zero-three-20260921/delay-snr0.f32
```

`impair_recording.py` requires Python with NumPy and refuses to overwrite its
output. The bundled interpreter used for this session was
`/home/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3`.
Replay never opens audio. The source fixture is the probe's default seed 417;
the original live codec wire bits have random salts and are saved independently.

The `.command.json` arrays give complete live invocations, including exact
fractional band edges and capture destinations. **They open the default real
audio devices.** Use fresh output filenames when repeating them. `run_fixed.py`
and `fixed-runs.json` preserve the sequential live sweep; the script refuses
existing result files. The matching `.preset.json` records which Auto settings
generated each argument list. No script adjusts operating-system volume.

The integrated test command uses a repository-local temporary directory because
an earlier GUI run exhausted `/tmp` while writing an unchanged key fixture:

```sh
mkdir -p build/tmp-acoustic-tests
env TMPDIR="$PWD/build/tmp-acoustic-tests" \
  ctest --test-dir build --output-on-failure -j 2 \
  -R '^(fast_.*|gui_fast.*|gui_application|gui_controller|gui_inspection|gui_binary_editor|gui_adapter_boundary|gui_contract|gui_link_boundary|compression_short|stream_receive|stream_codec|transfer|attachment)$'
```

Acquisition/maintenance sign probabilities in the report are conditional
calculations, not experimental estimates at probabilities near `2^-80`.
Whole-file success rates and arbitrary room changes are not qualified by this
small set of measurements.

Final integration passed 33/33 suites in 311.33 seconds, including all eight
new OFDM cases and the original 92-case Fast SNR matrix. `tests-last.log`
retains detailed case output. Both GUI backends and the CLI rebuilt; native
adapter code was unchanged and native rendering was not repeated. Archived
text logs have trailing whitespace normalized; original logs remain beside
the retained capture files.
