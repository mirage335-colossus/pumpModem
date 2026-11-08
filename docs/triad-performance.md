# Robust computational checks

Robust's maintained statistical timing tool is
[`tools/triad_abbreviated.py`](../tools/triad_abbreviated.py), with focused tests
in `tests/test_triad_abbreviated.py`. It has two bounded modes: retained-kernel
timing and the editable manual-case subwindow suite described below. Neither
estimates end-to-end receiver latency, physical reception probability, or radio
qualification.

Use the tool only for a declared computational question. Before running it,
record the source revision and dirty state, build/configuration, device, kernel
and fixed geometry, covered path strata, target unit count, output location,
uncertainty decision, and what the result can decide. Keep cheap deterministic
correctness checks separate from performance measurements.

Treat the JSON output as temporary evidence for that question, not a growing
benchmark archive. Retain only the current compact result while it supports an
active decision or validation claim; record reusable conclusions, negative
findings, assumptions and provenance in maintained documentation, then remove
raw and superseded outputs. Keep any unfinished comparison only with a stated
purpose, next action and cleanup trigger.

The harness measures four retained kernels: FFTScreen preparation and scoring
separately, GaussianStateFilter update, and SparseTimingForward. Its fixed
hierarchy uses 3, 5, then 9 outer trials, with 1- and 2-unit training
measurements and a 4-unit holdout. The maximum is 252 kernel measurements.
`--target-units` sets only the extrapolation target; it does not increase work.
Run the least stage that supports the predeclared decision and proceed only when
the predeclared uncertainty criterion requires it. The holdout checks the stated
fixed-geometry repeated-unit assumption. The estimate uses the harness's
conditional log-t model; it does not provide physical or general sampling-bound
qualification. It does not validate extrapolation
across different algorithms, memory or concurrency regimes, sample rates,
search sizes, configurations, devices, receiver stages, or physical channels.
Do not generalize its estimate beyond the measured kernel and declared units.

First run the inexpensive assertions:

```sh
python3 -m unittest discover -s tests -p test_triad_abbreviated.py
```

Example measurement:

```sh
python3 tools/triad_abbreviated.py \
  --output local-evidence/triad-abbreviated.json \
  --device cpu --target-units 1000
```

For a matched comparison, provide `--baseline previous.json` from an active,
compatible comparison and remove it when that comparison closes. Keep candidate
and baseline source, configuration, inputs, device and execution conditions
matched; record their provenance in the output. RunPod may accelerate this same
abbreviated process, but does not authorize more computation.

Do not run or retain full-observation, sustained, multi-day, exhaustive-sweep,
or whole heavy-calibration campaigns as routine checks or as a fallback for an
inconclusive abbreviated result. Resolve uncertainty with the next predeclared
small stage, a cheaper discriminating check, or report the unresolved result.
Historical anchors apply only when source, configuration, inputs and measured
computational regime match; they do not qualify changed code. Never describe an
extrapolated estimate as a direct end-to-end or physical measurement.

## Editable manual-case subwindow suite

Run the inexpensive canonical native checks first, then the case suite when a
manual-case capacity check is needed:

Use a Python environment with NumPy, SciPy, PyTorch and threadpoolctl. Prefer
the interpreter already selected by CMake (`Python3_EXECUTABLE` in
`build/dev/CMakeCache.txt`) and reuse its environment/cache; replace `python3`
below with that interpreter or activate its virtual environment. These are
research-test dependencies, not installed application dependencies.

```sh
./build.sh test triad
python3 tools/triad_abbreviated.py --cases \
  --output local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export
```

The case-mode CLI returns status 2 to mark that its staged results do not
certify an integrated receiver.

To rerun selected rows while debugging, first read the current
`_tests-RobustModem.md` list and use its current row indexes:

```sh
python3 tools/triad_abbreviated.py --cases \
  --case-index N --case-index M \
  --output local-evidence/robust-cases-selected.json \
  --exporter build/dev/triad_helper_export
```

The optional lower-confidence fixed-point follow-up is explicitly opt-in. Read
the current list and use the selected configuration's current row index:

```sh
python3 tools/triad_abbreviated.py --cases \
  --case-index N --paired-followup \
  --output local-evidence/robust-cases-paired-rerun.json \
  --exporter build/dev/triad_helper_export
```

`--cases` accepts an optional Markdown path; without a path it reads
[`_tests-RobustModem.md`](../_tests-RobustModem.md) on every invocation. The
strict parser hashes the input, preserves its order and duplicate rows, and
reuses a measurement only for an identical normalized configuration, with that
reuse identified in output. It requires all 16 named fields and rejects unknown
fields or unsupported values. It does not maintain a hardcoded case list.
By default every current row is run. For an affordable targeted rerun, repeat
`--case-index N` for each selected current index. The program
still rereads and strictly validates the entire current list; a stale or
missing index is an error. JSON records selected indices and the current-list
size, and the Markdown report labels a partial rerun prominently. A partial
rerun does not cover unselected rows or support a full-list conclusion.
Keep the raw JSON and generated Markdown report in ignored `local-evidence/`
only while they support this current validation question; when superseded,
distill any still-useful result into [validation](validation.md) and remove them.

The suite times actual native public-channel input with two sequential
research stages: acquisition uses the noisy Helper estimate and `FFTScreen`
public proposals over its observed prefix, then tracking pushes a bounded
subwindow through `PracticalTrackingReceiver`. It does not use the separate
`WeakReceiver` class. Acquisition is not whole-bit acquisition: the FFT stage
filters and scores an abbreviated DATA template subwindow. Tracking covers a
bounded DATA subwindow after the observed proposal; native template work done
on demand during tracking is timed there. Both stages retain the selected
native waveform and channel effects, all 13 epochs, and the original public
arrival/frequency/clock priors. No truth-selected proposal, bit, channel state,
or realized oscillator error is used to select the search.

Helper pilot-dwell gating is derived from the native public plan: it is enabled
only when Helper is enabled and the planned waveform uses FHSS, matching the
native Helper placement and generator. The Helper estimator explicitly returns
`gated_or_retuned_pilots_unsupported` for gated or RF-retuned pilots. When that
happens, retain the full public CFO/frequency search; do not interpret it as
an ungated pilot estimate or narrow the search from unsupported pilot timing.
The EME configuration exercises this fallback and must keep the full CFO search.

The primary capacity metric is elapsed stage service seconds divided by
received baseband seconds for a prepared, single input stream. A ratio at or
above 1 fails realtime; 0.1 is a 90% service-time margin. Do not sum CPU
thread-seconds, divide service time by core count, or substitute independent
Monte Carlo throughput. Acquisition and tracking ratios are separate and do
not include unmeasured full-receiver filtering or the complete projected
search. Observed utilization is diagnostic: without saturation of the
allocated CPUs, do not claim a whole-machine CPU ceiling. Even with saturation,
the measured stages do not establish the ceiling of an unmeasured full Robust
receiver.

The current runner defaults to one thread for case-suite CPU service
measurements, while still reporting the host's available CPU allocation;
`--threads` remains an explicit override. This choice follows the matched
case-6 HF/GPSDO tracking comparison: alternating 1/12/1/12-thread timing with
three post-warmup samples per block produced identical bit scores, 0.0833–0.0927 s/s
at one thread versus 0.680–0.868 s/s at 12 threads. One thread had high allocated-thread
utilization (0.88–1.04) on that workload. This is evidence for the default on
that measured small matrix case, not proof it is globally optimal or a
whole-machine CPU ceiling. A separate matched FHSS comparison also preserved
identical bank scores but measured 0.0427 s/s at one thread and 0.0429 s/s at
12 threads, showing no speedup for the 12-thread setting. These case/stage
measurements support retaining one thread as the local default; they do not
imply a host-core-count correction or a universal performance ordering.

Acquisition repeats the same fixed observation and reports its arithmetic mean
with a conditional Student-t interval across outer timing rows; there is no
duration-changing acquisition holdout. Tracking uses the duration-weighted
arithmetic mean of the 1- and 2-unit service ratios, with a conditional
Student-t interval across outer rows. Its 4-unit measurements are a holdout;
the observed log-ratio error floor remains in the reported uncertainty. Both
stages use the predeclared 3/5/9 outer looks and stop under their stated
decision/precision rules. Timing replicas reuse the same prepared input and
are not independent reception trials. These intervals rely on their stated
fixed-geometry sampling assumptions; they are not distribution-free guarantees.

