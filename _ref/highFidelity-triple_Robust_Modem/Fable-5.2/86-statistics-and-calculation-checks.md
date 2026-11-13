# Independent statistics and arithmetic checks

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md).
- **Purpose.** Record the bounded, independent arithmetic and inference corrections made 13 November 2026. These checks validate formulas and arithmetic only; they are not modem tests, field measurements, or empirical validation of propagation models.
- **Inputs.** Values below are the stated assumptions and rounded table values in [90](90-cross-check-calculations.md). Calculations used short scalar Python with the standard library only; no modem code or historic scratch scripts were run.

## Inference procedure for a proposed conditional screen

A fixed batch of 20 independent, identically distributed complete channel episodes is one illustrative statistical design for an affordable predeclared stratum, not a selected run or a universal sample requirement. The concrete 5/10/20-episode design and the admission gate below supersede that example for the proposed fifteen strata. Sample each episode independently from its stratum's specified distribution. Predeclare the distribution, random-seed generator, episode duration, outcome definitions, invalid-run rules and decision thresholds before inspecting results; retain sampled seeds for reproduction. Fixed seeds alone give a reproducible finite set, not iid sampling for Clopper–Pearson inference. Keep within-episode dependence intact; the independent sampling unit is the complete episode, not its waveform samples. Completion requires the receiver's two scored quiet slots. For an ordinary 95% interval, coverage is marginal for one predeclared stratum/outcome under the iid Bernoulli episode assumption. For a family-wide claim across K predeclared stratum/outcome decisions, use per-decision α=0.05/K Bonferroni intervals (or another predeclared familywise-valid adjustment), including selection of endpoints; do not select favorable results post hoc and report unadjusted intervals as family-wide 95% evidence.

For binary completion, report the exact two-sided Clopper–Pearson interval and decide only when the complete applicable interval lies on one side of the requirement. Otherwise report “inconclusive”; even 20/20 completions give a marginal two-sided 95% lower bound of only 0.832, so n=20 is a bounded screen, not a high-reliability demonstration. For episode-level continuous summaries, list all n observed values and report a fixed-n Student-t interval for the mean only where an approximately normal episode-level sampling distribution is credible; otherwise keep the interval descriptive and state that the chosen small sample does not support that approximation. For a time-series distribution comparison, do not pool dependent samples into an iid KS test or apply a generic effective-N substitution. Either compare episode-level statistics, or calibrate a predeclared statistic with independent null episodes generated under the null while preserving each episode's full dependence structure.

Do not extend a fixed initial n after looking and keep using its fixed-n interval. Any confirmation batch needs its own predeclared error allocation. If operational needs require stopping at arbitrary episode counts, use a confidence sequence with assumptions valid for iid episode outcomes; Howard et al. [Howard 21](60-simulation-methods-and-validation.md) establish time-uniform coverage for their stated nonasymptotic constructions. Their result does not justify treating correlated samples within an episode as independent. A physical receiver failure, service-deadline timeout, or exhaustion of a resource limit included in the service estimand is a failure. Mark an unrelated external interruption invalid only under objective criteria set before the run and independent of its outcome; log the cause and count, and replace only according to that rule. Report censoring and do not count incomplete receiver outcomes as success or omit them from continuous summaries without addressing informative censoring. No procedure here has yet been applied to a modem experiment.

## Scalar arithmetic checks

### 30 MHz groundwave contrast

The 100 km land basic-loss values in [90 §1.2](90-cross-check-calculations.md) range from 175.6 dB (3×10⁻² S/m) to 196.0 dB (10⁻⁴ S/m and the displayed ice row). Direct subtraction gives `196.0 − 175.6 = 20.4 dB`. At 300 km the corresponding land rows span 236.5–257.8 dB (`21.3 dB`). Thus “only a few decibels” is contradicted by the table; matching very-dry-ground and ice values at shown precision does not imply all land classes are close.

### 8.27 kHz illustrative antenna and level/noise

Inputs from [90 §2.1–2.4](90-cross-check-calculations.md): `f=8270 Hz`, `h_e=50 m`, `C=1 nF`, `Q=300`, `R_ground/environment=60 Ω`, transmitter `P=100 W`, range `d=100 km`, `c=299792458 m/s`. Formulas:

```text
λ = c/f
R_r = 160 π² (h_e/λ)²
X_C = 1/(2 π f C)
R_loss = X_C/Q + 60 Ω
I = sqrt(P/(R_r + R_loss))
P_radiated = I² R_r
V_antenna = I X_C
E_100km = 120 π I h_e/(λ d)
```

Scalar results: `λ=36250.60 m`, `R_r=0.0030042 Ω`, `X_C=19244.85 Ω`, `R_loss=124.1495 Ω`, `I=0.89747 A`, `P_radiated=0.0024198 W=2.420 mW`, `V=17.272 kV`, and `E=4.6667 µV/m` (`13.38 dBµV/m`). These round to the 2.4 mW, 17 kV and 4.7 µV/m entries. They remain conditional on the assumed antenna and loss model.

