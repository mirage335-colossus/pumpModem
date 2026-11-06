# Default 200-ppm audio discipline and bounded remote timing tracking

This study changes the isolated triad live defaults, preserves the prior
[50-ppm/fixed-bank evidence](../2026-11-06-continuous-clock/README.md), and adds
bounded timing-state reception. Ordinary Robust receiver integration is excluded.
No host-clock steps, mixer changes or cloud allocations were made.

## Implementation and conditional guarantees

* Live TX/RX follows UTC updates by default (`auto`); simulation/copy stays off.
  Explicit off remains. Shared ordinary audio defaults are unchanged. Dated ALSA
  is supported; unsupported providers fail visibly. Windows replay requires
  `--follow-system-clock off --receiver fixed`.
* Phase catch-up is 200 ppm, separate from device-rate calibration and the
  existing 1000-ppm total limit. The repeated-correction controller retains its
  original association, full waveform, encryption epoch and filter state.
* Exact local ADC source coordinates accompany PCM in one bounded mapped pipe.
  RX uses them for both DATA mixing and templates. This does not whiten ASRC noise.
* A public delay/rate HMM advances remote timing without restarting evidence.
  It retains competing states and never renormalizes pruned likelihood mass.
  Gaussian channel priors reset at each declared segment, while bit/coherence
  priors persist. Initial calibrated residual timing is an explicit assumption.
* Bounded uniform DATA caches eliminate redundant synthesis across hypotheses.
  Unsupported interpolation geometry uses the direct native reference. Candidate
  phase and the audio IF time-warp term remain explicit. Template preparation
  uses higher precision than bulk FP32 likelihoods; complete CPU timings include it.
* Missing rows intersecting DFT blocks cause conservative complete-block erasure,
  with extra discarded rows reported. Direct fixed reception still rejects
  partial DFT masks. Neither path claims a new covariance adapter.

The HMM is a normalized alternative under its declared Gaussian noise model.
Path deletion gives a pointwise lower likelihood than the complete alternative;
it does not guarantee true-path retention or sensitivity. Physical/Helper/ASRC
false-admission qualification remains outstanding. All sampled real-PCM and cable
results below withhold operational admission; large log-evidence is diagnostic.

## Cheap functional and waveform checks

Controller tests cover both signs of a two-second step and recurring corrections
through 38 virtual days. A two-second correction reaches below 1 ms in **14,409 s**;
maximum pre-hourly residual in the recurring test is **5.834 ms**. These are
controller elapsed-time simulations, not days of hardware uptime.

The forward-state tests compare exhaustive path sums, mixtures, pruning,
erasures and integer coordinates; a 47.4-virtual-day state test travels beyond
200 seconds of timing offset with bounded storage. It does not synthesize
47 days of analog samples. DSP references use independent dense Gaussian
covariances and explicit timing paths.

Native uniform mapped templates are bitwise equal to the prior basis arithmetic.
Independent nonlinear full-audio reconstruction gives relative squared error
3.70e-31. Native protocol and mapped packet tests check finite counts, exact
origins, malformed maps, split packets, fixed/multistage conversion and defaults.
The actual cable run below validates the complete native packet/Python reader path.

Six cache tests include real native samples at pulse, FHSS and burst boundaries:

| Case | Relative RMS error | Normalized peak error |
|---|---:|---:|
| Rate 36 ×10 | 3.28e-6 | 2.27e-5 |
| Rate 36 ×10 FHSS | 3.60e-6 | 2.10e-5 |
| EME 360 ×1 fast FHSS | 3.59e-6 | 2.45e-5 |
| Rate 36 ×10 Burst wait3 | 3.16e-6 | 1.83e-5 |
| Rate 24000 at 96 kHz, dense factor2 | 1.66e-5 | 6.36e-5 |

Native/shared checks passed: mapped basis, continuous controller, ALSA map/clock,
source protocol, audio CLI, resampler, ALSA full contract/interleave, Windows
contract, v3 source/channel/receiver and legacy bounded clock/control paths.
The three retained CTest groups took about 7.27, 24.55 and 15.24 seconds respectively.
Python suites cover10 frontend, 4 map, 9 forward, 12 tracking, 6 incremental and 6 cache cases.
No ordinary full regression/release campaign or GPU qualification is implied.
The preexisting capture_transport fixture failure recorded in the historical
study is not relabeled as a pass or repaired in this scope.

## Actual sampled full-audio comparisons

