# Helper Carrier integration: start here

This is the current starting point for agents implementing or integrating Helper
Carrier. It brings together the inexpensive **implemented and tested standalone
techniques**. Read this before treating the older Live receiver's architecture
or qualification history as a design requirement. The waveform contract is in
[Helper Carrier](helper-carrier.md); detailed experiments and reproducible
results are in [the continuation study](helper-continuation.md). The
[repository evidence package](validation-data/helper/README.md) preserves
selected per-case results, failure cases, controls, fixture keys and provenance
without depending on ignored local logs or a temporary executable.

The objective is useful frequency narrowing and a complete, corrected,
timestamped baseband stream. No particular FFT, filter, tracker, estimator,
receiver class or ordinary Robust search backend is required. The demonstrated
composition below is a starting point, not an exclusive design. Its standalone
success does **not** establish ordinary Robust interoperability, physical radio
retuning, hardware throughput or successful Robust decoding.

Useful assistance can be frequency-only, relative clock/phase correction, or
the complete buffered combination. Offer the information actually supported
and retain ordinary search/recovery for unresolved quantities; do not require
every optional capability to qualify before exposing any useful measurement.

## Transmission facts needed by a receiver

The [waveform specification](helper-carrier.md#dwell-waveform-and-metadata) is
authoritative; this is a navigation aid, not a separate wire format.

| Feature | Receiver consequence |
| --- | --- |
| 400 ms physical dwells, with or without FHSS; fixed 38 s lead including 36 s observation | Keep the capture calendar and the frozen observation frontier separate from delayed output release. |
| Low pilot 550 Hz at −3 dB, auxiliary 950 Hz at −9 dB, relative to high-pilot steady-tone power; high pilot at least 2400 Hz, raised for the configured payload guard | Use the configured actual frequencies and power normalization. Both straight pilots supply frequency/clock evidence; the auxiliary is a distinct component. |
| Auxiliary-only shaped phase inversion at 160 ms, tapering through zero over 152–168 ms | Timing can be measured without payload decoding or pilot ramps. Low/high pilots never perform this inversion. Fit the shaped waveform, not an instantaneous phase step. |
| Pilot boundary ramps for FHSS; continuous pilot phase/amplitude at internal boundaries with FHSS disabled | No-FHSS coherent techniques have a narrower operating model. Random RF hop phase must be re-estimated on every hop. Auxiliary gating/reset remains present in either mode. |
| Auxiliary APSK-16384, 50 baud, nine symbols at 212–392 ms: 14-bit modulo-time phase plus 112 callsign bits | This optional metadata is separate from inversion timing and from the standalone 360 Hz shaped test probe. Decoding it is not required for frequency/phase correction. |
| Modulo-time record, optional station-key encryption, no authentication tag | Do not interpret decoded metadata as absolute time, GPS qualification or proof of shared transmitter identity; follow the full format and capture-time requirements if using it. |

The transmitter implementation is
[`helper_carrier.hpp`](../include/datapump/helper_carrier.hpp) and its matching
source; the waveform, encrypted metadata and physical completion rules remain
unchanged by the experimental receiver paths.

## Recommended starting composition

Reuse the actual-chain assembly in
[`test_helper_complete_study.cpp`](../tests/test_helper_complete_study.cpp),
especially `CompleteHelperHistory` and `CompleteProfileHistory`, rather than
reconstructing a receiver from an early diagnostic alone. Reusable components
live in `include/datapump/` and `src/`; some experimental libraries are built
only for the standalone targets. The fixture's channel truth and clean probes
are evaluator inputs, **not** part of the receiver to port.

| Capability | Implemented choices and source entry points | How to use the evidence |
| --- | --- | --- |
| Frequency acquisition with a declared oscillator/capture range | [`HelperMeasurement`, `HelperPooledMeasurement`](../include/datapump/helper_measurement.hpp) | Broad acquisition is available for startup/recovery. The pooled implementation is also a cheap option when a sufficiently narrow, independently justified prior exists. Its work/range caps are implementation limits, not a requirement that every receiver use that search. |
| Cheap tracking and fresh phase on each hop | [`estimate_helper_aided_phase`](../include/datapump/helper_aided_phase.hpp), optional [`estimate_helper_joint_dwell`](../include/datapump/helper_joint_dwell.hpp) | Derotate using accumulated frequency information, then measure the current dwell. Local refinements, guarded correlations and cached tapers avoid repeating full acquisition. The complete fixture uses aided slope/covariance gating plus the bounded current-dwell joint likelihood, with fallback. The refined observation replaces the gated one; do not ingest both as independent data. |
| Relative audio clock, RF frequency and drift | [`HelperJointEstimator`](../include/datapump/helper_joint_estimator.hpp), [`HelperRelativeClock`](../include/datapump/helper_relative_clock.hpp) | Combine within-dwell frequency and across-dwell pilot-phase difference while retaining one unknown constant reference. The joint estimator includes shared-observation covariance and separate stochastic/model terms; the phase-difference-only estimator remains an alternate. Two pilots identify relative link quantities, not every physical oscillator or Doppler separately. |
| Improved fresh-hop phase and bounded recovery | [`HelperDualPilotPhase`](../include/datapump/helper_dual_pilot_phase.hpp), [`HelperFrequencyRecovery`](../include/datapump/helper_frequency_recovery.hpp) | Both pilots can contribute to the current hop's phase using a previous-only differential-phase prediction. Recovery can establish a frequency-step coordinate and replay original observations once. Neither a prior nor a recovered/conditioned result is an extra independent observation. These are selectable capabilities, not prerequisites for all useful tracking. |
| Delayed correction of complete captured dwells | [`HelperCompleteConditioner`](../include/datapump/helper_complete_conditioner.hpp), [`HelperDelayedBuffer`](../include/datapump/helper_delayed_buffer.hpp), [`HelperDelayedSmoother`](../include/datapump/helper_delayed_smoother.hpp) | Hold whole dwells (two seconds by default), obtain their own fresh phases and available clock/frequency information, and correct back to the beginning and forward to the end. A later measurement can improve still-buffered samples; it cannot rewrite an earlier published estimate. |
| Auxiliary inversion timing without payload decoding or pilot ramps | [`HelperInversionTiming`](../include/datapump/helper_inversion_timing.hpp), [`HelperInversionProfile`](../include/datapump/helper_inversion_profile.hpp) | Strict individual-marker detection and sensitive pooled calendar timing are separate options. The pooled detector fits independent complex gains per hop and combines weak data likelihoods under the retained clock; it does not require every dwell to detect independently. Use its conditional interval and actual observation dates. |
| Special phase-continuous operation | [`HelperCoherentFrequency`](../include/datapump/helper_coherent_frequency.hpp) | A separate no-FHSS option when continuity is actually justified. Never transplant its coherent gain to randomly phased FHSS. Random-FHSS pooling instead keeps each dwell's common RF phase independent. |

The current complete-stream command keeps `nominal` as its baseline boundary
mode. `pooled-inversion` selects the sensitive marker-based boundary adjustment;
`auxiliary` selects strict individual markers. The `tracked` ramp experiment
remains available for comparison, not a requirement for integration. At weak
SNR use the known schedule with stated uncertainty until useful marker evidence
arrives; lack of a marker does not erase useful pilot correction or input data.

## What the latest measurements establish

The [fresh 180 dB comparison](helper-continuation.md#fresh-180-db-holdouts-and-complete-output)
uses a standard 40 W Helper, 290 K, 10 dB noise figure, independent RF/audio
errors and phase noise, and cryptographic-keystream HF hops over 50 channels
at 50 kHz spacing around 30 MHz. A separate 40 W tone or 360 Hz shaped APSK
probe means **80 W combined** during simultaneous transmission. Probes never
supply receiver synchronization, alignment, branch selection or corrections.

| Demonstrated capability | Measured result and qualification scope |
| --- | --- |
| Frequency narrowing available at exactly 36 s | In 32 fresh cases, 31 estimates were available and all 31 conditional intervals covered: RMS error 0.01357 Hz, median half-width ±0.11695 Hz. The marker-timing method does not change that frequency result. Approximately ±0.1 Hz is a useful scale, not a hard delivery gate or a proved universal bound. |
| Fresh local hop phase | RMS 2.625°, worst 12.713° across 3,200 dwells. This is distinct from transporting corrections over all samples of the dwell. |
| Weak auxiliary timing | All 32 calendar estimates qualified at 36 s: RMS error 0.441 ms, worst 0.965 ms, median conditional half-width ±2.050 ms. No individual marker qualified at 180 dB. First calendar qualification took 6.4–19.6 s; pooled timing is not an independent measurement of every hop. |
| Complete-stream correction | Pooled timing reduced wrong-hop outputs 166,929→76,553 and all-output clean-twin phase RMS 8.843°→6.859°. Both arms applied phase/clock corrections to all 30,721,545 emitted samples. Conservative support labels were unchanged and did not mask output. Worst errors near boundaries remained about 180°. |
| Timestamp preservation | Clean, missing-input and system-clock-adjustment tests preserve capture coordinates, UTC/monotonic metadata, ordering and the retained reference. The missing-ADC case represents 9,600 lost samples explicitly. Hardware timestamp accuracy and the actual Robust consumer still need validation. |
| Cost | Tracking/history plus inversion used 0.336–0.374 process CPU seconds for exactly 36 s, including all threads. Complete 40 s conditioning plus drain brought median receiver CPU to 0.703 s; generation/metrology are separate. The inversion detector adds 267,408 bytes of fixed storage. These are local measurements, not a hardware guarantee. |

The [loss screen and controls](helper-continuation.md#sensitive-pooled-inversion-timing-experimental)
also cover 175/170/165/160 dB, negative controls, transients, different delays
and the existing wide-hop microwave fixtures. **Measured error and reported
uncertainty are different quantities.** Keep the limited seed counts and
conditional model visible when reusing these numbers.
The [retained case data](validation-data/helper/2026-10-31/README.md) includes
the unsuccessful cases and supports recomputing the comparisons with
`python3 tools/helper-evidence.py summary`. Verification needs no simulator run.

Other inexpensive choices have separate evidence. The
[pilot-measurement study](helper-measurement.md) recorded roughly 0.407–0.420 s
median CPU per 36 s for its randomized HF receiver and 0.033 s median with a
narrow GPSDO prior. Those timings exclude conditioning and come from different
receivers/assumptions; they are alternatives to investigate, not additive savings
or directly matched comparisons with the table above. The
[EME study](helper-eme-measurement.md) explores 1.5/15 kW near 220 dB under a
tightly calibrated audio-clock and removed-Doppler assumption. It is not a
requirement that the general Helper receiver have that clock discipline.

## Requirements versus implementation choices

Preserve the [transmitted waveform](helper-carrier.md#dwell-waveform-and-metadata),
the full 36-second observation requirement and fixed 38-second transmit lead,
the quiet-spectrum operating assumption, and the ordinary wire/progress/physical
completion contract in [development](development.md). For receiver design:

- Keep acquisition/recovery coverage appropriate to the declared prior and
  actual capture bandwidth. Broad FFT acquisition, pooled likelihoods,
  narrowband mixing, filtering/decimation, guarded tapers and local correlations
  are choices to assess on measured accuracy and cost, not mandatory stages
  or prohibited alternatives. Filtering alone creates no new information.
- Estimate a fresh common RF phase for each random hop. Joint clock/frequency
  history, pilot-phase differences and independent per-dwell likelihoods may
  span hops; coherent averaging of unknown RF hop phases may not.
- Preserve one relative timing/phase reference through gaps. Absolute startup
  timing, auxiliary decoding, microsecond reversal detection, pilot ramps and
  an exact ±0.1 Hz interval are **not prerequisites** for useful relative
  correction. Keep unresolved alternatives and conditional uncertainty explicit.
- Apply the best available correction to **all captured samples**, including
  outside the pilot-estimation aperture. A support mask describes confidence;
  it does not authorize dropping, blanking, fading or zero-filling uncertain
  data. Represent actual missing input as a discontinuity. Separate physical
  disturbances from estimator error and do not label an unsupported model as
  guaranteed correction.
- Account for actual filter/interpolation dependencies and uncertain boundaries.
  Delayed measurements can be used retrospectively within the buffer under the
  same-hop model. Frequency evidence alone cannot invent a fresh hop phase.
  The old 400 ms raw-stage limit, 0.5 s freshness rule, frozen tuning parameters,
  one-bank layout and full instantaneous-frequency hull are **not universal
  Helper receiver requirements**. Bound memory/work and validate the chosen
  replacement's behavior instead of inheriting those implementation limits.
- Carry original capture sample index/time, UTC/system-clock epoch and
  uncertainty, release time, relative source-time mapping/reference and latest
  Helper observation used. Use capture time for schedule/pattern lookup.
  Resampling may change sample count but must preserve that mapping and real
  gaps. Results labeled available at 36 s must use no later observations;
  delayed corrections using later evidence must say so.
- Preserve correlations and separate thermal, process and bounded-model
  uncertainty. Retain/replay original observations without counting reused
  data, predictions or conditioned outputs as independent evidence. Use
  matched captures, complete-output metrics and focused regression checks.

The existing complete conditioner uses per-input mixing followed by linear
interpolation in narrowband fixtures. It is not yet a general production
bandlimited resampler. Validate the eventual capture conversion, probe/data
bandwidth, actual receiver consumer, latency and timestamp conventions during
integration; do not carry simulation-only knowledge into that adapter.

## Remaining limits are specific, not exclusions of entire approaches

Auxiliary marker timing identifies the transmitted calendar, not arbitrary
independent RF-retune hardware delay. In the tested 2 ms early-retune case,
using the marker boundary **worsened** wrong-hop correction. Correct timing also
does not repair every phase pulse, settling disturbance, clock contradiction,
frequency step or curvature error. Arbitrary differential channel phase can
imitate clock changes; whole-cycle timing jumps remain counterexamples.

Earlier soft-joint/soft-rate, transient, taper, ramp and timing-signature
experiments retain their individual results in the continuation study. A failed
implementation, a diagnostic-only fit or an unexercised fallback does not reject
an entire technique. Nor do the recorded results establish diminishing returns,
a physical sensitivity limit or required expensive receiver architecture.

The [older product-integration record](helper-carrier-plan.md) and
[validation history](validation.md) retain genuine passes, failures and
source-specific limits. They must not be read as saying the newer standalone
receiver has not been implemented, or that its cheap alternatives are forbidden.
Conversely, standalone success does not clear those product-integration failures.

## Focused entry points and next integration step

Start by replaying the complete standalone path and comparing the baseline:

```sh
./build.sh configure
cmake --build build/dev --parallel 2 --target test_helper_complete_study \
  test_helper_inversion_profile test_helper_complete_conditioner
ctest --test-dir build/dev --output-on-failure --parallel 1 \
  -R '^(helper_inversion_profile|helper_complete_conditioner|helper_complete_profile_regression|helper_complete_clock_regression)$'
build/dev/test_helper_complete_study full 180 1930100001 clean tone 2 hf nominal
build/dev/test_helper_complete_study full 180 1930100001 clean tone 2 hf pooled-inversion
```

[Detailed reproduction](helper-continuation.md#work-bounds-checks-and-reproduction)
includes the loss/seed matrix, other delays, APSK, microwave geometry, controls
and CPU partitions. The registered Python regressions require Python at
configuration time. Earlier pilot, frequency-refinement, tracking and EME
studies each retain their own commands and assumptions.
For an exact recorded case, the [evidence utility](../tools/helper-evidence.py)
also replays its argument list into a new output directory and records the
current binary hash. Historical source binding is partial and documented;
newly replayed results must not be presented as the original campaign binary.

For actual integration, connect the real capture/retune metadata to the dated
measurement and correction interfaces, then connect the resulting complete
timestamped stream and dated frequency interval to the intended Robust consumer.
Keep baseline comparisons and run the applicable compatibility/regression stages
when that work is authorized. Do not make standalone algorithm development wait
for an ordinary Robust full regression campaign, and do not call integration
successful until the actual consumer has been tested.