Receiver readiness includes preparation plus a separately excluded warmup;
native channel/noise generation is reported separately and excluded from
readiness. **Preparation/readiness over five seconds is a warning** for the
measured implementation, not proof that the time is unavoidable. Full-coarse
receiver preparation reports reusable native templates and local profile
validation separately from clean-channel generation, projection, and noise
factor preparation. The same five-second warning applies to this full-coarse
preparation; its simulator channel generation is a separate excluded cost.

Full-coarse replay first applies bounded template-work admission. Exact raw
template work is capped at 30 million points; some long cases therefore remain
unresolved before receiver allocation. Fixed mapped profiles are limited to
S=60 for eligible slow non-hopping waveforms, or S=3 for eligible high-work
FHSS cases (exact work above 30 million points and required band below 240 Hz),
with a 120-million-point cap. S=60 requires inner symbol rate at most 1 Hz,
no hopping, and required band below 4 Hz. Each mapped profile is validated
afresh for the current case against its exact native basis on a local window
of up to two seconds. It checks retained rank/projector distance, stored
scoring-operator distance (the stored Gram inverse), wrong-bit subspace
separation, and matched clean and common-noise winner controls. The current
gates require matched retained rank, projector and stored-operator distance at
most 1%, column norm error at most 1%, Gram error at most 2%, wrong-bit
separation at least 90% of exact, and matching winner controls. These checks
bound a local working model; they do not prove full-waveform interpolation
accuracy or reception success. Other cases keep the exact template provider.
Do not infer that a mapped profile is universally valid outside its admission
conditions and current-input checks, or that this gate alone approves it as a
general replacement.

For case-suite Helper assistance, the caller explicitly requests continuous
interval admission. For each retained onset aperture, the Helper estimates a continuous union, clips and merges
it to the public CFO prior, and measures its total width against the prior
width; the existing 80% threshold still triggers full-prior fallback. This
prevents neighbor padding on a coarse diagnostic bank from rejecting a useful
interval. Successful onset branches are conservatively unioned without another
width gate. The default discrete-bank admission remains for other callers.
No usable pilot, flat or boundary profiles, inconsistent regions, gated pilots,
and retuned pilots preserve full-prior fallback; one usable pilot can suffice.

Both case callers explicitly extend the frequency allowance over the entire
searched DATA observation, rather than allowing the stored Helper prefix to
set its horizon. Per-aperture `available_at` describes the spectral estimate,
not the readiness time of the complete Helper procedure.

S=60 has an additional 5 Hz observation/template projection profile for public
required bands from 0.4 Hz to below 2 Hz, with 2 Hz pass and 2.45 Hz stop edges.
It uses a 0.2 second coarse arrival step. The 10 Hz sub-0.4 Hz and 20 Hz wider
band profiles remain available under their existing admission rules. A 3 Hz
profile was tested and failed the required 1% local fidelity gate, so it is not
used. Every selected profile still requires fresh validation against the
current exact native local window; template approximation does not establish
continuous timing accuracy or full-waveform equivalence.

The S=60 full-coarse search may use an exact lag-domain
[ZoomFFT](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.ZoomFFT.html) optimization
for uniform frequency groups. Its common-phase handling preserves the same
scores and candidates as direct per-frequency correlations. The increased
4096-frequency admission retains the existing 32768 transform-work cap, based
on the selected correlation computation, and bounds both lag batches and
workspace. It still evaluates the complete admitted frequency intervals,
all 13 epochs, both bits, all three public clocks, and the full admitted arrival
range; it does not prune from simulated truth. Numerical equivalence must pass
before case simulations rely on this path. Measure prepared local CPU service
on this computer before making any speed or realtime claim.

