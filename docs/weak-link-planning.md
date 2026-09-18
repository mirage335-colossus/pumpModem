# Fast planning for extreme weak links

`pump analyze-link` evaluates long-duration cases without generating PCM,
running the receiver, or benchmarking the computer. It combines exact wire
size and airtime with the existing fixed-laptop compute model and a bounded
Monte Carlo experiment on matched-correlation statistics. Work depends on the
requested trial count, not the number of audio samples or coherent segments.
The production receiver now has a fixed four-section option in addition to its
coherent match. The experiments below still describe separate reference
detectors with user-chosen segment lengths; they are not measurements of that
implementation.

The [1.2 kHz case study](1200hz-weak-link-planning.md) compares the requested
3 dBm/-170 dB mode with -200 dB and -230 dB, including a conditional day-long
-200 dB detector candidate and the current receiver's compute/workspace limits.

For the requested **3 dBm / -200 dB** case, received power is -197 dBm. With
the preset noise density of -164 dBm/Hz, actual C/N0 is **-33 dB-Hz** and SNR
in 100 Hz is -53 dB. This is 30 dB weaker than +3 dBm / -170 dB.

| TX power | Attenuation | Actual C/N0 | Ideal duration for 18 dB Es/N0 | Three bits plus one absent symbol |
| ---: | ---: | ---: | ---: | ---: |
| 3 dBm | -190 dB | -23 dB-Hz | 3.50 hours/bit | 14.0 hours |
| 3 dBm | -200 dB | -33 dB-Hz | 1.46 days/bit | 5.83 days |
| 3 dBm | -210 dB | -43 dB-Hz | 14.6 days/bit | 58.3 days |
| 3 dBm | -220 dB | -53 dB-Hz | 146 days/bit | 1.60 years |
| 3 dBm | -230 dB | -63 dB-Hz | 3.99 years/bit | 15.96 years |

These are energy calculations at the existing planner's 18 dB target, not
capacity limits, measured sensitivities or predictions of successful reception.
They omit waveform tails and channel losses. The exact `a=011` wire path still
has three bits; physical completion requires fully scored absence, including
a whole absent symbol when that symbol lasts longer than six seconds.

## Computed reference cases

The following results use 100 Hz bandwidth, 3 dBm transmit power, a 10 dB
noise figure, 0.5 degrees/sqrt(second) phase diffusion, zero residual frequency,
orthogonal template projections, one million prescribed search alternatives
and a 1e-6 total noise false-alarm budget. Each row uses 10,000 trials with
seed 1. The percentages describe one symbol in the segmented reference model,
conditional on correct timing and clock acquisition. They do not describe the
production receiver or a complete message.

| Attenuation | Symbol duration | Maximum coherent segment | Correct detection | 95% model-only interval |
| ---: | ---: | ---: | ---: | ---: |
| -200 dB | 125,892.541 s (1.46 days) | 20,000 s | 61.36% | 60.40–62.31% |
| -200 dB | 250,000 s (2.89 days) | 20,000 s | 99.98% | 99.927–99.995% |
| -200 dB | 500,000 s (5.79 days) | 20,000 s | 10,000/10,000 trials | 99.962–100% |
| -200 dB | 1,000,000 s (11.57 days) | 3,600 s | 10,000/10,000 trials | 99.962–100% |
| -210 dB | 1,250,000 s (14.47 days) | 20,000 s | 0.33% | 0.235–0.463% |
| -210 dB | 10,000,000 s (115.74 days) | 20,000 s | 10,000/10,000 trials | 99.962–100% |

All-success trials do not establish certainty, and these intervals omit model
error. In particular, replacing phase-path energy with its mean omits fading
variability. The 2.89-day symbol would require at least **11.6 days** for `a`
and a complete absent symbol, before waveform tails. These analyzed durations
are examples, not optimized minimum durations. At still weaker power, fixed
coherent segments enter the low-energy regime where useful accumulated
detection evidence scales approximately with power squared: another 10 dB
of attenuation can require about 100 times the observation time.

