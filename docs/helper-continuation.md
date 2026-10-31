# Standalone Helper covariance and recovery experiments

This study continues the independent Helper-only simulations described in
[frequency refinement](helper-frequency-refinement.md). It does not integrate
or test ordinary Robust reception. The compared receivers use the actual Helper
transmitter waveform, cryptographic-keystream random hop schedules and the same
generated impaired capture for each method. The HF schedule has 50 channels at
50 kHz spacing around 30 MHz; the existing FCC100 and EU20 wide-hop geometries
are available for microwave experiments. Deterministic public test keys support
reproducibility; schedule geometry alone does not certify regulatory compliance.

A full comparison observes 36 seconds of Helper, retains the frozen result at
that point, and uses the fixed 38-second lead before probes in seconds 38–40.
A later dwell can provide a fresh phase or update the current frequency model,
but cannot improve a result labeled available after 36 seconds. Each hop has an
independent common RF phase. A dwell-based correction uses approximately 0.4
seconds of buffering; it must not use phase from a future hop.

The 40 W examples specify total active-mean Helper waveform power and an
independent probe power. A simultaneous 40 W Helper and 40 W probe represents
80 W nominal combined power, not a 40 W total transmitter limit. Input
noise temperature, noise figure, path loss, audio-clock errors, shift-oscillator
errors, and phase diffusion are explicit simulation inputs. These are modeled
link conditions, not physical sensitivity or hardware-qualification claims.

## Covariance for the existing four-window estimator

The original estimator derives each pilot's frequency from four sub-aperture
phase observations. The optional `compute_slope_covariance` path retains its
existing frequency points, phase points, acquisition gates and legacy marginal
errors, while adding bounded work to describe the local estimator covariance.
The default remains unchanged when this option is disabled.

The thermal covariance uses the actual sample influence functions, including
the derivatives of the adaptive guarded-power weights and the overlap between
quarter-window correlations and the common full-window center-phase fit. It
therefore retains frequency/phase and cross-pilot terms instead of assuming
independent pilot measurements. It is a first-order local white-noise model;
its plug-in weights, data-dependent selection, colored interference and
frequency-selective paths still need separate validation. The local PSD
allowance remains explicit and is not a calibrated false-alarm guarantee.

The opt-in result also supplies a separate common-oscillator process covariance.
Adaptive high- and low-pilot weighting can give different Wiener-phase influence
functions, so their process errors cannot generally be represented by a single
identical rank-one frequency error. The joint estimator accepts a validated
positive-semidefinite process block as a replacement for its legacy scalar
common-process approximation, rather than adding both. Thermal and process
uncertainty remain separate.

Adaptive high and low frequency estimates can have different effective dates.
The joint fit uses those dates for the corresponding frequency rows, while the
pilot phase difference remains dated at the shared center. The dates are also
data dependent: the opt-in interface has a declared within-aperture maximum
frequency-drift rate, and reports its duration-scaled contribution as a
separate bounded model radius. A zero bound explicitly assumes constant
frequency within the aperture apart from the separately modeled common Wiener
phase; a caller must not treat it as an unspecified or measured drift bound.
The current HF comparison declares 0.001 Hz/s and the microwave comparison
0.125 Hz/s as operating assumptions, rather than selecting a bound from the
simulation truth.

The joint fit consumes the supplied frequencies and simultaneous pilot phase
difference once, including their covariance; predictions are not additional
measurements. It keeps the unknown phase-difference intercept and one fixed
relative source-time reference. Its conditional frequency interval retains the
recent-history consistency guard and reports stochastic and bounded-model
contributions separately. It remains conditional on the supplied covariance,
clock and bounded channel-transfer models. Independent evidence is needed to
separate arbitrary differential pilot-channel phase changes from audio-clock
changes.

## Matched simulation design

The continuation comparison runs the original four-window receiver, the same
receiver with covariance enabled, a joint estimator using that covariance, and
the partitioned-Hann joint/dual-phase comparator from the earlier study. The
covariance-only variant separates the cost of producing the summary from any
change in the underlying frequency estimate. The optional dual-pilot phase
combination uses a previous-observation prediction; unavailable or inconsistent
blend evidence leaves a valid high-pilot-only phase as the fallback.

The tone and 360 Hz shaped APSK probe never determine receiver synchronization,
correction coefficients, branch selection or adaptive gain, phase or timing.
The noisy Helper determines the correction applied to both the noisy probe and
its evaluator-only clean twin. Relative operation retains one constant receiver
reference, with its corresponding simulator-clock gauge used only for evaluation.
The evaluation reports support and per-hop errors without fitting a new gauge
per hop, window or symbol. An evaluator-only oracle is timed separately and is
not fed back to the receiver.

Receiver process CPU includes all receiver threads and is reported separately
through 36 seconds and through the 40-second capture. Acquisition, tracking,
fallback, covariance/joint work and actual sample-conditioning cost must be
interpreted with their reported signal durations. Simulation generation,
clean-twin/oracle processing and metrology are separate from receiver CPU;
conditioning the held-out probe interval does not establish the cost of
conditioning an entire stream.

## Matched 40 W results

The following clean HF examples use the actual Helper waveform, a 40 W Helper
and a separately powered 40 W probe, 290 K input noise reference and 10 dB
noise figure. The modeled relative RF error is +0.8 ppm, transmit and receive
audio-clock errors are +37 and −13 ppm, and common phase diffusion is
0.05°/√s. The declared within-dwell frequency-drift bound is 0.001 Hz/s. All
methods see the same keyed-random hop schedule and impaired capture per seed.
The flat common-transfer and bounded-model assumptions above apply.

For 32 matched seeds at 180 dB, all four listed methods qualified a 36-second
frequency interval and all 32 intervals covered the simulated frequency. The
reported widths are conditional engineering intervals, not calibrated coverage
guarantees. Phase RMS is the median and worst across per-seed clean-twin RMS
values, after removing one fixed evaluator gauge; the window count retains
unavailable cases in its denominator.

| Method | 36 s frequency error RMS | Conditional half-width min / median / max | Clean-tone phase RMS median / worst | Full held-out windows |
| --- | ---: | ---: | ---: | ---: |
| Four-window baseline, relative clock | 0.01658 Hz | ±0.20048 / 0.21393 / 0.23574 Hz | 3.08° / 6.34° | 160/160 |
| Four-window, covariance, joint fit and dual phase | 0.01762 Hz | ±0.10614 / 0.12211 / 0.15166 Hz | 2.30° / 4.44° | 159/160 |
| Partitioned-Hann, joint fit and dual phase | 0.01783 Hz | ±0.11354 / 0.12520 / 0.15567 Hz | 2.29° / 4.80° | 158/160 |

In these examples the four-window covariance and joint fit narrow the reported
interval compared with the original guarded fit, while the measured point-error
RMS remains comparable. The largest qualified half-width is still above
±0.1 Hz at 180 dB. The partitioned comparison uses its separately modeled taper
covariance; neither finite result establishes a universal estimator ranking or
physical sensitivity limit. The covariance-only four-window path produced the
same point errors and interval widths as the baseline, separating measurement
cost from the later joint-fit change.

For the four-window joint/dual method, the median reported thermal frequency
sigma at 36 seconds was 0.02445 Hz; common-process sigma was about 0.000059 Hz,
the bounded-model half-width was 0.00121 Hz, and the recent-half enclosure
added 0.04553 Hz to the conditional interval. These are modeled components,
not independently calibrated coverage. Its worst admitted clean-sample phase
error was 8.09°, the largest evaluator-only time-map error after removing the
fixed gauge was 7.67 µs, and no admitted cycle slip was observed. The median
of the per-seed fresh-phase 99th percentiles was 6.61°. The largest per-seed
clean-twin frequency RMS across later supported windows was 0.03894 Hz. The
median noisy-tone frequency RMS was 0.09026 Hz with median residual-fit
standard error 0.10761 Hz. These later measurements cannot narrow the frozen
36-second interval retroactively.

For the 32 matched APSK examples at 180 dB, the four-window joint fit with
high-pilot-only phase had noisy EVM median/worst 0.55158/0.59129 and clean-twin
EVM 0.05585/0.10968. The dual-pilot phase path had noisy EVM 0.54928/0.59060
and clean-twin EVM 0.04183/0.07706; the evaluator-only oracle had noisy EVM
median/worst 0.54971/0.58815. Each joint path retained 159/160 later anchors;
individual cases contributed 265–332 supported symbols. These are fixed-gauge
waveform measurements, not decoding results. The clean twin reveals a tracking
difference while noisy EVM is close to the oracle in this modeled link.

The same joint/dual APSK path was also screened with eight seeds each at 175,
170, 165 and 160 dB. All eight frozen frequency intervals covered simulated
truth and all 40 later anchors were supported at each level; every run supplied
at least 331 supported symbols. The noisy EVM median/worst values were
0.31041/0.33210, 0.17545/0.18706, 0.09909/0.10542 and 0.05631/0.05980
respectively. Corresponding clean-twin values were 0.02483/0.04049,
0.02067/0.03271, 0.01471/0.02204 and 0.01204/0.01519. These are matched
waveform measurements under the stated model, not APSK or Robust decoding
claims.

In the final-source clean replay before the separate covariance arithmetic
optimization below, the four-window joint/dual path used median/maximum
0.13995/0.15234 seconds of process CPU through 36 seconds and 0.18421/0.19505
seconds through 40 seconds, including approximately 2.4 seconds of probe
conditioning. The covariance-only path, which retains the original point
estimator and interval, used median 0.13692 seconds through 36 seconds versus
0.06369 seconds without covariance. Through 40 seconds, median acquisition,
correlation, joint/clock/dual and conditioning costs were approximately 0.0212,
0.1268, 0.0047 and 0.0280 seconds; other receiver work is included in the total.
Median simulation generation was 0.3210 seconds and separate tone metrology
0.0030 seconds, excluded from receiver CPU. These host-specific measurements do
not establish the cost of full-stream conditioning or a whole-receiver timing
for the subsequent isolated covariance optimization.

The initial loss screen used eight matched tone seeds at each of 175, 170, 165
and 160 dB, in addition to the 180 dB study. For the four-window joint/dual
path, all eight 36-second intervals at each level covered simulated truth and
all 40 later tone windows were supported. The largest conditional half-widths
were ±0.07087, ±0.04060, ±0.02335 and ±0.01358 Hz respectively. A further
eight-seed refinement at 176, 177, 178 and 179 dB gave half-width ranges of
±0.06786–0.07920, ±0.07595–0.08851, ±0.08501–0.09910 and
±0.09518–0.11096 Hz respectively; all reported intervals covered simulated
truth. In these samples the largest width crossed ±0.1 Hz between 178 and
179 dB. These results depend on the declared 0.001 Hz/s in-dwell drift bound
and other channel/model assumptions; they do not establish calibrated reliability
or a physical sensitivity boundary.


At 5.8 GHz and 180 dB, eight seeds were run with each wide-hop fixture. The
four-window joint/dual path qualified and covered seven of eight frozen
frequency intervals for FCC100 and eight of eight for EU20; the largest
qualified half-widths were ±0.21651 and ±0.22133 Hz respectively. All 40
later tone windows were supported in each profile; later phase support does not
make an unqualified 36-second estimate available. The partitioned comparator
qualified the same counts with largest widths ±0.14451 and ±0.14834 Hz, but
its allowance does not include the same opt-in random-date model bound. The
four-window fixture declares a 0.125 Hz/s in-dwell drift bound, so those widths
are not an equal-assumption precision comparison. Neither fixture establishes
regulatory or hardware compliance.


