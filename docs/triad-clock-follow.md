# Continuous full-audio clock discipline

The isolated continuous triad adapter **follows system-clock updates by default
for live transmission and reception**. Its `auto` policy selects best-effort
discipline of complete audio PCM on dated native audio providers, and `off` for
deterministic simulation. The native PCM tool likewise defaults to continuous
discipline for play/capture and off for device-free copy. Repeated UTC corrections
and continuing device/system-clock drift have no lifetime ±2-second displacement
limit. Explicit `off` remains available. The older
`bounded-data` source-only experiment remains available for historical replay;
its narrower scope is described below.

The [controller, sampled-receiver and cable evidence](validation-data/triad-v3/2026-11-06-continuous-clock/README.md)
records historical 50-ppm results and fixed-template failures. The current
200-ppm policy adds exact local capture coordinates and a bounded remote timing
filter, described below. Historical scores do not qualify this different receiver
or imply unchanged sensitivity.
The [200-ppm tracking qualification](validation-data/triad-v3/2026-11-06-clock-tracking/README.md)
retains direct/cache comparisons, independent sampled corrections, the default
mapped cable check and failures encountered during implementation.
The subsequent [multi-day weak-signal study](triad-multiday-tracking.md) exposed
the sensitivity cost of renewing the channel prior at every timing update. The
[current gentle-tracking study](validation-data/triad-v3/2026-11-06-gentle-tracking/README.md)
addresses that defect with persistent channel state and a fixed equal mixture
of nominal and adaptive timing branches. The 200-ppm phase catch-up controller
is unchanged. Low-IF results do not qualify arbitrary multi-day audio-IF
trajectories or physical noise admissions.

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
| Maximum UTC phase catch-up | 200 ppm | Gentle correction of clock steps, separate from actual device-rate calibration |
| Maximum rate slew | 10 ppm/s | Continuous change of converter rate |
| Phase time constant | 1000 s | Gradual removal of phase backlog after uncertainty is allowed for |
| Rate-estimation window | 60 s | Bounded-memory device-frame/monotonic slope estimate |
| Maximum rate-estimation window | 3600 s | Extend uncertain but plausible fits; report rejection if still uninformative |
| Maximum date extrapolation | 2 s | Longer gaps hold the last applied rate |

New small corrections update the current error immediately; they need not wait
for an earlier correction to settle. The cumulative offset is unbounded within
checked numeric/counter limits, but **the correction rate is bounded**. A
2-second error needs at least 10,000 seconds at the default 200-ppm phase cap,
before allowance for slew, uncertainty and gradual convergence. The synthetic
isolated-step check reaches below 1 ms in 14,409 seconds (about four hours).
The 38-day controller test with recurring 100-ms/hour and 10-ms/15-minute
corrections plus ±2-second events leaves at most 5.834 ms immediately before
the hourly update. These are injected-clock controller results, not measured
hardware accuracy or a guarantee for faster corrections.
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
unsupported Windows/browser providers reject it. On those providers the legacy
isolated capture path requires both `--follow-system-clock off` and
`--receiver fixed`; `off` alone does not supply the exact capture map needed by
tracking. Shared `audio::Options` defaults and ordinary Robust behavior are
unchanged. Timestamp quality remains
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
The adaptive continuous physical receiver withholds operational admission;
this mode alone does not relax that rule or certify its false-admission rate.
The optional [physical noise-profile receiver](triad-physical-admission.md)
instead scores original real ADC samples before ASRC, using a fixed public
likelihood bank and a declared global noise bound. Clock discipline and the
post-ASRC stream continue running; their colored output is not substituted for
the original ADC observations. Its admission is conditional on its matching
profile and is not a qualification of the adaptive timing score. General blind multi-day
acquisition, trajectories outside the declared dynamics and physical noise covariance need
separate qualification. No claim of unchanged sensitivity follows from controller
stability alone.

## Receiver timing inference

The live adapter's default timing receiver handles two distinct effects:

* **Known local resampling:** a bounded `mapped-v1` pipe packet carries each
  output sample's actual native ADC coordinate alongside its PCM. The map comes
  from the final resampler's source-position hook, not periodic clock reports.
  Mixing at those original ADC coordinates and evaluating templates there
  removes the artificial phase error caused by local RX steering. This does not
  invert the interpolation filter or whiten its noise.
