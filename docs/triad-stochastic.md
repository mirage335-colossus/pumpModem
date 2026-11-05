# Random-channel three-carrier study

This is the isolated triple Robust modem experiment. Ordinary Robust reception,
its wire format and its regression campaign are outside this study. Start with
the [efficient development workflow](triad-development-workflow.md), not a large
historical simulation campaign. New stochastic reception results must identify
their channel law; earlier fixed-ray/sinusoidal results do not qualify it.

## Physical model and reproducibility

The emitted Helper and DATA waveforms pass through one continuous linear,
time-varying channel. A ray has a delayed source waveform, a complex random
gain, a declared center Doppler and a changing propagation phase. Independent
noise is added afterward. Guards, processing gaps and missed observations do
not reset the channel or suppress its echoes. Overlapping carrier spectra are
never assigned to separate carrier-labelled filters.

The existing deterministic model remains a replay control. The new explicit
stochastic selection uses a stationary, proper-complex Gaussian ray process.
Its width `B = 2 sigma` describes each ray's Gaussian Doppler spectrum; it is
**separate from the existing ray-center separation**. Random gains and a fixed
local delay are a legitimate scattering model: a constant delay does not imply
a constant channel. The [ITU-R F.1487 HF model](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf)
uses delayed paths with random complex Gaussian gains and separately specified
Doppler centers and widths. Its HF scope does not make it a calibrated model
of every microwave, EME or VLF path.

`fixed-grid-gaussian-v1` generates seeded innovations on a physical-time clock
of `16 B`, applies a normalized 53-tap Gaussian FIR and interpolates with a C1
cubic. The interpolation variance is normalized analytically. Thus changing
capture sample rate or chunk size does not change the underlying random field
at shared physical timestamps. This finite/interpolated approximation has
spectral tails; the sampling allowance is an engineering convention, not a
hard band limit. Existing RX-rate OU/Gaussian laws retain their replay semantics.
The randomized knot origin makes the process stationary as a mixture of
conditional Gaussian fields; Gaussian one-time marginals do not establish an
exact jointly Gaussian law after marginalizing that origin.
The exporter identifies this selection as `channel_evolution=fixed-time-gaussian-v1`
and the transformed selective coefficients as `gaussian-erf-v1`. Legacy remains
an explicit reproducible control; request `--channel-evolution stochastic` for
this study rather than assuming an old preset silently changed its channel law.

Optional geometric delay motion joins bounded random knot values with a C2
quintic. With excursion `A` and knot interval `T`, its absolute derivative is
bounded by `3.75 A/T`. History and draining use the maximum permitted delay,
not the maximum observed realization. The same delay enters source interpolation,
hop coordinates and propagation phase. In a DATA-centered capture the latter
uses `RF shift + DATA IF`; motion-induced Doppler is counted once. Large delay
changes at microwave frequencies therefore require very long time scales if
the Doppler budget is small.

Additional selective modulation retains the existing causal three-branch DFT
delay operator, but its branch coefficients can now vary randomly. A unit
Gaussian process `g` becomes `u = erf(g/sqrt(2))`, with uniform marginal on
`[-1,1]`; each selected depth is a bounded peak excursion. Slow width is one
tenth of fast width. This is a **synthetic dispersive stress model**, distinct
from geometric path delay: at 0.00075 Hz spacing its operator still has about
889 seconds of memory. It must not be described as selectivity caused solely
by a millisecond propagation delay.

Independent slow and fast coefficients have branch mean-square gain
`1 + (d_s^2 + d_f^2)/3`, versus `/2` for the sinusoidal control. Each65 therefore
has about 0.453 dB less nominal branch power than the sine control. Whole-capture
received energy must still be measured; multipath, branch interference and the
transmitted spectrum prevent substituting this factor for actual DATA energy.

## Receiver assumptions

The dynamic adapter retains the complete complex three-carrier pattern. It
accumulates block correlations and Gram matrices before marginalizing evolving
physical ray gains with the maintained Gaussian-state likelihood. The static
two-ray model is the comparison arm. A finite public coherence/model family
is mixed with declared weights, not fitted through arbitrary symbol-by-symbol
carrier gains.