The baseline joint/dual path was also exercised without the optional recovery
experiment on four HF seeds per condition. At 180 dB, a 1.2 Hz post-observation
frequency step caused all four joint paths to suspend, with zero of 20 later
windows qualified. Imposed curvature produced no qualified 36-second result;
a two-dwell gap retained all 20 later windows, while a longer gap retained none.
A disturbance inside the 120 ms settled aperture left 13 of 20 windows
supported, with the affected hop withheld in these samples. Noise-only inputs
produced no qualified snapshot, and no admitted timing-cycle slips were
observed in these checks. The step result establishes withdrawal, not recovery.

A bounded ±0.02 rad differential pilot-phase variation retained 20/20 windows
in these sampled conditions. A ±0.8 rad change remained undetected at 180 dB
with all 20 windows supported; the largest admitted clean-sample phase error was
17.68°. At 160 dB, the same sampled disturbance suspended all four paths and
left zero later windows qualified. A channel phase outside the declared bound
can be indistinguishable from a clock change, particularly at low SNR; these
examples do not certify that the channel bound can always be detected.
Additional four-seed drift and reversed-audio-clock examples each covered the
36-second frequency and retained all 20 later windows under their declared
models. These are finite controls, not guarantees against arbitrary changes.

## Additional bounded diagnostics

An algebra-preserving optimization of the opt-in covariance calculation was
compared on 704 matched impaired captures: 631 covariance observations were
admitted and 73 declined. The point estimates, effective dates and gates were
identical, and the maximum diagonal-normalized covariance-element difference
was 1.51×10⁻¹⁴. The numerical stress includes common phase-diffusion inputs
from zero to 10¹⁵⁰ and amplitude scales of 2⁻¹⁰⁰ to 2¹⁰⁰; those extremes are
numerical checks, not physical operating conditions. The reproducer sources
are retained in ignored `local-evidence/helper-covariance-opt/` and are not
portable version-controlled fixtures.

In five interleaved isolated component benchmark runs, each processing 100
0.36-second dwells with 99/100 admitted, median process CPU was 0.146559 seconds
before the optimization and 0.133297 seconds after it. These figures exclude
startup, sample conditioning, simulation generation and metrology. They do not
measure a whole receiver, and the earlier 36- and 40-second receiver CPU figures
are not extrapolations from this component benchmark.

A separate diagnosis examined the three step-trial seeds that did not recover
in the first 32-seed study. Each proposed an epoch, but fewer than three valid
full-covariance observations arrived before the two-second detector-only prior
expired. The prior supplied a bounded diagnostic coordinate, not correction
authority. No branch-fit failure was established in those cases; this diagnosis
does not prove that extending a prior or changing gap handling would be safe.

## Bounded change detection and recovery

`HelperFrequencyRecovery` is a bounded detector for a single declared common
frequency change. It compares fresh pilot measurements with a dated prediction
from the preceding Helper model. Its candidate correction is a provisional
acquisition coordinate and epoch request, not a phase authority, posterior
pseudo-measurement or additional observation. The detector retains a fixed
reference, requires bounded fresh evidence, checks pilot-difference consistency,
and keeps the uncertainty shared by the old prediction from shrinking as if it
were new independent noise. Missing input is distinct from evidence that the
reference was lost.

An averaged aperture near a change can contain both old and new frequencies.
The detector's safe change bracket includes the boundary apertures; a later fit
must not replay a potentially mixed frequency or phase summary as a pure
observation. The detector's additive-Hz interpretation is a declared model; it
does not by itself identify whether a physical change was additive or scaled
with the commanded RF frequency. A selected epoch must preserve the original
phase/time reference, avoid counting reused observations twice, and withhold
correction when the model or retained ambiguity is not qualified. Detection
alone is not proof of useful end-to-end recovery.


The optional joint recovery experiment retains the original raw Helper
observations and phase-difference reference. It introduces one explicitly
selected additive-common-Hz step term, with the detector's conservative change
bracket; potentially mixed transition apertures are excluded. The detector
proposal is not ingested as another data point. The original 36-second snapshot
is frozen before any post-observation recovery. The same fresh measurements may
be used to establish and fit the selected epoch only once; the conditional
interval remains subject to the selected step, clock and channel models.

For 32 seeded 180 dB trials with a modeled 1.2 Hz additive step after the
36-second observation, the recovery variant accepted an epoch in 29/32 cases
and supported 116/160 held-out tone windows: four windows in each accepted
case. The original joint path without this option supported 0/160 windows in
the matched cases. Among the recovery windows, the largest clean-twin
window-frequency error was 0.09071 Hz and the largest admitted clean-sample
phase error was 12.95°; no admitted cycle slip was observed. All 32 frozen
36-second frequency intervals covered the simulated pre-step frequency. The
step selection and its interval are conditional engineering results, not
calibrated detection or coverage probabilities. No independent probe or true
step value was used to choose the epoch.

A separate 32-seed HF test predeclared an RF-scaled step model: the step was
1.2 Hz at 30 MHz and scaled with the commanded hop frequency. All 32 frozen
intervals covered the simulated pre-step frequency; 29 cases accepted an epoch
and recovered 116/160 later windows, four per accepted case. The largest
admitted clean-window frequency error was 0.09273 Hz, the largest admitted
sample-phase error was 12.98°, and maximum relative time-map error was 7.98 µs;
no admitted cycle slips were observed. The no-recovery joint path supported
zero later windows. The RF-scaled model was chosen before the observations;
the detector did not infer it by selecting the better fit. The modeled phase
integrates the keyed hop boundaries. The synthetic step wrapper rotates the
noise with the signal, so the scaled-step and clean runs do not share identical
noise samples, although methods within each individual run use the same capture.
Median/maximum process CPU was 0.145/0.184 seconds through 36 seconds and
0.185/0.240 seconds through 40 seconds with variable partial conditioning.
These are finite model-dependent samples, not a general RF oscillator recovery
guarantee.

An eight-seed 180 dB check used the same 1.2 Hz step with the separately powered
40 W shaped APSK probe. The recovery path accepted an epoch in all eight cases,
retained 32/40 later anchors and evaluated at least 265 supported symbols per
run. Median noisy and clean-twin EVM were 0.55245 and 0.04366, with no admitted
cycle slips. The same-capture joint path without recovery supported 0/40
anchors. The ordinary nonjoint path had a different supported subset, so its EVM
is not an equal-symbol comparison. These are waveform diagnostics, not decoding
claims or evidence that all step cases will recover.

The corresponding eight-seed APSK check with a predeclared RF-scaled step also
accepted eight epochs and retained 32/40 anchors, with 265 evaluated symbols
per run and no admitted cycle slips. Median noisy EVM was 0.55249 and median
clean-twin EVM was 0.04382; the same-capture evaluator-only oracle's noisy EVM
median was 0.54911. The Helper and probe are independently specified 40 W
signals, totaling 80 W nominal power during the probe interval. The detector
itself cannot identify the physical step model; this check declares it in
advance.

A separate 32-seed clean run proposed and accepted no step epochs. It supported
159/160 later windows, retained the reference and had no admitted cycle slips;
all 32 frozen frequency intervals covered the simulated frequency. Through 36
seconds, median/maximum recovery-receiver CPU was 0.1449/0.1732 seconds. In
the additive-step trials, the corresponding values were 0.1436/0.1853 seconds;
through 40 seconds, median/maximum was 0.1828/0.2293 seconds and included only
the actual, sometimes partial, held-out conditioning. Process CPU includes the
receiver threads; simulation, clean twin, oracle and metrology are separate.

In a separate 32-seed control, a 1.2 Hz frequency pulse lasted only one dwell
before returning to the prior frequency. The recovery detector proposed and
accepted zero step epochs, and 122/160 later windows were supported. The largest
supported clean-twin window-frequency error was 0.04525 Hz and largest admitted
sample-phase error 8.19°, with no admitted slips; all frozen intervals covered
truth. The pulse wrapper rotates the noise with the signal, so clean and pulse
runs do not have identical noise samples, although the modeled circular noise
distribution is preserved. Within each case, the compared methods still use the
same capture. This is a finite pulse stress, not a guarantee against arbitrary
multiple changes or phase-selective fading.

Four-seed controls of the optional recovery path at 180 dB retained all 20
windows after a short gap but none after a long gap. Curvature produced no
qualified 36-second result or later window. The 120 ms retune stress retained
13/20 windows; the affected hop was withheld in these samples. Noise-only
input did not qualify a snapshot. Bounded differential pilot phase, modeled
drift and reversed clock errors each retained 20/20 later windows. The large
±0.8 rad differential-phase change was not detected at this loss, and all 20
windows remained supported with a worst admitted clean-sample phase error of
17.68°. Thus the declared channel-phase bound remains an assumption, not a
guaranteed detector. No false recovery epoch or admitted slip was reported in
these sampled non-step controls.

The additive-step recovery path was also tested at 180 dB with four seeds per
5.8 GHz profile. FCC100 qualified and covered three of four frozen frequency
intervals, accepted recovery in two cases and supported 8/20 later windows; the
largest supported clean-window frequency and sample-phase errors were 0.05834 Hz
and 5.61°. EU20 qualified and covered all four frozen intervals, accepted three
recoveries and supported 12/20 later windows, with corresponding maxima of
0.06236 Hz and 7.03°. No admitted slip was observed. These small tests use the
declared additive model and do not identify the physical step mechanism,
qualify actual hardware or establish regulatory compliance.

With a predeclared RF-scaled step, four seeds per microwave fixture produced
similar finite-sample availability. FCC100 qualified and covered three of four
frozen intervals, accepted two epochs and supported 8/20 windows; the largest
supported clean-window frequency error was 0.05847 Hz and sample-phase error
5.60°. EU20 qualified and covered all four, accepted three epochs and supported
12/20 windows, with corresponding maxima of 0.06161 Hz and 7.03°. Neither had
an admitted cycle slip. These are model-dependent simulations rather than
hardware, propagation or regulatory qualification.

## Opt-in gap and extended-prior experiments

Two separate experimental options extend the preceding baseline. The gap option
retains the original relative timing/phase reference across a missing interval
of at most 12 seconds, assuming audio-clock evolution within 0.2 ppm/s. The
old branch restricts the alternatives considered but cannot authorize a
prediction: at least three fresh observations spanning 0.8 seconds must qualify
the branch before correction resumes. The extended recovery option increases the
detector-only prior age from two to three seconds under a declared absolute
frequency-curvature bound of 0.002 Hz/s². That prior supplies an acquisition
coordinate, not a new measurement or correction authority; reused observations
are not counted as independent evidence. Neither option establishes its model
assumptions from the probe, and the gap and step recovery options have not been
combined into a qualified gap-plus-step solution.

At 180 dB loss in the 30 MHz HF fixture, 32 matched long-gap tone trials
supported 126/160 later windows with the gap option, versus 0/160 without it.
The 9.6-second missing interval includes the 36-second cutoff, so no frozen
36-second frequency result was available. Over supported windows the largest
clean-twin frequency error was 0.082778 Hz, largest admitted sample-phase error
9.193°, and largest timing error 11.302 µs; no admitted cycle slips were
observed. The same 32 seeded trials with a declared 1.2 Hz additive step after
36 seconds and the extended prior accepted
31/32 epochs and supported 124/160 windows, compared with 29/32 and 116/160
for the two-second prior. All 32 frozen 36-second intervals covered the
pre-step frequency; their median and maximum half-widths were 0.12211 and
0.15166 Hz. The largest later clean-twin window-frequency error was 0.09288 Hz
and sample-phase error 12.947°, with no admitted slips. These results are
conditional on the declared models and are not certified coverage rates.

