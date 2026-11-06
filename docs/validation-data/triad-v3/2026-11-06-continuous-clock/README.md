# Repeated clock corrections: full-audio best-effort discipline

This qualification adds an opt-in shared native PCM controller, not a new wire
format or an ordinary Robust receiver integration. The [maintained guide](../../../triad-clock-follow.md)
describes use and limitations. Default operation remains unchanged.

## Implemented behavior

Capture steers the complete retained audio passband before the DATA mixer;
playback steers composed DATA/Helper before the final DAC-rate AES noise stage.
One continuous resampler preserves sample order, filter history and source/key
identity. Repeated UTC steps update one target, with no lifetime displacement
cap or correction queue. Dated device frames versus monotonic time estimate
physical sample-clock rate; UTC/monotonic steps affect phase separately.

The selected default separates up to 1000 ppm of device-rate correction from
**50 ppm maximum UTC phase catch-up**, with 10 ppm/s rate slew. This replaced
an initial design that allowed a clock step to consume the entire 1000-ppm
rate allowance. Insufficiently precise but plausible rate estimates can extend
from 60 to 3600 seconds without growing memory. Reports expose phase limiting,
rate saturation, holdover, uncertainties and actual converter coordinates.

Physical RX adds a public observation allowance from the two software rate
bounds, declared affine clock, arrival and echo bank, and an outward numerical
guard. A million-second observation requires at least 2001 additional seconds
at the maximum default software bounds, before other allowances. This protects
its declared calendar; it is not an adaptive remote timing estimator or a
physical-end certificate. Physical admission remains withheld and completion
is explicitly clock-tracking-unqualified.

## Evidence levels

- **Controller simulation:** 38 simulated days in each direction, continuing
  device drift and hourly 100-ms corrections plus smaller and ±2-second events.
  More than 90 seconds of cumulative corrections are handled in constant memory.
  Maximum pre-hourly-step residual: about **6.79 ms**. This is simulated elapsed
  time, not 38 days of wall time or sample-by-sample audio.
- **Isolated phase-step convergence:** ±2 seconds settles below 1 ms after
  **43,015 seconds (11.95 hours)** under the gentle default. The controller does
  not promise to settle arbitrary correction rates. Deliberately unsustainable
  sequences leave visible backlog and a phase-limit indication.
- **Coarse timestamps:** 100-ms endpoint uncertainty with 100-ppm device drift
  produces two accepted rate fits by 600 seconds. Previously every 60-second
  interval was rejected forever. Real ±100-ppm and +400-ppm device offsets are
  not clipped to the separate 50-ppm phase allowance.
- **Actual native ASRC:** the cheap ALSA stub exercises full PCM capture and
  playback, repeated corrections, opposite signs, finite-source completion,
  monotone frontiers, true counter faults, and gain/phase at 175/1300/2400 Hz.
  Retained logs distinguish initial broad-phase and final gentle-phase checks.
- **Maintained sampled receiver:** actual native keyed waveform and actual
  resamplers, fixed public hypotheses, paired pre-RX noise, independent TX/RX
  corrections. The receiver never receives the private correction trace.
- **Physical cable:** bounded paired runs use the actual headphone/microphone
  route with unchanged mixer settings. Host clock is never changed. These test
  live transport and receiver flow, not a 38-day uptime or false-admission rate.

## Receiver findings and the default choice

All following cases use Rate 36 × Added Symbols 10, 1725-Hz DATA at 6 kHz,
C/N0=8 dB-Hz, an explicit flat None/Off channel and ideal dated device frontiers.
The emitted observation is about 108 seconds plus public receiver padding.
Propagation, oscillator wander and additional selectivity are deliberately off
to isolate clock mismatch. No result here qualifies the difficult HF channel.

The conditional diagnostic threshold is log-e 22. It is unchanged across these
comparisons; operational admissions are withheld for physical/ASRC covariance.
The table uses the decision statistic including the two-bit prior, rather than
the unadjusted per-bit score also retained in the raw receiver JSON.

| Controller / fixed receiver basis | Corrected diagnostic results | What it establishes |
|---|---|---|
| Initial 1000-ppm phase catch-up, one tap | log-e 36.56, 36.49, **6.66**; one miss | Smooth audio alone does not preserve a fixed-template receiver |
| Initial phase catch-up, 13 delays across ±30 ms | 16.98 and 35.67; one miss | Broadening the nuisance model alone is not a free remedy |
| **50-ppm phase catch-up**, original one tap | 62.71, 62.66, 37.04, 79.60; all four pass | Gentler catch-up fixes the observed diagnostic miss without new receiver code |
| 50-ppm catch-up, seven fixed delays across ±12 ms | **136.44, 188.14**, both pass | Existing evolving delay basis can improve this bounded regime |

All raw bit decisions in these signal diagnostics were correct. The original
1000-ppm case reached about 299 ppm per endpoint and 17.83 ms net waveform
advance; the gentle case reached about 50 ppm and 4 ms. Independent seed 94003
was included after selecting the gentle controller. These small paired sets do
not estimate a high reception probability or a low false-admission probability.

