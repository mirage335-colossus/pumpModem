# Cheap theory screens for the broad coverage campaign

These are explicit mathematical models and a retained-template diagnostic, not
new sampled-channel receptions or a qualification of the user's configuration
Cartesian product. No ordinary receiver code was changed.

## Reproduction

From the repository root, with the standard Python interpreter:

```sh
python3 -m unittest discover -s tests -p test_triad_coverage_bounds.py
python3 tools/triad_coverage_bounds.py --gaussian-widths 0.001,0.005,0.05,0.1,10,55 --output /tmp/triad-coverage-bounds-fresh.json
```

The accepted source-bound result is `coverage-bounds-source-bound.json`, SHA-256
`236221bf37cbbdf79922a3da327ca07cb3a331ed55091b37e1425fd99324f2dd`.
The earlier `coverage-bounds.json` has the same numerical computation before the
source-digest field was added; do not substitute it for the source-bound receipt.
The tool source is `tools/triad_coverage_bounds.py`, SHA-256
`bb4e1cbf161c5f4b5d9af151e995c357ef0444907d31c312a215c19b2b3a0c5b`.
The 16 focused tests passed in 0.004 seconds. The report covers all 19 requested
C/N0 values, all 17 rate geometry references, and 114 separately requested
continuous-Gaussian necessary-information screens. The latter widths are
explicit stress laws; their coinciding numeric values with preset ray-center
separations do not map one law onto the other.

## Known-channel energy and presence references

For proper-complex white noise CN(0,sigma^2 I), equal-prior known means have
optimal bit error `Q(sqrt(||mu0-mu1||^2/(2 sigma^2)))`. For equal energy
`rho=||mu_b||^2/sigma^2` and real normalized correlation `r`, the error is
`Q(sqrt(rho(1-r)))`. Antipodal means have `r=-1`, orthogonal means `r=0`.
Global-phase alternatives can become indistinguishable when phase is unknown;
the favorable antipodal reference must not be used as an achievable triad bound.

A receiver favorably given the exact transmitted waveform, realized channel,
alignment, phase and white-noise variance requires

`rho >= 0.5 * (Phi^-1(Pd) - Phi^-1(alpha))^2`

for signal presence at detection probability Pd and false probability alpha.
Giving this known waveform the entire one-completion horizon allowance yields:

| Target | Energy rho | At -60 dB-Hz, unit useful-energy efficiency |
| --- | ---: | ---: |
| 99%, alpha=1e-4 | 18.273215 | 211.4955 days |
| 90%, alpha=.01 | 6.508469 | 75.3295 days |

These are favorable presence floors, not feasible duration prescriptions.
At the same -60 dB-Hz and 38 days of unit received active power, even that
oracle has Pd=0.123735 at alpha=1e-4 or 0.593342 at alpha=.01. Actual channel
energy gain or loss changes these values; nominal elapsed time is not energy.
The report separately applies declared attempts, bit/model/duration choices and
score-budget reserve. That Bonferroni allocation is a design choice, not a
universal necessary search penalty. Strict and relaxed horizons respectively
use 1,440 and 720 attempts at the default 60-second spacing.

With active-time C/N0 `c`, retained energy fraction `eta`, and duty cycle `d`,
`rho=c*eta*d*T_wall`. Thus 20 dB lower C/N0 requires 100 times longer in this
coherent-energy reference, and a 1/3 duty cycle costs three times elapsed time
when active-time power is unchanged. Position-dependent outages, phase evolution,
reacquisition and backlog are not included by that scalar conversion.

## An explicitly different diffuse-channel screen

For the continuous zero-mean Gaussian covariance envelope
`|K(t,s)| <= c*P*exp(-(pi*B*(t-s))^2/2)`, define

`J(T,B) = T*erf(pi*B*T)/(sqrt(pi)*B) - (1-exp(-(pi*B*T)^2))/(pi*B)^2`.

Then `KL(signal || white noise) <= min(c*P*T, (c*P)^2*J/2)`.
Data processing requires this divergence to reach
`d(Pd||alpha)=Pd*log(Pd/alpha)+(1-Pd)*log((1-Pd)/(1-alpha))`.
Below that value, the target is excluded for this explicitly stated model;
above it the screen is inconclusive. For large BT, `J ~ T/(sqrt(pi)*B)`,
showing a possible `T proportional to B/c^2` necessary scaling instead of
coherent `1/c`. Stable deterministic rays are outside this premise. The tool
does not add the sampled selective operator, finite Gaussian FIR covariance,
unknown clock interpolation, or a measured propagation model. Existing corrected
operator bounds need their separately verified source/clock/envelope assumptions.

## Harmonic expansion diagnostic and decision