The limits of the gap assumption matter. In eight trials imposing a timing
cycle jump during the gap, 30/40 windows were admitted and all eight trials
had admitted cycle slips; the largest sample-phase and timing errors were
160.37° and 545.66 µs. In eight trials with out-of-bound 0.6 ppm/s clock
evolution, 32/40 windows were admitted, with no observed slips and a maximum
timing error of 21.839 µs; the violation was not reliably detected. Arbitrary
differential pilot-phase changes or timing-cycle jumps can imitate a clock
change and cannot be reliably detected here without independent evidence. A
step during the gap yielded 0/40 supported windows in eight trials. The gap
option should therefore be used only with a defensible physical evolution and
channel model, and its inability to detect these violations must be considered.

Eight matched HF APSK trials per case separately specified 40 W Helper and
40 W probe power, 80 W combined during the probe. The gap option supported
32/40 anchors and 265–266 symbols per run; median noisy, clean-twin and oracle
noisy EVM were 0.55267, 0.045288 and 0.54911 respectively. The extended step
option supported 32/40 anchors, with median noisy and clean-twin EVM of 0.55245
and 0.043658. These probe measurements do not provide synchronization, branch
selection or adaptive alignment. Eight-seed clean and one-dwell pulse controls
had no admitted slips or false epochs.

In eight seeds per 5.8 GHz fixture, the gap option supported 32/40 windows for
each of FCC100 and EU20, versus zero without it. For the step comparison the
extended prior supported 18/40 versus 16/40 FCC100 windows, and both variants
supported 20/40 EU20 windows. These are simulated fixture outcomes, not
hardware or regulatory qualification.

With the optimized covariance calculation, median/maximum total receiver
process CPU through 36 seconds and through 40 seconds was 0.13285/0.14059 and
0.17481/0.18205 seconds for clean input with the gap option; the 40-second
figure includes 2.4 seconds of held-out sample conditioning. For long-gap
input the corresponding figures were 0.10529/0.11829 and 0.13269/0.14530
seconds, including 1.6 seconds of held-out conditioning; 9.6 seconds of input
were absent before the 36-second cutoff. For the extended-prior step case they
were 0.13526/0.14382 and 0.17556/0.18334 seconds, including 1.6 seconds of
held-out conditioning. These process-CPU figures include all receiver threads
but exclude simulation generation and metrology. Partial conditioning does not
establish full-stream conditioning cost.

In the 32 extended-step tone trials, the median and maximum fresh held-out
per-hop phase RMS were 2.79° and 5.49°; the largest per-hop phase error was
9.15°. The median noisy-tone frequency RMS was 0.09287 Hz, with median fit
standard error 0.10830 Hz. These noisy-probe diagnostics are distinct from the
clean-twin maxima above. The median process-CPU components through the
40-second capture were 0.02155 seconds startup, 0.11570 seconds correlations,
0.00943 seconds fallback, 0.00078 seconds global fit, 0.00651 seconds
clock/joint/dual work, and 0.01892 seconds conditioning for 1.6 seconds of
supported held-out input. Median simulation generation and tone metrology
were 0.32227 and 0.00243 seconds respectively and are excluded from receiver
CPU. These component timings cover the stated capture and can overlap in their
accounting; the measured total, not their sum, is the receiver CPU figure.

## Independent seeds and estimator diagnostics

The following checks use HF seeds 1–32, whose key residues are disjoint from
the earlier seeds 901–932. Fixture keys repeat every 256 seeds: distinct keys
do not prove that every finite schedule prefix is unique. In clean, paired
cases at 180 dB, all 32 frozen 36-second intervals covered truth, with median
and maximum half-width 0.12278 and 0.15425 Hz. Frequency point-error RMS was
0.01932 Hz and maximum absolute error 0.06338 Hz; 159/160 later windows were
supported with no admitted slips. Eight held-out seeds each at 175, 170, 165
and 160 dB also had all frozen intervals cover truth; their respective maximum
half-widths were 0.07333, 0.04183, 0.02396 and 0.01387 Hz. For eight held-out
seeds per microwave fixture, all frozen intervals covered and all 40/40 later
windows were supported; maximum half-width was 0.20537 Hz for FCC100 and
0.20590 Hz for EU20. These finite-sample outcomes do not certify coverage or
establish a physical sensitivity limit.

The evaluator now reports paired results only when both methods retain the
same fixed reference and gauge, and intersects their supported sample masks;
it also reports per-dwell phase and marks `conditioning_comparable`. These
comparisons do not feed the receiver. In the 32 paired HF cases at 180 dB,
high-pilot-only versus dual-pilot median per-seed phase RMS was 3.281° versus
2.924°; common-support clean-tone sample phase RMS medians were 3.091° versus
2.486°. On 265–332 common APSK symbols, median noisy EVM was 0.54340 versus
0.54189, and clean-twin EVM 0.05451 versus 0.04327. The separately specified
40 W Helper and 40 W probe imply 80 W combined. This evaluates existing
methods and is not a new receiver algorithm or a probe-aided correction.

On the final source, a separate eight-seed clean HF dual-pilot run at 180 dB
and 40 W per signal measured total receiver process CPU through 36 seconds at
0.134647/0.144234 seconds median/maximum and through 40 seconds at
0.177357/0.188834 seconds. Through 40 seconds, median component timings were
0.021875 seconds startup, 0.118160 correlations, 0.004351 fallback, 0.000840
global estimation, 0.004868 joint estimation and 0.027680 conditioning. The
conditioning covered 2.4 seconds of held-out probe signal, not the full stream;
component categories can overlap. Simulation generation and metrology were
separate, at median 0.422091 and 0.003075 seconds respectively. Fresh-hop
phase RMS had a median of 2.889° per seed, the median per-seed 99th percentile
was 6.593°, and the maximum admitted sample phase error was 7.434°. The
median noisy tone-frequency RMS was 0.09381 Hz versus 0.01201 Hz for its
evaluator-only clean twin.

An eight-seed evaluator-only pilot oracle isolated local measurement error:
94–95 aided-valid dwells per case, with median local aided phase RMS 3.281°,
known-frequency ideal 2.693°, and ideal dual phase using the true differential
2.201°. Median local aided frequency RMS was 0.08633 Hz versus 0.06914 Hz for
the ideal. The oracle subtracts exact nuisance waveforms and the per-sample
path, knows the true frequencies and differential phase, and starts its
frequency search at truth plus 0.2 Hz. The comparison is against the local
global-prediction-aided measurement, not the full receiver; the probe is absent
before 38 seconds. This is neither a realizable estimator nor an information
bound.

An isolated taper study also used truth-derived predictions rather than a
deployed receiver. Among 126/127 matched draws, training clean phase RMS was
3.17° with Hann and 2.83° with Tukey α=0.5. Under held-out 48 ms settling,
Hann was 3.47°, Tukey 3.82°, and the fallback 3.64°. The witness missed 68/127
settling cases and falsely flagged 6/127 clean cases. No taper replacement was
adopted. A separate analytic Gaussian diagnostic, rather than a waveform
simulation, reproduced the existing dual-phase estimate to 3.33×10⁻¹⁶; with
0.4 Hz frequency-gain mismatch, its frequency RMS error was 0.162 Hz versus
0.120 Hz for high-only. This diagnostic was not integrated.

An evaluator-only candidate for tighter dynamics bounds was rejected. With
an additional 0.001 Hz/s² curvature, seed 903 had 0.081335 Hz error against a
±0.078592 Hz candidate interval and seed 906 had 0.096834 Hz against
±0.084336 Hz, although the existing intervals covered; the diagnostic gates
were 2.376 and 2.322 standard deviations respectively. Its model allowances
included an additional additive curvature bound of 4×10⁻⁵ Hz/s², endpoint
offset 0.006 Hz and slope 0.0007 Hz/s; these are not general total oscillator
bounds. It used white-noise and overlapping linear covariance assumptions.
The curvature wrapper rotates the noise with the signal, so methods within a
case use the same capture but clean versus curved cases do not have identical
noise samples. These diagnostics do not establish diminishing returns. The
admitted gap-cycle slips and model violations described above remain material.

A separate, unregistered ramp-feasibility experiment asked whether Helper
envelopes could resolve the gap-cycle ambiguity. It fits the 2400, 550 and
950 Hz Helper templates with independent complex nuisance coefficients on each
side of a hop. It does not use probe templates, symbols or timing, and the
independently powered 40 W probe is unmodeled interference. Even under
optimistic assumptions—exact pre-gap reference, clock, rate and retune
calendar; only two true alternatives; five 28 ms boundaries after 38 seconds;
and an independent RF calendar for the imposed timing jump—the eight held-out
seeds 1–8 produced three false jump selections in clean cases and two missed
jumps, for both tone and APSK probes. A one-seed, 100 dB bright positive control
selected both alternatives correctly. This experiment supplies no receiver
feedback, is not a deployed detector and makes no CPU claim; the original
cycle ambiguity remains unresolved.

## Dwell-transient fixture and exploratory alternatives

The registered `helper_transient_fixture` exercises Helper-only simulations
with signal-only disturbances on dwells 25, 50, 75 and 95, retaining the same
thermal-noise realization across matched methods. Seven profiles include the
original high-pilot additive 48 ms disturbance, separate composite-phase
disturbances of 15, 48 and 80 ms, 80 ms gain disturbance, and constant and
decaying frequency settling. These fixtures do not compose with differential
phase, noise-only or audio-clock rewriting modes. A discontinuity crossing an
aperture does not have a single true aperture frequency; the evaluator oracle
is only a midpoint reference. For affected admitted samples, reported phase
error includes the physical transient itself and cannot all be attributed to
receiver estimation error.

The short probe verifies 56 distinct reserved key sets; fixture keys repeat
every 256 seeds, and this is not an exhaustive historical schedule claim. The
eight tuning seeds were 16777250, 16777251, 16777252, 16777253, 16777254,
16777255, 16777257 and 16777258. In clean HF cases at 180, 175, 170, 165 and
160 dB loss, all eight frozen 36-second intervals qualified and covered at each
loss, and all 40/40 later windows were supported. The respective maximum
conditional half-widths were 0.1290, 0.0743, 0.0425, 0.0245 and 0.0142 Hz.

An independent 32-seed clean HF tone and APSK check used distinct full noise
seeds and schedule key bytes held out from the scanned inventory. All 32
frozen intervals covered for both probes; point-error RMS was 0.0163052 Hz,
maximum absolute error 0.0327931 Hz, and median/maximum conditional half-width
0.122673/0.144864 Hz. Tone supported 160/160 windows and APSK 160/160 anchors,
with no admitted slips and maximum timing error 7.587 µs. Median per-seed
fresh-phase RMS was 2.8795° and median per-seed 99th percentile 6.948°; worst
admitted sample phase error was 8.217°. Median clean-twin tone-frequency RMS
was 0.01183 Hz versus 0.10532 Hz for the noisy probe. On the same 331 APSK
symbols per case, high-only to dual-pilot median noisy EVM changed from
0.544101 to 0.544233, while clean-twin EVM changed from 0.055171 to 0.046852.
The noisy result does not demonstrate a gain. This check did not change the
receiver algorithm.

