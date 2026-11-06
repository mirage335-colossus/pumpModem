# AES transmit background-noise qualification

Implemented default-off noise generation in the isolated native sampled channel,
Helper exporter, direct PCM mixer and raw-ALSA/continuous-stream adapters. The
[feature guide](../../../triad-background-noise.md) defines units and supported
paths. No ordinary Robust receiver integration or receiver campaign was run.

Source baseline: `862997c36415163c34df5456f602e6e05bd94345`, plus the files bound
by [source-sha256.json](source-sha256.json). Measurements and actual invocation
arguments are retained in [measurements.json](measurements.json). The local raw
artifacts are bound by [artifact-sha256.json](artifact-sha256.json); no keys or
IVs were retained. Fresh runs intentionally generate different noise.

## Digital and CPU measurements

Eleven eight-second tests exercised effective bits 0, 4, 10, 15 and 16,
obscured bits 6, 90 dB suppression, 1 mW relative to 4 W and 40 W, and sample
rates 12, 48 and 96 kHz. All passed the frozen engineering checks: integrated
power error below 0.15 dB, 16 broad Welch-band errors below 0.65 dB, and absolute
lag-one correlation below 0.015. These are measurement acceptance envelopes,
not confidence bounds on future hardware.

- Maximum final-binary power error: **0.02166 dB** (16-bit-equivalent S16).
- Maximum broad spectral-band error: **0.21036 dB**. Bands span 1–99% of
  Nyquist; the independent per-sample construction supplies the full-band white
  second-order spectrum, not a band-limited modulation sequence.
- Sub-LSB levels produced actual nonzero S16 codes; 90 dB power error was
  **+0.00386 dB**. Noise is not lost under the ordinary float-to-S16 truncation.
- At 48 kHz, S16 mixing took **0.00370–0.00478 CPU seconds per audio second**
  on this AMD Ryzen 5 PRO 5650U. This includes input validation, reading,
  encryption/distribution mapping and writing, but excludes process startup and
  the final small JSON write. Whole-command wall ratios were 0.00419–0.00534.
  These short timings are feature cost diagnostics, not a sustained receiver
  performance regression qualification. The processor exposes AES/VAES and
  OpenSSL 3.5.7 handles dispatch; no specific assembly path was instrumented.

Two native 300,000-source-sample checks used the calibrated complete Helper and
DATA references. With Helper on, 40 W corresponded to mean-square 9.99717;
with Helper off, 4 W corresponded to mean-square 1. Noise watts were 0.001 in
both cases. Expected/observed variances were respectively
0.000249929/0.000250048 and 0.000250000/0.000249835. Equal absolute noise sample
power is correct: both cases retain the same DATA-sample-to-watt conversion.
The relative ratios are 1 mW/40 W and 1 mW/4 W, respectively.

## Actual cable measurements

Raw ALSA `hw:1,0`, Realtek ALC257, 48 kHz stereo S16, capture channel zero;
unchanged mixer settings. Each run played six seconds of a 0.05-peak 1737 Hz
tone with fresh added noise. The tone kept the path active and independently
measured gain. Welch analysis used a central four-second interval and
1400–2100 Hz, excluding 1650–1820 Hz around that tone. Baseline gain was
**0.841498**. Prediction was (2v/F_s) times measured squared gain. Baseline
PSD was subtracted in linear units before comparison.

| Selected added noise | Observed floor rise over baseline | Added-PSD error versus prediction |
| --- | ---: | ---: |
| 40 dB below full-scale sine | 31.482 dB | +0.07120 dB |
| 60 dB below full-scale sine | 11.716 dB | +0.00476 dB |
| 10 ideal effective bits | 9.907 dB | −0.00262 dB |

All three passed the declared ±0.5 dB cable envelope. Capture/playback reported
no discontinuities; digital mixing reported zero clips, and the analyzed
capture intervals had zero samples above 0.98 full scale. Tone gain changed by
less than 0.003 dB. Each capture command took about 9.9 seconds.

The spectrum remained broadly flat through 20 kHz. The 20–23.5 kHz band's
added PSD was about 1.8 dB lower than the midband at the strongest level,
consistent with downstream analog/converter filtering. This is retained, not
equalized away. These records establish the requested comfortably measurable
noise-floor rise on this cable; they do not measure radiated RF watts, prove
arbitrary hardware behavior, or qualify a 90 dB physical floor hidden under
the codec's own noise.

The cable executable hash in the retained receipt differs from the final
digital executable hash. Subsequent changes corrected JSON/peak metadata,
rejected unresolvably tiny integer-mixture probabilities, removed test-only
constructor code from production, and measured final float rounding. They do
not change S16 generation at the three measured cable levels. Final digital
validation independently covers those supported arithmetic paths. This is an
explicit narrow reuse of cable evidence, not an exact-final-binary cable claim.

## Regression and reproducibility

- `CCACHE_DIR=/tmp/datapump-ccache ./build.sh test triad --cli --jobs 2`:
  **18/18**, 35.54 seconds CTest wall time. Includes new core/CLI checks and
  native channel regressions for noise during quiet source intervals,
  quantized noise power, default-off repeatability and fresh streams.
- Final focused `background_noise` and `background_noise_cli`: **2/2**,
  0.22 seconds, including seven native CLI boundary checks.
- The four Python suites named in the feature guide: **47/47**, 0.314 seconds.
- Existing Helper exporter contracts: **9 actual captures and 27 rejected
  invocations**, passed in 33.04 seconds.
- New core: address/undefined-behavior/leak sanitizers passed. The first leak
  check could not operate under the sandbox's ptrace; its failed environment
  log is retained separately. The same test passed outside that tracing layer.

Run `tools/triad_noise_qualification.py --output NEW_DIR` in the prepared
research environment for the cheap digital check. Add `--live --live-only
--device hw:1,0` for the four short physical captures. Use a fresh directory.
Native watt-reference reproducer:

```sh
build/dev-cli/triad_helper_export --case hf --seconds 8 --sample-rate 6000 \
  --spread-preset none --selective-perturbation off --oscillator-effects off \
  --control flat --background-noise-watts .001 --output NEW_DIR
# Repeat with --helper off and another fresh directory.
```

This feature does not inherit earlier Gaussian-null receiver guarantees: the
added source can become colored through propagation. Native reports disable
analytic/Gaussian-projection support when it is enabled. Full receiver
admission and sensitivity qualification with this new source remains separate.
The duplex native audio backend currently rejects this option; the tested raw
ALSA backend injects at the actual hardware sample rate. No cloud resources
were needed or allocated.
