# Conditional physical admission on native ADC samples

The isolated continuous receiver can use a frozen physical noise profile with
`--physical-noise-profile PROFILE.json`. This selects a **fixed public
real-waveform likelihood**, separate from the default adaptive timing receiver.
It is a scheduled one-bit admission path, not ordinary Robust integration,
message completion, general blind acquisition or an unconditional physical
false-admission certificate.

The initial supported hardware path is explicit native ALSA `hw:C,D`, with a
negotiated ADC sample rate, channel count and selected channel matching the
profile. The DATA bands must lie strictly between DC and ADC Nyquist. Helper
is currently excluded: deterministic pilot leakage under a DATA-absent null
needs its own model. Background-noise, mixer, device and signal conditions
outside the profile do not inherit its evidence.

The [short cable qualification](validation-data/triad-v3/2026-11-06-physical-admission/README.md)
records two correct DATA admissions and one rejected independent noise control,
including actual ADC/DAC rates, retained failures, CPU cost and uncertainty.

## Why this representation

The native audio tool's `--capture-format adc-v1` exports the selected real ADC
channel before capture resampling. The existing full-audio clock controller and
ASRC remain active; their output is processed but is not the observation used
by this likelihood. Thus a changing local resampling map cannot invalidate this
receiver's ADC noise model. Requested and negotiated rates must agree. Packets
retain exact uniform integer native coordinates and bounded buffering.

This does not remove remote timing uncertainty. Templates use the public
arrival/frequency/clock/epoch bank and channel priors fixed before input.
Remote nonlinear timing tracking remains a separate experiment. No transmitter
clock trace, fitted noise variance or selected timing trajectory is supplied
to the admission statistic.

Each disjoint ADC block retains positive integer DFT modes without DC,
Nyquist or conjugate duplicates. `sqrt(2) [Re FFT, Im FFT]` is a real
orthonormal projection. Both quadratures of the full real transmitted waveform
are projected identically, retaining image terms and improper complex noise
relationships rather than assuming analytic white noise. Constant ADC DC is
annihilated. Time-varying deterministic interference is not thereby removed.
Channel coefficients have two real components per complex tap. The maintained
Gaussian-state algebra is reused with half its complex log likelihood **before**
mixing fixed candidate and channel-model priors.

## Conditional false-admission argument

For the **complete** zero-mean real ADC noise vector `n`, the profile assumes

`E exp(tᵀ n) <= exp(v ||t||² / 2)` for every real vector `t`.

This is a global joint sub-Gaussian proxy, including dependence between blocks.
For jointly Gaussian noise, covariance domination `Cov(n) <= v I` suffices.
Marginal Gaussian appearance or an observed PSD maximum alone does not establish
the joint assumption. See the joint formulation in
[Hsu, Kakade and Zhang](https://www.cs.columbia.edu/~djhsu/papers/quadratic-ecp.pdf).

After the fixed orthonormal projection, for any fixed signal-prior mixture,

`L(y) = integral exp(muᵀ y/v - ||mu||²/(2v)) dPrior(mu)`

has null expectation at most one. This follows directly by integrating the
assumed moment bound. The real Gaussian channel likelihood computes this
mixture. Markov's inequality gives the declared endpoint bound; the maintained
operational policy accounts for the permitted attempts and alternatives over
a day. No independence between acquisition attempts is required for that union
bound. Prefix scores are diagnostic, not extra decisions. This is a fixed-endpoint
argument, not a license for arbitrary data-dependent stopping or prior selection.
The general expectation-at-most-one test framework is described by
[Shafer](https://www.glennshafer.com/assets/downloads/articles/article104_jrss_shafer-testingbybetting-with-discussion-response.pdf).

**A short cable experiment cannot establish that future physical noise obeys
this global bound with daily failure probability `1e-4`.** The profile records
this assumption explicitly. Its finite diagnostics check for contradictions;
they do not manufacture a rare-event guarantee from a few noise recordings.
The conditional noise-only admission bound is also not a bound on wrong-bit
decisions when a signal is present.

## Qualification and inexpensive reuse

1. Record native real noise with the intended hardware/mixer/background-noise
   settings. Use `triad_physical_noise_profile.py prepare` to freeze the
   variance proxy, source/settings identities, geometry and diagnostic policy.
2. Record a **new** independent noise capture, then use `finalize`. Failed
   validation stays failed; it does not enlarge the bound using the holdout.
   Check the whole interval that the likelihood may score. Trimming is valid
   only when the omitted interval is explicitly outside the qualified scope.
3. Use `triad_physical_qualification.py` for short separate DATA and noise-only
   cable trials against that frozen profile. Truth selects TX/evaluation only.
   The same `PreparedPhysical` adapter is used by continuous reception.
4. Admission requires the complete declared ADC observation, a successful native
   process exit, an unclipped receipt, matching hardware/source/mixer identity,
   and evidence that the clock/ASRC pipeline ran. Incomplete or discontinuous
   input is not padded with fictitious observed zeros.

The profile builder checks measured PSD, variance, block fluctuations, peaks
and clipping against the fixed proxy. These are engineering contradiction
checks, not confidence intervals or guarantees against all nonstationarity.
Explicit source, arithmetic, bank, device and mixer bindings prevent accidental
reuse across changed conditions. Do not copy a passing profile to a radio or a
different noise setting without new relevant evidence.

For controlled DAC-noise experiments, the duplex driver additionally requires
`binding.allowed_noise_dac_rates_hz` and checks the **actual hardware** rate in
the playback receipt. Logical playback rate is insufficient: this codec chose
44.1 kHz for a 6 kHz source, while the ADC remained at 48 kHz. A generic remote
receiver cannot observe the remote DAC; that part of its profile remains an
operator-supplied operating condition. Changed rates require relevant noise
evidence, even when the existing conservative bound ultimately still passes.

Cheap device-free checks are:

```sh
python3 tests/test_triad_audio_stream.py build/dev-cli/triad_audio_stream
python3 tests/test_triad_physical_admission.py
python3 tests/test_triad_physical_noise_profile.py
python3 tests/test_triad_physical_run.py
python3 tests/test_triad_physical_qualification.py
```

The Python receiver checks require the existing NumPy/SciPy/PyTorch environment.
They cover dense real covariance equivalence, correlated/improper noise,
FP32/FP64, chunk boundaries, source coordinates, profile mismatches, incomplete
input and native receipt failures. Reuse the matching short physical evidence;
unrelated edits do not require repeating historical multi-day campaigns or
ordinary-receiver regressions.
