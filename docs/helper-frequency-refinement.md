# Standalone Helper frequency refinement

See the [Helper integration guide](helper-integration.md) for current
integration constraints and the cross-study view of inexpensive approaches.
This page reports specific refinement algorithms and experiments, not
universal Robust receiver requirements.

This study compares inexpensive Helper-only tracking methods on the same
simulated captures. It does not integrate or test ordinary Robust reception.
The existing [Helper tracking study](helper-tracking.md) describes the prior
frequency-aided phase and relative-clock baseline.

Every full case observes the Helper for 36 seconds, retains a separate
36-second frequency result, and keeps the fixed 38-second lead before the
probe. The probe continues through 40 seconds. Later Helper samples can update
tracking and provide a fresh phase for a later hop; they do not change the
result labeled available at 36 seconds. A correction based on a complete dwell
uses approximately 0.4 seconds of buffering. Each hop has an independently
unknown common RF phase; the receiver does not predict that phase from the
previous hop.

The principal link budget uses 40 W total active-mean Helper waveform power and
an independently specified 40 W probe, subject to the same path loss. During
the probe interval this represents 80 W nominal combined power, not a 40 W
total transmitter limit. Temperature and noise figure are explicit inputs.
The principal channel is flat and assumes a bounded relationship between the
pilot transfer phases. It is a simulation, not evidence of actual transceiver,
antenna, propagation or regulatory performance.

The HF fixture uses a cryptographic-keystream-based randomized 50-channel
schedule with 50 kHz spacing around 30 MHz. The two microwave fixtures use
keyed random wide-hop FCC100 and EU20 geometries around 5.8 GHz. The test keys
are deterministic public fixture inputs. They permit paired reproducibility;
they do not certify regulatory compliance. Seeds separated by 256 reuse the
fixture key material and are not independent schedule keys.

## What contributes to the previous uncertainty

The previous receiver obtains a local frequency estimate for each pilot from
four sub-aperture phase measurements and fits a dated audio/RF clock model
across hops. Its local noise estimate uses an explicit power inflation, the
paired frequency fit permits additional uncertainty for unknown correlations,
and its reported global covariance includes a recent-history model-mismatch
allowance. These are distinct from the observed point error. The
recent-history allowance is a guard against model disagreement, so a small
point error on a few clean examples does not justify deleting it.

In an ideal, equally weighted linear offset-and-drift illustration, the
endpoint variance from the recent half of the observations is about twice the
full-history endpoint variance. The existing unconditional addition therefore
can materially enlarge the reported covariance even if the two fits agree.
Actual results also depend on the randomized RF geometry, two-pilot covariance,
oscillator process, measured noise and mismatch guards; this illustration is
not a calibration for the receiver.

Fresh pilot phase has its own thermal and oscillator contributions. A clock
model can reduce changing time-map error without reducing every individual
fresh-pilot phase error. Likewise, a low observed frequency error from later
probe windows is not an uncertainty bound on the frequency snapshot taken at
36 seconds. The study reports these quantities separately.

## Candidate estimators

The original four-sub-aperture slope remains available as the baseline.
Optional local methods refine frequency around the accumulated prediction
using centered, weighted whole-aperture moments and bounded local iterations.
The prediction chooses the coordinate and search neighborhood; it is not
counted as an independent measurement. The methods retain the short-aperture
residual and phase-evolution checks and request the existing broad Helper
acquisition when a prediction or fit is inconsistent. A whole-aperture Hann
window and a partitioned Hann window are evaluated separately. Their differing
endpoint weights mean a single whole-dwell Hann window is not automatically
more frequency-efficient than the existing slope.

The refined methods report a covariance for the actual taper using a weighted
sandwich calculation. Thermal covariance includes the pilot correlations and
the local noise allowance. The frequency effect of the configured common
oscillator phase diffusion uses a taper-specific discrete calculation and is
reported separately. Model or numerical allowances are not silently folded
into a claim of purely thermal precision. The covariance remains conditional
on the stated local tone and noise model; colored interference, leakage and
selection effects require their own evidence.

