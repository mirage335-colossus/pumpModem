# Complete multi-day weak-signal tracking experiments — 2026-11-06

**Bounded simulation qualification; production multi-day live tracking is not
fully qualified.** Complete encrypted DATA observations, continuing clock
corrections, shared random channels, actual maintained receiver classes and
conditional noise trials were exercised. Ordinary Robust integration, audio
hardware uptime, blind arrival/epoch acquisition and Helper tracking are outside
this experiment. The [maintained guide](../../../triad-multiday-tracking.md)
defines assumptions, cheap checks and next implementation priorities.

Base source revision: `95804f6fce8a7e2f2e74aeea622a4e808f82c933`, plus the source
changes identified in the retained evidence. Linux Ryzen 5 PRO 5650U locally;
remote inference comparison used RTX PRO 6000 Blackwell and one thread of an
AMD EPYC 9355. No production transmitter/receiver waveform or threshold changed.

## Scope and principal results

The main frozen protocol has 24 native observations / 44 C/N0 conditions. A
separate phase protocol adds four observations / six conditions. All complete
observations process every sample and retain channel tails; duration is not
represented by restarting a short window. Low sample rates are validated
narrowband representations, not time-compressed dynamics. Actual active duration
is rounded by pattern/tile constraints and differs from requested duration.

| Complete capture / configuration | Result |
|---|---|
| 2.11 days, rate .01 Hz, IF .005 Hz, flat channel | At −35 dB-Hz, 4096/4096 correct admissions per bit in all four clock/key cases. At −40, roughly 73–75%. |
| 7.45 days, same rate | At −43 dB-Hz, approximately 99.4–99.6%; at −40, 4096/4096. |
| 41.13 days, rate .001 Hz, IF .0005 Hz | At −60 dB-Hz, only 9–12/4096 admissions per bit; at −55, approximately 30%. Insufficient energy for the selected admission goal. |
| 514.56 days, same low rate, constant phase | At −60 dB-Hz, 4096/4096 per bit for periodic clock and two independent wandering-clock/key realizations. The clock accumulates over 1,300 seconds of correction at a 200-ppm cap. |
| 41.16 days, shared Sub9-best random channel, Each25/65 | Static scalar-channel matching loses approximately 39–46 dB and misses every trial even at −35 dB-Hz. The known-channel reference succeeds at −35; this is receiver mismatch. |
| Same Each65 random channel, finite Gaussian state | Two independent full sampled signal realizations at −35 dB-Hz were correctly admitted; static/reset references missed. One null rejected. No population reliability claim. |

The static bank is a **fixed public affine timing bank**, not access to private
clock history. The two independent wandering-clock seeds are 7103 and 8107.
Minimum complexity turns a 38-day request into 40.00003 active days. Source,
channel, pattern and clock seeds, sample counts, realized energy, candidate
counts, correct/wrong admissions, misses and exact conditional 95% intervals
are in [outcomes.json](outcomes.json) and the compact evidence archive.

Each65 raised received signal energy by about 1.11–1.20 dB in the random channel
cases. Receiver comparisons use identical samples/energy, so that increase does
not explain finite-state improvement. No realized-energy normalization hides it.

## Changing phase: blockwise likelihood and GPU validation

Phase diffusion is 0.001 rad/sqrt(s), giving phasor correlation time 2,000,000 s.
It is **not** a 0.001-Hz Doppler width. The shared source has ongoing seeded clock
steps and 200-ppm catch-up. Every disjoint 256-sample block retains its full
joint two-bit Gaussian noise covariance. The maintained finite Gaussian filter
and phase-only likelihood keep state through the entire capture. Whole-window
Gram Monte Carlo is not used for evolving-state models.

A public 25-ppm template was selected from the clock law's mean correction
(100 ms/hour = 27.8 ppm), without fitting private trajectories. The original
0-ppm failures and improvement with 25 ppm are retained. Input signal hashes
match, so the waveform did not change. Conditional Monte Carlo across different
template banks is not claimed to share identical physical noise; paired actual
sampled-noise validation supplies that comparison.

| Capture / −60 dB-Hz | Finite Gaussian correct bit0 / bit1 | Phase-only 128 states | Fixed equal three-model mixture |
|---|---:|---:|---:|
| 514.56 days | 3392 / 3442 | 3507 / 3554 | 3429 / 3481 |
| 1028.00 days | 4082 / 4076 | 4075 / 4060 | 4078 / 4065 |