For a link-level cross-check, `C/N₀ = P_rx[dBm] − N₀[dBm/Hz]`, `P_rx=P_tx−L`, and thermal `N₀=−174 dBm/Hz + 10 log₁₀(T/290 K)`. With `P_tx=40 W=46.0206 dBm`, `L=200 dB`, and GUI density `−164 dBm/Hz`, `C/N₀=10.0206 dB-Hz`, matching the rounded 10 dB-Hz statement. This is an input/output arithmetic check, not an assertion that the GUI's generic path-loss value describes a particular physical path.

### 5.760 GHz illustrative EME link

Inputs from [90 §3.1, §3.4](90-cross-check-calculations.md): `f=5.760 GHz`, geocentric distance `D=384400 km`, lunar radius `a=1737.4 km`, radar albedo `ρ=0.065`, dish diameter `d=3 m`, aperture efficiency `η=0.55`, transmitter `40 W`, and `c=299792458 m/s`. Formulas:

```text
λ = c/f
σ_moon = ρ π a²
L = (4π)³ D⁴/(λ² σ_moon)
G_dBi = 10 log₁₀(η (π d/λ)²)
L_net = 10 log₁₀(L) − 2 G_dBi
P_tx,dBm = 10 log₁₀(40·1000)
N₀,dBm/Hz = −174 + 10 log₁₀(T/290 K)
C/N₀ = P_tx,dBm − L_net − N₀,dBm/Hz
```

Results: free-space radar-equation loss `284.141 dB`, dish gain `42.561 dBi`, net loss `199.019 dB`, and `P_tx=46.021 dBm`. Therefore `C/N₀=28.64, 26.59, 23.86 dB-Hz` at `T=50, 80, 150 K` respectively (rounding to 28.6, 26.6, 23.9 in the chapter). The no-antenna-gain, 200 dB GUI example gives `10.02 dB-Hz` against `−164 dBm/Hz`. Assumed albedo, temperature and beam treatment remain model limitations.

### Width, correlation and coherent integration

For Gaussian Doppler with `2σ=B`, `S(f)∝exp(−f²/(2σ²))`: `FWHM=2√(2 ln 2)σ=1.17741 B`, and the time correlation `R(t)=exp(−2π²σ²t²)` has `1/e` time `√2/(πB)=0.45016/B`. For Lorentzian Doppler with full width `Γ`, `R(t)=exp(−πΓ|t|)`, giving `1/e=1/(πΓ)=0.31831/Γ`. For two independent equal-power paths with delay difference `τ`, the frequency-correlation magnitude is `|cos(πτ Δf)|`; its first 0.5 crossing is `Δf=1/(3τ)`, first zero `1/(2τ)`, and notch spacing `1/τ`. These reproduce the conversion formulas in [90 §6](90-cross-check-calculations.md).

The Gaussian diffuse-path mean coherent energy ratio over a rectangular integration time `T` is evaluated as `∫ exp(−f²/(2σ²))/(√(2π)σ) · sinc²(πfT) df`, where `sinc(x)=sin(x)/x` and `σT=BT/2`. Direct scalar Simpson quadrature gives:

| `BT` | 0.5 | 1 | 1.2 | 1.3 | 2 | 3 | 5 | 10 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Energy ratio | 0.8358 | 0.5954 | 0.5242 | 0.4939 | 0.3483 | 0.2434 | 0.1515 | 0.07776 |
| Loss (dB) | 0.779 | 2.252 | 2.805 | 3.064 | 4.581 | 6.136 | 8.197 | 11.092 |

These independently reproduce the rounded coherent-integration table in [90 §6](90-cross-check-calculations.md), including its approximately 3 dB point near `BT=1.3`. Numerical agreement checks the integral and conversion, not a receiver's detection probability.

The standard-library scalar quadrature used for this check is reproducible as:

```python
import math

def ratio(bt, n=200_000):
    s = bt / 2
    hi = max(12 * s, 10.0)
    n += n % 2
    h = 2 * hi / n
    def sinc(z): return 1.0 if z == 0 else math.sin(z) / z
    def g(x):
        return math.exp(-x*x/(2*s*s)) * sinc(math.pi*x)**2 / (math.sqrt(2*math.pi)*s)
    total = g(-hi) + g(hi)
    for i in range(1, n):
        total += (4 if i % 2 else 2) * g(-hi + i*h)
    return total * h / 3

for bt in (0.5, 1, 1.2, 1.3, 2, 3, 5, 10):
    e = ratio(bt)
    print(bt, e, -10 * math.log10(e))
```
## Finite experiment inventory, endpoints and stopping

The twenty-episode rule above is a **choice for an initial inexpensive stratum**, not a requirement to run twenty multi-hour episodes in every family. The following bounded probability-screen campaign is a later research specification, separate from 00’s twenty fixed diagnostic roots. Those fixed roots are a reproducible control set and carry no iid confidence claim. It is not executed here and is not authorization for intensive runs. First implement/verify the generator controls; then freeze the canonical native waveform, source payload, public calendar, keyed starting ordinal, sample rate and resource manifest before receiver trials. If those inputs are absent, report the corresponding experiment unstarted, rather than substituting a shorter receiver or claiming zero failures.

