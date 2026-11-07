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
The [current HF cost measurement](validation-data/triad-v3/2026-11-06-hf-current-cost/README.md)
uses the current encrypted patterns and planner-selected 46.4-second DATA bit.
It separates prepared RX cost, per-bit keyed preparation, one-bit airtime and
the estimated 22-bit `fox` calendar; it does not qualify sustained arbitrary
message throughput or physical admission.
The [CPU optimization and cadence qualification](validation-data/triad-v3/2026-11-06-realtime-optimization/README.md)
supersedes those runtime estimates for the optimized paths: paired complete RX
CPU cost fell about 1.8–2.0 times, and fresh native template-bank generation
about nine times. Eight independent fresh-key jobs offered at the 11.6-second
DATA cadence kept up on the local six-core Ryzen with two bounded workers.
This is computational throughput evidence, not a contiguous-message or physical
admission qualification. Use its cheap exact-reference tests before repeating
its approximately 92-second workload campaign.

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
The default CPU budget now uses all process-available logical CPUs, constrained
by affinity and readable ancestor container quotas. `triad_cpu.py` reuses the
project's resource detector without reserving a CPU. One available job receives
the full FFT budget; multiple scheduled jobs divide an even budget between two
private workspaces. Odd budgets use one workspace, avoiding unused capacity.
On this 12-logical-CPU host that means one ×12 or two ×6 FFT workers. Automatic
workspace replication is capped at two, rather than scaling memory per CPU.

Small-matrix BLAS/PyTorch operations automatically retain at most two threads
per job: measured indiscriminate 12-thread execution substantially increased
CPU and wall time in refinement preparation. The full CPU budget remains
available to bulk FFT work; utilization depends on runnable work. Explicit
`--workers` and `--threads` remain exact overrides; explicit `--threads` also
sets the linear-algebra count. Overcommit is reported, never silently introduced
by automatic mode. Use `--workers 2 --threads 2` to reproduce the earlier frozen
cadence experiment; its numbers are not silently relabeled as automatic-mode
results. See the [automatic CPU follow-up](validation-data/triad-v3/2026-11-06-cpu-default/README.md).

Install `threadpoolctl==3.6.0` alongside NumPy, SciPy and PyTorch. The controlling
thread configures already-loaded numerical pools before starting receivers.
Each scoring worker also applies the same fixed limit once, because some
OpenMP implementations keep this setting thread-local. No job retunes global
pools. Setting only environment variables after imports would be insufficient. Reports
include detected capacity, FFT allocation and observed library pool sizes.
Programmatic `WeakReceiver`/`RealtimeReceiver` constructors select their FFT
budget without changing process-global BLAS/PyTorch settings: their controller
must call `configure_numerical_threads` once and initialize their numerical
worker threads with `initialize_numerical_worker`. `StreamSession` does this before
starting its workers; do not concurrently construct sessions with different
global numerical settings. Explicit frozen qualification protocols keep their
declared budgets. Continuous fixed/tracking CLI reception also uses automatic
CPU allocation; transmit-only operation does not acquire a PyTorch dependency.

Each concurrent job owns its mutable receiver state. Queue and in-flight work
remain bounded, and shutdown uses one shared deadline rather than a fresh
timeout per worker. Process CPU is an aggregate across cores; more than one
CPU-second per input second does not itself imply a wall-time backlog on this
six-core host.

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

## Avoid repeated preparation and unsuitable FFT shapes

The segmented screen now defaults to bounded smooth FFT lengths, a small
inverse-transform product tile and in-place private transform buffers. The
`legacy` length policy remains an arithmetic comparison option. Zero-rank
blocks do no FFT work; logical hypotheses and executed work are reported
separately. Both bits, all declared timing/epoch/clock/frequency hypotheses,
filtered tails and final admission policy are preserved. The explicit
`fft_workers` setting belongs in the receiver policy identity.

Dynamic preparation shares interpolation geometry across CFO alternatives and
both bit patterns. Only exactly zero, zero-extended DATA support bypasses
projection/Gram work. All block times remain in the Gaussian state model,
including quiet gaps; do not remove state propagation when skipping zero
arithmetic.

Native `PatternSource::render_bank` shares pulse and oscillator geometry across
keys using bounded chunks. File export limits simultaneously resident pattern
caches; mapped/nonzero-coordinate stream requests retain the scalar path.
`triad_template_bank.py` constructs one complete public keyed template bank
directly in memory without generating a channel observation or temporary
template file. It verifies the explicit fixture key/epoch and native format,
calendar and gain against the manifest. This helper currently supports one
public whole-bit fixture slot, not arbitrary production key loading.

`triad_weak_throughput.py` measures fresh banks, per-key workspace preparation,
Helper/acquisition/refinement and loading at a declared arrival cadence. It
records aggregate parent plus child CPU, backlog, rejected jobs, identities and
separate startup. Saved scoring windows are prepared through the maintained
audio frontend beforehand; ADC, DDC and TX/channel simulation are outside its
job timer. Run it under an external process-group deadline because cancelling
a Python worker cannot interrupt a blocked native protocol read. See the
dated report for the frozen command and the distinction between independent
window throughput and a continuous transmitted message.

## Cheap checks first; occasional qualification separately

Reuse the maintained numerical environment and existing native exporter build.
The new Python-only tests are short, distinct-path checks:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python -m unittest discover -s tests -p 'test_triad_weak*.py'
python -m unittest discover -s tests -p 'test_triad_segmented_fft.py'
python -m unittest discover -s tests -p 'test_triad_operational_admission.py'
python -m unittest discover -s tests -p 'test_triad_stream*.py'
python -m unittest discover -s tests -p 'test_triad_template_bank.py'
python -m unittest discover -s tests -p 'test_triad_cpu.py'
python -m unittest discover -s tests -p 'test_triad_receiver_cpu_limits.py'
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