The probe uses the native transmitter, actual continuous Resampler and independent
clock schedules. Real full-rate white noise is added before RX resampling. RX
receives only its own source map, never the private transmitter trajectory.
Flat None/Off propagation and oscillator-off are explicit isolation controls.
Short cases contain109 s at6000 Hz with8 dB-Hz nominal DATA C/N0. Repeated clock
updates are slew-limited; a two-second UTC correction is not a two-second waveform
jump. The initial residual source-delay/rate prior is calibrated zero.

The first RX-only200-ppm case exposed both a model and a computational pitfall:

| Receiver on identical samples | Diagnostic log-e | CPU seconds |
|---|---:|---:|
| Fixed nominal sample coordinates |96.965|4.479|
| Fixed remote timing, exact local ADC coordinates |437.995|4.298|
| Remote timing HMM, direct native templates |358.415|206.062|
| Same HMM, cached templates |358.415|23.840|

The cached HMM is **8.64× faster by CPU time** (9.22× by wall time), with
3.60e-5 log-evidence difference. This is direct paired evidence, not an inferred
GPU acceleration or a runtime bound for other search sizes. Native repeated
synthesis accounted for about198.6 of the initial206 CPU seconds. Its worst 4 s
segment needed12.64 s wall; expansion stopped until the cache was implemented.
The HMM's independent4 s channel priors explain why the ideal mapped fixed
receiver collects more evidence in this stable-remote control.

Paired moving-TX/RX design and independent validation examples:

| Seed, Helper | Phase cap | Fixed nominal log-e | Tracking log-e | Tracking CPU s |
|---|---:|---:|---:|---:|
|94001 off|50 ppm|91.585|309.637|24.069|
|94001 off|200 ppm|64.307|364.480|25.633|
|94001 on|200 ppm|64.272|364.457|22.489|
|94002 off|200 ppm|27.578|349.602|23.876|
|94002 on|200 ppm|27.534|349.477|25.032|
|95017 off, independent|200 ppm|56.601|340.887|22.850|
|95017 on, independent|200 ppm|56.593|340.877|22.574|

A separate exact-paired moving-TX control isolates remote inference: at seed 94001
and 200 ppm, fixed nominal, fixed mapped and tracking log-e are64.307,87.362 and
364.480. Most recovery here requires the remote state model, beyond knowing the
local RX map. This comparison retains identical source/noise identities.

All eight completed short moving controls (including the retained50-ppm Helper
case) selected the expected bit and passed their unchanged diagnostic threshold.
They are a small paired signal sample, not a99% reception or rare-false-admission
measurement. Correct-bit selection and conditional threshold passage are distinct
from operational admission, which is withheld. No fresh noise-only campaign is
claimed; the conditional null argument and physical-null gap remain explicit.

## Outage failure and bounded remedy

The first independent 95017 erasure attempt stopped: an outage specified in
physical ADC time intersected only part of a logical DFT block after resampling.
The old fixed-reference path rejected partial masks before the tracking arm ran.
The original exit4 and error log are retained; they are not counted as a noisy
bit decision. The remedy is conservative complete-block erasure in the new
tracking receiver, not a relaxation of the direct receiver's covariance contract.

The repaired Helper-off trial selects bit 0 with diagnostic log-e 328.798. It
received 11998requested invalid rows and discarded 15000 rows: **3002extra rows
(0.5003 s)** in two edge blocks. Extra energy loss is exposed rather than hidden
as added Gaussian noise. Fully erased segments advance timing with unit evidence.
Three independent focused tests verify the exact block calendar, terminal merges,
raw-row behavior and equality to an explicitly erased reference. All-valid
before/after scores remain exactly equal, supporting reuse of the final cable
anchor for this unaffected branch.

## Longer sampled correction stream

The independently seeded long run processed **1002.2 seconds / 6,013,200 samples**
at 6 kHz, with 32 TX and 29 RX clock updates. It selected the correct bit with diagnostic
log-e 4659.826. Receiver cost was277.053 CPU seconds /263.572wall, or **0.2764 CPU s
per received second**; generation/preparation added 18.598 CPU seconds. Total
295.699 CPU seconds stayed below the 400 CPU-second protocol stop. The worst 4 s
segment needed1.573 s wall.

The 18.598 CPU seconds are experimental fixture generation, noise and TX/RX
resampling, not receiver startup. The receiver constructor took less than a
millisecond; on-demand native template-cache filling is included in its
277.053 CPU-second processing total.

The frozen engineering forecast was 354.59 CPU receiver +23.98 CPU preparation
(378.6total); observed total was about21.9% lower. This is a retained prediction
and full-run anchor, not a confidence-qualified runtime model. The long run
crossed the32 MiB cache ceiling, caused1156evictions and used no direct fallback;
it therefore adds evidence for a memory regime not present in the short runs.
Peak actual timing population was105states. No observation-sized DSP storage
was needed in the maintained receiver; the finite qualification driver separately
held its sampled test arrays.