At 180 dB the tuning transient screen showed:

| Signal disturbance | Supported later windows | Largest admitted clean-sample phase error |
| --- | ---: | ---: |
| High-pilot additive, 48 ms | 40/40 | 7.005° |
| Composite phase, 15 ms | 40/40 | 54.342° |
| Composite phase, 48 ms | 38/40 | 86.984° |
| Composite phase, 80 ms | 32/40 | 7.49° |
| Gain, 80 ms | 40/40 | 8.895° |
| Constant frequency settling, 60 ms | 1/40 | 4.586° |
| Decaying frequency settling | 40/40 | 29.695° |

The constant-frequency case recorded 32 contradictions; the decaying case
recorded none and had maximum supported residual tone frequency 0.2134 Hz.
These measurements do not establish that the admitted transient portions are
suitable for an ordinary receiver. Simulations retain the fixed 36-second
snapshot, 38-second lead and one relative reference, with 290 K thermal noise,
10 dB noise figure and the earlier oscillator assumptions. The independently
specified 40 W Helper and 40 W probe total 80 W during the probe. No GUI,
ordinary Robust receiver or audio hardware was involved.

On 32 fresh held-out HF tone seeds, the 48 ms high-pilot additive case
qualified and covered all 32 frozen intervals, supported 159/160 later windows
and had no admitted slips or model contradictions. Maximum timing error was
7.655 µs, worst admitted clean-sample phase error 8.273°, worst clean central
window frequency error 0.040660 Hz, and median noisy frequency RMS 0.10584 Hz.
The 48 ms composite-phase case likewise qualified and covered all 32 frozen
intervals, supported 152/160 windows, and recorded no admitted slips or model
contradictions. Maximum timing error was 7.913 µs, worst admitted clean-sample
phase error 91.731°, worst clean central-window frequency error 0.0364 Hz,
and median noisy frequency RMS 0.32959 Hz. The physical channel-phase pulse
is present in the probe and contributes to that admitted phase error. Median
common-sample clean phase RMS for high-only versus dual was 3.024° versus
2.654° for the additive case and 15.606° versus 15.515° for the composite
case. These are independent nuisance profiles, not tuning or proof of coverage
under arbitrary transients.

At 180 dB, eight reserved keys each for the FCC100 and EU20 5.8 GHz fixtures
gave 8/8 frozen interval coverage and 40/40 later windows in clean cases; the
maximum half-widths were 0.21119 and 0.21148 Hz. The high-pilot additive
48 ms case also covered 8/8 and supported 40/40 for both, with maximum
half-widths 0.21757 and 0.22114 Hz and largest admitted sample-phase errors
6.423° and 6.451°. The composite 48 ms pulse covered 8/8 for each and
supported 40/40 FCC100 and 39/40 EU20 windows, with maximum sample-phase
errors 85.685° and 92.023° and no admitted slips. The composite waveform pulse
is a physical perturbation, not an isolated estimator error. These fixture
geometries do not certify regulatory compliance.

Eight tuning-key controls per condition at 180 dB further expose the model
limits. Noise-only and curvature cases each qualified 0/8 frozen snapshots
and supported no later windows. Bounded differential phase covered 8/8 and
supported 40/40 windows, with maximum admitted phase error 7.115°. An
out-of-bound large differential change also covered 8/8 and supported 40/40,
with maximum admitted phase error 29.422° and no contradictions. Short gaps
covered 8/8 and supported 40/40; the retune-mid stress covered 8/8 and
supported 20/40. A step covered all frozen snapshots but the baseline
supported no later windows. In the whole-cycle gap stress, the optional gap
branch admitted 32/40 windows and all eight cases slipped, with worst phase
161.91° and timing error 545.267 µs; the baseline gap path supported no
windows. Model-contradiction checks are not reliable for arbitrary differential
phase or whole-cycle timing jumps.

Extended step recovery on the same eight tuning keys accepted 8/8 epochs and
supported 31/40 windows; all frozen intervals covered and no slips were
observed. Worst admitted clean-window frequency error was 0.1493 Hz and
sample-phase error 8.645°, so these cases do not support a post-step ±0.1 Hz
claim. Modeled drift and negative-clock controls each covered all eight frozen
intervals and supported 40/40 windows without slips; maximum admitted
sample-phase errors were 7.202° and 6.112° respectively.

Exploratory alternatives did not justify a receiver change. In six tuning
seeds and 24 affected dwells, three 120 ms calls per dwell qualified only 3/32
clean dwells versus 32/32 for the baseline. A piecewise fit used four 90 ms
segments, 31 frequency candidates and three split dates: at 48 ms it flagged
3/24 but left 16/23 admitted phase errors above 11.46°; at 80 ms it flagged
17/24 while all 19 admitted errors exceeded 11.46°, and frequency RMS rose
from 0.151 to 0.382 Hz. Its search coordinate used simulator truth plus an
offset; it was offline, outside the correction chain, and had no qualified
post-selection covariance. These tests reject these candidates under the
modeled cases, not every possible robust approach.

An offline waveform-level joint candidate on 664 common observations had
baseline/candidate frequency RMS of 0.08474/0.09281 Hz and phase RMS of
2.703°/2.714°. With a 0.4 Hz prediction-relation mismatch, candidate
frequency RMS was 0.16722 Hz and the pre-dwell gate rejected 0/666. Its
candidate covariance remains unvalidated, and no result entered the receiver
correction chain. Neither exploratory method was integrated. A separate oracle
run on the first eight reserved held-out HF keys had a median of 95 common
aided-valid dwells. Median local aided phase RMS was 3.298°, versus 2.723°
with exact known frequency and 2.250° for ideal dual phase with the true
differential. Median aided frequency RMS was 0.09559 Hz versus 0.07426 Hz for
the ideal profile. As above, the oracle knows unavailable nuisance waveforms,
the path, true frequencies and phase difference, and initializes its search at
truth plus 0.2 Hz; the local comparator uses global predictions. It supplies no
receiver feedback and is not an information bound.

On eight held-out clean HF keys at 180 dB and 40 W per signal, isolated
final-source receiver process CPU through 36 seconds was 0.132724/0.140243
seconds median/maximum, and through 40 seconds was 0.174932/0.181611 seconds.
The latter includes 2.4 seconds of held-out conditioning, not full-stream
conditioning. Through 40 seconds, median timings were 0.021638 seconds
startup, 0.117838 correlations, 0 fallback, 0.000824 global estimation,
0.004791 joint estimation and 0.027710 conditioning; fallback maximum was
0.00871 seconds. Categories may overlap, while total process CPU was directly
measured. Median simulation generation and metrology were 0.371904 and
0.003071 seconds, excluded from receiver CPU.

Another standalone timing-signature experiment changed the simulated
transmitter with a fixed coefficient; it did not change the production
waveform. Eight tuning seeds used independent hop phases and missing dwells
70–94, including a 32-second timing jump. Mean transmitter waveform power was
1.00001 times the baseline, with added tone energy 0.10000 times baseline and
maximum per-dwell power difference about 0.023%. An oracle branch scorer
knew exact source clock and frequency and compared two hypotheses. With stable
or +0.02 rad signature differential phase it made no false selections or
misses in eight cases each; with +2.717 rad differential it made eight false
and eight missed selections. These decisions were not supplied to either
actual gap tracker: both admitted cycle slips in all eight jump cases, and no
36-second snapshot was available because the gap crossed the cutoff.

For the no-jump case, actual-chain APSK clean-twin median EVM changed from
0.04992 to 0.05326 on 199 common symbols in seven of eight seeds; one seed
lacked a same-reference comparison. Supported anchors changed from 24/40 to
23/40. Fresh-phase RMS median changed from 2.953° to 3.136° on method-selected,
not common, dwells. The probe supplied no template and both Helper and
independent probe were 40 W. This is a conditional diagnostic without branch
correction, not a deployed improvement or a solution to arbitrary differential
phase ambiguity.

## Expanded-key dwell-v3 study

The simulation fixture now has an opt-in expanded schedule-key namespace;
legacy keys and schedules remain the default. The public test key combines the
24-byte prefix `HH-HELPER-FHSS-TEST-V3!!` with the full 64-bit seed and is
disjoint from the 256 legacy keys. The `hf-v3`, `microwave-v3` and
`microwave-eu-v3` case profiles select it. The short probe checks 56 new keys
against all legacy keys and prior reservations. These keys are fresh relative
to the scanned records, not necessarily every unlogged historical experiment;
distinct keys do not prove that finite generated schedule prefixes differ.
The tuning seeds were 1666973697–1666973704, held-out HF seeds
1667039233–1667039264, FCC seeds 1667104769–1667104776 and EU seeds
1667170305–1667170312.

Evaluator-only output now includes counterfactual oscillator phase before
impairment, a physical-disturbance mask enlarged by the actual resampler halo
and retained frequency-excursion phase, and each held-out dwell's supported
sample count and first/last input spans. A first/last span does not imply all
intervening samples were supported. Evaluator overhead is reported separately
from receiver CPU. None of these measurements feeds acquisition, correction
or branch selection. The six held-out disturbance profiles were reserved
before tuning in `tests/helper_transient_fixture.hpp`.

For 32 expanded-key clean HF cases at 180 dB, all frozen 36-second frequency
intervals covered; point error RMS was 0.01659 Hz, maximum absolute error
0.03238 Hz, and median/maximum conditional half-width 0.11342/0.13205 Hz.
Tone supported 160/160 windows; median per-seed fresh-phase RMS was 2.854°,
median per-seed 99th percentile 7.152°, worst admitted sample phase 7.224°,
maximum timing error 8.934 µs and no slips were observed. APSK supported
160/160 anchors with 331–332 common symbols per run. High-only versus dual
median noisy EVM was 0.55098 versus 0.54834, and clean-twin EVM 0.05923
versus 0.04391. These compare existing algorithms, not a new receiver gain.
The Helper and probe are independently specified 40 W, or 80 W combined.
The model retains 290 K noise, 10 dB noise figure, independent oscillator
errors, a 36-second frozen observation, 38-second lead and held-out probes
through 40 seconds.

On the first eight held-out HF keys, all frozen intervals covered and no slips
were observed for each of the six previously unseen disturbance profiles:

| Profile | Supported windows | Halo-affected supported samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| Additive 23 ms | 40/40 | 5,216 | 6.986° |
| Composite phase 37 ms | 38/40 | 5,928 | 43.403° |
| Composite phase 65 ms | 40/40 | 11,524 | 62.413° |
| Gain 55 ms | 38/40 | 8,520 | 7.031° |
| Frequency settling 85 ms | 5/40 | 0 | 3.276° |
| Decaying frequency settling | 39/40 | 46,343 | 52.6° |

The 65 ms phase case's largest clean central-window tone-frequency error was
0.6519 Hz; the decay case's was 0.4337 Hz. The decay count includes retained
phase from the excursion. Physical disturbances also affect the probe; these
phase errors are not solely oscillator-estimation errors. An aperture crossing
a nonstationary disturbance has no single frequency truth.

Eight tuning cases at each of 180, 175, 170, 165 and 160 dB all covered the
frozen frequency; respective maximum half-widths were 0.14461, 0.08091,
0.04559, 0.02591 and 0.01489 Hz. Supported windows were 39/40 at 180 dB and
40/40 at each other loss. At 180 dB, the tuning phase15, phase48 and decay
cases supported 39/40, 37/40 and 39/40 windows, admitted respectively 3,681,
7,512 and 52,960 halo-affected samples, and had maximum sample-phase errors
54.55°, 87.79° and 31.8°. Eight expanded-key clean cases per 5.8 GHz fixture
all covered and supported 40/40 windows without slips; FCC100 and EU20 maximum
half-widths were 0.22292 and 0.21378 Hz. The fixture does not establish hardware
or regulatory compliance.