* **Unknown remote movement:** a sparse forward filter carries public source
  delay/rate hypotheses across segments. It accumulates complex three-carrier
  channel likelihoods before combining timing paths, without restarting the
  keyed epoch or accumulated bit evidence. No private transmitter correction
  trace is supplied. State coordinates can move beyond the initial timing bank
  within checked counters; retained state count remains bounded.

The initial acquisition bank is still public and required. This is a
**scheduled post-acquisition** receiver: initial residual delay/rate indices
default to zero relative to an already appropriate public acquisition bank.
Positive delay means that the remote source is ahead; it is not a positive
arrival delay. No branch receives the private transmitter clock history.

The default `tracking` policy is a fixed **equal mixture** of two persistent
receivers. A nominal branch retains the declared initial affine hypotheses and
their channel evidence. An adaptive branch evolves sparse timing states while
transporting their channel posteriors. Both score the same observation; their
likelihoods are averaged with weights fixed before observing data, rather than
selecting whichever result passes. This preserves a useful simple path when a
slow waveform already tolerates the remaining clock motion. The mixture pays
at most `log(2)` in log evidence relative to either branch considered alone;
that is not a universal energy or sensitivity penalty.

`--receiver tracking-persistent` exposes only the adaptive persistent branch.
`--receiver tracking-reset` keeps the earlier independent-segment channel-prior
reference, and `--receiver fixed` keeps the fixed-bank incremental reference.
These choices make the coherence tradeoff measurable. None changes physical
admission qualification or the source clock controller.

Optional `tracking` settings in the public bank JSON include `segment_seconds`,
`delay_step_seconds`, `rate_step_ppm`, `maximum_rate_ppm`,
`maximum_acceleration_ppm_per_second`, `maximum_states`, `maximum_proposals`,
`initial_delay_indices`, `initial_rate_indices` and `model_probabilities`.
The delay-cell default is `1/(4 * nominal_triplet_bandwidth_hz)`; the default
extent is ±2000 ppm with 20-ppm/s acceleration, 64 retained states and 256
proposals. The audio/high-bandwidth cadence remains four seconds with 50-ppm
rate cells. For both DATA IF and triplet bandwidth below 1 Hz, automatic cadence
can increase up to 4096 seconds, constrained by envelope displacement, phase
curvature, rate-cell phase mismatch and bounded segment storage. In that slow
regime the automatic rate grid may coarsen up to 200 ppm. Explicit segment/grid
choices remain explicit; they are not silently rewritten.

These are cheap geometric design budgets, not measured sensitivity guarantees.
For a rectangular coherent interval of duration T, a residual frequency error
df retains `sinc(df*T)^2` of matched-filter energy. Half a rate cell contributes
`df = (abs(IF) + bandwidth/2) * rate_step_ppm * 1e-6 / 2` at the outer nominal
edge. The automatic low-IF grid limits this contribution to one eighth of a
cycle per segment, approximately a 0.22-dB rectangular proxy. Actual pulse tails,
channel evolution and state pruning still require waveform checks. The adapter
rejects grids whose stated acceleration cannot reach an adjacent rate cell in
one full segment, and rejects unsupported segment/workspace sizes.

Transitions advance source delay using the trapezoidal old/new rate and split
fractional delay cells. The persistent receiver carries proper complex Gaussian
channel means and covariances across timing updates, including between-history
uncertainty when histories merge. Its carrier phase follows the integrated
public rate path; rounding an envelope delay cell must not introduce a carrier
phase jump. Conditional moment matching at a history merge is an
**interacting-model approximation**, not the exact unmerged switching-channel
likelihood. Fixed bit/coherence priors are retained throughout the observation.