The joint estimator fits both within-dwell pilot frequencies and the evolution
of their simultaneous phase difference. It uses the supplied covariance among
those observations and retains a fitted, unknown phase-difference intercept.
The common random RF phase cancels in the simultaneous difference; it is not
assumed continuous between hops. The estimator keeps one reference, bounds
history and the number of ambiguity alternatives, and makes lack of a unique
branch or a consistent recent-history guard explicit. Its reported conditional
frequency interval encloses both whole-fit and recent-half intervals. Thermal,
common-process and bounded-model contributions are reported separately; the
result is an engineering interval conditional on the supplied model, not a
calibrated probability or a guarantee against arbitrary curvature.

The optional dual-pilot phase estimator combines the two fresh pilot phases in
the high-pilot gauge. It uses a prediction of the complete pilot phase
difference made from previous observations only, including the retained unknown
intercept. It does not add that prediction as independent current evidence.
The supplied thermal covariance, common process and bounded phase allowance
are kept distinct. If the blend is unavailable or fails its checks, the caller
can retain the valid high-pilot-only phase rather than discard the dwell.
Changing differential channel phase remains fundamentally confounded with
audio-clock evolution unless independent evidence bounds it.

## Evaluation and interpretation

The receiver sees only the generated Helper/probe mixture and the known
commanded RF schedule. Simulator truth never selects a branch, correction,
frequency or phase. Tone and shaped 64-APSK probe samples do not provide
synchronization or an adaptive alignment. For relative operation, the receiver
chooses and retains one coordinate reference; a single corresponding
simulator-clock gauge is used only by the evaluator. The clean probe twin
receives the same noisy-Helper-derived correction as the noisy probe. An
additional same-capture oracle is evaluator-only and is not fed back to any
receiver.

For each method, the full study separates the frozen 36-second frequency
estimate and its stated uncertainty from held-out fresh-hop phase and later
tracking updates. It reports qualified dwell and output support, per-dwell
frequency errors and stated sigmas, phase errors and tails, timing-map change,
cycle slips, clean-twin tone residuals and noisy tone measurements. For APSK,
it reports supported fixed-date symbols and noisy and clean-twin EVM. The
oracle is a diagnostic comparison, not an ordinary Robust decoder.

The guards cover a finite set of tested conditions, not arbitrary oscillator
or channel behavior. A retune can corrupt an aperture even if the later
240 ms evaluation window appears clean; sample-level supported-phase errors
and explicit support margins must be considered. A pilot-phase change outside
the declared differential allowance may be indistinguishable from clock change
at weak SNR. Ambiguity or contradictory evidence must remain visible, and a
frequency-model recovery must not silently reset the relative time reference.

Receiver process CPU includes all receiver threads and is reported through
36 seconds and through the stated 40-second capture. Acquisition, aided
tracking, fallback, clock/phase processing and actual sample conditioning are
separable. Generation, evaluator/oracle and metrology costs are separate from
the receiver total. Conditioning only the held-out probe interval does not
establish the cost of conditioning every sample of the 40-second stream.

## Matched HF tone results

The clean-channel sweep used the actual Helper waveform, a 40 W Helper and
independent 40 W tone probe, 290 K input noise reference and a 10 dB noise
figure. The modeled 30 MHz relative RF error was +0.8 ppm, the transmitter
and receiver audio errors were +37 and −13 ppm, and common phase diffusion
was 0.05°/√s. At 180 dB the high pilot has approximately 28.07 dB-Hz
carrier-to-noise density; the 40 W probe has approximately 4.43 dB nominal
SNR in 360 Hz. Each method processed the same generated capture for a given
seed. The 36-second estimates were frozen before later samples. These are
finite simulated samples under the flat, bounded-transfer model, not physical
sensitivity or calibrated coverage claims.

At 180 dB, the following uses 32 matched seeds. Frequency error RMS is across
the available 36-second point estimates. `Width` is the range of their stated
conditional half-widths; all 32 reported intervals covered the simulated
frequency in each listed method. Output phase is the clean-twin phase RMS
against each receiver's one fixed evaluator gauge, summarized across seeds.