The following IDs freeze **one** member of each existing finite case set. They are not a Cartesian product of every sensitivity coordinate. All use the complete canonical `A` source, fixed calendar and sample-rate/containment rules in 00 and the owning chapter. Unlisted effects are off. For 81 the magnetic reference plane and its own external/self-noise law apply; for 82 use its stated `Fa`/`Te` Gaussian terms; for 83 use its stated terminal Gaussian law; for 84 use its stated total on-Moon temperature. **85 C1/C2/C3/C4 are off in every row**: no impulses, blockers, nonlinear front end, ADC or clock disturbance is implicitly added. Its C0 power/noise accounting is a convention cross-check, not an extra noise source.

| ID / family | Exact frozen conditional case | Fixed episode budget |
| --- | --- | ---: |
| V1 / sub-9 kHz | 81 A0: 2.47 fT wanted amplitude, 50 fT/√Hz external noise, 5 fT/√Hz receiver noise, unit transfer | 5 |
| V2 / sub-9 kHz | Same amplitudes as V1; 81 B1 plus A1 with Φ=π/2, original B1 start 2025-03-20 11:45 UTC | 5 |
| V3 / sub-9 kHz | V2 with only the B1 window translated +6 h, the explicit dependence discriminator in 81; A1 phase clock unchanged | 5 |
| G1 / 1 MHz | 82 **1M-E(A)**: 1M-A's 100 km, εr=15, σ=.01 S/m, static phase/delay; replace calculated surface magnitude once by C/N₀=20 dB-Hz under 1M-E | 10 |
| G2 / 1 MHz | 82 **1M-E(D)**: 200 km reference, C/N₀=20 dB-Hz before adding the night return; R=0 dB, φ₀=π, 276.333544 µs excess; enable D's 600 s onset at active slot 3 | 10 |
| G3 / 1 MHz | G2 with only φ₀=0; same prescribed onset, delay, frequency/phase law and reference level | 10 |
| U1 / 30 MHz | 82 **30M-E(A)**: 100 km, 2 m heights, σ=5 S/m, εr=80, Nₛ=301.01990148; use E's normalized surface C/N₀=20 dB-Hz, static phase and delay | 10 |
| U2 / 30 MHz | 82 **30M-E(D(A))**: U1 plus D's R=0 dB, 5 ms/+1 Hz delayed wanted return, phase zero, specified slot-3/slot-9 onset/offset ramps | 10 |
| U3 / 30 MHz | U1 multiplied by **30M-B** with (A,B)=(20 dB,π rad), and the explicit π/2 shift of its phase sine; all competing returns off | 10 |
| H1 / skywave | 83 **F-MQ, G**: independent half-power taps at 0/.5 ms, D=.1 Hz, zero centroids, Cref/N₀=20 dB-Hz, fc=10 MHz | 20 |
| H2 / skywave | 83 **F-HD, G**: independent half-power taps at 0/7 ms, D=30 Hz, zero centroids; same level/carrier as H1 | 20 |
| H3 / skywave | 83 **F-HD, L**: H2's delays/powers, replacing each G gain by independent stationary OU with T_c=√2/(π·30)=.01500527194 s, matching **1/e correlation time**; no finite Lorentzian RMS width is asserted | 20 |
| E1 / EME | 84 **T52+MEAN+M**, exact §“Frozen inputs” geometry-acquisition rule and named kernel set, 40 W, 3 m dishes, .05° pointing offsets, η=.065, total Tsys=80 K | 20 |
| E2 / EME | 84 **T52+MEAN+SC**: E1 with η=.040, total Tsys=160 K, both offsets .30°, and the declared SW 6 dB smooth attenuation over source slots 6–7 | 20 |
| E3 / EME | 84 **H30+MEAN+M**: E1 with only the normalized .70 Hagfors(C=44)+.30 cos^1.5θ kernel replacing T52 | 20 |

G/U cases intentionally use the already specified normalized-level branch; their conditional receiver endpoint does not require trusting an unexecuted LFMF magnitude calculation. They do **not** qualify physical groundwave range or power. The separate physical-magnitude and empirical branches retain their solver/calibration gates. H2 is the stationary F-HD comparator, **not** the different S4-H birth/death transition. H1–H3 have slow gain, coherent fraction, microtap extension, moving delay and outage schedules off. E1–E3 retain 84's oscillator, polarization, finite scattering grid/filter and retarded geometry rules, including its fixed 1,800 s post-epoch public observation horizon and two-slot preroll; setup must prove that horizon covers 00's required final quiet opportunities. If not, the fixed EME fixture is unavailable, not silently lengthened.

This is 15 decisions and at most **195 complete receiver episodes**, not 15 draws from a natural population. For each row, the declared parameter/event trajectory is fixed; randomization is only over the explicitly specified Gaussian/noise/scatterer innovations. Thus the estimand is completion probability **conditional on that chosen mathematical scenario**. For the proposed model trial, draw each 64-bit root independently and uniformly with replacement from a recorded operating-system random source, then use 00’s declared engine/transforms; retain even a coincidental repeated root rather than condition the sample on uniqueness. Reuse corresponding roots across model alternatives for paired diagnostics, while keeping episode draws independent within each stratum. This is an executable pseudorandom approximation to the specified innovation law, not proof of a generator’s mathematical independence. An input table that gives only a median, percentile or range is not a sampling distribution. Empirical branches requiring an unavailable trace or calibration receive zero episodes and remain unavailable; no replacement synthetic stratum silently closes that empirical gate. The budgets are computational screening limits, not power calculations proving adequate reliability. A failed prerequisite ends that stratum before receiver assertions and does not erase earlier generator checks.