Each predictive Gaussian density remains normalized conditional on the past;
pruning only deletes weighted mass and never renormalizes likelihood. Under the
declared Gaussian null this supports the model-conditional e-value argument,
including the fixed nominal/adaptive mixture. Unlike the reset reference, the
moment-matched score is not a pointwise lower bound on an exact unmerged path
likelihood. These facts do not establish physical audio calibration. Long fades,
a poor initial bank or an unsuitable grid can discard a useful adaptive path;
dropped-mass diagnostics are not future miss-probability bounds. The nominal
branch protects only the declared affine alternatives, not arbitrary remote
motion. The recursion builds on the standard
[hidden Markov model likelihood construction](https://www.cs.cmu.edu/~durand/03-711/Readings/Rabiner89.pdf).

The default template path caches uniform keyed DATA by bit, epoch and bounded
tile, then applies candidate time maps and the complete audio-center phase.
Four-point interpolation avoids synthesizing the same encrypted pattern for
each state. Oversampling is selected from rate and FHSS ramp metadata, with a
maximum dense factor and explicit direct-native fallback. The spectral proxy is
not a strict finite-pulse error bound. Actual native comparisons cover Rate36×10,
FHSS and Burst boundaries, EME360×1 FHSS and Rate24000 at 96 kHz. Maximum relative
RMS template error in these checks is 1.67e−5. `--tracking-templates direct` keeps
the reference synthesis available. Receipts include cache size, work counts,
oversampling and fallback reasons; do not transfer the measured speedup to a
fallback or a different memory regime without measurement.
The receiver precision selection applies to its likelihood computation;
native/template preparation and phase-coordinate reductions retain their
higher precision. Their CPU cost is included in the reported complete timings.

For a DFT projection, timing reception discards each complete likelihood block
that intersects missing samples. It reports caller-requested invalid rows,
extra discarded rows and expanded blocks separately. A contiguous outage can
lose valid samples in at most its two edge blocks; terminal blocks may include
a merged short remainder. Fragmented loss can erase many blocks. Fully erased
segments supply unit likelihood and still advance timing. Raw-row projection
retains arbitrary valid rows without expansion. The older fixed receiver keeps
its explicit partial-DFT rejection; no colored-noise covariance adapter is
claimed by this conservative policy.

Helper is steered with DATA and its leakage remains in the declared projection.
This change does not add a multi-day two-pilot tracker. A lead-in frequency
estimate cannot certify days of stability. The bounded tracker improves
supported dynamics; it does not guarantee arbitrary trajectories, indefinite
outages or −60-dB-Hz sensitivity.

## Use and inexpensive qualification

Live TX/RX uses this policy by default. `--follow-system-clock off` replays the
old audio policy. `--receiver fixed` retains fixed-bank reception; `auto` selects
the persistent nominal/adaptive mixture for physical capture and fixed reception
for simulation. Select `--receiver tracking` explicitly for a paired simulation
of the practical mixture. Existing
key, epoch, power and public receiver-bank requirements remain. Simulation rejects
explicit continuous hardware discipline; use injected timestamps in tests instead.

At the native PCM interface, select:

```text
triad_audio_stream --mode play|capture --frames N --sample-rate HZ --device DEVICE
```

The native and triad adapter options `--clock-max-ppm`, `--clock-phase-max-ppm`,
`--clock-slew-ppm-per-second` and `--clock-phase-seconds` adjust the controller.
Copy defaults to off and rejects explicit continuous discipline. Native capture
remains raw PCM by default; the Python tracking adapter requests `mapped-v1`.
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
Tracking completion is reported as `completed_tracking_physical_null_unqualified`;
the fixed-bank clock-following diagnostic retains
`completed_clock_tracking_unqualified`. Admission remains withheld. Undeclared
hardware/path timing is not covered.

Use cheap injected-clock and actual-resampler checks before live testing:

```sh
./build.sh configure --cli
CCACHE_DISABLE=1 cmake --build build/dev-cli --parallel 2 --target \
  test_continuous_clock test_alsa_audio triad_audio_stream test_triad_mapped_basis triad_helper_export
ctest --test-dir build/dev-cli --output-on-failure \
  -R '^(continuous_clock|alsa_continuous_clock|triad_audio_stream|triad_mapped_basis|triad_stream_source)$'
# Use the study Python environment with NumPy/SciPy/Torch installed.
OPENBLAS_NUM_THREADS=1 python tests/test_triad_persistent_tracking.py
OPENBLAS_NUM_THREADS=1 python tests/test_triad_tracking_budget.py
OPENBLAS_NUM_THREADS=1 python tests/test_triad_timing_forward.py
OPENBLAS_NUM_THREADS=1 python tests/test_triad_audio_mapping.py
OPENBLAS_NUM_THREADS=1 python tests/test_triad_continuous.py
# Four-second native waveform/cache check, only when this stage changes:
OPENBLAS_NUM_THREADS=1 python tests/test_triad_tracking_basis.py build/dev-cli/triad_helper_export
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