The full-coarse workspace accounting includes retained DATA banks and paired
clean observations, noise workspace, and a conservative Helper transient
workspace estimate while the DATA bank remains live. The Helper estimate
reserves about 1024 bytes per raw input sample for simultaneous working arrays,
256 bytes per padded convolution-FFT row, and 16 MiB for bounded profiles and
diagnostics. It is an explicit conservative estimate, not an allocator peak or
safety certificate. Reported headroom is compared with 50% of current
OS/cgroup available RAM. Abbreviated receiver subwindows and bounded replay do
not certify the memory needed by an unbounded/full operational receiver.

The suite also runs bounded full-bit coarse sensitivity when its geometry and
memory preflight admits the case. It replays complete continuous native clean
bits 0 and 1 on one fixed channel realization, with the same FIR, the actual
noisy Helper and all 13 epochs under the original public priors. It adds common
finite colored Gaussian noise with Helper cross-covariance preserved and uses
a maximum-bit GLRT; no-decision, invalid, or tied results count as failures. This
measures correct-bit probability conditional on a transmission and that one
channel seed, for this coarse procedure. It is not operational admission, full
Robust reception, physical-channel population performance, or `fox` message
success.

The projected-noise covariance factor keeps its default 100-million-operation
work limit. Finite-boundary covariance preparation also keeps its 100-million
work ceiling. For each covariance lag it chooses the cheaper exact clipped dot
products or cumulative sums anchored at the leading/trailing edge. It never
obtains a tiny tail by subtracting nearly equal full and prefix sums; windows
clipped at both ends retain direct dots. Scratch storage is included, and the
shared raw Helper prefix and projected DATA covariance are unchanged.
Only an already current-input-validated S=3 mapped bank with Helper
disabled may request a limit up to 500 million operations. That exception first
calibrates at most 8192 output rows using the same actual FIR and decimation
phase, builds one probe factor, and times three noise draws. Forecasts scale
linearly with output rows and include a 2x cache/model allowance; admit only if
the factor forecast is at most 15 seconds and a draw forecast at most 2 seconds.
This calibration is a bounded cost gate, not a full matrix timing guarantee.
All existing memory, output-row, bandwidth, boundary, and other workspace caps
remain in force; Helper-enabled or otherwise ineligible geometries retain the
100-million-operation limit.

The sensitivity allocation is fixed. The initial gate uses noiseless input
when Helper is disabled; with Helper enabled it uses finite noise at the larger
of configured nominal DATA C/N0 and the provisional single-bit model target
plus 18 dB, avoiding a degenerate zero-noise level estimate. Then it runs up to
four paired pilot SNRs: model target -6, 0, and +6 dB plus configured nominal
when distinct, with 4 replays per bit at each SNR (at most 32 pilot receives).
The lowest two pilot SNRs with 4/4 correct for each bit are eligible for
confirmation. Each candidate round uses fresh noise seeds, at most 42 trials
per bit, and stops a bit at its first failure. A candidate is supported only
when both bits complete all 42 trials without failure. Its exact lower bounds
use alpha 0.0125 per bit per round; Bonferroni covers both bit strata across
both candidate rounds jointly at least 95%. Stop after the first supported
candidate. The total is at most 2 initial + 32 pilot + 2 x 42 x 2 confirmation
receives = 202 per case under the standard branch. There is no campaign
extension or cross-case simultaneous claim. Earlier reports retain their own
recorded allocation and are not retroactively results of this current policy.

Before confirmation, the run forecasts one candidate's 84-receive service from
the largest observed per-receive service time among the initial gate and pilot.
The forecast must be at most 600 seconds per candidate. If it exceeds that
budget, the default run preserves the model and small-pilot result as
provisional and stops without an empirical >90% bound. By default the
alternative `--paired-followup` remains off. When explicitly requested, it is
eligible only if the standard 84-receive forecast exceeds 600 seconds and a
separate 44-receive forecast, using the same largest observed gate/pilot
service time, is at most 600 seconds. It tests only the lowest eligible pilot
point: 22 independent common-noise pairs, where each fresh seed is used once
for bit 0 and once for bit 1, and a pair succeeds only if both decisions are
correct. Stop at the first failed pair; do not try a second point. All 22 pairs
correct gives an exact 90% one-sided lower bound of
`0.1**(1/22) = 0.9006280` for the paired event, and therefore for each
marginal bit probability. This is not 44 independent trials, a real-message
probability, or the standard joint 95% qualification; the standard result
remains unresolved. This optional path uses at most 2 initial + 32 pilot + 44
paired receives = 78 receives, within the unchanged 202 standard-branch
ceiling. Neither branch permits retries beyond its declared budget.