The primary binary endpoint is **exact source-message recovery with valid physical completion by the declared horizon and within all service resources**. Failure includes missed/incorrect Helper acquisition, wrong decoded content, premature completion followed by resumed source data, missed final completion, deadline exceedance, receiver memory/worker admission failure, and invalid output. Also retain disjoint failure reasons, bit/null/erasure decisions, false candidates and time-to-completion; do not collapse them into “no decode.” An unrelated external interruption is invalid only by the predeclared objective rule above. No run silently disappears from the denominator.

Define the horizon from public geometry and evaluator-only source truth, before generating noise: let `t_end` be the final nonzero source sample; let `D_max` and `T_tail` be the declared propagation support and filter-tail allowance under 60's truncation contract. Let `q0` be the first configured public slot whose **entire scoring input window begins after `t_end+D_max+T_tail`**. Render and score through the complete input window of slot `q0+2`, including any native lookahead. That supplies three final quiet opportunities, with two required for completion and one fixed margin. All state/noise continues. The receiver receives neither `t_end`, `q0` nor expected payload length. A completion earlier than the last true payload is premature even when its two quiet decisions are internally valid. End of the observation horizon is a timeout, not a completion event. The manifest records these exact timestamps; a delayed tail that reaches them forces the support/horizon contract to be corrected before a new campaign, not truncated away after an unfavorable result.

### Admission budget before any whole-message campaign

**195 is a maximum statistical design, not a selected execution plan or current authorization.** The physical-duration burden is substantial: even the thirteen sub-9 kHz payload slots plus only two final quiet slots at the inspected 6,750 s recurrence span 28.125 h per episode; fifteen such episodes span **421.875 h of simulated time**, before the third quiet margin, preroll and any extra tails. The exact returned scoring windows determine the larger actual sample count. Simulated duration alone proves neither feasible wall time nor real-time capacity. A literal EME evaluation with 256×512 surface cells at 48,000 samples/s for 1,800 s requires **11,324,620,800,000 cell–sample products per episode**, excluding preroll, geometry, filtering and receiver work. A lower-cost native realization must first preserve the declared operator and numerical error bounds; this count is not a forecast of an optimized implementation.

A concrete **future research pilot proposal** is limited to **10 minutes elapsed on one exact machine**, after its native build/input prerequisites are ready. Select H1 as the single component stratum; first run cheap deterministic source/generator checks, then one bounded native component cost measurement with at most 60 seconds of timed work and one abbreviated holdout. A component-only check does not qualify a complete receiver. Do not start a long message merely because a component is fast. If the pilot cannot finish within its budget, retain its measured partial coverage and stop; no automatic extension or extra hardware spending is implied.

Before admitting any whole-message batch, produce an upper wall-time and peak-memory forecast for **every** selected episode using its exact integer sample count `N=stop_sample−start_sample`, full source/propagation/filter support, public hypotheses, unsuccessful acquisitions, tracking, quiet scoring, preparation and native scheduling. Use applicable current native complete-work calibration and abbreviated holdouts under the maintained project procedure; a generator timing alone is insufficient. Charge startup/finish and recurring work separately, sum the per-stratum upper wall estimates over the entire proposed batch, and include fixed control runs. If applicable calibration is absent, report the batch unadmitted. The first campaign may be admitted only when this **whole-campaign upper wall-time estimate is at most 30 minutes** on that exact machine, including control and scoring work. These 10/30-minute limits are explicit research-proposal budgets, not claims about hardware capability or user authorization. A larger or different study requires a new bounded design; never default to days of processing.

Fix the actual OS hardware allowance H, automatic `W=2H`, owner limit, permitted arithmetic/backend, native memory admission and explicit user resource limits before measurement. Preserve the full W ceiling and every public hypothesis. Forecast memory from the application baseline, started stacks, live owners and full DSP bounds, source/input assembly, delay/filter/generator state and retained results over their actual lifetimes. Use the exact machine's available/admitted memory and explicit allocation; do not invent a portable 2 GiB cap or reduce W to fit an arbitrary research cap. Stream samples when the native ownership contract permits; retaining an entire multi-hour capture is not a default requirement. Bound temporary output to the necessary configuration, seeds, summaries and identified diagnostic excerpts, with a deletion trigger after the findings are distilled.

A stratum that cannot fit these admission bounds is **unrun/inconclusive**, with zero receiver assertions; it is not a success or a binomial failure. If a running, admitted service trial exceeds the deadline/resources included in its estimand, that episode is a failure under the rule above. If the global pilot/campaign budget ends before the predeclared fixed n, report the observed count and incomplete design; do not quietly drop missing episodes, reuse a fixed-n success claim or increase the budget. The episode limits concern communication outcomes; component checks and capacity measurements retain their distinct estimands. Wall time from communication trials is descriptive unless the maintained complete-receiver capacity procedure is separately satisfied.