Eight tuning-key actual-chain controls per condition at 180 dB produced no
qualified snapshots or windows for noise-only and strong curvature. Bounded
differential phase covered 8/8 and supported 40/40 windows, with maximum
admitted phase error 6.33°; large differential phase also covered 8/8 and
supported 40/40, with maximum 19.31°, and could go undetected. Drift covered
8/8 and supported 40/40 with maximum 5.45°. Short gaps covered 8/8 and
supported 39/40 with maximum 7.29°. A baseline step covered 8/8 frozen
intervals but supported 0/40 later windows. Retune-mid covered 8/8 and
supported 17/40, with maximum admitted phase error 61.06° and no slips. For a
whole-cycle gap, the optional branch admitted 32/40 windows and all eight
cases slipped, with maximum phase error 164.01°; the gap crossed the cutoff so
no 36-second snapshot was available, and the baseline supported no windows.
These tuning controls do not establish held-out guarantees.

An offline pooled actual-waveform Huber/fine-scan candidate was evaluated on
eight expanded-key tuning seeds and 32 dwells. At gate 3.5 it flagged 0/32
clean and 2/32 phase15 cases, leaving 10,800 physically disturbed input samples
in provisional, uneroded spans and a worst phase error of 56.14°. At gate 2.5
it flagged 5/32 clean cases and lost 15,120 clean samples, yet retained 8,640
phase15-disturbed samples with worst phase error 54.30°. The coordinate used
truth ±0.15 Hz, no probe or correction chain, and no qualified post-selection
covariance. This rejects the tested candidate under these conditions, not all
robust approaches.

An offline soft-joint candidate used the full Hann and four quarter-aperture
shared-sample covariance with a soft previous prior, conditional white-noise
and declared bounded-error assumptions. In eight 40-second tuning cases,
668/668 attempts fell back after the current-phase and postcurrent-time L1
checks and combined 0.5 rad guard. An independent audit declined 84/84 in
one case; its median preclock radius was 0.358 rad plus 0.345 rad from relative
clock-evolution uncertainty, not the unknown initial gauge. Artificially
zeroing clock uncertainty admitted 83/84 in a phase-only diagnostic, but is
not a real operating condition; a 0.4 Hz mismatch witness missed all short
cases. The candidate was not integrated or tested in the actual correction
chain because the guard admitted no viable phases. An earlier nominal offline
2.775° to 2.608° phase result is preliminary, not a runtime gain claim. A
future known-output-band guard cannot safely use the 980 Hz anchor factor
alone, which applies only to a predeclared 1420–1780 Hz band. It also needs
within-dwell clock evolution, conditioner-versus-joint clock mismatch,
RF/process transport and actual resampler signed-tap/operator error bounds,
with the same policy for both compared methods. The current API is insufficient
for an output guarantee; the guard is future work, not an implemented guard or
a ±0.1 Hz claim. The baseline continues to use its separate current policies.

The existing idealized oracle was also run on eight held-out v3 HF keys, with
median 94.5 common aided-valid dwells. Median local aided phase RMS was 3.249°,
versus 2.653° with known frequency and 2.103° with true differential phase;
median local aided frequency RMS was 0.09323 Hz versus the oracle profile's
0.07639 Hz. It assumes exact interfering waveforms and path, true frequency
and differential phase, and a truth-plus-0.2 Hz starting coordinate. Its
comparator is the local global-prediction-aided measurement, it supplies no
probe feedback, and it is not a proven information bound.

In an isolated final-source v3 run with eight clean held-out HF keys, 40 W
Helper and 40 W probe at 180 dB loss, dual-pilot receiver process CPU through
36 seconds was 0.144464/0.198246 seconds median/maximum and through 40 seconds
0.190034/0.251776 seconds. The latter includes 2.4 seconds of partial
conditioning, not full-stream conditioning. Median through-40 component times
were 0.023060 seconds startup, 0.124621 correlations, 0 fallback (maximum
0.009842), 0.000868 global estimation, 0.005038 joint estimation and 0.031085
conditioning; categories may overlap. Excluded from receiver CPU were median
simulation setup 0.065506 seconds, 40-second generation 0.351816, tone metrology
0.003266, phase evaluator 0.000139 and mask evaluator 0.000402. The mask
evaluation is included in the 0.029970-second median evaluator-stream total;
the phase evaluator is separate. Median
supported input span was 37.662101–39.937904 seconds; this overall span is
not necessarily contiguous, and per-dwell output provides actual counts and
spans. These measurements do not establish full-stream conditioning throughput.

No GUI, audio hardware or ordinary Robust receiver was accessed. The v3 build
and 25 selected focused checks passed.

## Actual-history dwell-v4 investigations

This follow-up evaluates practical candidate measurements without involving
the GUI, audio hardware or ordinary Robust receiver. No candidate advanced into
the correction chain. Expanded-key v4 reservations use tuning seeds
1680000001–1680000008, held-out HF seeds 1680100001–1680100032, FCC seeds
1680200001–1680200008 and EU seeds 1680300001–1680300008. The 56 keys were
checked distinct from all 256 legacy keys and the 56 v3 reservations; freshness
is relative to scanned evidence, not all possible unlogged prior work. Six
`v4held-*` fixture combinations were reserved before tuning.

In a short regression on the previously used seed 1667039233, the clean
36-second frequency error was 0.007368 Hz with a 0.119497 Hz conditional
half-width, and all five later windows were supported. With a 15 ms composite
phase pulse, the central phase error was 2.49°, but the physical pulse reached
56.26° and 460 halo-affected samples were still supported. The pulse affects
the probe itself; no evaluator truth is passed to the receiver.

In eight tuning-key clean HF cases at each of 180, 175, 170, 165 and 160 dB,
all frozen intervals covered and each loss supported 40/40 later windows.
Their maximum conditional half-widths were respectively 0.13743, 0.07824,
0.04450, 0.02540 and 0.01462 Hz. The 180 dB tuning disturbances produced:

| Profile | Supported windows | Halo-affected supported samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| High-pilot additive 48 ms | 40/40 | 1,537 | 7.335° |
| Composite phase 15 ms | 40/40 | 3,681 | 53.226° |
| Composite phase 48 ms | 37/40 | 6,261 | 91.826° |
| Composite phase 80 ms | 32/40 | 0 | 4.784° |
| Gain 80 ms | 40/40 | 16,163 | 12.834° |
| Frequency settling 60 ms | 3/40 | 0 | 2.332° |
| Decaying frequency settling | 40/40 | 52,963 | 30.957° |

These common-phase pulses physically alter the probe; the measured error is
not solely oscillator-estimation error. Trace support and withholding counts
are evaluator-only and cover only produced tone output from 38.02 to 39.98
seconds. APSK trace counts are unavailable; missing pre-initialization and
post-output samples are not counted. Withheld clean samples are not
automatically useful. As before, tests use 290 K thermal noise, 10 dB noise
figure, independent oscillator errors, a frozen 36-second observation, a
38-second lead and probe intervals through 40 seconds, with separate 40 W
Helper and 40 W probe signals (80 W combined).

On 32 fresh v4 held-out HF clean cases, 31/32 frozen 36-second intervals
qualified and all 31 covered; the remaining result was unavailable, not a
coverage miss. Among qualified cases, point error RMS was 0.018642 Hz, maximum
absolute error 0.047710 Hz, and median/maximum conditional half-width
0.114820/0.151212 Hz. Tone supported 158/160 windows, with median per-seed
fresh-phase RMS 2.876°, median per-seed 99th percentile 6.908°, worst admitted
sample phase 7.841°, maximum timing error 5.982 µs and no slips. Median later
clean-twin tone-frequency RMS was 0.011215 Hz versus 0.118032 Hz for the noisy
probe. Median supported output was 39,722 samples from approximately 2.4
seconds of conditioning; 13,938 clean samples were withheld within the
produced 38.02–39.98 second tone trace, subject to the scope and usability
limits above. APSK supported 158/160 anchors, with a median 331 common symbols.
High-only to dual-pilot median noisy EVM changed from 0.553263 to 0.552148
and clean-twin EVM from 0.056916 to 0.043587. These are comparisons of existing
methods, not a new receiver gain.

In eight held-out v4 cases per disturbance at 180 dB, all frozen intervals
covered and no slips were observed. The trace counts below are summed over the
eight cases, include the resampler halo and apply only to produced tone output
from 38.02 to 39.98 seconds; the preceding trace-scope limitations apply.

| Held-out profile | Supported windows | Disturbed supported / withheld samples | Worst admitted clean-sample phase |
| --- | ---: | ---: | ---: |
| Additive 31 ms | 39/40 | 6,752 / 0 | 11.164° |
| Composite phase 12 ms | 39/40 | 3,104 / 0 | 60.667° |
| Composite phase 53 ms | 33/40 | 2,744 / 8,232 | 65.782° |
| Gain 73 ms | 38/40 | 12,964 / 1,852 | 10.506° |
| Frequency settling 70 ms | 7/40 | 0 / 66,083 | 3.631° |
| Decaying frequency settling | 39/40 | 34,560 / 12,320 | 23.187° |

The decay case had a maximum clean central-window residual tone frequency of
0.362 Hz. Phase errors include physical perturbation of the probe. In the
12 ms phase case, median per-case anchor phase RMS was 2.827° against
both the actual physical and counterfactual unperturbed phase, because the
anchor fell outside the pulse; nevertheless 3,104 halo-affected outputs were
supported and the maximum admitted sample phase error was 60.667°. Anchor
precision therefore did not detect or correct that pulse. For frequency70,
retained excursion phase means the counterfactual pre-impairment phase is not
the same as actual post-event settled phase.

Eight clean held-out cases for each of the 5.8 GHz FCC100 and EU20 fixtures covered all
frozen intervals and supported 40/40 windows without slips; their maximum
half-widths were 0.217181 and 0.214211 Hz respectively. These fixtures do not
establish regulatory compliance.

Eight v4 tuning controls per condition produced 0/8 qualified snapshots and
no later windows for noise-only and curvature. A baseline step covered 8/8
frozen intervals but supported 0/40 later windows. Bounded and large
differential phase each covered 8/8, with 40/40 and 39/40 windows and maximum
admitted phase errors of 8.07° and 21.36° respectively. Short-gap, retune-mid,
drift and negative-clock controls each covered 8/8 and supported respectively
40/40, 20/40, 40/40 and 39/40 windows; their maximum phase errors were 7.94°,
4.72°, 7.42° and 6.70°, with no admitted slips. For a whole-cycle gap, the
optional branch had no 36-second snapshot, admitted 32/40 later windows and
slipped in all eight cases, with maximum phase 164.14° and timing error
546.04 µs. The baseline supported no windows. These tuning controls do not
establish detection of arbitrary mismatch.

For the 1.2 Hz step on these tuning keys, the existing extended recovery
option covered all eight frozen intervals, accepted 8/8 epochs and supported
32/40 later tone windows with no slips. The largest clean-window frequency
error was 0.07956 Hz and largest admitted sample-phase error 8.516°. Later
recovery measurements do not alter the frozen 36-second result.