For an admitted complete coarse procedure, the runner also measures the same
streaming FIR on bounded 1/2/4 chunks with its actual taps and decimation,
complete finite tails, the timing hierarchy, and the 4-unit holdout. At the
configured nominal SNR it adds that supported FIR estimate to the slower bit's
full public Helper/FFT service. If the abbreviated FIR holdout remains
unresolved, the runner may instead measure its actual fixed finite extent,
provided the raw input is at most 4 million samples and remaining-memory
preflight passes. This fallback applies the same canonical FIR, taps, dtype,
chunking, and complete finite tail to fixed synthetic input, with one excluded
warmup and exactly three repeats. The 5-second elapsed limit is checked between
invocations. Because the algorithm does not branch on sample values, the
synthetic input exercises the same fixed extent. The result uses a conditional
Student-t interval from those three fresh repeats, not a distribution-free
bound, with no duration
extrapolation and no pooling with the unresolved abbreviated attempt; the
failed abbreviated evidence remains reported. This is a bounded filter-stage
check only, not a fallback to native/channel generation, the full FFT bank, or
communication trials. The full coarse estimate accepts this fixed-extent
measured status without claiming extrapolation. A capacity pass also requires
complete public candidate results and high utilization of the allocated CPU
for both terms. This is a conditional service estimate for this full coarse procedure; it
excludes `PracticalTrackingReceiver`, framing, operational admission, GUI, and
deployment. It is not computed by dividing by 12 host CPUs (or by any core
count). Simulator generation, clean projection, Gaussian draws, native
template setup, and noise-factor setup are outside the measured service ratio;
the preparation report keeps those costs separate.

The native planner is authoritative for the exact DATA/Helper physical
calendar; the full-bit replay is bound to that planned schedule and its Helper
timing, and does not substitute an independently reconstructed Helper
calendar. The report also includes canonical literal `fox`, encoded as 22 bits,
for duration context only. Its provisional
`>90%` duration-model target is not an observed whole-message threshold.
Reported combined-triad DATA C/N0 and effective Helper C/N0 are nominal
received/link-budget quantities in 1 Hz dB-Hz; Helper C/N0 is separately
derived from its noise density using the existing 10 dB noise-figure
assumption. They are configured references before the selected fading and
frequency-selective perturbations, not measured realized post-channel
component powers. Selective perturbations are not energy-normalized, so do not
infer actual per-component received power or realized C/N0 from the nominal
values. The simulated SNR targets set the injected-noise scale relative to
that nominal link budget, conditional on the one fixed sampled channel. They do
not report a post-channel power measurement. Independent seed trials with full
public search and integrated admission remain necessary for whole-message
qualification. The staged receiver is not connected to the GUI. `Fake: FCC`
denotes synthetic FHSS and is not RF retuning or a regulatory claim.

Resolved projection, preparation-cost and thread comparisons are distilled in
[validation](validation.md); remove their raw outputs and temporary reproducer
variants once that review is complete. Retain the current consolidated
`local-evidence/robust-cases-current.json` and Markdown companion only while
they support that validation record or its explicitly unresolved questions,
then replace/retire them with equivalent current evidence. Local S=60/S=3
admission checks support their stated measured regime, not universal approval
of a collapsed template model. Source, case-list,
or native-binary changes invalidate a claim of current-source qualification;
use only measurements whose completion identity checks all remain unchanged.
An explicitly labeled report may preserve still-applicable measurements from
an earlier source phase while rerunning the affected cases. Keep each phase's
original hashes, policy and limits, record the later renderer/analysis identity,
and never relabel old timings as execution of the updated source or combine
separate confidence families. Every phase must bind to the current manual
configuration being reported. RunPod may accelerate this bounded
method but does not replace local CPU measurement or authorize heavier work.