Each entry is out of 4096 independently generated conditional noise trials.
The third model, constant channel, missed all trials. Every model had zero wrong
admissions and zero admissions in its 4096 null trials. For the longer finite
Gaussian result, correct-admission 95% intervals are **99.427–99.813%** and
**99.247–99.701%**. The combined mixture's bit1 lower bound is **98.927%**;
therefore that combined decision has not demonstrated 99% at 95% confidence.
The longer capture contains 1,387,795 samples, approximately 1026.667 active
days, 2450.58 seconds accumulated clock displacement and 5421 inference blocks.
This tests one phase/clock path, not population reliability.

For the full 4096-trial / three-model / 514-day batch, strict FP32 inference took
**193.396 s on one CPU thread versus 7.125 s on the GPU: 27.14x**. Preparation
and noise generation are included; imports, native generation, upload, startup
and serialization are excluded from these stage timers. Including input hashing
and identity checks gives 193.432 versus 7.305 s. All admission counts matched;
maximum paired log-score difference was 5.241e-5. The 1028-day GPU batch took
13.566 s. Declared aggregate working storage is approximately 203 MiB, excluding
framework caches/allocator overhead. These are singleton measurements, not
confidence-qualified performance regression anchors.

A 128-trial paired batch gained only **1.06x** over the same-host CPU. Compared
with the local 5650U it was 2.33x faster, but that includes a host difference.
GPU batching helps the larger campaign; it does not establish a single live
receiver speedup. The entire cloud payload was under 165 MB in two uploads;
the second 41-MB upload took 20.86 s. Temporary compute and container storage
were removed and zero ongoing spend verified. Account details remain private.

## Actual timing search: retained failure and positive control

The maintained `TrackingReceiver` was run over 55,529 samples / 41.13 days,
using deliberately long **4096-second** segments, 10-second delay steps,
100-ppm rate steps, ±200-ppm rate range and 10-ppm/s acceleration bound.
These are not the live four-second defaults. Exact native templates replaced
failed cubic/higher-order interpolation at legacy-RRC support discontinuities.
Twenty native candidate comparisons now agree exactly, without changing the
waveform or loosening their tolerance. The server only receives public state
coordinates and format/key identity.

| C/N0 | Beam / proposals | Dynamic log-e | Retained initial path mass | CPU including native basis |
|---|---:|---:|---:|---:|
| −50 | 8 / 32 | −29.259, miss | 3.22e-13 | 8.36 s |
| −50 | 64 / 256 | −1.119, miss | 0.536 | 24.04 s |
| −30 | 8 / 32 | 1982.585, correct | 3.22e-13 | 9.19 s |
| −30 | 64 / 256 | 2019.383, correct | 0.242 | 24.75 s |

The persistent nominal receiver admitted the same weak observation (log-e
25.489). A separate nominal reset reference missed it (−0.494). Wider beams
mitigate pruning but do not repair the evidence lost by channel-prior renewal.
These four sampled realizations establish causal behavior, not probabilities.
Preserving channel/phase posteriors along timing paths remains the main live
weak-tracking implementation gap; this task does not silently change that model.

## Numerical, physical and statistical checks

- Independent source-window reference including real 6-kHz audio downconversion:
  maximum relative waveform RMS 2.82e-6. Omitting IF clock-warp phase loses
  21.5–28.9 dB. These numerical windows do not qualify actual audio hardware.
- Whole 40-active-day shared random Each65 channel at 0.125/0.25 sample/s:
  444,509/888,998 samples including tails, relative RMS 7.44e-5 after proper
  anti-alias filtering; energy difference 2.40e-6 dB. No receiver-private clock
  trajectory is used except in this labeled numerical reference.
- At 1725-Hz IF, the complete two-day constant-frequency diagnostic searched
  512,265 bins and lost approximately 24.2 dB under the selected clock motion.
  This rules out that shortcut for this trajectory, not all dynamic algorithms.
- Half-grid low-IF 512-day mismatch: 0.066 dB. Near-boundary full two-day FP32
  and FP64 trials give identical decisions; Gram relative error 1.44e-8.
- Maintained sampled-noise Gaussian receiver versus blockwise sufficient
  statistics: maximum score difference about 4.1e-14 in FP64.
- Native protocol CTest passed in 0.29 s; likelihood six checks and blockwise
  five checks passed; exact-native/identity-guard 28 checks passed in 2.29 s.
  Start/end source and sampled-input guards invalidate changed experiments.