| Method | 36 s point-error RMS | Conditional half-width range | Clean-twin phase RMS median / worst seed | Full held-out windows |
| --- | ---: | ---: | ---: | ---: |
| Existing four-slope + relative clock | 0.01658 Hz | ±0.20048–0.23574 Hz | 3.08° / 6.34° | 160/160 |
| Partitioned-Hann + relative clock | 0.01675 Hz | ±0.20745–0.25389 Hz | 3.18° / 6.33° | 160/160 |
| Partitioned-Hann + joint fit, high pilot phase | 0.01783 Hz | ±0.11354–0.15567 Hz | 3.18° / 6.33° | 158/160 |
| Partitioned-Hann + joint fit, dual pilot phase | 0.01783 Hz | ±0.11354–0.15567 Hz | 2.29° / 4.80° | 158/160 |

The refined joint fit narrows the reported interval in these samples, while
its point-error RMS is comparable to the baseline. **None of the 32 joint
intervals was within ±0.1 Hz at 180 dB.** The dual-pilot option changes the
fresh phase anchor, not the already-frozen frequency solution. Across these
samples its worst admitted clean-sample phase error was 7.81°, versus 11.51°
for the same joint fit using high-pilot phase only. The worst observed fresh
held-out pilot-phase errors were 7.71° and 11.59°, respectively. These are
sampled maxima, not bounds on future trials.

Across all measured dwell phases, the median of the per-run 90th and 99th
percentiles was 5.35° and 7.88° for high-only, versus 4.59° and 6.63° for
dual-pilot phase. The largest individual measured phase errors over all
dwells were 15.79° and 11.41°, respectively. These include acquisition as well
as held-out observations and are separate from output-sample errors.

The partitioned-Hann local frequency method by itself did not materially
improve 36-second point accuracy or its existing guarded interval in the
32-seed comparison. In the initial eight-seed screen, the whole-Hann method
had a 0.02413 Hz point-error RMS and a maximum ±0.29171 Hz guarded half-width,
versus 0.01709 Hz and ±0.22347 Hz for the four-slope baseline on those same
captures. The extra local refinement therefore did not establish a standalone
frequency benefit. In this experiment, partitioned refinement provided the
covariance used by the joint fit, so the incremental comparison is against the
partitioned method with and without that fit. A subsequent
[continuation](helper-continuation.md) evaluates an opt-in covariance for the
legacy slope.

For the dual-pilot joint method at 180 dB, median reported thermal frequency
sigma was 0.02579 Hz, common-process sigma was 0.000059 Hz, the bounded-model
half-width was 0.000125 Hz, and the recent-half enclosure contributed an
additional 0.04812 Hz to the median conditional interval. These components
depend on the stated covariance and channel model. The largest evaluator-only
time-map error after removal of the fixed gauge was 7.63 µs; no admitted cycle
slips were observed. Across runs, the change from the first to last admitted
timing anchor ranged from −1.28 to +1.56 µs. Supported sample counts varied
with guards, and the two missing full windows remain in the 158/160
denominator.

Across those same 32 seeds, the maximum per-seed clean-twin frequency RMS over
later supported tone windows was 0.03673 Hz. The median noisy-tone frequency
RMS was 0.09213 Hz, with median residual-fit standard error 0.10761 Hz. The
noisy fit is a measurement with substantial uncertainty; neither the noisy
nor clean later-window result retroactively narrows the 36-second interval.

The initial loss screen used eight matched seeds at each level. For the
partitioned-Hann joint/dual method, all eight conditional 36-second intervals
covered the simulated frequency at each level. The largest half-widths were
±0.13482 Hz at 180 dB, ±0.07881 Hz at 175 dB, ±0.04757 Hz at 170 dB,
±0.03358 Hz at 165 dB and ±0.02778 Hz at 160 dB. These small clean-channel
samples identify conditions worth further testing; they do not establish a
sharp or physical transition.

