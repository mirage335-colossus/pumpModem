# Independent statistics and arithmetic checks

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md).
- **Purpose.** Record the bounded, independent arithmetic and inference corrections made 13 November 2026. These checks validate formulas and arithmetic only; they are not modem tests, field measurements, or empirical validation of propagation models.
- **Inputs.** Values below are the stated assumptions and rounded table values in [90](90-cross-check-calculations.md). Calculations used short scalar Python with the standard library only; no modem code or historic scratch scripts were run.

## Inference procedure selected for an initial screen

Use a fixed batch of 20 independent, identically distributed complete channel episodes per predeclared scenario stratum, sampled independently from that stratum's specified distribution. Predeclare the distribution, random-seed generator, episode duration, outcome definitions, invalid-run rules and decision thresholds before inspecting results; retain sampled seeds for reproduction. Fixed seeds alone give a reproducible finite set, not iid sampling for Clopper–Pearson inference. Keep within-episode dependence intact; the independent sampling unit is the complete episode, not its waveform samples. Completion requires the receiver's two scored quiet slots. For an ordinary 95% interval, coverage is marginal for one predeclared stratum/outcome under the iid Bernoulli episode assumption. For a family-wide claim across K predeclared stratum/outcome decisions, use per-decision α=0.05/K Bonferroni intervals (or another predeclared familywise-valid adjustment), including selection of endpoints; do not select favorable results post hoc and report unadjusted intervals as family-wide 95% evidence.

For binary completion, report the exact two-sided Clopper–Pearson interval and decide only when the complete applicable interval lies on one side of the requirement. Otherwise report “inconclusive”; even 20/20 completions give a marginal two-sided 95% lower bound of only 0.832, so n=20 is a bounded screen, not a high-reliability demonstration. For episode-level continuous summaries, list all 20 values and report a fixed-n Student-t interval for the mean only where an approximately normal episode-level sampling distribution is credible; otherwise keep the interval descriptive and state that the chosen small sample does not support that approximation. For a time-series distribution comparison, do not pool dependent samples into an iid KS test or apply a generic effective-N substitution. Either compare episode-level statistics, or calibrate a predeclared statistic with independent null episodes generated under the null while preserving each episode's full dependence structure.

Do not extend the initial n=20 after looking and keep using its fixed-n interval. Any confirmation batch needs its own predeclared error allocation. If operational needs require stopping at arbitrary episode counts, use a confidence sequence with assumptions valid for iid episode outcomes; Howard et al. [Howard21](60-simulation-methods-and-validation.md) establish time-uniform coverage for their stated nonasymptotic constructions. Their result does not justify treating correlated samples within an episode as independent. A physical receiver failure, service-deadline timeout, or exhaustion of a resource limit included in the service estimand is a failure. Mark an unrelated external interruption invalid only under objective criteria set before the run and independent of its outcome; log the cause and count, and replace only according to that rule. Report censoring and do not count incomplete receiver outcomes as success or omit them from continuous summaries without addressing informative censoring. No procedure here has yet been applied to a modem experiment.

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