The final displayed remote source offset was137.78 ms, well beyond the earlier
few-ms static bank. The transmit sample-count difference is consistent with
roughly 138.67 ms near its endpoint, but is not a per-segment timing oracle. RX
never receives the TX correction trace. Retained initial path mass was 1.36e-11:
pruning is real, and this strong-evidence run does not establish true-path
survival for arbitrary long fades or multi-day very-weak signals. Operational
admission remains withheld because the actual ASRC covariance is unqualified.

## Selected random-channel limit

A paired RX-only test retained the shared stochastic HF-worst channel: 21 ms
excess delay, 90 Hz ray separation, 30 Hz random Doppler width, 1 µs delay motion
and Each 65% random additional modulation. Oscillator effects were explicitly off.
Native thermal noise was made negligible at 200 dB-Hz, then the same known real
8 dB-Hz noise was added before RX ASRC. This preserves the full channel and its
tails without accidentally folding an additional full-Nyquist noise image into
DATA. No transmitter warp was applied after the channel. Both runs retain the
complete 131.0703 s observation, including channel/filter tails.

| Clock condition | Fixed mapped log-e | Tracking log-e | Diagnostic threshold |
|---|---:|---:|---|
| Stable |−6.746|−32.861|Both miss|
| RX200-ppm corrections |−6.745|−33.756|Both miss|

Both selected the expected bit, but neither passed detection. Complete channel
DATA energy was 89.291 normalized units, with 80.609 in the diagnostic ±160 Hz
band. Loss of signal energy alone does not explain failure; matching flat-source
energy was 69.8174 normalized units, so total channel energy was 1.068 dB higher. The one-tap
0.5/2/infinite-coherence bank does not represent this rapidly changing channel;
the unknown-channel problem may also require a longer observation. No known-
channel or optimal unknown-channel benchmark was run here, so this does not
prove that a different receiver can recover the same DATA allocation. The paired
result only separates the additional RX timing effect from the already failing
channel/duration configuration. These failures remain unresolved, not tuned away.

## Cable and source identity

The final default-policy cable run (`live/final-default/`) captured **451,200**
logical 6 kHz frames (75.2 s) and transmitted 439,200 frames (73.2 s), with a 44.1 kHz
native ADC. It consumed all mapped packets, preserved the predeclared 13-arrival
bank, used Helper, and had no clipping or stream error. Correct/wrong bit scores
were 82543.80/1774.90; physical admission remained false.

Wall time was 76.003 s. Python CPU 14.392 s plus child CPU 5.123 s gives **0.2595 CPU s
per received second**, including TX source generation and both audio processes.
The slowest 4 s receiver segment took1.504 s wall. Cache occupancy was 6.82 MB;
receiver declared working storage was 97.96 MB, excluding process/library overhead.
Both default clock controllers were active; no actual host clock steps occurred.
This is high-SNR transport evidence, not marginal sensitivity or multi-day uptime.

An earlier successful run is retained in `live/cached-default/`, but source
files changed during it. The final run deliberately repeats the same declared
case with identical start/end source hashes. The subsequent erasure-only patch
leaves the all-valid numerical branch unchanged; its matched before/after
reference and separate missing-row tests justify reusing this cable evidence.
The interrupted pre-cache preflight is retained; it never opened the audio device.

## Reproduction and scope

Start with [the maintained inexpensive commands](../../../triad-clock-follow.md#use-and-inexpensive-qualification).
Use the NumPy/SciPy/Torch study environment. Native tests use build/dev-cli after
`./build.sh configure --cli`; old build-* directories are not current evidence.
Do not repeat this sampled/cable campaign for unchanged branches. The cache
reference takes about 4.4 s; core Python checks take seconds. Requalify the affected
stage when waveform arithmetic, maps, mask/block semantics or inference changes.

For occasional reproduction, copy the retained sampled scripts/protocols into
`local-evidence/triad-tracking-200/sampled/`, compile its small `warp.cpp` against
src/resampler.cpp, and use a fresh --tag. The live script runs from the repository
root, writes beside itself and requires a fresh --tag; copy it to ignored
local-evidence before execution. It preflights throughput before opening hw:1,0.
Do not change the host clock. Source/build/input identities are retained per run.

Supported tracking is conditional on acquisition coverage, declared rate/acceleration,
coherence and enough evidence to retain the correct state. Four-second channel
resets, long fades/outages and beam pruning can lose sensitivity. This study does
not establish arbitrary multi-day analog-waveform tracking, −60-dB-Hz reception,
a multi-day two-pilot tracker, physical false-admission rates, or GPU speedups.