An eight-seed refinement at each intermediate loss gave joint/dual
conditional half-width ranges of ±0.07524–0.08753 Hz at 176 dB,
±0.08374–0.09730 Hz at 177 dB, ±0.09324–0.10832 Hz at 178 dB, and
±0.10389–0.12074 Hz at 179 dB. All reported intervals covered the simulated
frequency and all 40 held-out tone windows were supported at each loss.
For these seeds and this model, the range crosses ±0.1 Hz between 177 and
178 dB; it is not a calibrated threshold or physical sensitivity limit.

At 5.8 GHz and 180 dB, eight-seed checks used each wide-hop fixture. The
FCC100 fixture had seven qualified and covered 36-second intervals; EU20 had
eight. The largest qualified half-widths were ±0.14451 and ±0.14834 Hz,
respectively. Both sets supported all 40 later tone windows. A later phase
measurement or tracking update does not make the missing FCC100 36-second
interval available.

The shaped 64-APSK comparison used the same 32 matched HF seeds at 180 dB,
with the same independent 40 W Helper and 40 W probe powers. The joint fit
with high-only phase had noisy EVM median/worst seed 0.5516/0.5919 and
clean-twin EVM 0.0560/0.1094. With the dual-pilot phase blend, the
corresponding values were 0.5493/0.5908 noisy and 0.0417/0.0860 clean.
The matched evaluator-only oracle's noisy EVM median/worst seed was
0.5497/0.5882. Each joint variant retained 158 of 160 held-out anchors;
individual runs contributed 265–332 supported symbols.

The clean twin shows the phase-tracking difference more clearly than noisy
EVM in these samples, where the channel noise is substantial. These are
fixed-reference waveform measurements, not decoding results. The probe never
selects a timing or phase reference, and the oracle never feeds the receiver.

The additional eight-seed HF APSK screen used the same joint/dual method and
matched powers. All eight 36-second intervals covered the simulated frequency
and all 40 later anchors were supported at each loss; each run supplied 331
supported symbols. The table separates noisy measurements from the clean twin.

| Loss | Noisy EVM median / worst seed | Clean-twin EVM median / worst seed |
| ---: | ---: | ---: |
| 175 dB | 0.3104 / 0.3322 | 0.0236 / 0.0408 |
| 170 dB | 0.1755 / 0.1872 | 0.0211 / 0.0327 |
| 165 dB | 0.0989 / 0.1054 | 0.0146 / 0.0204 |
| 160 dB | 0.0563 / 0.0598 | 0.0120 / 0.0145 |

On the local Ryzen 5 PRO 5650U Release build, the 32 dual-pilot joint cases
used a median 0.159 s and maximum 0.182 s process CPU through 36 seconds.
Through 40 seconds, including approximately 2.4 seconds of probe
conditioning, the median and maximum were 0.206 and 0.230 s. Median
acquisition, local correlations, clock/joint/dual processing and conditioning
costs through 40 seconds were approximately 0.021, 0.143, 0.0031 and 0.029 s,
respectively; other receiver work and fallback are included in the total.
Median simulation generation and separate tone metrology were 0.336 and
0.0031 s and are excluded. Results depend on the host, compiler, signal and
workload.

## Recovery and model limits

In the four seeded 180 dB mismatch checks, the 1.2 Hz step produced valid
36-second snapshots before the step but the joint path withheld all 20 later
windows; useful joint recovery was not demonstrated before 40 seconds. The
curvature cases did not qualify a 36-second joint snapshot. A two-dwell gap
retained all 20 later windows in those trials, while a longer gap left the
joint result unavailable through the probe. The original reference was not
silently reselected. With the modeled 120 ms retune disturbance inside the
otherwise retained support, 13 of 20 held-out windows remained supported;
the affected hop was withheld in these cases. The largest admitted clean
sample phase error was 7.29°. This is not a guarantee that arbitrary settling
disturbances will be detected.

An early development version falsely admitted a post-step window with a large
sample-level phase error after frequency reacquisition. The later version
withdraws joint authority on the contradictory evidence rather than treating
a reset frequency fit as a validated unchanged clock model. This observed
failure and the withheld recovery are both material limitations; the finite
tests do not guarantee detection of arbitrary model changes.

