# Cable level calibration evidence, 2026-09-21

Runtime revision: `cd1182cbb7b07964a293a25b73e97706581012a8`.
No runtime source, mixer setting, framing or GUI control changed during this
study. The [report](../../../fast-cable-level-calibration.md) explains the
recommended existing amplitude of 0.30 and the evidence limits.

## Records

- `summary.md`, `summary.csv`, `summary.json`: all 26 modem trials and eight
  tone levels. Every radio-labeled trial used the cable, not an IC-7100.
- `*-a*.json`: original probe output; matching `.log` records progress and
  matching `.command.json` records exact argument arrays where available.
  `wide-a030` was the initial manual trial and has no original command-array
  file; its full settings are in its probe JSON. A reproduction is below.
- `*-known.json`: offline known-source residual and payload bit-error metrics,
  with hashes of their probe/bit/IQ inputs. Probe `raw_wrong_bits` is not
  counted in codec mode; the offline helper supplies that count.
- `environment.json`: default physical devices and complete mixer snapshot.
  No system mixer changes were made. The user connected left output to mic;
  identical mono PCM was sent on both channels with `--stereo`.
- `tones/`: production capture metadata, steady sine analysis and generator
  metadata. The hardware path's fitted gain is separate from normalized RX
  constellation gain.
- `dsp-controls/`: noiseless float TX-to-RX results, known-symbol metrics,
  reconstructed offline reproduction script, and settled production-PCM power
  audit source/results. `cable-dsp-floor-summary.json` is the first calculation;
  `cable-dsp-floor-recomputed.json` uses full-precision live metrics and input
  hashes. The initial mid/narrow live dB values were rounded before calculating
  illustrative power ratios. The underlying coherence values agree.
- `raw-and-binary-sha256.json`: sizes and SHA-256 of original PCM, literal TX
  bits, RX payload IQ, and the two live diagnostic binaries. Large raw files
  remain at their recorded `/tmp` paths for this session and are not committed.
- `known-evm-tests.log`, `tone-analysis-tests.log`: 7/7 and 11/11 analysis tests.
  `analysis-validation.log`: all 26 known-source analyses completed; every raw
  probe's independent payload error count agrees. These are offline checks.

The raw probes do not use the file decoder, so their `exact=0` means some
uncoded bits differ, not a failed file. There are **eight radio/raw trials**,
four per bandwidth; all eight recovered every bit.
There are eight cable/raw trials, seven short cable/codec trials and three
long cable/codec trials, totaling 26.

The diagnostic permits a 60-second FIFO. The manually selected 1.8 kHz cable
geometry accumulated about 4.6 seconds, exceeding the application's one-second
queue. Those captures support offline amplitude analysis, not real-time
qualification. Low-drive wide trials with expensive failed LDPC decoding also
exceeded one second. Successful nominal-band file trials did not.

## Reproduce the offline analyses

Run from the repository root. Use **Python 3 with NumPy**; the host's system
Python lacks NumPy. The interpreter used here is:

```sh
cal_python=/home/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3
cal_archive=docs/validation-data/fast/cable-level-calibration-20260921
"$cal_python" tests/test_fast_known_evm.py
"$cal_python" tests/test_cable_snr_analysis.py
"$cal_python" tools/fast_known_evm.py \
  --probe /tmp/cable-level-20260921/wide-a030.json \
  --tx-bits /tmp/cable-level-20260921/wide-a030.bits \
  --rx-symbols /tmp/cable-level-20260921/wide-a030.iq \
  --output /tmp/cable-known-recomputed.json
```

`analyze_all.py` reads retained raw files and reproduces the 26 `*-known.json`
reports, including the raw BER cross-check. It does not open audio. It writes
analysis outputs in its input directory, so use a working copy to preserve
archived report hashes. `summarize.py DIRECTORY` needs only the compact reports
and Python's standard library; NumPy is not needed for that summary step.
Plotting is optional and requires an existing matplotlib installation.

The noiseless-control script reads existing files by default and never opens
audio. Every command it can launch explicitly includes `--offline`:

```sh
"$cal_python" "$cal_archive/dsp-controls/reproduce_cable_dsp_floor.py" \
  --offline-dir /tmp --live-dir /tmp/cable-level-20260921 \
  --output /tmp/cable-dsp-floor-recheck.json
```

Its `--print-commands` option prints pinned reconstructed commands;
`--run-offline` regenerates controls only in fresh filenames. These are
reconstructions from the recorded settings, not a shell-history transcript.
The power audit source can be compiled against the current Linux build:

```sh
c++ -O3 -std=c++20 -Iinclude \
  "$cal_archive/dsp-controls/fast-cable-radio-output-power.cpp" \
  build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl \
  build/third_party/xz/liblzma.a -o /tmp/cable-radio-power-check
/tmp/cable-radio-power-check
```

This audit only generates PCM in memory. Its finite sampled peaks are not
worst-case bounds; the live longer-file fixtures exposed larger peaks.

## Repeat a live trial

The following command uses the currently selected real audio devices. Choose
new capture filenames and the appropriate output channel for the actual cable.
The recorded setup required `--stereo` because the cable was on the left.
The probe otherwise defaults to right-only output.

```sh
build/fast_cable_probe --profile wire --capacity --stereo \
  --mode codec --bytes 100000 --amplitude .30 --tail 7 \
  --capture-save /tmp/cable-repeat.f32 \
  --tx-bits-save /tmp/cable-repeat.bits \
  --rx-symbols-save /tmp/cable-repeat.iq \
  > /tmp/cable-repeat.json 2> /tmp/cable-repeat.log
```

The three long trials used `--bytes 1000000`, with amplitudes 0.40, 0.35, 0.30
in that order. Raw trials and their exact rates/interval counts are recorded
in each `.command.json`. `run_sweep.py` is preserved as used, including its
hard-coded original output directory and 120-second per-probe bound. It runs
live audio, refuses existing results, and must be copied and given a fresh
output directory before repeating the sweeps. None of these scripts changes
system mixer settings. Source seed defaults to 417; the production codec
generates a fresh random salt, so source bytes repeat but codec wire bits vary.

The separate tone experiment generated its fixture without opening audio,
then used the production audio capture tool:

```sh
"$cal_python" tools/cable_snr_live.py --output /tmp/cable-tone-repeat --mode levels
build/cable_audio_capture \
  --input /tmp/cable-tone-repeat/transmit-mono.f32 \
  --output /tmp/cable-tone-repeat/production-capture.f32 --stereo
"$cal_python" tools/cable_snr_report.py /tmp/cable-tone-repeat \
  --capture-file /tmp/cable-tone-repeat/production-capture.f32 \
  --capture-channels 1
```

The generator has no `--live` in this recipe. It produces eight 997 Hz sine
plateaus, 3 seconds each, at peaks 0.03, 0.06, 0.12, 0.25, 0.35, 0.50, 0.70,
0.90, separated by silence. Analysis uses a central two-second fit, rejecting
transients. SINAD includes the residual noise and harmonics in its specified
measurement band. It is not a QAM peak-drive recommendation.
