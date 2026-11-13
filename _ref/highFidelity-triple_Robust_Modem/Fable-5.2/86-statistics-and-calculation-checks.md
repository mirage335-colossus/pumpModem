# Independent statistics and arithmetic checks

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md).
- **Purpose.** Record the bounded, independent arithmetic and inference corrections made 13 November 2026. These checks validate formulas and arithmetic only; they are not modem tests, field measurements, or empirical validation of propagation models.
- **Inputs.** The earlier scalar checks use the stated assumptions and rounded table values in [90](90-cross-check-calculations.md); the finite-method checks use the explicit laws in60/85. The earlier arithmetic used scalar Python standard-library calculations. The follow-through below adds a small temporary C++17 reference calculation for finite Gaussian vectors and frequency responses; it has no modem, operating DSP path or persistent receiver state. No modem code or historic scratch script was run by these particular scalar/finite-method checks;90 records the separate later recovered-source work.

## Selected inferential purpose and procedure

The selected first experiment is a **failure-finding pilot of at most 12 wanted and six control complete episodes in one HF model family**, consistent with the adjacent experiment protocol. It does not estimate an aggregate natural success rate. The fifteen-case catalogue below preserves the former 195-episode proposal and its original statistical failure; it is not a second mandatory campaign. A separate, fixed **41-episode H1 confirmation** is specified below if its full cost is admitted. Its purpose is to distinguish a proposed conditional completion probability of .8 from .95, with a two-sided 95% exact interval and at least 80% power at .95. These are explicit research choices, not service requirements supplied by the user.

An independent complete episode is the sampling unit. Draw new roots with replacement from the recorded random source for confirmation; reproduce each root using 00's engine and transforms. A fixed diagnostic root set is not an iid population sample. Samples, bits, bursts, timing replicas and quiet slots within one episode remain dependent. The estimand includes exact message content, acquisition, continued bit/null decisions, two scored quiet slots, declared horizon and service resources. Censoring rules and complete-work admission are specified below.

For each completed fixed-n binary confirmation, report `x/n` and the exact two-sided Clopper–Pearson (CP) interval. For K predeclared stratum/endpoint decisions use per-decision error `.05/K`; Bonferroni does not require independence between decisions. Decide only when the whole applicable interval is above or below the proposed requirement; otherwise report inconclusive. Pairing alternative models on the same roots is useful for differences but does not create independent environments or extra episodes. Fixed diagnostic controls receive no confidence interval or probability of natural occurrence.

Continuous outputs are episode-level values: report every value, failure category, empirical range and median, and the restricted time-to-completion `min(T_completion,T_horizon)` with a timeout indicator. The latter is a well-defined bounded service summary; a success-only average is biased when failure is informative. Do not infer a tail percentile from the pilot. A Student-t interval is not selected for this small, mixed, censored pilot. An independent, fixed-n approximately normal episode-mean study could justify one later. Dependent waveform samples never receive an iid KS p-value or a generic effective-N substitution. The finite null-calibrated maximum-statistic example below supplies a fully specified alternative for generator checks.

No optional extension of fixed n is permitted. A confirmation selected or modified after seeing outcomes is a new batch with a new, declared error allocation. Howard et al.'s time-uniform confidence sequences are an available alternative when their episode-level assumptions hold, but are not needed for this fixed design. Unrelated external interruption is invalid only under the objective rules below; admitted service timeout or resource exhaustion is failure. No statistical procedure here has yet been applied to an operating modem.

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