For this exact 15-decision campaign, familywise α=0.05 means each two-sided CP interval uses α=0.05/15. A provisional engineering screening requirement `p≥0.8` may be used for the conditional model, with pass only if its adjusted lower limit exceeds 0.8, fail if its upper limit is below 0.8, and otherwise inconclusive. This chosen threshold is not a user requirement or a claim about real-world service. Even 20/20 has adjusted lower bound 0.7263, so this budget generally diagnoses failures and leaves successful cases inconclusive. If a different service target is required, freeze it and justify a new independent confirmation budget before collecting that batch; do not retroactively increase n until a pass. Stop at the fixed n, or at a technical setup failure with incomplete coverage clearly reported. No repeated significance tests or optional early success stop is allowed.

Add fixed, deliberately hand-picked **Off, exact-zero channel, in-message outage followed by resumption, and delayed-tail-through-source-silence** controls for each family before randomized trials. Their outcomes are deterministic conformance evidence at those inputs; they receive no binomial interval or occurrence probability. At B=0 distinguish a fixed unit gain from a frozen random Gaussian gain. This prevents a fixed synthetic “worst case,” or multiple timing replicas of one channel record, from being mislabeled iid natural episodes.

## Expanded arithmetic coverage and independently reproducible recipes

The follow-through scalar check used Python 3 standard-library `math`, `re`, `pathlib` and `statistics` on 13 November 2026. It generated no channel waveform, receiver state or modem implementation. It did not rerun the absent historical scratch programs. The coverage boundary is:

| Chapter 90 quantity | Independent check and result | Limit |
| --- | --- | --- |
| §1.2 field/loss pairs | All 144 displayed pairs satisfy `E+Lb=142+20log10(f_MHz)` to≤0.0795 dB, within the 0.1 dB combined rounding interval | Fields themselves and the propagation solver were not reproduced |
| §1.3–1.5 solver-dependent ranges, radius/height/phase/selectivity and GUI-equivalent distance | NOT independently reproduced; only field/loss units and the 20.4/21.3 dB contrasts were checked | Historical residue-series code is absent. No selected scenario may rely on these as independently verified predictions; obtain official LFMF vectors/source and a complex-field phase reference first |
| §2 bare antenna capacitance | Radii 1 mm/5 mm/0.3m give 338.5273/403.3232/786.0935 pF usingε0=8.8541878128×10⁻¹²F/m,h=60.96m | Electrostatic approximation; does not verify the separate top-loaded wire solver in 11 |
| §2.2 electrical parameters | All seven frequency rows were independently evaluated for wavelength, radiation resistance, reactance and resonating inductance; e.g.8.27 kHz requires 1.089307H with 340 pF,76 Hz requires 12898.35H | These are ideal component values, not feasible hardware specifications |
| §2.3 antenna operating cases | All five 100W cases independently recomputed below;1kW results follow exactly by current/voltage/field×√10, radiated power×10, unchanged efficiency/Q | Q300 and 60 Ω are assumed, not the distinct Q400/S2 physical model in 81 |
| §2.3 voltage ceiling |30 kV gives bare 8.27 kHz I=.530012A,Pr=.314022 mW; top-loaded I=1.558858A,Pr=7.300332 mW | Voltage is treated as the same RMS/phasor convention as IX; a physical peak/corona ceiling needs the appropriate√2 conversion and measured geometry |
| §2.3 detuning | At fixed L, `dln f0=−½dln C`;1% change gives magnitude 34.99°,57.17°,63.43° at Q70/155/200 | Corrected the sign of resonance-frequency shift in 90; phase sign depends on transfer convention |
| §3.1 all 24 EME loss cells | Independent radar equation reproduces all rounded 0.1 dB values; mean 5.76 GHz 284.141 dB | Albedo remains assumed; range convention is geocentric here |
| §3.2 geometry | Overhead delays 2.323551/2.510347/2.659117 s; limb depth 11.590685 ms; dimensional speckle 5.757736m; equatorial rotational rate 1.301806 Hz/s | These scalar geometries are not the later ephemeris-driven bistatic calculation |
| §3.3 illustrative kernels | Eight laws' mean delay, RMS delay about the mean,2×RMS Doppler/LL, and five cumulative-delay fractions independently integrated; six beam relative powers/mean delays checked below | Original FWHM/−10 dB/50–90%-Doppler-width columns were not independently recomputed and are not selected acceptance inputs |
| §3.5 Faraday rotation | All five frequency rows independently evaluated; at 5.76 GHz/10 TECU the result is.00284529rad | The stated single-path B∥/TEC assumptions remain; no ionosphere or polarization measurement |
| §3.4 dishes and power | All seven gain/HPBW pairs reproduce rounding; 40W two 3m budgets and 15kW Helper arithmetic checked | Total on-Moon Tsys includes lunar noise once; no station calibration |
| §4 noise | All ten Fa/N0/E/B rows independently recomputed from equations | E=cB assumes a propagating plane wave; it does not establish local E/H impedance or actual antenna noise |
| §5 mirror paths | All 42 delay differences independently recomputed using spherical chords below, within 0.005 ms of shown two-decimal entries | Fixed geometry, not ionospheric group-delay or path-visibility validation |
| §6 width, coherence, integration and timing | All seven Gaussian/Lorentz width definitions, seven two-path correlation rows, eleven coherent-integration losses and five timing rows checked algebraically/scalarly | Model-specific quantities, not communication probabilities |
| Selected 84 eleven-node lunar law | Independently integrated from its explicitly provided nodes and wavelength rule; results below | Numerical replication of a forecast law; no whole-disc 5.76 GHz measurements |
| 60's proposed finite FIR covariance |2,064 analytical comparisons at fractional positions 0,¼,½,¾ on both ends and integer lags 0…128 gave maximum absolute covariance error 0.000602031 (midpoint variance 0.999397970) | Scalar coefficient/covariance calculation only; native implementation, interpolation and receiver sensitivity remain untested |

