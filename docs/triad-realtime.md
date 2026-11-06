# Isolated Helper-assisted CPU acquisition

This experimental path addresses the HF case with Rate 36, Added Symbols ×10,
free-running crystal base clock, IC-7100 shift clock at 30 MHz, margin 3,
HF-worst two-ray spread and random Each65 selective modulation. It preserves
the encrypted transmitted waveform, the automatic shaping plan and the 38-second
Helper lead. It does not integrate the ordinary Robust receiver or implement
the missing multi-bit message adapter.

Start with `tools/triad_realtime_receiver.py`, rather than multiplying the
small-bank dynamic reference receiver across the full timing/epoch/CFO/clock
product. The earlier [CPU diagnosis](validation-data/triad-v3/2026-11-05-local-cpu/README.md)
describes that reference-path bottleneck. The new receiver retains its likelihood
as a final refinement stage; it does not claim an equivalent exhaustive search.

The [frozen local-CPU validation](validation-data/triad-v3/2026-11-06-realtime/README.md)
records 16/16 independent target-case admissions, a 61-capture signal/null/stress
campaign, and 18.5–21.4-second fresh-process acquisition of 84.7 seconds of I/Q
starting from raw keyed templates. It also charges approximately 8.4 seconds of
extra cold template preparation separately. The weaker-link and renewing-ray
stress cases remained unresolved; this is bounded evidence, not general release
or streaming qualification.

## Acquisition and model boundaries

1. `triad_realtime_helper.py` evaluates an energy change point across the original
   arrival prior, retaining competitive onset intervals with an explicit guard.
   It places a 36-second pilot-frequency observation relative to those intervals.
   The original DATA timing prior stays unchanged. Ambiguous, weak, erased or
   unsupported observations return unresolved work.
2. The existing frequency estimator runs an explicitly identified specular-core
   policy, with zero added-AM frequency allowance. Complete simulated modulation
   remains in its input. This approximation needs its own validation; onset
   acquisition alone does not justify replacing the original 90-Hz allowance.
   Frequency assistance grants no pilot-to-DATA phase authority.
3. `triad_realtime_fft.py` filters the whole capture through a fixed causal FIR,
   including its tail, and uses complex FFT correlations over the complete
   original arrival lattice, both bit patterns and all 13 key epochs. A coarse
   clock covering bank precedes local refinement. The fixed two-column GLRT is
   a static-channel proposal statistic, not a claim that random fading vanished.
   The nominal 400-Hz passband/490-Hz stopband at 1 ksample/s is an explicit coarse
   projection; the original complex capture remains available to refinement.
4. Proposals for **both** bit alternatives become one correlated sparse
   `(epoch, arrival, CFO, clock)` list. Clock refinement preserves DATA-midpoint
   alignment. `ScanJointReceiver` scores this list using its original sampled
   input, two physical taps and 0.5-second/infinite-coherence alternatives. It
   retains the observation projection planned from the original uncertainty
   bounds. No Cartesian re-expansion occurs.
5. Admission requires a new, source-bound gate for the complete adaptive
   procedure. Existing Helper or dynamic receiver gates are not transferable.

The FFT screen caches templates and small Gram factors, shares each observation
FFT across epochs/clocks/bits, and retains only bounded candidate lists. Valid-only
correlation needs an FFT length at least the cropped observation length: for
retained lags 0 through `L-M`, every nonzero template index remains inside the
length-`L` observation. It does not need padding for unused convolution tails.
All source/filter tails still belong to the template and support checks.

For well-conditioned two-ray designs, the Hermitian quadratic form is evaluated
explicitly instead of sending a 2×2 contraction through a general BLAS operation.
FFT/template bulk arithmetic is FP32 by default; Gram construction, rank and
quadratic score arithmetic remain FP64. Rank-deficient or ill-conditioned designs
retain the reference contraction. FP64 force is available for paired numerical
checks. No tensor or GPU arithmetic is selected implicitly.

