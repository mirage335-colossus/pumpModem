# Continuous full-audio clock discipline

The isolated continuous triad adapter offers **opt-in best-effort discipline of
complete audio PCM**, using `--follow-system-clock continuous`. It supports
repeated UTC corrections and continuing device/system-clock drift without a
lifetime ±2-second displacement limit. The default remains `off`. The older
`bounded-data` source-only experiment remains available for historical replay;
its narrower scope is described below.

The [controller, sampled-receiver and cable evidence](validation-data/triad-v3/2026-11-06-continuous-clock/README.md)
records the initial failures, gentler default, tested public delay-basis option
and remaining receiver limits. Successful audio discipline alone does not
qualify a multi-day fixed-template receiver.

The new policy uses the shared native audio resampler. On capture it adjusts
PCM **before DATA mixing or projection**, so DATA, Helper pilots and auxiliary
symbols, interference and captured noise share the same time map. On playback
it adjusts the composed waveform before the final hardware-rate cryptographic
background-noise stage. It preserves the original logical sample sequence,
cryptographic epoch/counters, interleave and source calendar. There is no clock
step converted into a sample skip, repeated block, filter reset or new epoch.
This is an audio-time correction, not an RF-oscillator correction.

The operating system's clock and the ADC/DAC clock are distinct. A wall-clock
step alone does not prove that the incoming analog waveform changed. This
policy deliberately disciplines elapsed logical PCM time against **dated device
frames** and the corrected system clock. It retains the session's initial
association; it does not replace start scheduling, choose a new source epoch
or establish absolute alignment with an independent remote transmitter.

## Mechanism and limits