An offline differential-segment witness at threshold 1.35 flagged 6/32 clean
and 23/32 centered additive-48 tuning dwells. Own-date counterfactual phase RMS
changed from 10.60° to 7.40°, but selected intervals still overlapped 6,933
nuisance samples. It had neither halo treatment nor post-selection covariance
and was not integrated. A separate two-pilot pooled GLRT used actual previous
Helper history and current aided frequency, with overlapping 10 ms bins and
gate 3.5: it flagged 1/32 clean and 3/32 phase15 dwells but retained 10,800
physically disturbed input samples, with worst phase error 57.06°. Five ms bins
flagged 5/32 phase15, retained 10,080 disturbed samples and had worst error
54.72°. There was no per-segment threshold, and no truth or probe drove
selection; the provisional input spans were uneroded and the test was offline
and uncalibrated.

A joint split experiment on eight fresh tuning keys had 73/128 common
observations. Baseline versus split phase RMS at the second-half date was
3.995° versus 3.653°; first-half frequency RMS at each method's own date was
0.09524 versus 0.22831 Hz. It used baseline-only history and quarter gates,
without conditioner support. Only this candidate was rejected. A practical
actual-chain guard still lacks the necessary actual-history information
contracts and certified signed FIR operator; the 980 Hz band anchor factor
alone is insufficient, and waveform APSK phase is undefined near zero
amplitude. These results do not exhaust all approaches or establish a
physical sensitivity limit or diminishing returns. The v4 build and all 28
focused checks passed.

An idealized oracle run on the first eight v4 held-out keys had a median of
94.5 common aided-valid dwells. Median local aided phase RMS was 3.151°,
versus 2.709° with exact frequency and 2.208° with true differential phase;
median aided frequency RMS was 0.09228 Hz versus the oracle's 0.07044 Hz.
The oracle knows exact nuisance waveforms and path, true frequencies and
differential phase, and starts its search at truth plus 0.2 Hz. Its comparator
is the local global-prediction-aided measurement; it supplies no receiver
feedback and is not a physical bound.

In an isolated final-source run of the first eight clean v4 held-out HF keys
at 180 dB and 40 W per signal, dual-pilot receiver process CPU through 36
seconds was 0.142710/0.150445 seconds median/maximum and through 40 seconds
0.188306/0.206315 seconds. The latter includes only 2.4 seconds of partial
conditioning and does not establish full-stream throughput. Median through-40
component times were 0.022643 seconds startup, 0.126609 correlation, 0.002322
fallback, 0.000894 global estimation, 0.005309 joint estimation and 0.030606
conditioning; categories may overlap. Excluded from receiver CPU were median
simulation setup 0.062936 seconds, 40-second generation 0.425408, tone
metrology 0.003348, phase evaluator 0.000147 and evaluator stream 0.029568,
which includes mask evaluation of 0.000401. The median overall supported
input span, 37.662102–39.937905 seconds, is not necessarily continuous;
per-dwell counts and spans are provided in the output.

The standalone candidate sources are `tests/test_helper_diff_segment_v4.cpp`,
`tests/test_helper_transient_glrt_v4.cpp` and
`tests/test_helper_joint_split_v4.cpp`; their results do not feed the actual
receiver. The held-out disturbance definitions are in
`tests/helper_transient_fixture.hpp`.

## Joint-dwell and conditional-guard v5 experiments

Some v5 measurements in this section are intermediate: a later guard
correction adds an output-map phase term and the full halo distance. Results
explicitly identified as final-guard measurements were rerun on the corrected
source; other figures retain the earlier guard.

The standalone v5 chain adds an optional local current-dwell two-pilot
likelihood. The earlier aided result supplies acquisition gates, local noise
estimates and fallback; its measurement is not ingested as a second independent
likelihood row when the candidate is selected. A previous Helper prediction
provides search coordinates, not an independent prior observation. Fresh
phase and frequency estimates, covariance and model allowances are conditional
on the aided gate and a locally stationary, white-noise two-tone model; a
fallback remains available. The source implementation is experimental and is
not integrated into ordinary Robust.

Matched v5 and baseline runs both use the same conditional input-phase guard
for a declared 1420–1780 Hz target band. It budgets three stochastic standard
deviations plus model, frequency/clock transport and phase diffusion within
0.5 rad, then the conditioner erodes support using its actual interpolation
halo. This conditional input-point budget is not a guaranteed output FIR error
bound for arbitrary modulation or unmodeled physical disturbances. Each fresh
dwell requires 0.4 seconds of buffering/look-ahead. The same fixed initial
relative reference is preserved, and the independent probe supplies no
synchronization or branch selection.

The expanded-v3 key namespace was used with eight tuning keys
1690000001–1690000008, 32 held-out HF keys 1690100001–1690100032, and eight
reserved microwave keys each for FCC 1690200001–1690200008 and EU
1690300001–1690300008. Freshness is relative to the scanned key inventory,
not a proof about unlogged runs or distinct finite schedule prefixes. Thermal
noise was modeled at 290 K with 10 dB noise figure; Helper and probe powers
were separately 40 W, 80 W combined. The observation was frozen at 36 seconds,
the transmit lead was 38 seconds, and held-out tone probes ran through 40.

On the 32 held-out clean HF captures at 180 dB, a final-guard rerun reproduced
the previous signal and support results. Joint-dwell and baseline each
qualified and covered all 32 frozen frequency intervals. Joint-dwell point
error RMS was 0.01279 Hz with worst absolute error 0.02726 Hz; its conditional
half-width median/max was 0.11670/0.15975 Hz. The baseline values were
0.01302 Hz RMS, 0.02769 Hz maximum absolute error, and 0.11821/0.16260 Hz
median/max half-width. Neither method attained a conditional ±0.1 Hz interval
in these 32 cases. Later joint-dwell and baseline respectively supported
159/160 and 157/160 tone windows; worst admitted clean-sample phase errors
were 8.51° and 11.09°, maximum timing errors 4.98 and 4.46 µs, and neither
reported slips. Median clean-twin tone-frequency RMS was 0.01131 versus
0.01222 Hz; median noisy tone RMS was 0.09693 versus 0.09260 Hz. These
method-selected window counts and noisy measurements are not necessarily on
the same samples. Their fully supported window counts were 154/160 versus
140/160, with 1,252,180 versus 1,203,039 supported stream samples. When the
36-second model is frozen, later prediction frequency RMS was 0.01458 versus
0.01494 Hz, with maxima 0.03295 versus 0.03352 Hz; later samples did not alter
the frozen result. One case's supported input spans were 38.06208–38.33786,
38.46211–38.73784, 38.86208–39.13786, 39.26211–39.53784 and
39.66209–39.93786 seconds; the overall span is not contiguous.

For equal fixed-gauge, common-supported samples in these same cases, median
per-seed all-dwell phase RMS was 2.638° for joint-dwell versus 2.888° for the
baseline, and median clean-twin sample-phase RMS was 2.073° versus 2.447°.
The evaluator's common-support intersection contained 20,864–33,098 tone
samples per case, 995,341 in total; it does not feed either chain. Overall
common-dwell phase RMS was 2.623° versus 2.886°, with worst phase 9.071° versus
11.093°. The candidate accepted all
2,664 attempted local joint dwells in this clean sample. In an eight-seed
tuning screen rerun on the final guard at 180, 175, 170, 165 and 160 dB,
both methods covered all
frozen intervals. Joint-dwell maximum half-widths were respectively 0.1350,
0.0779, 0.0443, 0.0253 and 0.0146 Hz; it supported 40/40 later windows at
each loss. These finite cases do not certify uncertainty coverage under
arbitrary drift, clock, differential-phase or channel disturbances.

In eight final-guard clean HF APSK cases, joint-dwell and baseline supported
2,640 and 2,503 symbols respectively. Their method-selected mean noisy EVM
was 0.5434 versus 0.5459 and clean-twin EVM 0.0409 versus 0.0460. On the
same 2,503 common symbols, noisy EVM was 0.5450 versus 0.5459 and clean-twin
EVM 0.0404 versus 0.0459. These are waveform diagnostics, not decoded data
or a claim of broad sensitivity improvement.

In eight final-guard clean 5.8 GHz cases per fixture, both methods qualified
and covered all frozen intervals and had no slips, but joint-dwell supported
less output. FCC100 had 26 versus 38 windows with any support, zero versus nine
fully supported windows, and 78,270 versus 220,139 supported samples; EU20 had
28 versus 40 windows with any support, zero versus 14 full windows, and 89,267 versus
246,599 samples. This candidate did not improve those fixture outcomes;
the schedules do not prove regulatory compliance.

The initial eight-seed 180 dB transient screen does not establish robust
transient tracking. The phase-selection candidate supported 32/40 windows for
the high-pilot additive 48 ms case versus the guarded baseline's 37/40, with
192 versus 609 halo-affected physical-disturbance samples supported. For a
15 ms common phase pulse it supported 37/40 versus 38/40, and both retained
3,529 halo-affected samples; worst admitted clean-sample phase was 53.06°
versus 52.99°. For 60 ms frequency settling both supported only 1/40 windows
and had seven of eight frozen intervals available and covered. These
disturbances affect the probe physically as well as estimator observations,
so admitted pulse error is not solely oscillator-estimation error. Support
counts are evaluator-only within the produced trace and do not include samples
never produced; they do not authorize use of withheld samples.

On the first eight held-out HF keys, a final-guard rerun reproduced all
non-timing results from the earlier capture. The phase-selection candidate admitted no
halo-affected samples for the previously exercised 48 ms phase case versus
800 for baseline; worst admitted sample phase was 10.74° versus 81.01°, with
30 versus 29 full windows. On 205,064 common-supported samples, clean-twin
phase RMS was 2.700° versus 2.722°. For the unseen 31 ms phase case both
admitted 1,688 disturbed samples, with worst phase 64.35°. For unseen
additive 41 ms they admitted 5,184 versus 5,240 disturbed samples. For unseen
frequency settling 80 ms neither admitted disturbed samples, but both retained
only 29,288 supported samples and 4/40 windows; seven of eight frozen
intervals were available and covered. The other three profiles had all eight
frozen intervals covered, and no slips were observed. Counts refer to produced
tone output and the physical mask is evaluator-only. These held-out cases show
both a useful reduction in one modeled pulse and clear failures on others.

A separate soft-conditioned current-dwell option, using the preceding Helper
model and its declared uncertainty, accepted 604/664 attempts in the eight
clean tuning cases and fell back 60 times; it supported 38/40 tone windows,
equal to the guarded baseline in that sample. In the 15 ms phase-pulse screen
it retained 3,220 halo-affected samples and had worst admitted clean-sample
phase 52.33°. Neither the soft nor phase-selection option resolves arbitrary
differential phase, cycle ambiguity, changing channels or the physical pulse.
The earlier gap-cycle counterexamples and baseline model limits still apply.

A final-binary eight-key clean test of the separate `soft-rate-v5` option
accepted 604/664 attempts. Its pooled phase RMS was 2.679° versus 2.895° for
baseline, but clean-twin tone-frequency RMS was 0.0771 versus 0.0180 Hz,
supported tone samples 195,125 versus 240,815, and fully supported windows
16 versus 34. All eight frozen intervals covered. Mean receiver CPU through
36 seconds was 0.283 versus 0.179 seconds. In eight retune-mid cases, worst
admitted sample phase was 18.87° versus 7.85°, tone support was 108,228 versus
132,195 samples and full windows 8 versus 19. Common clean-twin frequency RMS
was 0.0576 versus 0.0203 Hz, with no slips. This implementation failed the
overall comparison; these cases do not reject every soft-prior approach.