These choices follow established parallel timing-search and acquisition-resampling
methods, rather than a new modulation format: see the primary
[GNSS-SDR acquisition implementation](https://gnss-sdr.org/docs/sp-blocks/acquisition/),
[Psiaki and Jung's FFT acquisition work](https://gps.mae.cornell.edu/psiaki_iongnss2004.pdf)
and [partial-matched-filter FFT acquisition](https://doi.org/10.1016/j.ast.2016.11.017).
Those sources motivate computational structure, not this modem's sensitivity
or false-admission claims.

## Cheap development checks

Use a numeric Python environment with NumPy, SciPy and CPU PyTorch. The local
study environment is `/tmp/triad-weak-env/bin/python`; this path is not an installer.

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python -m unittest discover -s tests -p 'test_triad_realtime*.py'
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python -m unittest discover -s tests -p 'test_triad_dynamic_sparse.py'
```

These focused checks validate the valid-lag FFT against direct arithmetic, clock
and ray coordinates, both-bit retention, numerical precision/rank fallbacks,
onset/window guards, sparse-versus-Cartesian likelihood equality and admission
identity/rank semantics. They intentionally do not replay the sampled campaign.
Changes to the existing dynamic scorer also require its focused joint,
acquisition, scan and projection tests. No native code changed in this path.

An actual capture invocation is:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python tools/triad_realtime_receiver.py CAPTURE_DIRECTORY --templates TEMPLATE_DIRECTORY --output local-evidence/realtime-result.json
```

Here the capture manifest must already describe the **receiver's public
configuration**, including its epoch center. The CLI does not construct that
configuration from a transmitter export. For blind epoch validation, load the
public reference manifest/templates separately, instantiate `RealtimeReceiver`
with those, then call `score(raw, valid)` for each observation. The frozen
campaign recipe does exactly this while actual transmit epochs vary ±6 seconds.
Do not replace the receiver's epoch center or expected Helper state with each
trial's actual transmitter metadata; doing so changes the acquisition problem
and gate identity.

The result distinguishes unresolved acquisition from a scored bit. A scored bit
is **not an admitted message**. `--gate` requires a matching frozen procedure
identity. `freeze_gate()` and `admit()` implement conservative per-capture rank
handling, including unresolved captures and ties. Its finite-sample statement
requires exchangeable calibration/test null observations; it is not a daily or
continuous-monitoring guarantee. Helper-present/DATA-absent and pure-noise nulls
are distinct laws and must remain separately reported.

## Performance and qualification boundaries

Charge cold template synthesis, FFT workspace construction, Helper estimation,
coarse acquisition and dynamic preparation/scoring separately. Constructor
preparation and cached campaign throughput are not interchangeable single-capture
latencies. The Python receiver consumes zero-centered complex I/Q; device capture,
real-audio conversion and streaming overlap require their own named measurements.
Source/input identities, first-capture and batch times, numerical comparisons and
sampled outcomes belong in the accompanying validation record.

The requested 4-W/180-dB DATA link is 20.0206 dB-Hz at the simulator's −164 dBm/Hz
noise density; total 40-W Helper is 30.0206 dB-Hz. The 8 dB-Hz planning setting does
not weaken that physical link. Injected 8 dB-Hz and nonzero random-ray widths
are separate stress regimes. The matched baseline uses zero random-ray width
with 90-Hz ray separation, 21-ms excess delay and random Each65 modulation at
90/9-Hz fast/slow rates. Zero random-ray width does not turn random selective
modulation off, but it does preserve a specular component useful to the coarse
statistic. More general renewing channels need separate acquisition validation.

Current explicit limits include all-valid FFT support, a single complete bit,
ungated/non-retuned Helper pilots, fixed nominal coarse bandwidth and bounded
frequency/refinement work. Arbitrary partial erasures, unbounded clock/channel
uncertainty, very different rates, full live streaming, generic no-Helper blind
acquisition and multi-bit text are not established by this targeted path.
An unsupported branch must not silently use a smaller prior, omit a channel
effect or borrow an admission gate from another procedure.