At the 1.46-day point, the ideal coherent reference succeeds in 99.73% of
trials; the default uninterrupted phase model succeeds in 2.45%. Reducing phase
diffusion tenfold to 0.05 degrees/sqrt(second) raises that uninterrupted model
to 99.65% (99.51–99.75%). This comparison shows why oscillator stability and
tracking matter at least as much as making the configured bandwidth smaller.

## Relative phase across shorter sections

Loss of whole-symbol coherence does not erase every useful phase transition.
A differential receiver can compare neighboring matched sections; a receiver
that models phase drift can use relationships between several nearby sections.
Neither approach requires a reliable decision on each individual transition.
These are different statistics from both whole-symbol correlation and a sum
of section energies. The latter discards relationships
between section phases, so its results do not bound a differential receiver.
See the primary study of
[differential combining for weak GPS acquisition](https://www.sciencedirect.com/science/article/abs/pii/S0165168406002696).

For illustration, the hobby-GPSDO phase model (0.5 degrees/√second) produces
5 degrees RMS random phase change over 100 seconds. That is small relative to
a 180-degree reversal, even though absolute phase can wander greatly over days.
At actual C/N0 of −47 dB-Hz, however, a 100-second section has only −27 dB
Es/N0. Accumulation must account for that noise, residual carrier error, timing,
competing patterns and the search's false-alarm budget. Clock stability alone
does not give a reception probability. GPS discipline improves long-term time
and frequency accuracy; short-interval stability also depends on the local
oscillator and control loop ([NIST](https://www.nist.gov/publications/measurement-transient-environmental-effects-gps-disciplined-clocks)).

The production receiver retains that whole-symbol match and also fits four
fixed quarters for patterns lasting at least 16 seconds with at least 16
complete chips per quarter. Each quarter has its own unknown amplitude and
phase. The receiver combines explained energy across the complete bit while
excluding the strongest quarter, requiring support beyond an isolated burst.
It accounts for the extra fit coefficients and pays an `ln(2)` evidence penalty for choosing
between the coherent and section scores. This tolerates channel changes between
quarters while retaining the known transitions inside each one. The waveform,
wire count and full-symbol completion rule are unchanged. See the
[score and compute model](simulation-estimates.md#probability-model).
Compact receivers keep the coherent path if the added section state cannot fit
their RAM allowance.

Four sections still require useful coherence within each quarter. With the
0.5-degree/√second model, a quarter of a 37-day bit has roughly 447 degrees RMS
phase movement; this receiver does not supply arbitrary 100-second comparisons
over that duration. More flexible differential and channel-tracking detectors
remain distinct possibilities. The live constellation shows phase differences
and current amplitude for display; its plotted values are not decoder inputs.

Power changes can reduce a coherent match without the vector cancellation that
phase changes can cause. The new separate section gains accommodate some of
these variations, but neither detector permits arbitrary independent gain and
phase at every chip: that would absorb the transitions distinguishing the bits.
Numeric **RX reference** values retain the coherent-branch model with its
detector-choice penalty. The extra branch's reception gain is unquantified;
the reference is not a proven lower bound.

## Run an analysis

```sh
./build/pump analyze-link --text a --bw 100 --target-snr -36 \
  --tx-dbm 3 --attenuation-db -200 --coherent-seconds 3600 --trials 10000
```

The target -36 requests three dB of integration margin relative to the actual
-33 dB-Hz channel. It selects a 251,188.643-second symbol at 100 Hz. The target
does not change channel power. To examine a specific observation duration:

```sh
./build/pump analyze-link --bits 011 --bw 100 --symbol-seconds 250000 \
  --simulation '3dBm -200dB' --coherent-seconds 20000 --trials 10000
```

Custom power/attenuation inputs also allow cases beyond the GUI presets. Their
attenuation convention is negative: use `--attenuation-db -210`. Preset and
custom link inputs cannot be mixed. The GUI offers
`3dBm -200dB` and `3dBm -230dB` for the existing sampled simulation; adding a
preset does not establish that the receiver can decode it.

The GUI's **Oscillator model** dropdown and CLI `--oscillator` option select
free-running crystal, hobbyist GPSDO/XO without an oven, GPSDO/TCXO without an
oven, or GPSDO/OCXO impairments. They change both the sampled channel and the
estimates. See [oscillator models](oscillator-models.md) for the illustrative
values and the distinction between GPS frequency accuracy and phase stability.
An individual `--clock-error-ppm` or `--phase-noise` value overrides that part
of the selected model. The examples and table above use the unchanged
free-running-crystal default.

Useful comparison controls are `--phase-noise` (degrees/sqrt(second)),
`--coherent-seconds`, `--noise-figure-db`, `--hypotheses`,
`--residual-frequency-hz` and `--template-correlation`. Defaults are 0.5,
3600 seconds, 10 dB, one million hypotheses, zero residual frequency and
orthogonal template projections, respectively. `--false-alarm` defaults to
1e-6 for the complete hypothetical search. `--seed` makes runs repeatable
within an implementation; standard-library random distributions need not
produce identical draws across compiler libraries.

## What the JSON means

- `transmission` retains the actual wire-bit count and sampled waveform
  geometry. `symbol_duration_source` identifies an explicit duration override;
  in that case the displayed TX target did not choose the final duration.
  `simulated_seconds` includes the existing completion tail.
- `current_receiver` describes the existing receiver's requested search,
  coverage limits and reference CPU/GPU compute estimates. When the carrier
  is outside its search or the modeled memory does not fit, its probability
  remains unavailable. Compute time in this case describes requested work,
  not a simulation that is guaranteed to run or decode successfully. The GPU
  estimate remains hypothetical; the production receiver runs on the CPU.
  `tracking_seconds` is the modeled serial continuation component included in
  both CPU/GPU totals, and `tracking_symbol_windows` includes fully scored
  absence. Competing/noise tracks and reacquisition are not upper-bounded.
  For eligible long patterns, its numeric probability is the coherent reference,
  while compute costs include the implemented four-section work.
- `ideal_coherent` assumes perfect phase stability and zero residual carrier
  error across the whole symbol.
- `coherent_phase_model` applies the selected phase diffusion and residual
  frequency over the whole symbol.
- `segmented_phase_model` accumulates energy from shorter coherent segments.
  Its arbitrary segment count, prescribed template correlation and statistical
  threshold remain a separate experiment, not a simulation of the production
  four-section detector.

The experiments condition on matched symbol timing and sample-clock rate.
Their duration is the nominal transmitted symbol duration.
`--frequency-offset` and `--clock-error-ppm` describe the actual channel for
the current receiver estimate. `--residual-frequency-hz` instead describes
what a hypothetical acquired/tracked filter would leave uncorrected. A zero
residual does not mean the real receiver has acquired the signal.
`link.zero_residual_coherent_energy_asymptote_linear` is linear Es/N0 for an
uncorrected coherent filter at zero residual frequency; it is null when phase
diffusion is zero and there is no finite asymptote.

`correct_detection_probability` is a per-symbol reference-detector event:
the signal statistic beats its prescribed alternative and threshold. It is
not whole-message RX success. The Wilson interval describes finite Monte Carlo
sampling uncertainty within that model, not uncertainty about real-world
performance. A zero `noise_pair_above` count does not verify extremely small
false-alarm rates. Only two noise projections are sampled per trial; the
separate analytic bound accounts for the supplied total hypothesis count.

## Why uninterrupted coherent integration stops helping

Write `r = 10^(C/N0 / 10)` and phase diffusion `sigma` in radians/sqrt(second).
For duration `L` and residual carrier frequency `f`, the mean normalized
correlation energy of the constant-envelope reference waveform is

`eta = 2 Re[(z - 1 + exp(-z)) / z^2]`,
where `z = (sigma^2/2 - i 2 pi f) L`.

The zero-noise, zero-offset limit is one. Pure constant frequency error gives
squared-sinc loss. Diffusion and frequency are integrated jointly; multiplying
separate loss factors is not generally correct.

With zero residual frequency and the default 0.5 degree/sqrt(second), the
1.46-day symbol loses about 4.80 dB of expected coherent energy. As duration
increases, one uncorrected coherent filter approaches expected energy
`4 r / sigma^2`: only **14.20 dB** at C/N0=-33 dB-Hz. This is a limitation of
that receiver and channel model, not an impossibility theorem for communication.
Within-observation phase fluctuations matter; see the filtered Wiener phase
model in [Ghozlan and Kramer](https://arxiv.org/pdf/1503.03130).

The current carrier search is another constraint. At 125,893 seconds per bit,
its 4097-frequency cap covers approximately +/-4.07 mHz, while the default
100 ppm shift at a 1500 Hz carrier is 0.15 Hz. A complete +/-200 ppm search
would need about 302,145 frequencies before timing/clock alternatives.
Longer integration alone does not supply that coverage.

## The reduced statistical experiment

Equal segments cover the entire symbol: `K = ceil(T / requested_L)` and
`L = T / K`. No final partial interval is discarded. The reference model uses
independent unit-variance circular complex Gaussian noise after correlation.
For a fixed total matched signal energy `g = r T eta`, the desired energy
statistic has distribution `2 X ~ noncentral chi-square(2K, 2g)`; noise alone
has `X ~ Gamma(K,1)`. These distributions can be sampled in constant space
without generating K segments. Tests compare the reduction to independent
explicit complex segment simulations.

The alternative template has the same prescribed normalized correlation
`rho` in every segment. Zero is the orthogonal reference; at one, the two
templates cannot be distinguished. This is not a measurement of DataPump's
finite APSK waveforms. In particular, very narrow bands can have few chips
within a segment; inspect `chips_per_segment` and do not assume orthogonality
from the configured bandwidth alone.

For M tested alternatives and total noise false-alarm budget alpha, set
`x = ln(M/alpha)` and threshold `q = K + sqrt(2 K x) + x`. The Gamma tail bound
and union bound give a conservative noise-only bound of alpha across M cells,
even if search cells are correlated. The supplied M must include all relevant
time, carrier, clock, bit, epoch and key trials. This is not the production
receiver's adaptive evidence threshold, and the complete search is not run.
`--hypotheses` is prescribed independently of `current_receiver` geometry;
it is not an automatic count of that receiver's search. For example, the
-36 target above needs about 602,855 frequencies for full +/-200 ppm
coverage, before other alternatives, while the reference default is one
million cells. After counting both bit labels and the other alternatives,
that complete production search exceeds the reference default. Increase M
when examining a larger search.

Nonzero phase diffusion replaces random path energy with its exact mean eta.
The resulting detection distribution is therefore an approximation that omits
phase-fade variability, marked by `phase_mean_energy_approximation`. It is
particularly limited for a single long coherent segment. It also omits actual
timing acquisition, evolving clock/Doppler trajectories, interference,
waveform-dependent Gram matrices and the modem's adaptive decisions.
[ESA's acquisition discussion](https://gssc.esa.int/navipedia/index.php/Baseband_Processing)
describes the coherent/noncoherent integration tradeoff and squaring loss.

The implemented fixed four-section fit is a bounded first step. Shorter or
adaptive sections and explicit clock/drift trajectories need their own scoring,
resource limits and sampled signal/noise validation. This planning command
does not change transmission, symbol admission, pending-bit progress or
physical completion.