With Helper present, compare `Helper + DATA(bit)` against `Helper`; without
Helper, compare DATA against noise. Both straight pilot tones are public
templates. Their information is uncertain and shares the physical channel;
they are not perfect phase references. Auxiliary metadata is not assumed decoded.
The first adapter projects disjoint raw blocks onto a public union of DATA and
pilot Fourier modes while excluding the auxiliary band. Identical orthonormal
projections apply to observations and templates, preserving white raw thermal
noise. Actual auxiliary emissions remain in the sampled experiment. Their
finite-window leakage, random-channel sidebands and model mismatch require
measurement and complete-procedure calibration.

Short blocks can lose frequency separation; long blocks can violate the
constant-within-block channel approximation. An auxiliary conflict or alias
guard rejects this adapter regime explicitly. This is an implementation limit,
not proof that the physical link is impossible. Helper-disabled operation has
no auxiliary exclusion. A selected Gaussian inference law is an approximation
to the independently generated Gaussian-spectrum/bounded-delay channel.

An explicit `core` projection can retain only the nominal DATA/pilot core when
the full added-AM envelope conflicts with the auxiliary band. It does not
truncate the simulated channel or renormalize the retained signal. Use its
measured component leakage and separately calibrated complete-waveform results,
not the omitted allowance as a claim of absent sidebands.

The tiled acquisition planner checks continuous timing uncertainty before
sharing pilot/DATA phase. It includes endpoint coverage, clock drift and declared
delay excursion. If unresolved, it deterministically uses all three DATA
carriers and reports `helper_phase_timing_unresolved`. A DATA-rate timing grid
can be much too coarse for separated Helper pilots. An explicitly discrete
prior is a separate contract, not a way to relabel an unsearched continuous
interval. See the retained failed joint campaign and corrected fallback in the
results report. Conditioned exact-arrival diagnostics remain separate.

### Frequency assistance survives the phase fallback

Use `HelperFrequencyAcquisition` in `tools/triad_dynamic_acquire.py` for
Helper-assisted stochastic acquisition. `TiledAcquisition` remains the explicit
full-frequency reference. The adaptive adapter runs the pilot frequency
estimator even when the timing guard reports `helper_phase_timing_unresolved`.
Unknown constant pilot phase or unresolved pilot-to-DATA phase transfer does
not itself invalidate pilot frequency information.

`triad_dynamic_frequency.py` coherently transforms a public 36-second complex
pilot aperture, then profiles Gaussian ray-core powers and a local background
independently for each pilot. It uses phase evolution within that aperture;
it neither treats instantaneous pilot phase as a DATA reference nor requires
auxiliary decoding. It retains the union of competitive frequency regions,
including Doppler aliases, and propagates the public audio-clock and RX-rate
uncertainties to DATA-center frequency. Clock hypotheses remain unchanged.
One usable pilot can help. Weak, ambiguous, excessively broad or unsupported
observations retain the full original bank. A flat but present pilot also
requires fallback; silently discarding it would invalidate coverage calibration.
The FFT uses complex FP32; the small batched spectral fits and reductions use
CPU FP64. Their cost is reported separately and can dominate a large amortized
campaign. This frontend is not a GPU implementation or a claim that changing
the receiver precision also changes its arithmetic.

All captures use the same full-prior observation projection. Each frequency
tile is prepared once for the batch and scored only for captures retaining it.
Per-capture likelihoods are normalized by their own selected hypothesis count.
The stable outer procedure identity binds estimation, frequency fallback,
independent phase fallback, source, templates and configuration. A realized
subset is an execution receipt, not a new calibration identity.

The spectral score and its default allowances are engineering models, not
chi-square confidence intervals. Frequency-coverage training, admission
calibration and validation must use separate seeds. The maintained study CLI
`tools/triad_dynamic_frequency_study.py` consumes existing native captures,
shares templates, compares the full and assisted banks, and reads evaluator
truth only after scoring and admission decisions. Without calibration rows it
reports rankings and timing only. Its schedule is a JSON array of
`{id, directory, phase}`, with phase `calibration`, `signal` or `null`.
Do not replay a historical sensitivity campaign for a routine edit: use
`python -m unittest discover -s tests -p 'test_triad_dynamic*.py'` first, in the
prepared NumPy/SciPy/PyTorch study environment.