In eight-key final-guard impairment controls, moderate curvature and noise-only
input qualified no frozen or held-out results. Short gaps supported 40/40
windows for both methods; cycle-gap cases had no frozen result or later
window. Both methods admitted all windows with an out-of-bound 0.8 rad
differential change, with maximum admitted phase errors of 22.521° and 25.039°
for joint-dwell and baseline. Retune-mid supported 21 versus 20 windows, with
worst admitted phase error 54.849° versus 10.932° and four contradictions for
each method. For a step after 36 seconds, all eight pre-step intervals covered,
but neither method supported later windows and each recorded 16 contradictions.
These controls show that the gates do not reliably reject all channel-model
violations.

In the final-guard matched held-out run, process CPU through 36 seconds was
0.321/0.409 seconds mean/maximum for joint-dwell versus 0.202/0.247 for the
guarded baseline; through 40 seconds the means were 0.405 versus 0.269 seconds.
Joint-dwell mean startup, tracking, fallback and conditioning CPU through 40
seconds was 0.0318, 0.3137, 0.00462 and 0.0459 seconds respectively; tracking
includes joint processing and component categories may overlap. These are campaign measurements, not an isolated
full-stream throughput benchmark. The 36-second calendar observation contains
90 settled 0.36-second apertures; the 40-second cost includes only 2.4 seconds
of probe conditioning. Mean 40-second simulation generation was 0.565 seconds
and metrology 0.0046 seconds, excluded from receiver CPU along with clean-twin
and evaluator work. A focused 30-test run passed before the guard correction,
and six focused tests passed again after the final build. An isolated final
binary run of one held-out seed measured joint-dwell versus baseline process
CPU of 0.2162 versus 0.1350 seconds through 36 seconds, and 0.2778 versus
0.1839 seconds through 40. Joint-dwell startup was 0.0220 seconds,
correlations 0.2153 including joint processing, fallback zero, global fit
0.000864, clock 0.00531 and conditioning 0.03383 for 2.4 seconds of signal;
component categories may overlap. Excluded costs were 0.0613 seconds setup,
0.3937 seconds generation for 40 seconds of signal, 0.00325 metrology and
0.03165 evaluator stream. This one case is not a throughput guarantee, and
full-stream conditioning was not measured.

## Piecewise and evolving-clock v6 feasibility

Two test-only options extend the standalone chain; neither is integrated with
ordinary Robust. Both preserve the existing initial relative reference and use
actual previous Helper history, never a probe-derived phase or new per-hop
reference. Expanded-key reservations use tuning seeds 1700000001–1700000008,
HF held-out 1700100001–1700100032, FCC 1700200001–1700200008 and EU
1700300001–1700300008. Freshness is relative to the scanned reservation
inventory, not all unlogged schedules. Conditions retain the 36-second frozen
observation, 38-second lead, probes through 40 seconds, 290 K thermal noise,
10 dB noise figure and independent 40 W Helper plus 40 W probe (80 W combined).

The `piecewise-v6` option scans bounded intervals of 15 ms correlation bins
for common or individual pilot phase and gain disturbances, using prior-only
frequency coordinates. It profiles linear frequency evolution, retains the
gated baseline's factor-two finite-PSD safety allowance, and accounts separately
for the factor of two between complex-correlation variance and an individual
circular-noise quadrature.
When a disturbance is detected, the contaminated full-aperture observation
is not ingested; the candidate either selects a guarded side for phase only
or withdraws phase authority. Support must also pass the shared conditional
guard and actual interpolation halo. Plugin PSD and gain, the selected model,
and declared dynamics make the nominal threshold conditional and uncalibrated;
a missed detection is not evidence of a clean dwell.

Four tuning seeds, not held-out validation, were compared at 180 dB with
matched captures. For the 15 ms phase pulse the candidate detected no events,
supported all 20 later windows and retained 1,840 halo-affected samples; the
worst admitted clean-sample phase error was 51.90°. For the 48 ms phase pulse
it detected eight events, supported 18/20 windows and retained 2,224 affected
samples, with worst phase 86.50°. The earlier phase-selection option in the
same captures supported 16/20 and retained no affected samples in that 48 ms
case; the baseline supported 19/20 and retained 2,297. For high-pilot
additive 48 ms, piecewise detected two events and retained 384 coincident
marked samples, while the earlier phase-selection option retained none. A
common phase pulse physically affects the probe; the high-pilot-only additive
fixture does not add its disturbance to the probe, so its marked samples are
coincident in time. Counts exclude output never produced, and support,
selection and phase-uncertainty calibration remain limitations. No slips were
observed in these finite tuning cases.

The separate `evolve-v6` option refines only the current correction frequency
using the preceding joint clock forecast and one fresh likelihood, with the
forecast evaluated before ingesting the current dwell. It retains the original
phase/time reference, preserves the existing overlapping-history enclosure,
uses conservative L1 uncertainty sums for unknown correlation, limits the
fresh-observation weight and falls back when the model or prediction is
inconsistent. It does not re-ingest a prediction, rewrite the clock history,
or improve the frozen 36-second result retrospectively.

One tuning-seed feasibility run covered its frozen clean frequency at a
0.10485 Hz conditional half-width, so it did not establish ±0.1 Hz narrowing.
The evolving correction and the v5 joint comparator had clean-twin tone RMS
0.0191 and 0.0185 Hz respectively; during a 15 ms physical phase pulse their
worst admitted sample phases were 49.82° and 49.74°. In a retune-mid case
they were 53.86° and 53.77°, and neither supported a later window after the
tested step. These one-seed examples do not establish a general outcome. Both
candidate implementations were rejected after the short falsification checks;
they were not advanced to held-out tests. No claim of physical sensitivity
limits or exhausted alternatives is supported.

The local joint-dwell implementation also has a default-on static window cache
with 86,400 bytes of bounded static data and a dynamic fallback. An opt-in
`cache-pair-v6` comparison runs cached and uncached methods on the same
capture. In a 32-seed held-out comparison between frozen binaries differing
only in this cache, all non-timing outputs were identical. Mean through-36
receiver process CPU changed from 0.34474 to 0.32639 seconds, with maxima
0.42509 and 0.43388; mean joint-component CPU through 40 seconds changed from
0.14457 to 0.12304, and total receiver CPU through 40 seconds from 0.43215 to
0.41388. In an eight-seed screen at 180, 175, 170, 165 and 160 dB, respective
joint-component means changed from 0.11560 to 0.10140, 0.10227 to 0.08643,
0.10146 to 0.08633, 0.10220 to 0.08860 and 0.12347 to 0.10616 seconds.
These runs were concurrent and do not establish a fixed speedup; fixed
within-case method order and allocator/cache effects may affect CPU. The
40-second receiver timing includes only partial held-out probe conditioning.
The durable same-capture selector also verified identical signal output for
one tuning seed, with cache-on/off CPU through 36 seconds 0.27858/0.28762
seconds and joint-component CPU through 40 seconds 0.10019/0.10916. This
one case is not a throughput guarantee. An additional same-binary comparison
on eight fresh held-out seeds had identical signal fields for each pair.
The initial 32 focused checks passed;
after the cache flag and selector change the five directly affected tests
passed again.

An isolated same-binary run on one held-out seed measured cache-on/off process
CPU through 36 seconds of 0.209561/0.214264 seconds and through 40 seconds of
0.266518/0.271020 seconds. For the cached method, startup was 0.021692,
correlations 0.201102 including 0.077952 joint processing, fallback 0.004446,
global fit 0.000909, clock 0.005489 and 2.4-second conditioning 0.032422
seconds. Excluded costs were 0.097374 seconds setup, 0.337163 generation for
40 seconds of signal, 0.004053 metrology and 0.029611 evaluator stream.
The cache method ran first on the shared capture, categories may overlap, and
this single case does not establish standalone full-stream throughput.

On the fresh 32-key HF held-out captures with the existing joint-dwell and
baseline methods, both covered all frozen intervals, but neither had a
conditional half-width at or below 0.1 Hz. Joint versus baseline frequency
point-error RMS was 0.02145 versus 0.02115 Hz, maximum absolute error 0.04864
versus 0.05011 Hz, and median/maximum half-width 0.11999/0.14378 versus
0.12025/0.14893 Hz. Their pooled phase RMS was 2.556° versus 2.805°; median
per-case 99th-percentile phase was 6.199° versus 6.790°, worst phase 9.114°
versus 10.262°, and later-hop phase RMS 2.218° versus 2.554°. Frozen-model
prediction frequency RMS across 160 later hops was 0.02414 versus 0.02377 Hz.
Supported samples were 1,249,893 versus 1,196,530 and fully supported windows
155 versus 138. On 991,635 common tone samples, mean clean-twin phase RMS was
2.242° versus 2.547°. Maximum timing drift was 1.160 versus 1.363 µs and no
slips were observed. Mean clean tone-frequency RMS was 0.01568 versus
0.01522 Hz, noisy-probe RMS 0.10769 versus 0.09741 Hz, and evaluator-only
oracle clean RMS 0.000274 Hz. These compare existing methods, not the
piecewise or evolving candidate, and the frozen 36-second intervals do not
support ±0.1 Hz narrowing at 180 dB in these cases.

In eight fresh HF APSK cases, cached and uncached results were identical.
Joint-dwell versus baseline supported 2,639 versus 2,494 symbols; mean noisy
EVM was 0.5468 versus 0.5495 and clean-twin EVM 0.0421 versus 0.0475. On
2,494 common symbols, noisy EVM was 0.5484 versus 0.5495 and clean-twin EVM
0.0427 versus 0.0475. For eight FCC microwave cases, cache results were again
identical and both methods covered all frozen intervals, but joint-dwell had
30 versus 39 windows with any support, zero versus 17 full windows, and
87,332 versus 211,837 supported samples; median interval half-width was
0.20992 versus 0.19624 Hz. In eight EU microwave cases, cache results were
also identical and both methods covered all frozen intervals without slips.
Joint-dwell versus baseline supported 24 versus 40 windows with any support,
zero versus ten full windows, and 61,305 versus 183,043 samples; median
half-widths were 0.21446 versus 0.20132 Hz and hop RMS 2.790° versus 2.797°.
The evaluator-only APSK true-reference clean EVM was 0.01056. These are
fixture results, not regulatory or hardware qualification.

Fresh eight-key controls per condition with joint-dwell versus baseline gave
no noise-only qualifications. A 0.02 rad differential disturbance supported
40/40 windows with worst admitted phases 6.02° versus 9.06°; an excessive
0.8 rad disturbance also supported 40/40, with worst phases 17.79° versus
17.87°. Curvature qualified no frozen or later output and produced 78
contradictions per method; a whole-cycle gap had no frozen or later output.
A post-36-second step covered all eight frozen intervals but produced no
later output, with 18 contradictions each. Retune-mid supported 14/40 windows
for each, with worst phase 9.51° versus 9.39° and six contradictions each;
its physical event is not marked by the retune mask. These results do not
guarantee detection of arbitrary channel or clock mismatch.

On the same eight keys, a held-out 22 ms physical phase pulse admitted 5,024
marked samples for each method, with worst sample phase 46.37° versus 41.43°;
112,483 versus 137,332 clean samples were withheld. A 29 ms high-pilot-only
additive event had 6,368 versus 6,369 coincident marked samples and worst
phase 6.79° versus 9.54°; the probe itself was not additively contaminated.
For a 44 ms frequency event, neither method admitted marked samples, but
54,176 marked and 322,144 versus 322,143 clean samples were withheld, with
32 contradictions per method. Counts apply only to the produced tone trace;
marked coincidence and withheld clean samples do not establish probe
contamination or usable recovered samples by themselves.