Corrected interpretation, not arithmetic failure: a total **system** temperature cannot be followed by an additional undifferentiated “Moon noise” penalty. Likewise a speckle dimensional scale does not prove independent fading of separated apertures, and the GUI's synthetic 1 mW input does not uniquely define antenna e.m.r.p. These corrections are now in 90.

### Antenna table reconstruction

Use the formulas above with `(f,h_e,C)` equal to `(8270,30.5,340 pF)`, `(8270,50,1 nF)`, `(2970,30.5,340 pF)`, `(2970,50,1 nF)`, `(1000,50,1 nF)`. At 100W:

| f / antenna | RlossΩ | I A | V kV | Pr mW | E at 100 km µV/m | Loaded Q | BW Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
|8270/bare|248.6750|.634137|35.8937|.449526|2.01140|227.6154|36.3332|
|8270/top|124.1495|.897475|17.2718|2.419768|4.66668|155.0098|53.3515|
|2970/bare|585.3679|.413319|65.1434|.024630|.470817|269.2500|11.0306|
|2970/top|238.6251|.647354|34.6901|.162373|1.208867|224.5675|13.2254|
|1000/top|590.5165|.411513|65.4944|.007439|.258740|269.5182|3.7103|

Here `Q_loaded=X_C/(Rloss+Rr)` and `BW=f/Q_loaded`. The original 100 km noise comparison uses unrounded antenna fields and P.372 Fa values 147.8/174.7 dB, so reusing rounded 2.0/4.7 µV/m need not reproduce its tenths exactly. Stanford mean-envelope noise remains a different estimator from RMS, independent of arithmetic agreement. The separate chapter 11 antenna loss model adds dielectric loss and has different C/Q/ground inputs; its 2 mW estimate must not be inferred from this simplified 2.42 mW control.

### Chapter 11 / 81 T-antenna input check

Independently recomputing the distinct selected T inputs `C=902 pF`, `h_e=49.8 m`, `f=8270 Hz`, and `R_loss=R_ground+X_C/Q_coil+X_C tanδ` gives `R_r=.002980218 Ω`, `X_C=21335.7584 Ω`, `L=.41060356 H`. For S2 `(Q_coil=400,R_ground=30 Ω,tanδ=.003)`, the aggregate loss is147.346671 Ω; at100 W accepted input, `I_rms=.82380694 A`, `V_rms=17.576546 kV`, `P_rad=2.022548 mW`, efficiency−46.94101 dB, loaded Q144.79680 and bandwidth57.11452 Hz. This independently reproduces the2.0 mW input used in81. Omitting dielectric loss would be a different model.

At29 kV **RMS** the same C/he gives5.505894 mW. S1 at an unconstrained100 W would require30.90047 kV RMS and radiate6.25118 mW, so applying the declared29 kV ceiling is necessary to obtain its5.5 mW entry. S3/S4 at100 W give.664184/.252067 mW. No computation here validates the MoM-derived capacitance/effective height, conductor construction, extrapolated corona onset or the natural environment; these remain model inputs, not measured installation facts.

### Independent spherical geometry and noise recipes

For spherical-earth radiusR=6371000m, great-circle distance d, layer height h and n equal hops, the imposed geometric length is

```text
L(d,h,n)=2n sqrt[h²+4R(R+h) sin²(d/(4nR))].
Delay difference in ms = 1000·(L2−L1)/299792458.
```

For example the 3000 km row gives `.4035,.3140,.5785,1.4576,2.5862,3.9065,.3192 ms` in the chapter's column order. For 50 km it gives `1.8419,1.2558,1.9978,3.9979,5.9987,7.9997,.9976 ms`. This checks materially different short/long geometries while preserving their physical limitations.

For each noise row compute `e_dB=Fa+20log10(f_Hz/10⁶)−95.5`, `e_µV=10^(e_dB/20)` and `b_fT=e_µV·10⁹/c`; `N0=−174+Fa`. This reproduces, for example,8.27 kHz/Fa 150 as 4.390414 µV/m/√Hz and 14.644846 fT/√Hz, and 30 MHz/Fa 18 as.00400056 µV/m/√Hz and.0133444 fT/√Hz. The rounded−174 constant is a convention; exact Boltzmann 290K gives approximately−173.975 dBm/Hz, a 0.025 dB change that must not be mistaken for model disagreement.

### Lunar normalization and quadrature

Use `u=cosθ`, so surface area is `dA=2πa²du` and excess delay is `τ=K(1−u)`, `K=2a/c=11.59068518 ms`. For the old illustrative law define