[`ContinuousClock`](../include/datapump/continuous_clock.hpp) maintains a bounded
rate estimate and one current correction target. Device-frame progress versus
monotonic time estimates the clock rate over finite windows. UTC minus
monotonic changes update the phase error without being mistaken for device-rate
changes. On Linux, monotonic time also follows ordinary system-clock frequency
slewing; comparing it with actual device frames therefore detects that drift,
unlike observing wall-minus-monotonic alone. See the
[Linux clock definitions](https://man7.org/linux/man-pages/man3/clock_gettime.3.html)
and [ALSA timestamp design](https://www.kernel.org/doc/html/latest/sound/designs/timestamping.html).

The phase error is evaluated at the resampler's actual native/logical frontier,
with a deadband for reported timestamp uncertainty. If `q` is the estimated UTC
seconds per nominal device second, the requested logical rate is
`r = q + clamp(effective_phase_error / phase_time_constant, +/- phase_cap)`.
Playback uses correction
`r - 1`; capture uses `1/r - 1`. The existing resampler integrates the mapping
with continuous phase and retained filter history. There is no correction queue
or second independently integrated source clock.

Defaults are:

| Control | Default | Meaning |
|---|---:|---|
| Maximum software rate correction | 1000 ppm | Existing resampler limit; not an RF tolerance or synchronization certificate |
| Maximum UTC phase catch-up | 50 ppm | Gentle correction of clock steps, separate from actual device-rate calibration |
| Maximum rate slew | 10 ppm/s | Continuous change of converter rate |
| Phase time constant | 1000 s | Gradual removal of phase backlog after uncertainty is allowed for |
| Rate-estimation window | 60 s | Bounded-memory device-frame/monotonic slope estimate |
| Maximum rate-estimation window | 3600 s | Extend uncertain but plausible fits; report rejection if still uninformative |
| Maximum date extrapolation | 2 s | Longer gaps hold the last applied rate |

New small corrections update the current error immediately; they need not wait
for an earlier correction to settle. The cumulative offset is unbounded within
checked numeric/counter limits, but **the correction rate is bounded**. A
2-second error needs at least 40,000 seconds at the default 50-ppm phase cap,
before allowance for slew, uncertainty and gradual convergence. The synthetic
isolated-step check reaches below 1 ms in about 11.95 hours. A larger explicit
phase cap can catch up faster but imposes more transient receiver mismatch.
Corrections arriving faster than
the available rate reserve can accumulate a backlog. Reports expose saturation,
phase limiting, holdover, uncertainty, rate estimates, native/logical coordinates and phase
error rather than claiming synchronization. Invalid timestamp queries hold the
last rate; a genuine device-counter discontinuity remains an explicit error or
existing capture-recovery event. The program never changes the host clock.

This is separate from `audio::Options::follow_system_clock`, its strict
`UtcTimeline` and affine presentation contract. Combining continuous discipline
with strict following or fixed-PCM timing observation is rejected. The new mode
requires an explicit mapping observer and a supported dated native provider;
unsupported Windows/browser providers reject it. Timestamp quality remains
estimated unless the provider independently supports a stronger contract.

## Waveform and receiver consequences

The complete retained audio passband is steered together, including Helper and
FHSS/Burst envelopes. Their logical lead-in, dwells and silence remain in the
source sequence, while their physical duration changes slightly with the rate.
This avoids independently distorting DATA relative to its local Helper. It does
not establish acquisition or sensitivity under every possible remote clock
trajectory. The phase cap sustains 100-ms corrections each hour in the tested
model; arbitrary corrections faster than its available reserve cannot settle.

The converter has a finite interpolation/antialias filter. Its declared flat
passband is `0.42 * min(input_rate, output_rate)` even for equal-rate adjustable
conversion. At 6 kHz this is 2520 Hz, which includes a 2400-Hz pilot; higher pilot
placements need an adequate sample rate. Enabling discipline is consequently
not sample-identical to an equal-rate bypass. Retain source/channel tails and
supply any required terminal silence when qualifying finite captures; no filter
can be assumed to preserve the full Nyquist band or arbitrary truncated tails.

A 1000-ppm local correction moves a 1725-Hz audio tone by up to approximately
1.725 Hz. It does **not** multiply the 30-MHz or 5.8-GHz RF shift. Independent TX
and RX corrections can temporarily differ. A GPSDO-derived oscillator bank alone
may therefore be too narrow for the resulting software-rate excursion, and a
single full-bit affine template need not describe a multi-day varying map.
Helper measurements can describe received pilot motion but local clock telemetry
is not the remote transmitter's private correction trace. Do not feed that trace
to RX or silently treat it as observed synchronization evidence.

Adjustable capture reports an **unknown capture transport recipe**: the
interpolation colors noise and its covariance changes with the rate history.
The fixed nominal white-noise transport witness is deliberately unavailable.
The current continuous physical receiver already withholds operational admission;
this mode does not relax that rule or certify its false-admission rate. Receiver
scores and conditional model passes remain diagnostics. General blind multi-day
tracking, remote nonlinear clock inference and the physical noise covariance need
separate qualification. No claim of unchanged sensitivity follows from controller
stability alone.

## Use and inexpensive qualification

Add `--follow-system-clock continuous` before the native `--` separator of
[`triad_continuous.py`](../tools/triad_continuous.py) in transmit or receive mode.
Use the intended policy independently at each endpoint. Existing key, epoch,
power, source configuration and public receiver-bank requirements remain.
`simulate` rejects this hardware policy: use injected device timestamps through
the controller and native audio tests for controlled clock experiments.

At the native PCM interface, select:

```text
triad_audio_stream --mode play|capture --frames N --sample-rate HZ \
  --device DEVICE --clock-discipline continuous
```

The native and triad adapter options `--clock-max-ppm`, `--clock-phase-max-ppm`,
`--clock-slew-ppm-per-second` and `--clock-phase-seconds` adjust the controller.
Copy mode has no dated device and rejects continuous discipline.
Read the final `clock_discipline` receipt; enabling the policy is not evidence
that it acquired a reliable date or settled its backlog.

Continuous physical RX adds a public endpoint allowance before collecting any
samples. With software limit `c` at each end, the maximum relative duration
factor is `(1+c)^2`. The adapter also includes the declared affine-clock bank,
positive arrival and delayed-path extent, using checked rational arithmetic
and a small outward numerical guard (64 double-precision epsilons plus two frames).
At 1000 ppm this adds 2001 seconds to a nominal million-second observation
before other declared allowances. This is a conservative capture calendar, not
a physical-end detector or permission to claim reception outside the bank.
Completion is reported as `completed_clock_tracking_unqualified`; operational
admission remains withheld. Undeclared hardware/path timing is not covered.

Use cheap injected-clock and actual-resampler checks before live testing:

```sh
./build.sh configure --cli
CCACHE_DISABLE=1 cmake --build build/dev-cli --parallel 2 --target \
  test_continuous_clock test_alsa_audio triad_audio_stream
ctest --test-dir build/dev-cli --output-on-failure \
  -R '^(continuous_clock|alsa_continuous_clock|triad_audio_stream)$'
OPENBLAS_NUM_THREADS=1 python3 tests/test_triad_continuous.py
```

The controller test covers long simulated elapsed time, repeated small and
±2-second corrections, drift, inverse TX/RX signs, holdover and an unsustainable
correction rate. The focused ALSA stub mode uses actual full-PCM resampling with
injected timestamps. These exercise different claims: neither is a days-long
hardware uptime or end-to-end weak-signal reception certificate. Keep the
existing strict audio tests separate and intact. A short cable check can then
measure timestamp availability, passband, stream continuity and CPU cost. Do
not repeat historical GPU campaigns or wait days merely to test a clock map.

## Legacy bounded-DATA experiment

`--follow-system-clock bounded-data` smoothly changes only the DATA pattern
coordinate before its final audio-center NCO. It retains nominal receiver
templates and requires the same public source profile at both ends. Its scope
is fine-symbol rate at most 0.001 Hz, with no Helper, FHSS or Symbol Burst. It has
a **total ±2-second displacement bound**, 300-ppm rate bound and 0.2-ppm/s
acceleration bound; one 2-second change takes 12,500 seconds to settle. A public
two-second endpoint allowance retains its complete source support. It cannot be
combined with full-audio discipline.

The existing [source and receiver evidence](validation-data/triad-v3/2026-11-06-clock-follow/README.md)
remains valid for that historical profile. Its largest measured low-rate template
loss was 0.000706 dB. The separate HF local-affine representation experiment was
initialized from known trajectory and is not blind acquisition. Neither result
transfers automatically to the new complete-audio policy.

That mode's Python `ControlClock` observes wall-minus-monotonic steps, so ordinary
Linux NTP slewing may be invisible and device drift is not measured. Its bounded
quintic map is useful for replay/diagnostics, not the implementation for indefinite
cumulative correction. Preserve its dedicated `triad_clock_follow`,
`triad_control_clock`, source-probe and diagnostic checks when changing it.