The adjacent [experiment protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md#stage-c-a-bounded-communication-pilot) and this chapter now have one order: deterministic generator checks, one bounded 12+6 diagnostic pilot, then an independently sampled, powered confirmation only if warranted and affordable. The catalogue records possible conditions and the **superseded** 5/10/20 allocations for provenance. It carries no instruction to execute all fifteen rows. Freeze the canonical native waveform, source payload, public calendar, keyed starting ordinal, sample rate and resource manifest before receiver work. Missing inputs mean unstarted work, not a substituted shorter receiver or zero failures.

The following IDs freeze **one** member of each existing finite case set. They are not a Cartesian product of every sensitivity coordinate. All use the complete canonical `A` source, fixed calendar and sample-rate/containment rules in 00 and the owning chapter. Unlisted effects are off. For 81 the magnetic reference plane and its own external/self-noise law apply; for 82 use its stated `Fa`/`Te` Gaussian terms; for 83 use its stated terminal Gaussian law; for 84 use its stated total on-Moon temperature. **85 C1/C2/C3/C4 are off in every row**: no impulses, blockers, nonlinear front end, ADC or clock disturbance is implicitly added. Its C0 power/noise accounting is a convention cross-check, not an extra noise source.

| ID / family | Exact frozen conditional case | Superseded screen n |
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

The former proposal comprised 15 decisions and **195 complete receiver episodes**; its successful outcomes could not reach its proposed adjusted .8 pass threshold. It is retained as a rejected reliability-screen design, not fifteen draws from a natural population or a current execution budget. For each row, the declared parameter/event trajectory is fixed; randomization is only over the explicitly specified Gaussian/noise/scatterer innovations. Thus the estimand is completion probability **conditional on that chosen mathematical scenario**. For the proposed model trial, draw each 64-bit root independently and uniformly with replacement from a recorded operating-system random source, then use 00’s declared engine/transforms; retain even a coincidental repeated root rather than condition the sample on uniqueness. Reuse corresponding roots across model alternatives for paired diagnostics, while keeping episode draws independent within each stratum. This is an executable pseudorandom approximation to the specified innovation law, not proof of a generator’s mathematical independence. An input table that gives only a median, percentile or range is not a sampling distribution. Empirical branches requiring an unavailable trace or calibration receive zero episodes and remain unavailable; no replacement synthetic stratum silently closes that empirical gate. Those historical allocations were computational screening limits and failed to supply adequate acceptance power. A failed prerequisite ends that stratum before receiver assertions and does not erase earlier generator checks.

The primary binary endpoint is **exact source-message recovery with valid physical completion by the declared horizon and within all service resources**. Failure includes missed/incorrect Helper acquisition, wrong decoded content, premature completion followed by resumed source data, missed final completion, deadline exceedance, receiver memory/worker admission failure, and invalid output. Also retain disjoint failure reasons, bit/null/erasure decisions, false candidates and time-to-completion; do not collapse them into “no decode.” An unrelated external interruption is invalid only by the predeclared objective rule above. No run silently disappears from the denominator.

Define the horizon from public geometry and evaluator-only source truth, before generating noise: let `t_end` be the final nonzero source sample; let `D_max` and `T_tail` be the declared propagation support and filter-tail allowance under 60's truncation contract. Let `q0` be the first configured public slot whose **entire scoring input window begins after `t_end+D_max+T_tail`**. Render and score through the complete input window of slot `q0+2`, including any native lookahead. That supplies three final quiet opportunities, with two required for completion and one fixed margin. All state/noise continues. The receiver receives neither `t_end`, `q0` nor expected payload length. A completion earlier than the last true payload is premature even when its two quiet decisions are internally valid. End of the observation horizon is a timeout, not a completion event. The manifest records these exact timestamps; a delayed tail that reaches them forces the support/horizon contract to be corrected before a new campaign, not truncated away after an unfavorable result.

### First diagnostic pilot and explicit C1–C4 coverage

Use the HF family as the first bounded synthetic diagnostic. The three frozen conditions are H1/F-MQ, H2/F-HD, and 83's S4-H mode transition. Draw two independent innovation roots per condition and render each root at Cref/N₀=20 and10 dB-Hz, giving12 wanted episodes. The levels are feasibility inputs, not proven native acquisition thresholds. The first two are standard statistical controls; S4-H is an explicit hypothesis, not an observed storm trajectory. This instantiates the protocol's three-state structure without mislabeling the transition as measured. No combined success interval across these heterogeneous cases is meaningful. If the exact canonical episode cannot contain the specified event and final physical completion, that condition is unadmitted; do not time-compress it.

The six companion controls below are deliberately chosen, not additional iid strata. Use H1's fixed geometry and20 dB-Hz reference when wanted data are present. A separate algebraic/generator test establishes each effect's own observables before combining it with the receiver. All unspecified85 switches remain off, including C2 mains/sweep branches and C3 passive-loss option; the effect parameters are exactly85's frozen controls. N2/N3 share both initial tone phases and the same complete input samples. C0 adds no duplicate noise.

| Control | Frozen intervention and diagnostic | Covered consequence and limitation |
| --- | --- | --- |
| N1 | C1 clustered law,ρ=10, wanted H1 | Pulse energy, event times, clustering and retained innovation history; wanted reception under this law, no atmospheric occurrence rate |
| N2 | C2's +1000/+2000Hz two-tone case, each−20dBm, full-message `[tm,tm+60s]` interval and10ms ramps, wanted H1, linear front end | Frequency/phase, ramp and occupied spectrum; establishes input for N3 |
| N3 | N2 plus C3's full stated compression/AM–PM, oscillator, AGC and ADC branch | C2×C3 interaction, recovery and clipping; paired with N2, not an independent environmental replicate |
| N4 | C4 +5ppm clock and the declared0.1s validity gap, wanted H1 | Fractional timing, timestamp/mask continuity, no gap compression; its linear band-limited scope excludes impulsive C1 |
| N5 | Canonical A with wrong configured key, plus at least three wholly noise-only public slots before and after | False acceptance under wrong-address/noise input and continuing null decisions; one compound diagnostic, no false-alarm probability claim |
| N6 | H1 with83's exact S7-H outage, resumed DATA, and a fixed delayed wanted component whose support crosses the source's final silence | Interior erasures, resumption, retained tail, then genuine quiet completion; choose the additional delay as one full public recurrence T and half the original total path power, reducing original H1 paths to half total power |

N6's half-power delayed copy uses the same H1 input realization at its retarded time and a deterministic zero additional carrier phase at the reference epoch; it is a conformance discriminator, not independent scattering or a physically measured echo. The event and delayed source support determine the horizon by the rule above. Its delayed path is deliberately beyond H1's physical delay catalogue and must never be counted as HF environmental validation. N5 preserves the public calendar and uses a fixed different key, not secret truth to direct receiver geometry. These six controls cover C1–C4, the consequential C2×C3 interaction, and physical completion. They do not qualify all combinations, impulse timing through C4, wrong-ordinal acquisition, or every family’s nonlinear frontend. Add any needed new control only through a newly frozen bounded scope; do not call omitted coverage passed.

Stop this pilot on a decisive conformance inconsistency, a prerequisite failure, or its predeclared work limit; retain completed and unexecuted controls by ID. Otherwise stop at12+6. Results select a scientific explanation or a next discriminator, not a reliability pass.

### Fixed conditional confirmation: threshold, interval and power

The sole initial confirmation decision is **H1 at20 dB-Hz, C1–C4 off**, with independently sampled complete episodes and the endpoint above. Select `n=41` before sampling. Test the proposed conditional threshold `p₀=.8` using the two-sided95% CP interval; design power is evaluated at `p₁=.95`. Pass requires at least38 successes (at most3 failures); its lower bound at38/41 is.8007540. At27/41 or fewer the upper bound is below.8 (27/41 gives.7991659);28–37 successes are inconclusive. Even all41 successes yield lower bound.9139562, so this is not a demonstration of extremely reliable service.

Exact binomial enumeration, not an asymptotic normal approximation, gives `P_p=.8(pass)=.0243581` and `P_p=.95(pass)=.8525945`.41 is the smallest n with a CP-compatible pass rule achieving at least80% power at.95. The design can therefore reach its stated decision, unlike the former20/20 adjusted proposal. This does not authorize41 expensive episodes if admission fails. A failed cost gate leaves a mathematically complete but unexecuted design.

| Predeclared decision family K | Smallest n per decision for≥80% power at.95 | Largest failure count allowing pass | P(pass\|p=.8) | P(pass\|p=.95) |
| --- | ---: | ---: | ---: | ---: |
| 1, selected H1 confirmation |41|3|.0243581|.8525945|
| 5, example one base plus four equipment decisions |59|4|.00461372|.8281329|
| 15, former physical-case catalogue expanded to confirmation |74|5|.00144418|.8349341|
| 19, catalogue plus four separate equipment decisions |75|5|.00123212|.8276259|

Only K=1 is selected. The others quantify the multiplicity cost and are **not** campaigns to run. A new false-acceptance endpoint consumes its own error allocation; six selected negative controls cannot certify a small rate. For example, with zero false accepts, a two-sided95% CP upper bound below.05 requires72 independent negative episodes even for K=1. No such false-alarm campaign is selected. For paired changes report the full2×2 episode table and paired differences; no independent-two-sample calculation is valid. The pilot's paired levels are not pooled into confirmation.

### Admission budget before any whole-message campaign

**Neither the rejected195-episode proposal nor the powered41-episode design is selected for execution or authorizes intensive work.** The physical-duration burden is substantial: even the thirteen sub-9 kHz payload slots plus only two final quiet slots at the inspected 6,750 s recurrence span 28.125 h per episode; fifteen such episodes span **421.875 h of simulated time**, before the third quiet margin, preroll and any extra tails. The exact returned scoring windows determine the larger actual sample count. Simulated duration alone proves neither feasible wall time nor real-time capacity. A literal EME evaluation with 256×512 surface cells at 48,000 samples/s for 1,800 s requires **11,324,620,800,000 cell–sample products per episode**, excluding preroll, geometry, filtering and receiver work. A lower-cost native realization must first preserve the declared operator and numerical error bounds; this count is not a forecast of an optimized implementation.

A concrete **future research pilot proposal** is limited to **10 minutes elapsed on one exact machine**, after its native build/input prerequisites are ready. Select H1 as the single component stratum; first run cheap deterministic source/generator checks, then one bounded native component cost measurement with at most 60 seconds of timed work and one abbreviated holdout. A component-only check does not qualify a complete receiver. Do not start a long message merely because a component is fast. If the pilot cannot finish within its budget, retain its measured partial coverage and stop; no automatic extension or extra hardware spending is implied.

Before admitting any whole-message batch, produce an upper wall-time and peak-memory forecast for **every** admitted pilot or confirmation episode using its exact integer sample count `N=stop_sample−start_sample`, full source/propagation/filter support, public hypotheses, unsuccessful acquisitions, tracking, quiet scoring, preparation and native scheduling. Use applicable current native complete-work calibration and abbreviated holdouts under the maintained project procedure; a generator timing alone is insufficient. Charge startup/finish and recurring work separately, sum the per-stratum upper wall estimates over the entire proposed batch, and include fixed control runs. If applicable calibration is absent, report the batch unadmitted. For the first HF pilot, retain the proposed **30-minute upper wall-time budget** on that exact machine, including controls and scoring. The10-minute component/30-minute pilot numbers are research-proposal choices, not user mandates or portable limits for all geometries. Confirmation requires its own finite, geometry-specific admitted budget under the maintained minimal-work procedure; no automatic41-episode run follows the pilot. If necessary work cannot fit a justified small budget, report the exact missing outcome and use an analytically valid or component discriminator. Never default to days of processing.

Fix the actual OS hardware allowance H, automatic `W=2H`, owner limit, permitted arithmetic/backend, native memory admission and explicit user resource limits before measurement. Preserve the full W ceiling and every public hypothesis. Forecast memory from the application baseline, started stacks, live owners and full DSP bounds, source/input assembly, delay/filter/generator state and retained results over their actual lifetimes. Use the exact machine's available/admitted memory and explicit allocation; do not invent a portable 2 GiB cap or reduce W to fit an arbitrary research cap. Stream samples when the native ownership contract permits; retaining an entire multi-hour capture is not a default requirement. Bound temporary output to the necessary configuration, seeds, summaries and identified diagnostic excerpts, with a deletion trigger after the findings are distilled.

A stratum that cannot fit these admission bounds is **unrun/inconclusive**, with zero receiver assertions; it is not a success or a binomial failure. If a running, admitted service trial exceeds the deadline/resources included in its estimand, that episode is a failure under the rule above. If the global pilot/campaign budget ends before the predeclared fixed n, report the observed count and incomplete design; do not quietly drop missing episodes, reuse a fixed-n success claim or increase the budget. The episode limits concern communication outcomes; component checks and capacity measurements retain their distinct estimands. Wall time from communication trials is descriptive unless the maintained complete-receiver capacity procedure is separately satisfied.

The rejected15-decision design used two-sided CP error.05/15. Its20/20 lower bound is.726261, below the proposed.8 requirement; its5/5 and10/10 bounds are lower still. This is the original C13/S51 failure, now resolved by separating diagnostic work from the powered confirmation. Retain those calculations as counterexamples to a misleading successful-screen claim. The selected fixed-n confirmation has no optional early success or failure test. A technical setup failure or global budget exhaustion stops execution with incomplete coverage; it does not license a completed-batch interval or an increased budget.

External invalidity is restricted to documented host/service interruption unrelated to receiver or input (for example host reboot or storage failure before an output is available). The harness records its timestamp and reason without inspecting decoded content. At most two replacement draws are allowed, using new roots and retaining every invalid record; a third ends the batch as incomplete. A reproducible generator defect, invalid manifest or malformed canonical source ends setup and is not resampled as an unlucky environment. Numerical exceptions, receiver crash, admitted resource failure, input-backlog deadline or noncompletion caused by the tested channel are endpoint failures. Report these categories separately. Do not exclude a difficult but valid realization.

Add fixed, deliberately hand-picked **Off, exact-zero channel, in-message outage followed by resumption, and delayed-tail-through-source-silence** controls for each family before randomized trials. Their outcomes are deterministic conformance evidence at those inputs; they receive no binomial interval or occurrence probability. At B=0 distinguish a fixed unit gain from a frozen random Gaussian gain. This prevents a fixed synthetic “worst case,” or multiple timing replicas of one channel record, from being mislabeled iid natural episodes.

## Expanded arithmetic coverage and independently reproducible recipes

The follow-through scalar check used Python 3 standard-library `math`, `re`, `pathlib` and `statistics` on 13 November 2026. It generated no channel waveform, receiver state or modem implementation. That earlier scalar pass did not rerun the historical scratch programs, which had not been recovered at that stage. The originals have since been recovered;90 records the later source/reproduction checks separately. This historical statement must not be read as a current claim that those artifacts are absent. The coverage boundary is:

| Chapter 90 quantity | Independent check and result | Limit |
| --- | --- | --- |
| §1.2 field/loss pairs | All 144 displayed pairs satisfy `E+Lb=142+20log10(f_MHz)` to≤0.0795 dB, within the 0.1 dB combined rounding interval | Fields themselves and the propagation solver were not reproduced |
| §1.3–1.5 solver-dependent ranges, radius/height/phase/selectivity and GUI-equivalent distance | The earlier scalar pass did not reproduce them. The later independent solver/matched-input receipt in90 §1.1 now covers them | Use90's exact inputs, official-vector checks and phase/convergence limits; these are different checks from this earlier scalar pass, not field validation |
| §2 bare antenna capacitance | Radii 1 mm/5 mm/0.3m give 338.5273/403.3232/786.0935 pF usingε0=8.8541878128×10⁻¹²F/m,h=60.96m | Electrostatic approximation; does not verify the separate top-loaded wire solver in 11 |
| §2.2 electrical parameters | All seven frequency rows were independently evaluated for wavelength, radiation resistance, reactance and resonating inductance; e.g.8.27 kHz requires 1.089307H with 340 pF,76 Hz requires 12898.35H | These are ideal component values, not feasible hardware specifications |
| §2.3 antenna operating cases | All five 100W cases independently recomputed below;1kW results follow exactly by current/voltage/field×√10, radiated power×10, unchanged efficiency/Q | Q300 and 60 Ω are assumed, not the distinct Q400/S2 physical model in 81 |
| §2.3 voltage ceiling |30 kV gives bare 8.27 kHz I=.530012A,Pr=.314022 mW; top-loaded I=1.558858A,Pr=7.300332 mW | Voltage is treated as the same RMS/phasor convention as IX; a physical peak/corona ceiling needs the appropriate√2 conversion and measured geometry |
| §2.3 detuning | At fixed L, `dln f0=−½dln C`;1% change gives magnitude 34.99°,57.17°,63.43° at Q70/155/200 | Corrected the sign of resonance-frequency shift in 90; phase sign depends on transfer convention |
| §3.1 all 24 EME loss cells | Independent radar equation reproduces all rounded 0.1 dB values; mean 5.76 GHz 284.141 dB | Albedo remains assumed; range convention is geocentric here |
| §3.2 geometry | Overhead delays 2.323551/2.510347/2.659117 s; limb depth 11.590685 ms; dimensional speckle 5.757736m; equatorial rotational rate 1.301806 Hz/s | These scalar geometries are not the later ephemeris-driven bistatic calculation |
| §3.3 illustrative kernels | Eight laws' mean delay, RMS delay about the mean,2×RMS Doppler/LL, and five cumulative-delay fractions independently integrated; six beam relative powers/mean delays checked below | The original width columns were not in this scalar pass;90's later independent receipt now checks all14 laws/84 width quantities and112 delay/power quantities with convergence/rounding limits |
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
- **82 ground controls.** The refractivity inversion `ln(.25/.04665)/.005577=301.01990148` reproduces its4/3-earth fixture. Two2m antennas on effective radius`(4/3)×6370km` have summed geometric horizon11.65733028km. Its **flat**100km-high midpoint mirror over200km has excess`[2hypot(100km,100km)−200km]/c=276.33354430µs`; this differs from90’s spherical mirror by design. The1km knife edge18m above line of sight at the midpoint gives`v=.5092930791` at30MHz. `N0=k[290×10^(Fa/10)+100]` yields−93.97518718/−155.95151698dBm/Hz forFa80/18. The stated Fresnel normalization givesD(0)=½ andD(−∞)=1. The TM-layer impedance reduces algebraically toZ2 for zero thickness andZ1 for identical media; scalar complex counterexamples confirmed these limits to roundoff. No LFMF field/phase calculation belonged to this scalar check;90 now records the separate independent solver reproduction.
- **85 white-noise plane and filter.** AtFs48000,−164dBm/Hz is3.981071705535×10⁻²⁰W/Hz. Complex white sample power is1.910914418657×10⁻¹⁵W, equally split between I/Q. For`a=exp(−2π3000/Fs)=.675231906656`, ENBW`Fs(1−a)/(1+a)=9305.4987901Hz`, hence output power3.704585793916×10⁻¹⁶W. This follows independently from the geometric impulse-energy sum `(1−a)² sum a^(2n)`. It is the full-complex-interval convention, without an extra factor2.
- **85 pulse energy and clustering.** The240-sample double exponential has raw discrete energy.000184079963198 in the stated units; normalization factor.0135676071287 gives energy1.0000000000000002. Atρ=1,`Jbar=2.388643023321×10⁻¹⁶J`, `J0=1.200323127297×10⁻¹⁶J`, and`.99J0+.01(100J0)=1.99J0=Jbar`. Both8/s independent children and2/s parents×4 children therefore give mean power`8Jbar=ρN0Fs`; uniform independent phases cancel the expected overlap cross terms. Atρ=10 all energies/powers scale by10. This identity verifies the constructed ensemble mean, not a realized finite-record energy or natural sferic rate.
- **85 passive accounting and ADC.** At the default external density, output-referred density is`N0/2+k·145+k·100=2.328794857767×10⁻²⁰W/Hz` (−166.32868767dBm/Hz). With external20K it is`k·255=3.52065495×10⁻²¹W/Hz` (−174.53376537dBm/Hz). Wanted power also halves. If holding the original wantedC fixed, these give19.31838771/27.52346541dB-Hz respectively. ADC full scale is.001√W per component and step3.0517578125×10⁻⁸√W. These values do not qualify nonlinear recovery, quantization error statistics or a particular receiver.

No native receiver, random waveform campaign or hardware check was run. These checks establish arithmetic consistency under the declared inputs only.

## Executed finite-method research checks

These checks were executed on13 November2026 using Python3 standard-library scalar arithmetic and a temporary C++17 program compiled with `g++ (Debian14.2.0-19) -O2 -std=c++17`, binary64 arithmetic, no fast-math option. They evaluate mathematical operators, not a native channel renderer, canonical waveform, receiver or hardware capacity. An initial C++ compile rejected an unqualified scalar `abs` overload; it ran zero assertions. Qualifying every scalar/complex absolute value as `std::abs` repaired setup before the results below. The existing2,064 FIR-covariance checks remain separate and unchanged. Temporary reference code and downloaded sources served review of these findings and were removed after acceptance; the durable equations, inputs, draw order and results here replace those temporary artifacts.

### Shape, finite-record normalization and state

For unit proper-complex Gaussian gain, Wick's identity gives `Cov(|g(t)|²,|g(s)|²)=|R(t−s)|²`. Match Gaussian and OU by their1/e correlation time T_c, so `R_G(u)=exp[−(u/T_c)²]` and `R_O(u)=exp(−|u|/T_c)`. With `x=T/T_c`, exact variance of the record-average power is

```text
V_G(x)=sqrt(π/2) erf(sqrt(2)x)/x + expm1(−2x²)/(2x²)
V_O(x)=1/x + expm1(−2x)/(2x²).
```

| T/T_c | Gaussian power variance | OU power variance |
| --- | ---: | ---: |
| .1 | .996679953 | .936537654 |
| 1 | .763955655 | .567667642 |
| 10 | .120331414 | .095000000 |
| 100 | .012483141 | .009950000 |

Both have unit mean power; dividing each record by its own power incorrectly collapses these nonzero variances to zero. Matching correlation time therefore does not match coherent integration, fade durations or finite-record fluctuations. For differentiable unit proper-complex Gaussian gain, amplitude derivative is independent of amplitude and has variance `−R''(0)/2`; multiplying its mean positive derivative by Rayleigh density `2ρ exp(−ρ²)` gives upward-crossing rate `sqrt[−R''(0)/π] ρ exp(−ρ²)`. Gaussian thus gives `sqrt(2/π)ρ exp(−ρ²)/T_c`, equivalent to60's `sqrt(π)Bρ exp(−ρ²)`. OU is nondifferentiable and has no finite continuous-time rate under this formula. This is a mathematical counterexample, not measured ionospheric fade statistics.

For the explicitly **hard-truncated** OU spectrum, choose `|f|≤F`, `S_F(f)=[2T_c/(1+(2πT_cf)²)]/m_F`, `m_F=(2/π)atan(2πT_cF)`. Its finite derivative variance is `[2/(πT_c²)]·[a−atan(a)]/m_F`, `a=2πT_cF`; its covariance is the Fourier integral of S_F and must be used in the oracle. For the bounded comparison choose `F=8/T_c`: retained unnormalized mass.987336522544, normalizedR(0)=1, R(T_c)=.372587726980, first1/e crossing at1.013142299954T_c, and derivative variance31.4104287336/T_c². Thus its upward-crossing prefactor is3.162000948/T_c, versus Gaussian.797884561/T_c at the original matchedT_c. Composite Simpson4096→8192 frequency intervals changed all covariance entries on the seven-time grid below by at most5.75581249329×10⁻¹⁴. The cutoff is a declared research comparison, not a measured natural bandwidth. This defines a reproducible alternative and shows crossing rate increases without bound as F increases. It is not WSJT-X's offset-subtracted finite line construction. FWHM, RMS width,1/e time and finite spectral-line spacing remain different coordinates.

The exact sampled OU state has stationary variance because `r²+v=1`; zero startup instead has variance `1−r^(2n)`, and its remaining impulse-energy fraction after n updates is `r^(2n)`. The selected `expm1` form avoids subtractive cancellation for smallδ/T_c. A Gaussian FIR initialized with all negative-index innovations has its same finite covariance from its first retained sample; starting its missing prehistory at zero does not. These algebraic checks establish state requirements but do not test a renderer's actual chunking, retained delay tails, C3 burn-in or receiver quiet decisions.

### C4: a failed full-filter refinement and a passing interpolation calculation

The original85 test jointly changed the physical low-pass filter from1025 to2049 taps and interpolation half-support64 to128, while requiring whole-output relative MSE≤10⁻⁶. At integer interpolation phase both kernels are identity. Aligning the two group delays, the exact white-input expected MSE is already

```text
E_full = sum_k (h_1025[k]−h_2049[k])² / sum_k h_2049[k]²
       = 0.000228531838154 > 10⁻⁶.             FAIL
```

Use the85 coefficients centered at zero, normalize each sum to1 and zero-extend the shorter filter in this finite sum. This is an exact Parseval counterexample, not Monte Carlo variation. Much of the difference is in the different8-kHz transition bands. A strict lowpass brick-wall limit is also a different apparatus; increasing filter length cannot silently change the experiment while being called numerical refinement.

The research correction freezes the1025-tap apparatus filter and refines **only interpolation**. For normalized sinc `sinc(u)=sin(πu)/(πu)`,

```text
K_L(u)=sinc(u)[1+cos(πu/L)]/2, |u|<L; 0 otherwise
I_L(d,f)=sum_k K_L(d−k) exp[j2πf(k−d)/48000] / sum_k K_L(d−k)
H_h(f)=sum_k h[k] exp[−j2πfk/48000]  (centered filter)
E_interp(d)=∫₀²⁴⁰⁰⁰ |H_1025(f)|² |I_64(d,f)−I_128(d,f)|² df
             /∫₀²⁴⁰⁰⁰ |H_1025(f)|² df.
```

Composite trapezoid integration over2048 then4096 equal frequency intervals, at17 phases `d=0,1/16,…,1`, gave maximum E_interp `9.43410515705×10⁻¹³` then `9.43410515755×10⁻¹³`. This bounded grid/quadrature check passes the engineering10⁻⁶ target for the declared filtered white input. It is not a proof for arbitrary spectra, every continuous phase or a nonlinear/impulsive input.

As a different discriminator, compare the original two complete filters on wanted tones0–3000 Hz, aligning delay.17 phases×257 frequencies and then33 phases×513 frequencies both gave maximum complex-response difference `1.58159791575×10⁻⁶` and magnitude ratio difference `.000013737596dB`, within.01dB. Passing this wanted-band check does not erase the full-noise failure.85 now preserves both results. Actual±5ppm moving clocks, phase-boundary crossings, finite-buffer edges, masks and chunk invariance remain native implementation checks.

### Finite covariance oracle and calibrated maximum statistic

This executed reference uses seven dimensionless timestamps `t/T_c=(0,.25,.5,1,2,3,4)`, separately Gaussian and OU covariance above, no fitted parameters, and20 independent proper-complex vectors per diagnostic batch. Ordinary Cholesky gives `A A*=R`; relative Frobenius residuals were `5.78950274868×10⁻¹⁷` and `8.08389074492×10⁻¹⁷`, with maximum diagonal errors `1.11022302463×10⁻¹⁶` and `2.22044604925×10⁻¹⁶`. All seven pivots were positive. No eigenvalues were clipped. A valid covariance factor gives the same Gaussian distribution as a Hermitian square root, though not the same sample path for the same normal vector.

The two7×7 reference matrices have minimum Cholesky pivots.025991593135(Gaussian) and.393469340287(OU), independently recomputed with scalar Python. For two paths, use gates `m₁=(1,1,.5,0,.5,1,1)`, `m₂=(1,1,1,1,1,1,1)` andρ=0,.5,1. Form the14×21 factor from common/private copies of the same7×7 Cholesky factor. All six resulting14×14 covariance comparisons matched `m_j(t)m_k(s)R(t,s)[ρ+(1−ρ)1_(j=k)]` within2.22044604925×10⁻¹⁶. Across the zero-gain timestamp the retained first-path covariance between t/T_c=.5 and2 remains.0263498061405(Gaussian) or.0557825400371(OU); resetting the latent process would incorrectly make it zero. This checks a nonstationary/shared-mode mathematical counterexample, not native state persistence. A first attempt at this scalar recipe had a syntax error and executed zero comparisons; the corrected expression produced the six results above.

For every20-vector batch define the **whole** statistic before generation:

```text
mean_j=(1/20)Σ_e g_ej
cov_jk=(1/20)Σ_e g_ej conj(g_ek)             # uncentered; known mean is zero
T=max( max_j |mean_j|, max_jk |cov_jk−R_jk| ).
```

All mean and covariance coordinates are included in one maximum; none receives an independent KS test. Generate4096 independent calibration batches for each law, sort T, and choose the3924th order statistic (one-based). Exact binomial summation gives `P[Binomial(4096,.95)≤3923]=.99102012`, so this is a99%-confidence upper tolerance limit for that law's95th percentile, under independent ideal null batches. This confidence statement is per law, not a simultaneous99% claim for two laws. It permits a conservative5% diagnostic false-rejection target; it says nothing about power against unspecified alternatives.

| Null law | Frozen threshold | Independent holdout exceedances | Holdout exceedance proportion and marginal95% CP interval |
| --- | ---: | ---: | --- |
| Gaussian | .649642846949 |175/4096|.0427246 [.0367375,.0493745]|
| OU | .664855698788 |156/4096|.0380859 [.0324338,.0444084]|

The holdouts were4096 **new batches**, each with20 new vectors. They were not used to retune thresholds. These observed rates are consistent with conservative calibration. A deterministic covariance tolerance of.001 applied directly to a noisy20-episode covariance estimate would be a completely different test. This calculation closes the previously unspecified null-calibration method for this finite oracle; it does not calibrate every future timestamp grid, fitted model, moving-delay process or native generator. Those require the same predeclared procedure with their own null law and multiplicity allocation.

Reproduction is fully defined by the equations and this draw order: initialize `std::mt19937_64` with calibration seeds `0xF0526000`/`0xF0526001` for Gaussian/OU, holdout seeds `0xF0526100`/`0xF0526101`; traverse batch, episode, coordinate in increasing index order. Each complex innovation consumes two engine words, with `u=((word>>12)+.5)·2⁻⁵²`, `v=((next_word>>12)+.5)·2⁻⁵²`, and `z=sqrt(−ln u) exp(j2πv)`. Compute lower-triangular Cholesky in increasing row/column order; sum coordinates in increasing index order. These are reproducible pseudorandom draws approximating the ideal continuous law; they are not proof of RNG independence or a production-generator performance measurement. No public seed is reused between calibration and holdout.

### Optional finite-state noise: stationary law and truncation counterexample

This is a small algebraic check of the optional method described in60, not a new selected modem experiment. Define states i=0,…,3, Poisson raw weights `q_i=exp(−A)A^i/i!`, normalized `p_i=q_i/sum(q)`,Γ=.1, x=.9 and a declared1ms state step. Set `P_ij=x·1_(i=j)+(1−x)p_j`, initialize state fromp, and let its conditional proper-complex noise power be `v_i=(Γ+i/A)/(1+Γ)`. This power formula would have unit expectation for the untruncated Poisson law. Direct scalar summation gives:

| A | Retained Poisson mass | Actual truncated mean power | Maximum stationary residual `\|pP−p\|` | Four mean dwell times(ms) |
| --- | ---: | ---: | ---: | --- |
| .2 | .999943159759 | .999007542676 |2.17×10⁻¹⁹|55.18072,11.95822,10.16648,10.01093|
| 1 | .981011843124 | .943181818182 |0 at shown arithmetic|16,16,12.30769,10.66667|

Thus even small removed probability need not remove an equally small share of power. If a finite comparator requires exact unit mean power, scale all conditional variances once by their **ensemble** mean above; do not normalize each realization. This remains a pruned mixture, not an exact infinite ClassA marginal. For independent zero-mean emissions conditional on states, raw noise covariance at nonzero lag is zero, but power covariance is `x^lag Var_p(v)`; at lag1 these examples give3.70357917776 and.601433367769. A white raw covariance therefore does not establish absence of bursts. Dwell time in seconds requires the stated state step and cannot be imported directly as a natural sferic duration. These numerical examples verify the transition algebra and a materially different pruning counterexample only.

### Reproducing the power calculation

For K decisions, n episodes and allowed failures c, enumerate

```text
A(n,c,p)=Σ_(j=0)^c choose(n,j) (1−p)^j p^(n−j).
Choose the smallest n having a c with A(n,c,.8)≤.025/K
and A(n,c,.95)≥.8; use the largest such c at that n.
```

This gives the41/59/74/75 table above and is independently equivalent to the lower CP-bound rule. Use log-factorials for large n; direct exact integer binomial coefficients suffice for these small n. The upper/lower CP equations earlier in this chapter reconstruct the reported limits. This checks intermediate counts as well as all-success and all-failure boundaries. These are prospective conditional-model powers, not measured modem probabilities.

## Final S35–S59 dispositions and remaining gates

“Research specification complete” means the selected law, evidence, decision and uncertainty are now explicit. It does not mean the native implementation exists, an unavailable source was read, or a physical environment was validated. **E** below is an external measurement/source/physical-calibration dependency; **I** is a later implementation or qualification check. The source URLs and section locators are in60; mathematical inputs/results are above. The original failed C4 test remains failed.

| ID | Completed result and consumer | Exact residual gate |
| --- | --- | --- |
| S35 | YB00 supporting journal text, BB05 §III-B–C, Pat09 §§II–V and previous Kom03/PB01 reads compare periodicity, startup, conditioning, quadrature and finite-window laws;60 selects finite covariance/FIR/OU | I: native realization and cost. Unread YB numeric tables are unused, not silently inferred |
| S36 | Standard durations separated from conditional episode inference; FN2001 technical findings and official2017/2024 MIL AppendixE clause comparison inspected;60/86 no universal3000/B or10/p budget | E: FK09 original coefficient law and authenticated/digitized FN2001 curves if those specific extensions are adopted. Current110D Change1 E.5.3–E.7.4 access/edition gap is closed; no curve reproduction or MIL compliance claimed |
| S37 | Inspected Matz03 §§2–5 supplies non-WSSUS/smoothing assumptions; explicit gated/shared finite covariance checked in86 | E: measured stationarity/cross-mode law. I: native moving-delay implementation. Bello63/Matz05 unique unread results remain contextual |
| S38 | Existing2,064 analytical FIR comparisons retained; two7×7 covariance factors pass roundoff limits; exact OU startup/alias/tail algebra specified | I: actual renderer, wider timestamp geometries, moving interpolation and modem sensitivity; no global native-generator pass |
| S39 | **Original joint filter refinement failed** with2.285318×10⁻⁴ MSE. Fixed1025-filter interpolation-only refinement passes the bounded grid/quadrature check at9.434105×10⁻¹³;85 reconciled | I: actual±5ppm clock, moving-delay crossings, buffers, finite masks and receiver consequences. No whole-filter pass |
| S40 | Pulse energy/prehistory and finite-state stationary/mean-power checks, C3 initialization/burn-in observables and exact noise accounting specified in85/86 | I: coupled60/120s nonlinear burn-in, compression/recovery/ADC and actual event renderer. Analytic pulse means are not empirical pulse distributions |
| S41 | Gated/shared covariance passes six14×14 comparisons, preserving nonzero pre/post-outage dependence; continuous state/tails/noise and random-stream order fixed | I: actual chunk invariance and physical quiet/resumption/tail controls. Observation EOF/masks never qualify completion |
| S42 | Equal1/e Gaussian/OU finite-power and crossing comparison, declaredF=8/T_c truncated law/quadrature, and PathSim v1.0 pp.15–17 width interpretation completed | E: choice of natural Doppler shape. I: finite native rendering/width/crossing checks; no OU finite RMS width |
| S43 | Whole finite maximum statistic, order3924/4096 calibration and separate4096-batch holdouts executed for each seven-time null law | I: recalibration for a different native grid/model or fitted parameters. The displayed thresholds are not universal KS/covariance masks |
| S44 | Otnes erratum's reference-only correction and vWalree08 §§II–IV primary replay limits inspected;60 corrected | E: Naderi PaperE body if its unselected fitted model is pursued; underwater measurements never substitute for HF holdouts |
| S45 | Original Ndo remains abstract-only; Mirbadin2021 §2.2 provides inspected optional finite-state law, with stationary/pruning checks and explicit §2.1 correction | E: atmospheric calibration and original source bodies of unselected Ndo/patent extensions. C1 remains its explicit pulse law |
| S46 | Baur/Li supporting versions read and later2025 journal publication verified; other named learned/GPU examples individually contextualized | E: target-domain training/independent holdout data. I: qualified native acceleration; no long-signal performance inferred |
| S47 | Every named methods-appendix reference receives supporting-read, unavailable or reasoned contextual/unselected disposition in60 | Unique unread results remain at their stated grades; a later decision to adopt one requires its original support, not an automatic completion claim |
| S48 |85 freezes observable fitting, identifiable combinations, calibration uncertainty, physical-episode/site/device separation and locked holdout protocol;60 uses it | E: matched target captures/instrument calibration and sufficient independent holdouts; no fitted physical validation yet |
| S49 |15-case conditional catalogue retained; first12+6 HF diagnostic and sole H1 confirmation are distinct; no natural-population aggregation | I: admitted complete episodes. E: natural occurrence weights/joint distributions if a population claim is later sought |
| S50 | N1–N6 explicitly cover C1–C4, C2×C3, wrong key/noise and physical-completion discriminators, with omitted branches listed | I: actual controls. Other interactions, wrong ordinal and all-family equipment robustness are unqualified |
| S51 | C13's impossible adjusted20/20 pass corrected by an explicitly diagnostic pilot and powered n=41 confirmation; exact enumeration passes | I: conditional confirmation execution and cost admission. p=.8 remains a proposed research threshold, not user service acceptance |
| S52 | Endpoint/horizon formula uses complete source support, D_max/T_tail and full q0…q0+2 scoring windows; no payload/endpoint leak | I: exact canonical native timestamp manifest and proof each fixed family horizon contains it; unsupported geometry remains unavailable |
| S53 | Objective invalidity, maximum two replacement draws, admitted service failure, timeout/censoring and restricted-time summaries frozen | I: faithful harness logging and outcomes. Incomplete design never becomes a fixed-n success claim |
| S54 | Exact CP boundaries, K=1/5/15/19 power, dependent paired roots, multiplicity and secondary-endpoint rules specified and checked | I: complete independent episode counts; new decisions require new frozen error/cost allocation |
| S55 |10/p, optional sequential tests and rare-event tilting explicitly excluded from the selected fixed design; direct likelihood/inference requirements retained | No adopted-method dependency on unread rules. Any later acceleration requires its own justified likelihood and unbiased/controlled validation |
| S56 | Geometry-specific whole-work and memory admission, sample counts, finite stopping and proposed10/30-minute first-pilot budgets stated | I: actual current native complete-work cost/holdouts. No component-only forecast, days-long fallback or extra spending authorization |
| S57 | W=2H, all public hypotheses, actual backend/arithmetic/hardware and lifecycle/memory contracts preserved | I: native service/capacity and representative-hardware qualification; none executed here |
| S58 |86's former scalar-only limits now point to90's separate full independent field/range/phase/lunar-width receipt | Use90's exact solver/rounding/convergence limits; field validity and actual station data remain E |
| S59 |60/86 now distinguish inspected source, completed mathematical specification, passed finite calculation, failed C4 comparison, unimplemented native experiment and physical validity | E/I remain explicitly visible. No communication reliability, natural worst-case coverage or hardware-capacity pass is claimed |


## Historical documentation and integration checks

The earlier 13 November 2026 completion check covered all 26 Markdown documents in this
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