Frequency information is dated. The wrapper refuses a narrowed result whose
declared horizon does not cover the scored capture and uses the actual receiver
block length for its Wiener average-frequency allowance. This is not an
instantaneous-frequency bound or multi-day drift qualification. The native
exporter currently uses zero deterministic RF drift; another source requires
an explicit drift bound or refreshed estimation. Gated pilots and physical
retuning currently fall back. Broad timing can overlap DATA in the pilot
aperture only when the public fitting envelopes permit it; actual leakage stays
in the sampled observation and requires calibration.

For physical motivation and the limits of spectral channel models, see
[Gappmair and Bergmann's carrier/channel estimation study](https://ietresearch.onlinelibrary.wiley.com/doi/10.1049/iet-com.2015.0080).
Its Rician, negligible-multipath assumptions do not prove this modem's two-ray,
random selective-modulation estimator optimal. The separate
[frequency-assistance qualification](validation-data/triad-v3/2026-11-05-helper-frequency/README.md)
records the implemented model, measured narrowing, reception and remaining limits.

## Whole-bit Symbol Burst

The new opt-in calendar is distinct from v3's within-bit `burst_wait_tiles`.
Each Helper-assisted bit has a fresh 38-second lead before **all** DATA pulse
support, including its leading halo. The logical keyed pattern begins after
that halo. The existing production Helper punctuation endpoint is protected.
Source silence leaves one continuous channel running.

On the maintained UTC 30-second support grid, the proposed 76-second minimum
becomes 90 seconds. The next support starts at

```
ceil_30s(max(previous_support + 76,
             previous_helper_end + wait_count * bit_duration + 38))
```

The next Helper lead is not counted as receiver processing silence. Longer
bits and requested backlog waits increase the interval. Integer UTC seconds
and sample phase are retained separately. Each v3 source keeps its existing
integer-second key epoch and explicit symbol ordinal; fractional placement is
recorded in the calendar and templates, not added to the cryptographic format.

## Evidence levels

### Sensitivity reference for a renewing unknown channel

Let `rho=C/N0` in linear inverse seconds. With known timing, no reference,
proper-complex zero-mean Gaussian fading and two fixed waveforms, whitened
observations have covariance `Sigma_b = I + rho K_b`. Their exact complex
Gaussian KL divergence is `tr(Sigma_1^-1 Sigma_0) - logdet(Sigma_1^-1 Sigma_0) - n`.
The Hessian of `-logdet` on matrices at least `I` gives the upper bound
`D <= rho^2 ||K_0-K_1||_F^2 / 2`. Positive-semidefinite covariance matrices have
nonnegative cross trace. For unit-envelope, equal-energy signals of duration T,

```
tau2 = integral |R_h(u)|^2 du
D <= rho^2 T tau2
P(correct) <= 1/2 + sqrt(rho^2 T tau2 / 8)       [equal priors; Pinsker]
```

An ideal Gaussian Doppler spectrum with width `B=2 sigma` has
`R_h(u)=exp(-pi^2 B^2 u^2/2)` and `tau2=1/(sqrt(pi) B)`. At −60 dB-Hz and
38 days, these optimistic ceilings are 51.52%, 50.22% and 50.009% for random
widths 0.001, 0.05 and 30 Hz respectively. They concern this Gaussian model,
not the existing preset center separations. With peak power kappa relative to
unit average and fixed energy T, multiply the KL upper bound by kappa.

This is not a universal bound on the implemented transformed-Gaussian AM and
moving-delay mixture. Its covariance and distribution must be treated separately;
even the finite/interpolated teacher requires conditioning on its grid origin
and bounding the conditional covariance, or an explicit Gaussian approximation.
Its measured squared-coherence integral alone is insufficient to prove a
Gaussian divergence bound for the mixture. The calculation explains a serious possible failure mode
and motivates model-specific experiments. A known static channel instead has
`E/N0=3.2832` at that power/duration, giving 96.5% ideal orthogonal-bit correctness,
before acquisition or presence admission. These very different assumptions must
not share one duration-planner curve. Low-SNR capacity also depends on allowed
signal peakiness and coherence; see [Zheng, Tse and Médard](https://web.stanford.edu/~dntse/papers/lizhong_low_snr_revised.pdf).

Pilot conditioning changes both the channel mean and its remaining covariance.
For Gaussian state covariance R and pilot design P,
`V = R - RP^H(PRP^H+N_p)^-1 PR`; DATA then has mean `A_b mu` and covariance
`N_d + A_b V A_b^H`. Reliable pilots can restore useful coherent evidence;
weak pilots cannot supply a free reference. At −60 dB-Hz DATA, Helper/DATA ratios
10 and 375 provide only 0.00036 and 0.0135 total energy SNR in 36 seconds,
before splitting Helper power among its components.

The delay-motion budget is also material: `|f_motion|=|f_c tau_dot|`. A 0.5-ms
excursion at 5.8 GHz with a 6-Hz motion budget needs a knot interval at least
20.98 days under the bounded quintic law. Faster random complex fading is still
possible with locally fixed delays. EME needs its own geometry-dependent model;
[NBS lunar-fading measurements](https://nvlpubs.nist.gov/nistpubs/jres/64D/jresv64Dn5p455_A1b.pdf)
support that distinction, not universal adoption of an HF spectrum.

### Qualification sequence

Keep channel moment/covariance, tone response, shared-time/chunk invariance,
spectral refinement and tail checks separate from receiver success. Start with
publicly conditioned timing/oscillator controls, then expand acquisition. Use
paired observations for algorithms, independent seeds for validation, and
separate correct admissions, wrong admissions, misses and DATA-absent admissions.
Report source energy, received energy, model loss, simulated duration and wall
time independently. A small empirical per-capture gate does not establish the
requested operational daily false-admission probability.

Read the [source-bound sampled results and limits](validation-data/triad-v3/2026-11-05-stochastic/README.md)
before changing duration plans. The dynamic receiver improved the tested
random-channel cases substantially, but the −6-dB-Hz moving-delay case still
missed or wrongly admitted bits. Operational daily gates and multi-day stochastic
−60-dB-Hz reception remain unqualified. A long simulated span alone is not
sustained CPU/GPU evidence.

## Minimal development commands

Use the existing Python research environment with NumPy, SciPy and PyTorch.
Do not reinstall dependencies or run the sampled cohorts for an unaffected edit.
The five small dynamic modules took about seven seconds on the local CPU:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 -m unittest discover -s tests -p 'test_triad_dynamic_*.py'
```

For native model/calendar changes, reuse a compatible build:

```sh
./build.sh configure --cli
cmake --build build/dev-cli --parallel 2 --target test_triad_v3_channel test_triad_helper_burst triad_helper_export
ctest --test-dir build/dev-cli -R '^(triad_v3_channel|triad_helper_burst)$' --output-on-failure
./build.sh test triad --cli
```

Run the exporter smoke test only for exporter/source contract changes. The two
additional commands are occasional complete-waveform qualifications, not routine
receiver regressions:

```sh
python3 tests/test_triad_helper_export.py build/dev-cli/triad_helper_export
python3 tests/test_triad_helper_export.py build/dev-cli/triad_helper_export --whole-bit-qualification
python3 tests/test_triad_helper_export.py build/dev-cli/triad_helper_export --component-qualification
```

A bounded independent known-alignment cohort can be reproduced with:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 tools/triad_dynamic_campaign.py \
  --exporter build/dev-cli/triad_helper_export --output local-evidence/random-hf-new \
  --seed-base 1080000 --device cpu --threads 2 --batch-size 8 --export-workers 2 \
  --execution scan --projection core \
  --export-arg='--case hf --seconds 16 --cn0 0 --channel-evolution stochastic --random-doppler-width-hz .5 --selective-modulation-rate-hz 90 --helper on --timing-policy bounded --sample-rate 6000 --data-if 1725'
```

Use `--device cuda` on available hardware. The output must be new; the harness
freezes its schedule/procedure and retains failures. Default 39/32/32 calibration,
signal and null samples are an exploratory per-capture diagnostic, not a daily
gate. Generation, input loading, preparation and scoring have separate timers.
`triad_dynamic_replay.py --help` describes same-I/Q pilot/execution/projection
comparisons. Preserve its complete-procedure calibration; do not reuse gates
after changing receiver policies. Tiled acquisition prepares each tile once
for an entire trial batch: avoid calling it separately for every capture.

The [performance matrix](triad-performance-matrix.md#random-channel-and-joint-receiver-extension)
records the new paths and unsupported forecasts. Functional tests, a GPU speedup,
or a green historical performance guard do not independently validate runtime
extrapolation for this extension.