```text
H_C(u)=(C/2)[u⁴+C(1−u²)]^(−3/2)
q(u)=(1−f_d)H_C(u)/∫₀¹H_C(v)dv + f_d(n+1)u^n.
∫₀¹q(u)du=1; Eτ=∫qτdu; στ=sqrt[∫q(τ−Eτ)²du].
(2σ_Doppler/LL)²=½∫q(u)(1−u²)du.
P(τ≤t)=∫_(max(0,1−t/K))^1 q(u)du.
```

The eight `(C,f_d,n)` rows in 90 give mean/RMS delay respectively: `(1.11710,2.15838)`, `(1.07545,2.16386)`, `(1.52268,2.40093)`, `(1.78337,2.52014)`, `(1.92135,2.67819)`, `(2.37475,2.82151)`, `(2.71832,2.87047)`, `(2.98007,2.90726)ms`. Their 2σ/LL values are `.272758,.266560,.318258,.344075,.354023,.392768,.419951,.438861`. This resolves “rms delay” here as standard deviation about the mean, not raw root-mean-square excess delay.

For centered identical Gaussian power beams, multiply q by `b(u)=exp[−8ln 2(asin(a√(1−u²)/D)/β)²]`, withβ in radians andβ=`(70λ/dish_m)π/180`. The **power** factor is `P_b=∫qbdu`; use qb/P_b only to compute the *conditional shape*, while retaining P_b in the absolute link budget. For dishes 3,5,7.3,10,25,64m this gives beam powers `−.32445,−.83925,−1.58397,−2.47537,−5.80046,−10.85481 dB` and mean delays `2.16309,1.82863,1.36282,.87570,.16524,.03826 ms`. The old table's 0.87/0.16 ms entries were truncated;90 now uses nearest rounding .88/.17. The difference does not change its qualitative comparison.

For the selected 84 eleven-node measured-law interpolation, use its listed 3.8/23cm dB values. Atλ=.05204730174m the log-wavelength weight is `.1747114641`; interpolate dB between wavelengths, then power linearly inθ, holding the first value toθ=0 and tapering linearly from 87° tozero at 90°. Integrating in u gives raw `∫qdu=.03440778`, mean 1.709519 ms, RMS-about-mean 2.182632 ms, t50=.707955 ms and t90=5.086946 ms. Composite Simpson 20,000→40,000 intervals changes the raw integral by 3.46×10⁻⁸ and t90 by.000023 ms; this convergence result is much smaller than source angular/wavelength-model uncertainty. Normalize total cross section to `.065πa²`; do not mistake the raw integral for measured radar albedo and do not normalize individual speckle realizations.

The one-dimensional Simpson recipe is:

```python
import math

def integral(f, n=40_000):  # integral over u in [0,1], n even
    assert n > 0 and n % 2 == 0
    return (f(0) + f(1) + sum((4 if k % 2 else 2)*f(k/n)
                             for k in range(1, n))) / (3*n)

# Insert exactly one q from the equations or node table above.
# Compare n and 2*n. Delay quantile t solves
# integral(lambda u: q(1-u*t/K))*t/K / integral(q) == probability
# for 0<=t<=K, e.g. by 35 bisection steps.
```

### Closed-form integration and finite-record power states

The earlier spectral Simpson integral has an independent time-domain closed form. With `a=π²(BT)²/2`,

```text
E_coherent = √π·erf(√a)/√a − (1−exp(−a))/a,
```

using its limit 1 atBT=0 and `expm1`/a small-a series to avoid cancellation. It reproduces all eleven chapter 90 losses (including 0.0355 dB atBT=.1 and 3.5472 dB atBT=1.5). For the time average of **power** of a proper unit-variance Gaussian process, `Cov(|g(t)|²,|g(s)|²)=|R(t−s)|²`. The finite variance-equivalent number of power samples is therefore `N_eff=1/E_coherent(√2 BT)`, giving 1.01639,2.16044,18.04867,177.56427 atBT=.1,1,10,100. The asymptote 1.77245BT applies only at largeBT; this N_eff does not calibrate an arbitrary KS statistic or convert one episode into iid Bernoulli trials.

A centered constant chirp with phase`π a_dot t²` has coherent-power loss `|∫_(-1/2)^(1/2) exp(jπ a_dot T² u²)du|²`. At `a_dot T²=2`, scalar Simpson quadrature gives energy.800304796 and loss.9674458 dB. Hence 2/T² is an approximately 1 dB drift guide for a centered rectangular integration, not a universal tracking threshold. All five manual-row timing conversions in 90 are arithmetic guides; they do not prove what the adaptive native receiver integrates coherently.

### Exact interval reconstruction

For x successes out of n independent episodes, the lower CP bound solves `P_p(X≥x)=α/2` and the upper solves `P_p(X≤x)=α/2`; use0 and1 at the respective empty/all-success endpoints. Direct binomial summation and70 bisection steps give these checks:

| n / successes | Marginal95% interval | 15-decision adjusted interval |
| --- | --- | --- |
|5/0|[0,.521824]|[0,.721792]|
|5/5|[.478176,1]|[.278208,1]|
|10/5|[.187086,.812914]|[.100423,.899577]|
|10/10|[.691503,1]|[.527454,1]|
|20/0|[0,.168433]|[0,.273739]|
|20/10|[.271958,.728042]|[.189099,.810901]|
|20/20|[.831567,1]|[.726261,1]|

