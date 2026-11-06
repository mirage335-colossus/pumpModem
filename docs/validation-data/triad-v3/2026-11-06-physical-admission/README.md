# Short physical admission qualification, 2026-11-06

**Two independent live DATA captures admitted the correct bits; a separate
noise-only capture was rejected.** This qualifies a controlled short cable path
under the explicit conditional noise model in the
[maintained guide](../../../triad-physical-admission.md). It does not certify
unconditional rare physical false admissions, the adaptive timing receiver,
Helper leakage, arbitrary RF channels or multi-day hardware operation.

No mixer or host-clock changes were made. Hardware capture time across all
diagnostics and trials was **334.98 seconds (5.58 minutes)**. Development,
review, preparation and analysis are separate from that figure. No RunPod
resources were needed: the physical endpoint and real-time target are local.

## Configuration and observed outcomes

Host: AMD Ryzen 5 PRO 5650U, Linux 7.2.6, native ALSA `hw:1,0`, Realtek ALC257
headphone-to-microphone cable. ADC: 48 kHz, S16 stereo with channel zero selected.
DATA playback: 6 kHz logical, **44.1 kHz actual DAC**. Full-rate AES background
noise: 40 dB below full scale, temporary transmission keys. Mixer hash is in the
profile; observed settings were Master/Headphone 87, PCM 255, Mic Boost zero,
Capture 37 and Digital Capture 60. No clipping or native discontinuity was
reported in any final DATA/null capture.

Rate 36 Hz, Added Symbols ×10, current independently keyed APSK patterns,
1725 Hz DATA IF, Helper off, FHSS/Burst off, no simulated RF effects. The cable
supplies the actual analog channel. Explicit key/epoch fixtures make the patterns
reproducible; they do not replace the normal clock-derived key mechanism.
Requested pattern duration 32 seconds resolved to about 35.07 seconds of source
support. The emitter retained its existing approximately 38-second lead and
73.2-second transmit calendar. No waveform or framing changes were made.

RX uses 32 public arrivals from 1.000 to 1.124 seconds in 4-ms increments, a
single complex tap, fixed equal 0.5-second/infinite-coherence models, FP32 bulk
and state arithmetic, FP64 accumulated log scores. Capture blocks are 0.25
seconds, with a longer merged terminal block; memory is bounded independently
of observation duration. The full-audio clock/ASRC pipeline stayed active.
Observed capture corrections reached 6.36 ppm; these trials did **not** inject
a 200-ppm excursion or a system-clock step.

| Independent capture | Admitted result | log e | Capture time | All TX/RX CPU seconds per input second | Preparation |
|---|---|---:|---:|---:|---:|
| Bit 0 | Correct 0 | 55.362 | 74.473 s | 0.421 | 1.571 s |
| Bit 1 | Correct 1 | 48.732 | 74.473 s | 0.423 | 1.143 s |
| Noise only | No admission | −4.989 | 74.473 s | 0.441 | 1.073 s |

The unchanged endpoint threshold was **21.4934**, corresponding to the
conditional `1e-4/day` policy with at most 216,001 attempts/day and one duration
alternative. Counts are 2 correct admissions, 0 wrong admissions, 0 misses,
and 0/1 noise admissions. These small counts cannot estimate a useful operational
success/false-admission probability: the one-sided 95% binomial lower bound for
2/2 success is only 22.4%, and the upper bound for 0/1 noise admissions is 95%.
The much smaller conditional daily bound comes from the likelihood/moment
argument, not those empirical counts.

The named receiver alone used 21.81–23.90 CPU seconds per capture. Reported total
CPU ratios include Python, native template generation, playback and capture,
and are therefore conservative for RX-only cost. Wall time was 76.02–76.58
seconds per run. Estimated owned receiver workspace was 141.36 MB plus the
bounded template cache; this is not a measured total RSS bound. The maximum
block processing time includes the longer terminal block and is not a steady
0.25-second-block deadline estimate.

Independent same-path noise density was approximately `1.582e-9 FS²/Hz` in the
DATA band. A separate posthoc waveform fit estimated matched coherent signal
components of **7.11 and 7.41 dB-Hz**. The original public timing grid retained
6.95 and 6.50 dB-Hz, respectively. Finer measurement-only timing explains about
0.16/0.91 dB of this difference. These are component estimates, with residual
channel/timing loss and noise-estimation uncertainty, not precise total received
power measurements. They never changed the bank, bound or admission. Analysis
took 2.38 seconds wall; its Gaussian-model intervals are explicitly conditional,
not physical confidence guarantees.

