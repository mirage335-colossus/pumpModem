# Measured HF complex-envelope fixture

`samples.hpp` contains the **2048 complex binary64 values** of `yDire` from
`ElecDirectwave.mat` in the open dataset associated with Mingkai Ding,
Peng Tong, Yinsheng Wei and Lei Yu, *Multiple Phase Screen Modeling of HF Wave
Field Scintillations Caused by the Irregularities in Inhomogeneous Media*,
Radio Science (2021), [paper DOI 10.1029/2020RS007239](https://doi.org/10.1029/2020RS007239),
[dataset DOI 10.5281/zenodo.4698276](https://doi.org/10.5281/zenodo.4698276).
The dataset is licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
This attribution and license apply to the measured data in `samples.hpp`.
No MATLAB algorithm, simulation output, or external modem implementation is
included.

The deposited interpretation identifies this as the measured direct wave at
range cell 280, received on **20 August 2019, 11:44:49–11:45:30**. The paper
describes a 12.7 MHz direct skywave over a 763 km ground path (891 km group
path). This is one receiver-terminal voltage sequence, with receiver filtering
and residual measurement noise still present. It is not a measured broadband
impulse response, isolated propagation-only phase, absolute path gain, or a
statistical population of disturbed days. The range-cell data alone does not
identify an arbitrary-bandwidth channel.

The source supplies no sampling frequency or time vector. The native replay
therefore explicitly assumes **50 Hz**, giving last-knot support of
`2047/50 = 40.94 s`. A separate `2048/41 Hz` duration convention is available to
test that timing ambiguity; its last knot is `40.97998046875 s`. Neither is
asserted to be a recovered exact instrument clock. The sample values are the
same under both conventions. No time wrapping, record repetition, extrapolated
phase, or synthetic zero tail extends the measured support.

The native loader divides every complex voltage by **one fixed full-record
RMS**. It reports the original mean power and scale. No received realization,
sliding window, subsegment, or faded output is renormalized. Piecewise-linear
interpolation is performed in the complex plane, so amplitude and phase remain
coupled and the original knots are reproduced. Its continuous-time energy can
be lower than knot-mean energy and is reported separately. This interpolation
does not establish that unmeasured fast fluctuations are absent. Independent
native checks cover a resolved analytic phasor, exact linear-chord energy,
last-knot admission and representation subdivision; they do not validate a
physical interpolation law between the recorded samples.

`amplitude_only` and `phase_only` are **constructed ablations**, not additional
captures. They operate after interpolation. Amplitude-only retains the
instantaneous magnitude; phase-only uses unit magnitude and preserves the
interpolated argument, except an exact zero remains zero because its phase is
undefined. A native diagnostic also reports unwrapped phase standard deviation
after one global least-squares linear detrend. Its estimator is explicit;
agreement with a paper's rounded phase statistic is not independent channel
calibration.

The executed native check reports normalized knot mean power `1`, continuous
linear-interpolation mean power `0.999884`, amplitude range
`0.141160–1.75018`, fitted phase slope `0.502443 rad/s`, and linearly detrended
phase standard deviation **`0.884792 rad`**. The last value differs from the
paper's rounded measured value of `1.02 rad`; the paper does not specify this
suite's global least-squares trend estimator. The discrepancy is unresolved.
The deposited sequence is replayed without fitting it to the paper's statistic;
neither numerical correctness nor matching file identity establishes that the
published phase-statistic calculation has been reproduced. One predeclared
native discriminator removed the straight line joining the unwrapped phase
endpoints instead: its phase standard deviation was `1.1343737016858497 rad`
and slope `0.56248078744165142 rad/s`, versus the least-squares values
`0.88479249206566601 rad` and `0.5024432732048747 rad/s`. Neither reproduces
`1.02 rad`; no slope was fitted to that number. The result establishes that the
trend convention materially affects this statistic, and retains the unresolved
published-estimator difference. The native replay group passed **208 checks**,
including the independent interpolation controls and the expected minimum
residual variance of the least-squares trend.

## Provenance and lossless conversion

The public archive `Data_MultiplePhaseScreenModeling.rar` was downloaded from
Zenodo record 4698276. Its posted MD5 was verified before extraction:

- Archive size: 21,370,531 bytes.
- Archive MD5: `1ccf44722ad1018c2f1204cc64928762`.
- Archive SHA256: `6475632af2f46815691e73efde81a3418864514255b7e5edd0ce2a2c6d78fcf7`.
- `ElecDirectwave.mat`: 31,913 bytes; SHA256
  `8b2b663989bf294e8279622b6e30dc7d9a278aaf8914634e0a6e38ddca7f6c53`.
- Ordered little-endian binary64 `(real, imaginary)` pairs: SHA256
  `6d56885c5bfc375b08718e5266106475c5372e1c6244d16d1e8251429b0678d0`.

Conversion used only Python standard-library `zlib`, `struct`, and hexadecimal
double serialization: check the MAT-v5 little-endian header; inflate its
`miCOMPRESSED` element; verify complex `mxDOUBLE_CLASS`, dimensions `(2048,1)`,
name `yDire`, and both `miDOUBLE` arrays; interleave the real and imaginary
values; emit their exact `float.hex()` literals. **No DSP, filtering, phase
processing, fitting or normalization occurred during conversion.** The compiled
native loader performs the stated normalization and observables.

The fixture is retained because the coupled measured voltage sequence is
necessary for the current unusual-HF replay and ablation question; a summary
cannot reproduce it. Keep this small source fixture and attribution while those
tests remain active. The downloaded archive and unused simulation outputs were
removed after the retained source and provenance were independently verified.
The small measured MAT and interpretation text remain in ignored local evidence.
Replace the fixture only with
an explicitly reviewed dataset/model change; do not silently append or repeat
it to meet a longer modem calendar.