In the four 180 dB out-of-bound differential-phase trials, the ±0.8 rad
variation was not reliably detected and all 20 held-out windows remained
supported; the worst admitted clean sample had 17.33° phase error. At 160 dB
the sampled same disturbance caused the joint path to withhold all 20
windows. These observations do not prove the assumed differential-channel
bound in either case. A bounded 0.02 rad variation retained 20/20 windows in
the sampled 180 and 160 dB cases. Noise-only trials did not produce a qualified
joint result.

Two additional four-seed 180 dB controls covered clock evolution. With
transmitter audio drift of +0.04 ppm/s and relative RF drift of +0.00002
ppm/s, all four 36-second intervals covered truth and all 20 later windows
were supported; the largest conditional half-width was ±0.12659 Hz. Reversing
the audio errors to −37 ppm transmitter and +13 ppm receiver also produced
four covered intervals, with 19 of 20 later windows supported and a largest
half-width of ±0.12473 Hz. These are bounded examples, not arbitrary clock
coverage.

An independently powered 4 kW probe was also tested with the 40 W Helper,
using two seeds each at 180 and 160 dB. All five later joint windows were
supported in each case; worst admitted clean-sample phase errors were 7.07°
and 1.11°, respectively. The probe was active only during 38–40 seconds, so
it could not affect the 36-second frozen estimate. During that interval the
specified Helper and probe powers sum to 4.04 kW; this is a bright metrology
case, not a 40 W total-transmitter test.

The single whole-Hann local refinement did not outperform the baseline in the
initial matched frequency screen. The partitioned local refinement alone did
not yield a clear accuracy or interval advantage, while costing more receiver
CPU. In this study the joint-fit comparison used it to obtain covariance;
the later [continuation](helper-continuation.md) evaluates covariance for the
legacy slope. A separate narrowband filter-and-decimate receiver
was not implemented or benchmarked in this study. Later-ramp timing was not
extended here; the preceding [ramp feasibility study](helper-tracking.md#later-ramp-feasibility)
does not establish absolute timing qualification. Neither omission establishes
that those approaches could not help in other conditions.

## Reproduction

Build the existing development profile, then run the bounded feasibility
check before full cases:

```sh
./build.sh
cmake --build build/dev --target test_helper_refinement_study test_helper_joint_estimator test_helper_dual_pilot_phase test_helper_aided_phase test_helper_relative_clock test_helper_tracking --parallel 2
build/dev/test_helper_refinement_study --probe
build/dev/test_helper_refinement_study --case 180 901 hf clean tone 40 all
build/dev/test_helper_refinement_study --case 180 901 hf clean apsk 40 all
build/dev/test_helper_refinement_study --case 180 901 microwave clean tone 40 all
build/dev/test_helper_refinement_study --case 180 901 microwave-eu clean tone 40 all
build/dev/test_helper_refinement_study --robustness
ctest --test-dir build/dev --output-on-failure -R '^helper_(refinement_study|refinement_robustness|joint_estimator|dual_pilot_phase|aided_phase|relative_clock|tracking|tracking_robustness)$'
```

The case arguments select loss in dB, deterministic seed, RF fixture,
impairment, probe waveform, probe power in watts, and method set. The case
modes include clean, short and long gaps, frequency step and curvature,
differential pilot-phase change, retune disturbances and noise-only input.
The executable's argument validation is authoritative for supported spellings.
Use the same seeds and channel inputs to compare methods; do not treat repeated
keys or qualified-only subsets as independent performance trials.

For the matched 32-seed HF tone comparison, the `finalists` method set runs
the baseline, partitioned-only, partitioned joint and joint/dual paths on each
capture:

```sh
for seed in $(seq 901 932); do
  build/dev/test_helper_refinement_study --case 180 "$seed" hf clean tone 40 finalists || break
done
```

For the eight-seed initial loss screen or the intermediate refinement, use
the same command with seeds 901–908 and the loss values listed in the tables;
select `all` instead of `finalists` to include the whole-Hann alternatives.
Replace `tone` with `apsk` to reproduce the matched constellation study.
The development build and 14 focused checks passed for this study. The full
ordinary Robust regression campaign was not run.