The seven-delay comparison retains matched stable-bank controls: stable/event
log-e **214.14/136.44** and **196.42/188.14**. It costs approximately
**0.027–0.030 CPU seconds per second of input** on this host; the original
single-tap path costs roughly 0.014–0.021. It uses an arrival of −12 ms and
nonnegative delays 0..24 ms at 4-ms spacing, with fixed 0.1/0.5/1-second Gaussian
coherence alternatives and equal total prior power. It covers only this
110-second, phase-only 50-ppm-per-endpoint envelope; it does not cover the full
1000-ppm device uncertainty, arbitrary duration, or all propagation delays.
It remains a measured public-bank option, not an automatic universal setting.

Infinite-coherence evidence still drops substantially during corrections.
An evidence ratio is **not** a calibrated sensitivity loss in dB. No unchanged
sensitivity or general remote clock-tracking claim is made. A broader blind
receiver needs ongoing timing inference (potentially two-pilot relative timing)
or suitable public evolving-delay models. The extra noise covariance from
adjustable capture remains unqualified; nominal white-noise transport is not
silently reused.

## Live drain defect and controls

The first two cable attempts produced the expected raw bit but failed playback
completion. Their error receipts are retained in `initial-cable/` and
`second-cable/`; neither is counted as a successful complete cable test.

The second attempt identified the exact terminal state:
`EIO, SETUP, confirming DRAIN=EAGAIN, SETUP`. The raw hardware ALSA adapter uses
its own state check; Linux's nonblocking DRAIN can return EAGAIN even when the
stream is already in SETUP. Requiring a confirming return of zero was therefore
incorrect. The narrow repair requires SETUP before and after a nonfatal
confirmation and retains failures for XRUN, suspend and disconnection. It
never recovers, replays or drops source samples. Sixteen focused controls cover
terminal success, incomplete drain, state changes, fatal errors and finite tails.
See [ALSA hardware implementation](https://github.com/alsa-project/alsa-lib/blob/v1.2.14/src/pcm/pcm_hw.c)
and [Linux PCM drain](https://github.com/torvalds/linux/blob/master/sound/core/pcm_native.c).

The final [paired cable summary](live-summary.json) has no errors in either
mode. Both transmit all 439,200 logical frames and capture all 451,200 declared
frames (75.2 seconds at 6 kHz); both select the expected bit 0. Peak captured
amplitudes are 0.02425/0.02432 and no clipping is reported. This is a high-SNR
transport check, not a threshold or sensitivity comparison between independent
analog noise realizations.

All-process CPU cost is 0.1454 seconds/input-second with discipline off and
0.1400 with it on, on the Ryzen 5 PRO 5650U. This single pair does not establish
a speedup. Final continuous playback/capture use 1.516/2.154 CPU seconds,
respectively. Each obtains one rate fit; final corrections are approximately
+0.874/−0.779 ppm, with no total-rate saturation or phase limiting. Three
playback startup reports are explicit holdover. There are no injected clock
steps in the live runs; repeated-step evidence comes from the controlled tests.

Final DAC-rate AES noise remains at 44.1 kHz: 3,228,120 active samples, expected
variance 1.2417634e-9 and measured 1.2419865e-9 in the continuous run. It is added
after output resampling. These receipts also preserve actual binary identities.
The final adapter subsequently received only a decimal-ppm parsing boundary fix;
the cable-default path was unchanged and its device-free regression passed.

Final focused native checks: **6/6 pass in 24.95 seconds**, including the
existing complete ALSA and Windows mock contracts, resampler, controller,
continuous-audio mock and pipe adapter. The Python adapter guards pass 8 tests;
the native pipe CLI passes 9 tests. Standalone controller ASan/UBSan passes with
leak detection disabled; this is not sanitizer coverage of every audio backend.
See the retained test logs and [source/build provenance](provenance.json).

## Reproduction and routine scope

Use the short commands in the maintained guide. Routine clock regression is the
controller, focused ALSA continuous test and adapter guards. Run the existing
shared ALSA/Windows contract and resampler checks after changing shared audio.
Do not repeat these historical receiver campaigns or use RunPod to test a clock
map. These diagnostics are inexpensive local CPU workloads.

The retained `run.py`, `warp.cpp`, protocol and results files document each
sampled experiment. They used the corresponding source/header hashes recorded
in their results; the initial scripts' default phase cap belongs to the earlier
controller. To reproduce that historical branch on the current header, explicitly
set `ContinuousClockOptions.maximum_phase_correction=.001` in its probe before
constructing the controller. The gentle probes use the current default. Scripts
write under `local-evidence/continuous-audio-clock/receiver-pair/`; recreate their
expected directory and compile the small probe against `src/resampler.cpp`
using C++20 and `-O2 -Iinclude`. Keep original evidence before rerunning.

`capture_transport` has a separate **pre-existing** failure: unchanged HEAD's
fixture puts Helper/Data too close under current shaping rules. It reproduces
without linking native audio objects. The extracted/HEAD test SHA256 is
`05ad24ec5b7388a5a6846051046589abe20baee405834a9c45e25817cc596f62`.
Its log is retained. This unrelated ordinary fixture was not modified or counted
as passing. No ordinary receiver integration, broad regression campaign, GPU
qualification or cloud rental was performed for this change.