Threshold is ln(10000), with fixed normalized bit/candidate/model mixtures.
Under the stipulated proper Gaussian noise model each likelihood ratio has
unit null mean; Markov bounds a **single fixed endpoint** by 1e-4. A schedule
with at most one endpoint per day uses that daily budget. Extra starts,
durations and chosen models require budget allocation or a normalized mixture.
Do not accumulate a fresh per-day allowance and spend it all at a long bit's end.
The static cases saw 0/32768 null admissions per condition; their two-sided 95%
upper bound is about 1.13e-4. The GPU 0/4096 bound is 9.00e-4. These counts alone
do not demonstrate 1e-4 physical false admissions. Physical ASRC noise, RF noise
and arbitrary repeated stopping are unqualified. Null cohorts are not pooled
across dependent conditions.

Ideal known-signal Gaussian Neyman–Pearson presence detection at alpha1e-4 and
99% probability needs E/N0≈18.27, or 211.5 days at −60 dB-Hz before shaping/search
loss. Ideal bit selection alone at 38 days is a different, easier problem.
The retained known-channel likelihood reference uses a conservative Markov gate,
not an exactly calibrated optimal detector. For independently renewed rank-one
Gaussian channels, total KL≈C² tau T/2 at low block energy; this quantitatively
explains the severe reset penalty. These assumptions are explicit in the guide.

## Reproduction and cost discipline

Use [campaign-protocol.json](campaign-protocol.json),
[phase-protocol.json](phase-protocol.json) and the commands retained in
`evidence.zip`. All raw compact reports and NPZ sufficient statistics are
included; large regenerable sample streams and cloud account records are not.
`evidence-index.json` binds archive members to SHA256 hashes.

The main 24-case campaign cost **552.58 child CPU seconds / 554.12 wall seconds**
including generation, subprocess imports and analyses. Source/channel refinement
adds 16.59 CPU s; four dynamic cases add 66.34 CPU s; the two random-channel
finite-state signal trials plus null and exports add 81.96 CPU s. These are
separate work boundaries, not an end-to-end production latency forecast.
Run only affected focused checks routinely; do not replay historical long
campaigns to obtain a green check. No ordinary-receiver regressions were run.

```sh
./build.sh configure --cli
CCACHE_DISABLE=1 cmake --build build/dev-cli --target triad_tracking_export -j2
ctest --test-dir build/dev-cli -R '^triad_tracking_export$' --output-on-failure
# PYTHON means an environment with numpy, scipy and torch.
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 "$PYTHON" tools/triad_multiday_campaign.py \
  --protocol docs/validation-data/triad-v3/2026-11-06-multiday-tracking/campaign-protocol.json \
  --exporter build/dev-cli/triad_tracking_export --output /tmp/triad-new-campaign
# Focused phase qualification, not routine CI:
build/dev-cli/triad_tracking_export --rate .001 --sample-rate 1 \
  --sample-rate-denominator 64 --days 1024 --data-if half-rate \
  --clock-profile periodic --template-ppm 25 --seed 11113 --key-seed 101113 \
  --phase-diffusion .001 --output /tmp/triad-new-phase1024
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 "$PYTHON" tools/triad_multiday_block_mc.py \
  --samples /tmp/triad-new-phase1024 --template-ppm 25 --cn0 -60 \
  --trials 4096 --batch-trials 4096 --seed 873031 --phase-states 128 \
  --device cuda --output /tmp/triad-new-phase1024-result.json
```

For a CPU diagnostic, change device to `cpu` and both trial counts to 128; that
changes the noise cohort and its confidence interval. Exact full source/build
hashes will differ after the metadata-only native server and provenance fixes.
The canonical waveform is unchanged. Exact historical driver sources are
retained in `source-cohorts/` inside the archive. One main-campaign run loaded
the old scalar reset code while its finish-time source hash observed a newer
vectorized reduction; this known provenance exception is retained explicitly.
A paired equivalence comparison accompanies the source snapshots. Subsequent
source/input guards prevent silently qualifying such an event.

## Outstanding boundaries

No claim is made for arbitrary multi-day remote timing trajectories, full
oscillator noise spectra, blind synchronization, Helper-assisted high-IF
tracking, production ASRC covariance, hardware uptime, FHSS or Symbol Burst.
The faster random channel at −60 dB-Hz is not qualified by a success on the much
slower phase-diffusion model. Independent phase/channel path populations and
persistent timing-path channel inference remain necessary before widening the
production-live claim. A 2048-day source was generated but not receiver-tested;
it supplies no additional reception evidence.