## Reproduction

Build the development profile and the standalone targets before running the
short check and matched cases:

```sh
CCACHE_DISABLE=1 ./build.sh --jobs 2
CCACHE_DISABLE=1 cmake --build build/dev --target test_resampler test_helper_measurement test_helper_measurement_hops test_helper_live_probe test_helper_tracking test_helper_aided_phase test_helper_relative_clock test_helper_ingest test_helper_envelope_feasibility test_helper_joint_estimator test_helper_frequency_recovery test_helper_dual_pilot_phase test_helper_joint_dwell test_helper_soft_joint_dwell test_helper_evolving_clock test_helper_piecewise_dwell test_helper_refinement_study test_helper_continuation_study test_helper_pilot_oracle test_helper_taper_feasibility test_helper_transient_fixture test_helper_pooled_transient_feasibility test_helper_joint_soft_feasibility test_helper_transient_glrt_v4 test_helper_diff_segment_v4 test_helper_joint_split_v4 --parallel 2
build/dev/test_helper_continuation_study --probe
build/dev/test_helper_continuation_study --case 180 901 hf clean tone 40 all
build/dev/test_helper_continuation_study --case 180 901 hf clean apsk 40 all
build/dev/test_helper_continuation_study --case 180 901 microwave-eu clean tone 40 all
build/dev/test_helper_continuation_study --robustness
build/dev/test_helper_continuation_study --case 180 901 hf step tone 40 recovery
build/dev/test_helper_continuation_study --case 180 901 hf scaled-step tone 40 recovery
build/dev/test_helper_continuation_study --recovery-robustness
build/dev/test_helper_continuation_study --case 180 901 hf long-gap tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf gap-clock-cycle tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf gap-step tone 40 gap
build/dev/test_helper_continuation_study --case 180 901 hf step tone 40 recovery-extended
build/dev/test_helper_continuation_study --case 180 901 hf long-gap apsk 40 gap
build/dev/test_helper_continuation_study --case 180 901 microwave-eu long-gap tone 40 gap
build/dev/test_helper_continuation_study --case 180 16777250 hf transient-high-add48 tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1667039233 hf-v3 clean tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1667039233 hf-v3 transient-heldout-phase37 tone 40 phase-pair
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 clean tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690000001 hf-v3 transient-phase15 tone 40 transient-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 transient-v5held-phase31 tone 40 transient-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 differential-large tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 retune-mid tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1690100001 hf-v3 step tone 40 joint-v5
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 transient-phase15 tone 40 piecewise-v6
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 clean tone 40 evolve-v6
build/dev/test_helper_continuation_study --case 180 1700000001 hf-v3 clean tone 40 cache-pair-v6
build/dev/test_helper_continuation_study --case 180 1690000001 hf-v3 clean tone 40 soft-v5
build/dev/test_helper_pooled_transient_feasibility 1666973697 8 phase15 3.5
build/dev/test_helper_joint_soft_feasibility 180 1666973697 100 clean 0 0
build/dev/test_helper_diff_segment_v4 center48 1.35
build/dev/test_helper_transient_glrt_v4 phase15 3.5
build/dev/test_helper_joint_split_v4 1680000001 32
build/dev/test_helper_continuation_study --gap-robustness
build/dev/test_helper_continuation_study --extended-recovery-robustness
build/dev/test_helper_pilot_oracle 180 1 100 apsk
build/dev/test_helper_pilot_oracle 180 1667039233 100 apsk-v3
build/dev/test_helper_pilot_oracle 180 1680100001 100 apsk-v3
build/dev/test_helper_taper_feasibility short 32 901
build/dev/test_helper_taper_feasibility settle-mid 32 933
python3 tests/test_helper_joint_local_feasibility.py
ctest --test-dir build/dev --output-on-failure -R '^(resampler|helper_(measurement|measurement_hops|live_probe|tracking|tracking_robustness|aided_phase|relative_clock|ingest|envelope_feasibility|joint_estimator|frequency_recovery|dual_pilot_phase|joint_dwell|soft_joint_dwell|evolving_clock|piecewise_dwell|refinement_study|refinement_robustness|continuation_study|continuation_recovery|continuation_gap|continuation_extended_recovery|pilot_oracle|taper_feasibility|joint_local_feasibility|transient_fixture|pooled_transient_feasibility|joint_soft_feasibility|transient_glrt_v4|diff_segment_v4|joint_split_v4))$'
```

The case arguments specify path loss in dB, deterministic seed, RF fixture,
impairment, probe waveform, independently specified probe power in watts, and
method set. Valid argument names are determined by the executable. Use the same
seeds and channel parameters when comparing methods, and report unavailable
cases rather than selecting only successes. For example, reproduce the paired
32-seed HF tone comparisons and the eight-seed gap-cycle control with:

```sh
for seed in $(seq 901 932); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf long-gap tone 40 gap
  build/dev/test_helper_continuation_study --case 180 "$seed" hf step tone 40 recovery-extended
done
for seed in $(seq 901 908); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf gap-clock-cycle tone 40 gap
done
for seed in $(seq 1 32); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf clean tone 40 finalists
  build/dev/test_helper_continuation_study --case 180 "$seed" hf clean apsk 40 finalists
done
for seed in $(seq 1690100001 1690100032); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 joint-v5
done
```

The following loops reproduce the final v5 tuning loss screen, held-out
impairment and transient cases, clean APSK and microwave fixtures, and the
soft-rate tuning comparison. Each method pair processes the same capture:

```sh
for loss in 180 175 170 165 160; do
  for seed in $(seq 1690000001 1690000008); do
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 joint-v5
  done
done
for mode in gap gap-clock-cycle differential-large curvature noise retune-mid step; do
  for seed in $(seq 1690100001 1690100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 joint-v5
  done
done
for mode in transient-v5held-phase31 transient-v5held-add41 transient-v5held-frequency80 transient-phase48; do
  for seed in $(seq 1690100001 1690100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 transient-v5
  done
done
for seed in $(seq 1690100001 1690100008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean apsk 40 joint-v5
done
for seed in $(seq 1690200001 1690200008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-v3 clean tone 40 joint-v5
done
for seed in $(seq 1690300001 1690300008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-eu-v3 clean tone 40 joint-v5
done
for mode in clean retune-mid; do
  for seed in $(seq 1690000001 1690000008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 soft-rate-v5
  done
done
```

The v6 frozen-binary timing comparison used separate cached and uncached
binaries. The current executable can reproduce the signal cases and compare
the cache alternatives within the same capture:

```sh
for loss in 180 175 170 165 160; do
  for seed in $(seq 1700000001 1700000008); do
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 joint-v5
    build/dev/test_helper_continuation_study --case "$loss" "$seed" hf-v3 clean tone 40 cache-pair-v6
  done
done
for seed in $(seq 1700100001 1700100032); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 joint-v5
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 cache-pair-v6
done
for mode in gap gap-clock-cycle differential differential-large curvature noise retune-mid step transient-v6held-phase22 transient-v6held-add29 transient-v6held-frequency44; do
  for seed in $(seq 1700100001 1700100008); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 joint-v5
  done
done
for seed in $(seq 1700100001 1700100008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean apsk 40 joint-v5
  build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 clean tone 40 cache-pair-v6
done
for seed in $(seq 1700200001 1700200008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-v3 clean tone 40 joint-v5
done
for seed in $(seq 1700300001 1700300008); do
  build/dev/test_helper_continuation_study --case 180 "$seed" microwave-eu-v3 clean tone 40 joint-v5
done
for mode in clean transient-high-add48 transient-phase15 transient-phase48; do
  for seed in $(seq 1700000001 1700000004); do
    build/dev/test_helper_continuation_study --case 180 "$seed" hf-v3 "$mode" tone 40 piecewise-v6
  done
done
```

Each method pair in a case uses the same impaired capture. The ordinary Robust regression
campaign is outside the scope of this standalone study. The earlier development
build and 17 focused checks passed before the opt-in follow-up. The follow-up
build (`CCACHE_DISABLE=1 ./build.sh --jobs 2`) and all 19 selected focused
checks passed; CTest took 79.69 seconds. After the independent-seed follow-up,
`CCACHE_DISABLE=1 ./build.sh --jobs 2` and all 22 selected focused checks
passed; CTest took 91.48 seconds. After the dwell-v2 fixture changes, the same
uncached main build and all 23 selected focused checks passed; CTest took
87.70 seconds. For v3 the project build and all 25 selected focused checks
passed; CTest took 95.40 seconds. The full ordinary Robust regression campaign
was not run. After the v4 changes, `CCACHE_DISABLE=1 ./build.sh --jobs 2`,
the focused target rebuild and all 28 selected checks passed; CTest took
104.09 seconds. During v6, 32 focused checks passed before the cache flag and
selector change; the five directly affected checks passed again afterward.

The evaluator-only dynamics experiment is not a registered test. One local
Linux build using this development profile and its bundled libraries is:

```sh
g++ -std=c++20 -O3 -DNDEBUG -Wno-subobject-linkage -ffp-contract=off -fno-fast-math -Iinclude -Itests -Ithird_party/xz/src/liblzma/api tests/test_helper_dynamics_benchmark.cpp build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-dynamics-benchmark
/tmp/helper-dynamics-benchmark 903 .001 18
/tmp/helper-dynamics-benchmark 906 .001 18
```

The separate ramp experiment was added after the 22-test run and is not
registered in CTest. It can be reproduced locally with:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_gap_ramp_feasibility.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-gap-ramp
/tmp/helper-gap-ramp 1 8 tone 180 0
/tmp/helper-gap-ramp 1 8 apsk 180 0
```

The waveform-level joint feasibility experiment is also unregistered. One local
Linux reproduction uses:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_joint_wave_feasibility.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-joint-wave
/tmp/helper-joint-wave 180 16777250 100 clean 0
/tmp/helper-joint-wave 180 16777250 100 clean .4
```

The unregistered piecewise diagnostic, with onset relative to the settled
aperture and four dwells per seed, can be reproduced locally with:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_piecewise_feasibility.cpp build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a build/dev/third_party/xz/liblzma.a -lcrypto -lpthread -ldl -o /tmp/robust_piecewise_joint
/tmp/robust_piecewise_joint 6 16777250 .14 .048 1 1.570796 .15 high 4
/tmp/robust_piecewise_joint 6 16777250 .12 .08 1 1.570796 .15 high 4
```

The separate timing-signature experiment has a local Linux reproduction:

```sh
g++ -std=c++20 -O2 -Iinclude -Itests tests/test_helper_timing_signature_experiment.cpp build/dev/CMakeFiles/datapump_audio_native.dir/src/audio.cpp.o build/dev/CMakeFiles/datapump_audio_native.dir/src/audio_policy.cpp.o build/dev/libhelper_joint_dwell_experiment.a build/dev/libdatapump.a build/dev/libdatapump_fast.a build/dev/libdatapump.a -lcrypto build/dev/third_party/xz/liblzma.a -ldl -lpthread -o /tmp/helper-timing-signature-new
/tmp/helper-timing-signature-new 16777250 1 apsk 0
/tmp/helper-timing-signature-new 16777250 1 apsk .02
/tmp/helper-timing-signature-new 16777250 1 apsk 2.717052
```