`harmonic_probe.py` and `harmonic_probe.json` preserve a 1.564-second CPU
calculation using the retained public shaped basis with SHA-256
`376f2fe4cfc30b7da313a88bf80ab7cc291b4f774c42cc713c54441a1401f09a`.
This script uses NumPy/SciPy (the recorded interpreter was
`/tmp/triad-weak-env/bin/python`) and one BLAS/OpenMP thread. It contains the
recorded local repository/input paths. The actual noisy observation was unused.

Two stable delayed/Doppler-shifted ray templates are passed through an
independent exact finite cubic-FIR/DFT expansion. The identity plus three
branches, two sinusoid rates and positive/negative sidebands gives 26 complex
columns per bit. All full filter tails are retained. This is an algebraic
constructed-signal test, not an independent physical channel experiment.
Across 64 independently seeded modulation-phase/ray-phase draws per bit/depth:

- Rank was 26 for both bits; minimum/maximum singular ratio was at least .1366.
- Correct expanded-subspace projection residual was at most 1.1e-14 relative.
- Maximum inter-bit canonical correlation was .2564.
- Simple global two-ray fitting retained median 71.0% of Each65 signal energy
  (worst 63.6%); Each25 retained about 94.4%.

For a fixed known-noise rank-r projector, its null statistic is Gamma(r,1),
while twice its statistic under a fixed signal is noncentral-chi-square with
2r degrees of freedom and noncentrality 2*rho. Numerically inverting these
exact distributions gives required projected energy for 99%/1e-4:

| Rank | Required projected rho |
| ---: | ---: |
| 2 | 23.9171 |
| 26 | 46.6828 |
| 72 | 67.3857 |
| 960 | 203.8695 |

The unstructured 26-column fit therefore costs more nuisance dimensions than
its typical 1.5-dB recovered AM energy justifies in this example. Do not add it
as a default merely to collect all sidebands. The consequential comparison is
global/physically constrained coherent fitting versus unnecessarily independent
short-window gains on stable channels. Real oscillator phase evolution, timing
and tap mismatch must be tested before claiming the global fit works. The
current source template and propagated fractional-delay composition also differ;
adding delay/Doppler taps alone is not the exact finite-FIR selective expansion.

## Numerical and physical pitfalls

- C/N0 is total triplet power/noise density, not bandwidth SNR; sample SNR is
  `C/N0 - 10 log10(Fs)` for the stated unit-power sample convention.
- FP32 signal-plus-noise addition becomes risky around 144.5-dB sample SNR;
  covariance addition or total-minus-projection residuals can become risky
  around 72.2 dB. Corresponding FP64 scales are 319.1 and 159.5 dB. These are
  scale warnings, not exact failure thresholds. Structured `I+low-rank`
  likelihoods and directly formed residuals can avoid cancellation without
  making all bulk work FP64. Quantization already lost in stored statistics
  cannot be recovered by later wider arithmetic.
- Spread presets describe stable ray-center separation and excess delay. They
  do not specify diffuse width B or imply its quadratic low-C/N0 penalty.
- Corrected spacing-scaled AM imposes additional memory `2/(3 spacing)`;
  changing rate changes that synthetic channel unless a fixed selectivity
  scale is also compared. It is separate from geometric ray delay.
- Large CFO is a capture problem before it is a search problem. No differential
  invariant can recover energy excluded by the analog/digital capture band.
- Rate/spreading changes cannot provide free useful energy at fixed power and
  duration. They can alter distinguishability, channel degrees of freedom,
  capture loss, interleave/erasure behavior and computational cost.

## Primary research sources consulted

- [Tse and Viswanath, Appendix A](https://web.stanford.edu/~dntse/Chapters_PDF/Fundamentals_Wireless_Communication_AppendixA.pdf): Gaussian binary detection and sufficient statistics; the formulas above are derived for the stated proper-complex normalization.
- [Tse and Viswanath, wireless-channel chapter](https://web.stanford.edu/~dntse/Chapters_PDF/Fundamentals_Wireless_Communication_chapter2.pdf): distinct delay, Doppler and LTV channel parameters. It does not calibrate the synthetic stress laws here.
- [Durisi, Boelcskei and Shamai, peak-constrained noncoherent WSSUS capacity](https://www.mins.ee.ethz.ch/pubs/files/dbs_isit07.pdf): wideband noncoherent performance depends on peak constraints and channel scattering; increasing bandwidth/spreading is not universally beneficial. The capacity bounds are not directly a one-bit duration prescription.
- [NIST DLMF floating-point arithmetic](https://dlmf.nist.gov/3.1): binary32 and binary64 significand widths and unit roundoff used for the scale diagnostics.
- [NVIDIA floating-point guidance](https://docs.nvidia.com/cuda/floating-point/index.html): finite-precision operation ordering/FMA behavior; CPU/GPU arithmetic should be compared at actual decisions, without assuming bitwise identity across platforms.

No new RunPod resource, subprocess beyond the completed local probes, or ordinary
receiver change is owned by this theory subtask.