These endpoint and middle-count examples check the interval formula rather than just its favorable all-success case. The adjustment does not require independence between the15 decisions; independence is required across episodes for each binomial model. Paired alternative-model results can therefore share seeds without invalidating this Bonferroni coverage, but an adaptively chosen subset of trajectories is a different estimand.

## Checks of the finalized scenario controls

These additional scalar checks bind to the explicit mathematical inputs in81,82 and85 as inspected on13 November2026; they do not certify an implementation.

- **81 detuning and noise.** With fixed coil L/R and dielectric loss `tanδ/(ωC(t))`, capacitance changes +0.1%/−0.1% give current-gain changes−.08624236/−.09399028dB, phase−8.23439076/+8.24358291° and resonance shifts−4.13190133/+4.13810384Hz. The RLC envelope time constant `2L/R=.00557318667s` leaves amplitude2.59932×10⁻¹⁶ after.2s in the unforced linear model. Comparing2.47fT signal with20/50fT/√Hz gives−18.16666085/−26.12546102dB-Hz; adding5fT/√Hz independent self-noise in quadrature gives−18.42995024/−26.16867476dB-Hz. The field amplitude is a chosen control input.
- **82 ground controls.** The refractivity inversion `ln(.25/.04665)/.005577=301.01990148` reproduces its4/3-earth fixture. Two2m antennas on effective radius`(4/3)×6370km` have summed geometric horizon11.65733028km. Its **flat**100km-high midpoint mirror over200km has excess`[2hypot(100km,100km)−200km]/c=276.33354430µs`; this differs from90’s spherical mirror by design. The1km knife edge18m above line of sight at the midpoint gives`v=.5092930791` at30MHz. `N0=k[290×10^(Fa/10)+100]` yields−93.97518718/−155.95151698dBm/Hz forFa80/18. The stated Fresnel normalization givesD(0)=½ andD(−∞)=1. The TM-layer impedance reduces algebraically toZ2 for zero thickness andZ1 for identical media; scalar complex counterexamples confirmed these limits to roundoff. No LFMF field/phase calculation was executed.
- **85 white-noise plane and filter.** AtFs48000,−164dBm/Hz is3.981071705535×10⁻²⁰W/Hz. Complex white sample power is1.910914418657×10⁻¹⁵W, equally split between I/Q. For`a=exp(−2π3000/Fs)=.675231906656`, ENBW`Fs(1−a)/(1+a)=9305.4987901Hz`, hence output power3.704585793916×10⁻¹⁶W. This follows independently from the geometric impulse-energy sum `(1−a)² sum a^(2n)`. It is the full-complex-interval convention, without an extra factor2.
- **85 pulse energy and clustering.** The240-sample double exponential has raw discrete energy.000184079963198 in the stated units; normalization factor.0135676071287 gives energy1.0000000000000002. Atρ=1,`Jbar=2.388643023321×10⁻¹⁶J`, `J0=1.200323127297×10⁻¹⁶J`, and`.99J0+.01(100J0)=1.99J0=Jbar`. Both8/s independent children and2/s parents×4 children therefore give mean power`8Jbar=ρN0Fs`; uniform independent phases cancel the expected overlap cross terms. Atρ=10 all energies/powers scale by10. This identity verifies the constructed ensemble mean, not a realized finite-record energy or natural sferic rate.
- **85 passive accounting and ADC.** At the default external density, output-referred density is`N0/2+k·145+k·100=2.328794857767×10⁻²⁰W/Hz` (−166.32868767dBm/Hz). With external20K it is`k·255=3.52065495×10⁻²¹W/Hz` (−174.53376537dBm/Hz). Wanted power also halves. If holding the original wantedC fixed, these give19.31838771/27.52346541dB-Hz respectively. ADC full scale is.001√W per component and step3.0517578125×10⁻⁸√W. These values do not qualify nonlinear recovery, quantization error statistics or a particular receiver.

No native receiver, random waveform campaign or hardware check was run. These checks establish arithmetic consistency under the declared inputs only.

## Final documentation and integration checks

The 13 November 2026 completion check covered all 26 Markdown documents in this
directory: 370 local links and anchors resolved, fenced blocks balanced, no
whitespace errors under `git diff --check`, and no whitespace inside Markdown
external-link URLs. Git scope inspection found exactly those 26 modified files
and no untracked repository files; all changes remained inside Fable-5.2.
Independent final scalar checks repeated the C3 ENBW, C0 noise density, C1
unit pulse energy, binary64 open-interval 52-bit midpoint endpoints and the
sub-9 kHz/EME workload-count identities. These checks passed.

The Gu and QAN–ALE public archive MD5 values were independently rechecked
against the inspected repository metadata. Their bounded content/schema
findings, access routes and calibration limits are recorded in 12 and 32.
This is data-feasibility evidence, not waveform or environment validation.
Native-interface, source/domain and completion reviews repaired the recorded
normalization, time-horizon, Helper-power, source-access and physical-transfer
issues. No native modem, physical solver, ephemeris execution or receiver
benchmark was run for these documentation checks.
