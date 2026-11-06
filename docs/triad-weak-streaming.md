# Weak-signal acquisition and continuous audio experiments

This is the isolated three-carrier study, not ordinary Robust integration or a
finished message receiver. Use `tools/triad_weak_receiver.py` for the new weak
Helper and renewing-ray experiments. The earlier
[CPU acquisition study](triad-realtime.md) remains a frozen reference. Neither
path should be replaced by enumerating the full Cartesian dynamic-reference
search: the final likelihood is affordable on a small correlated candidate set.

The present target is **injected DATA 8 dB-Hz and total Helper 18 dB-Hz**,
preserving the 10:1 power ratio. This differs from the physical 4 W/180 dB example,
which gives DATA 20.0206 dB-Hz at the declared −164 dBm/Hz noise density. The tested
Rate 36/Added ×10 format has 23.2 seconds of DATA, a 38-second Helper lead, encrypted
keyed patterns, automatic shaping, and the full public arrival/epoch/clock
uncertainty. Exact operating evidence and failures belong in the linked dated
validation report; successful small tests do not qualify an operational modem.
The [sampled and live evidence](validation-data/triad-v3/2026-11-06-weak-stream/README.md)
records independent trials, preserved misses, actual streaming CPU/backlog,
audio image effects and the remaining physical-null qualification gap.
The separate [HF background-noise target study](validation-data/triad-v3/2026-11-06-hf-background-target/README.md)
measures a single scheduled bit after added full-audio noise; it does not
qualify unrestricted packet streaming or a physical null.
For automatic weak-receiver DATA duration planning and native fixture export,
see the [provisional duration-planning guide](triad-duration-planning.md).

## Acquisition without discarding complex evidence

* `triad_weak_helper.py` first fits spectral cores in the guaranteed DATA-free
  part of the public lead. Continuous narrowband filtering supplies onset
  proposals. Persistent rays use independently refined complex means; renewing
  or moving rays use an energy change point. A wide retained onset hull is
  partitioned into at most eight cells. Every cell receives the unchanged
  36-second frequency estimator, and all frequency proposals are unioned. A
  fallback in any plausible cell preserves fallback; cells are never silently
  discarded. Only a cell containing the true onset justifies its DATA-free
  aperture. These are working acquisition regions, not calibrated confidence
  intervals. DATA timing and clock priors remain unchanged; no pilot-to-DATA
  phase authority is claimed.
* `triad_segmented_fft.py` retains full filtered source tails and correlates
  complex blocks before squaring. It fits a two-ray complex subspace within
  each block and sums block energies. The default 0.5-second block permits
  a 0.5-Hz CFO proposal lattice, avoiding whole-pattern fine-frequency work when
  phase coherence does not last that long. Both bits, all 13 epochs and the
  declared coarse clock bank remain in the proposal search. Block transforms
  are tiled rather than retained in a large tensor. FP32 bulk FFT arithmetic
  uses FP64 subspace orthonormalization and reductions. An independent direct
  SVD test checks every lag; tiny nearly dependent tail representations have a
  separately declared FP32/FP64 comparison budget.
* `triad_weak_receiver.py` chooses whole-pattern or segmented proposals from the
  public channel law, preserves both bit alternatives, snaps sparse refinements
  onto a fixed public lattice, then applies the existing dynamic Gaussian
  channel likelihood to the original observations. The final projection is
  fixed from the original uncertainty bounds. It is a lossy DATA-core receiver
  projection, not removal of effects from the simulated channel. Coherence
  alternatives 0.5, 2 and infinite seconds are a fixed mixture. Clock/CFO
  refinement is approximate; this is not exhaustive continuous acquisition.
  If Helper returns an explicit frequency fallback, acquisition can use the
  original oscillator interval under the screen's existing frequency/product/
  memory limits. This matters when a disciplined oscillator already has a
  narrower prior than the pilot measurement can resolve. Helper remains marked
  as fallback; no original prior mass or admission penalty is removed. Early
  Helper failures without a frequency result still return unresolved.

## What the false-admission calculation does and does not establish

`triad_operational_admission.py` retains the **original prior mass** of each
selected lattice atom. For normalized likelihood ratios against the same
zero-mean Gaussian null, the selected nonnegative mixture is pointwise bounded
by the full fixed mixture, even when selection uses the observations. Averaging
over only the selected candidates without the selected/full-bank mass correction
would lose this property. Fixed bit/channel-model mixtures are also included.

The default threshold allocates 10⁻⁴ across at most 216001 attempts in any rolling
24 hours on a 400-ms schedule. Duration alternatives need their own budget.
Independence between attempts is not assumed. The receiver checks that every
model's H0 evidence is zero. Gaussian noise covariance must be dominated by the
declared receiver covariance; an estimated noise variance is not automatically
such a bound. Noncentral Helper leakage requires a justified expectation
penalty or a valid null model. Random modulation can make the unconditional
null a non-Gaussian mixture even when its covariance looks plausible.