## Noise qualification and retained corrections

The quiet ADC showed substantial correlation (lag one approximately 0.689).
Full-rate AES noise raised the floor and dominated that baseline. The fixed
global real-noise proxy was **`v = 1e-4`**. It was frozen before fresh validation;
no capture-dependent scale or threshold was fitted during reception.

1. Calibration: 25.793 seconds, whole interval, maximum measured global PSD
   proxy `3.814e-5`.
2. Independent 26-second ADC/active-ASRC validation: maximum `3.872e-5`.
3. Supplemental same-path 44.1-kHz-DAC/48-kHz-ADC null capture: maximum
   `4.102e-5`; all whole-capture diagnostic checks passed unchanged.

The first diagnostic omitted the special one-sided PSD scaling at DC/Nyquist.
This was corrected, with negative controls, and a fresh prospective 26-second
validation was run at unchanged `v`. The initial records remain in the archive.
An initial direct template comparison also failed because it required a known
absolute phase. The allowed unknown complex channel absorbs the constant
unit-phase difference: fitted residual was `1.4e-12`; wrong-pattern residual
was 0.986. No transmitter or receiver template was altered to conceal this.

Completion review then identified that calibration used a 48-kHz noise DAC,
while actual DATA playback negotiated 44.1 kHz. The retained independent
noise-only capture checks that actual path against the **already frozen** bound.
The derived `profile-dac-rates.json` explicitly allows the two measured rates,
links the original profile hash and labels this as supplemental held-out
validation. Original preparation, profile and DATA reports remain unchanged.
The duplex driver now rejects an undeclared/wrong actual noise-DAC rate.
Compatibility checks reused identical scores; they are not new receptions.

PSD, block-variance, mean-motion, peak and clipping checks are finite diagnostic
contradiction tests. They do not prove the complete joint sub-Gaussian moment
assumption for all future physical noise. The generous proxy is approximately
4.2 dB above the measured same-path DATA-band white-coordinate variance. It is
an explicit sensitivity cost, not a claimed optimal noise model.

## Software checks and reproduction

Final focused results: **24** physical core/profile/adapter/driver tests,
**16** native-stream tests, and **11** existing continuous tests passed. The
independent dense real Gaussian reference matched within `8.88e-16`. Tests
include colored/improper cross-block noise, FP32/FP64, chunking, DC rejection,
native waveform correspondence, mismatched profiles/rates, clipping and
incomplete or invalid receipts. Ordinary Robust regression was not run.

Use the existing NumPy/SciPy/PyTorch environment; reuse compatible native outputs
under `build/dev-cli`. Configure through `./build.sh --cli` when necessary, then
build the isolated `triad_audio_stream` and `triad_helper_export` targets. Do not
repeat old multi-day campaigns for these focused paths.

```sh
python3 -m unittest discover -s tests -p 'test_triad_physical*.py'
python3 tests/test_triad_audio_stream.py build/dev-cli/triad_audio_stream
python3 tests/test_triad_continuous.py
```

For a matching authorized cable setup, after extracting the evidence and
checking the profile still matches:

```sh
timeout --signal=TERM --kill-after=5s 110s env OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 \
  python3 tools/triad_physical_qualification.py \
  --spec local-evidence/triad-physical-admission/spec-qualified.json \
  --output local-evidence/new-physical-bit0 --bit 0
```

`--bit 1` and `--bit null` select independent TX/control trials. A profile
mismatch requires relevant new preparation/validation, not bypassing its checks.
Standalone receive uses `triad_continuous.py --mode receive` with the same public
bank, source arguments and `--physical-noise-profile`. Its remote-DAC operating
condition is operator supplied; only the duplex tool can verify local playback.

`evidence.tar.gz` retains complete native PCM losslessly as S16, restoration
hashes, metadata, frozen/derived profiles, original decisions, commands,
analysis, tests, current source snapshots and the tracked patch against
`3c563d486091143d93e81f3c4f7a6739ace919e9`. Extract under `local-evidence/` and
run its `restore_pcm.py` to reconstruct the exact recorded float32 files.
Source/binary identities are in each profile/result. The driver acquired its
stricter DAC-rate check after capture; the numerical admission implementation
and frozen scores were unchanged. No binary release or general physical
certification is implied.