Every result therefore separates `passes_conditional_bound` from
`physical_null_qualified`, which remains **false**. Small Helper-only/noise
cohorts are mismatch diagnostics; zero admissions in them cannot establish a
10⁻⁴/day physical rate. Relevant foundations are
[safe testing](https://arxiv.org/abs/1906.07801) and
[time-uniform inference](https://arxiv.org/abs/1810.08240). The implemented
construction is the simpler fixed-schedule mixture/union bound, not a claim
that the adaptive receiver itself is an anytime-valid martingale.

## Continuous streaming scope

`triad_stream_frontend.py` performs causal complex direct conversion and
integer decimation with persistent FIR history, oscillator phase, integer
sample timestamps and validity. Arbitrary chunks agree with a full-convolution
reference. Missing input contaminates all affected output support; synthetic
flush tails are invalid and cannot establish physical absence.

`triad_stream_scheduler.py` uses a bounded ring and asynchronous scoring queue.
It copies public analysis windows before overwrite, never waits for scoring
during ingestion, and explicitly reports invalid windows, overload, scorer
errors and incomplete drain. Timing distinguishes wall time, process CPU and
worker-thread CPU; process deltas from overlapping activities must not be added
as if they were exclusive.

`triad_stream_slots.py` adapts the native whole-bit fixture's public source
calendar and per-slot templates. It moves analysis starts earlier to retain
observed quiet support, expands timing uncertainty for the clock shift, and
does not reset propagation or filters. The fixture sends alternating bits;
it is not the missing arbitrary text/message adapter.

`triad_stream_run.py` supports continuous prepared-PCM replay, recorded-PCM
replay and raw ALSA stereo cable capture. It saves receiver results without
opening evaluator truth. Device acquisition continues while a worker constructs
and scores each window. All returned bits are pending candidates: this harness
does not implement unscheduled stream discovery or the production physical-end
and message-completion contracts.

At DATA IF 1725 Hz the complete 6-kHz analytic capture cannot be converted to real
audio without possible image overlap. `to_audio`'s nominal bandwidth field is
not a prefilter. The actual conversion, sidebands and overlap remain in the
experiment; a 12-second white-noise diagnostic found approximately unchanged
DATA-core noise and twice the noise near the low pilot. Do not transfer native
white-null qualification to audio based only on nominal bandwidth. A separate
cable gain probe may set a fixed gain; fitting gain/alignment from the payload
must not be passed off as blind reception.

Current sampled qualification covers this Rate 36/Added×10, 6-kHz analytic
geometry and 48-kHz audio bridge. The [duration study](validation-data/triad-v3/2026-11-06-duration-planner/README.md)
adds a bounded Rate100/Added×1 stable-oscillator example and an original-prior
fallback check. A practical Helper-free broad-frequency acquisition fallback
is not qualified here. Higher rates, larger ray renewal, unscheduled overlapping
messages, multi-day frontends and different audio hardware do not inherit this
study's sensitivity or timing evidence. The full public scheduled window is
currently collected before scoring; templates are reusable inputs, but this
harness does not yet overlap their generation with the Helper lead.

## Cheap checks first; occasional qualification separately

Reuse the maintained numerical environment and existing native exporter build.
The new Python-only tests are short, distinct-path checks:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python -m unittest discover -s tests -p 'test_triad_weak*.py'
python -m unittest discover -s tests -p 'test_triad_segmented_fft.py'
python -m unittest discover -s tests -p 'test_triad_operational_admission.py'
python -m unittest discover -s tests -p 'test_triad_stream*.py'
```

Independent native qualification is explicitly opt-in. Its protocol freezes
sources, binary, public templates, seeds and operating mixture before generation;
individual scores are written before reading truth. It is not a routine CI
prerequisite and must not automatically replay historical campaigns:

```sh
python tools/triad_weak_qualify.py --check-contracts
python tools/triad_weak_qualify.py --prepare-protocol --output local-evidence/NEW-STUDY
python tools/triad_weak_qualify.py --run --output local-evidence/NEW-STUDY --workers 2
```

For a newly generated public whole-bit fixture, start with software replay
(`analog-gain 1`), then use independently measured gain for live capture. Hardware
access is exclusive; check that another receiver is not using the endpoints.
Keep the source, channel and frontend continuous across the entire train.

```sh
python tools/triad_stream_run.py --source-dir TRAIN --mode replay --output local-evidence/NEW-REPLAY
python tools/triad_stream_run.py --source-dir TRAIN --mode live --exclusive --device hw:1,0 --fixed-gain .002 --analog-gain MEASURED_GAIN --record-pcm --output local-evidence/NEW-LIVE
```

Read the [efficient regression contract](triad-performance.md) before expanding
routine tests. Preserve historical failed results and source identities, use
direct full-target checks when they are already inexpensive, and distinguish
timing precision, predictive validity, functional coverage, hardware health,
sensitivity and admission assurance.
