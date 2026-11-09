# Robust manual-case receiver capacity

The manually edited `_tests-RobustModem.md` defines absolute receiver-capacity
checks. Run `tools/triad_manual_cases.py`; the former
`triad_abbreviated.py --cases` invocation forwards to the same implementation.
The ordinary `triad_abbreviated.py` performance regression asks whether measured
work degraded against a comparable baseline. A regression pass cannot establish
real-time capacity. Both tools use the same efficient, predeclared statistical
approach, with different decisions and explicitly recorded work coverage.

The current receiver implementation and validation are recorded in
[current architecture](robust-current.md) and [validation](validation.md).
Earlier coarse-only and isolated tracking measurements remain historical stage
evidence; they do not satisfy the complete-receiver contract below.

Run the local suite with the numerical Python environment used by the build:

```sh
python tools/triad_manual_cases.py --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-manual-capacity-current.json
```

The default compares the supported local worker allocations and performs the
current-source native message/quiet/EOF gate before measuring every current row.
`--case-index`, `--threads` and `--skip-message-gate` are diagnostic controls;
partial coverage or a skipped gate cannot produce an integrated suite pass.
For targeted comparison of sustained fine work, add
`--sustained-allocation-probe` with the full worker pilot enabled. The option
also applies to a replay calibration fit. It reuses the pilot-selected
allocation's first three valid continued observations and takes at most three
fresh six-second observations at each distinct physical-core and full logical
allocation, within the visible quota. The two alternate allocations add at
most 36 seconds of repeated probe exposure to the same baseband slice and two
actual acquisitions per row. These are not unique stream seconds or independent
communication trials. All use
the real resident bank and require matching public proposals, retained state,
native cache work and DSP counts. Only declared worker dispatch counts may
differ. The report separates measured fine service from any supported
slot-normalized fine forecast. This exploratory comparison does not select
the inference allocation or qualify whole-receiver service or CPU ceiling.
Exit codes are 0 for conditional capacity pass, 1 for measured capacity failure,
and 2 for unresolved or invalidated evidence. JSON and its neighboring Markdown
report record the limits; a capacity pass is not a success-probability certificate.

The SNR candidate uses the complete receiver's actual fixed grid and the normal
affine residual/model priors, including the duration-dependent frequency step.
It remains a modeled true-component target until actual complete-path trials
qualify acquisition, wrong-bit decisions and physical completion. A separate
cheap diagnostic replays the simulator's known noise seed to recover the clean
sampled waveform, applies the canonical quiet projection, and estimates
DATA-to-QUIET probability under known proper-complex Gaussian noise. This is
conditional on that one channel realization; it is outside receiver CPU timing
and cannot supply receiver initialization or a population/message success claim.
A bound near one is inconclusive, not an observed false-end probability: an
admitted DATA decision takes precedence over the quiet statistic.

## Complete receiver work

A complete capacity estimate must account for received-baseband filtering,
Helper processing, full public coarse search, fine synchronization/refinement,
continuous tracking, bit/null decisions, quiet-slot completion, bounded source
decoding and result presentation. Include unsuccessful candidates, noise-only
listening, interior loss and reacquisition. Candidate-dependent setup, template
cache fills, key/ordinal changes, dispatch, synchronization and merging are
receiver work. Only reusable preparation before reception and simulator
generation may be excluded from sustained service; report readiness separately.

Receiver inputs are the samples and public local configuration. Evaluator truth
such as transmitted bits, actual offset, selected epoch, channel state or message
length must not choose the receiver's hypotheses, endpoint or decision. Coarse
proposals may select fine candidates, but coarse and refined scores from the
same observations must not be added as independent evidence.

Operational DATA admission and final bit selection use the fixed initial-affine
lane with the complete public-prior correction. The adaptive lane and combined
scores remain diagnostic but are fully executed and charged. Fine Gaussian
processing defaults explicitly to FP64 after a reproduced high-SNR FP32
innovation failure; numerical guards remain enforced and precision does not
change in response to an observation.

## Local parallel capacity

Report prepared elapsed processing seconds per second of received baseband for
one complete receiver workload. A ratio at least 1 fails real-time capacity;
0.1 represents a 90% processing-time margin. Never divide elapsed service by
core count, sum thread CPU counters as elapsed time, or substitute throughput
from parallel independent simulation trials. CPU counters diagnose utilization.

Detect visible affinity, quotas and physical-core topology. Compare a small
declared set of same-stream worker allocations that includes the complete
available logical-CPU budget and physical cores when known. Keep one aggregate
concurrency budget across numerical libraries and outer workers. Measure the
actual dispatch, merge and native-template work; do not assume that requesting
more library threads uses the CPU productively. Report remaining serial,
memory or scheduling limits. Workspace must include simultaneous worker state,
shared buffers, native services and transient scratch, with explicit uncertainty.

## Efficient measurement

Predeclare path strata, candidate/state limits, measurement budget, uncertainty
decision and stopping rule. Begin with cheap complete-path correctness checks.
Use brief actual receiver work and fixed-geometry extrapolation with a holdout;
account for every stage and its full multiplicity. Test cold, steady and terminal
work where their costs differ. Unmeasured stages, changing state regimes or a
failed holdout leave the complete estimate unresolved. A short timing probe is
not an independent success-probability trial or proof of full-message reception.

The manual harness shares the regression's 1/2/4-unit holdouts and 3/5/9
sequential timing looks. Its default nominal measurement requires actual complete
public acquisition, then keeps one fine session alive through a fixed two-second
prefix and cumulative one-, two- and four-second continuation. Charge prefix
setup and finish once per public slot, and the measured continuation rate for
the remaining window. Each outer observation resets the native cache once;
continuation must demonstrate new native cache fills, nonzero template work and
stable state geometry. Material state/cache changes or a failed cost holdout
prevent a supported extrapolation. The maximum is nine six-second observations
per row, not a full long transmission. Partial acquisition cannot supply
work-only coordinates to this nominal measurement.

The continuation sampler stops for supported relative precision, the bounded
look limit, or a failed work or holdout check. Apply the real-time decision
only after normalizing the prefix, continuation and finish by slot cadence and
adding the other required service. A review found that the historical v3 run
stopped four rows using the raw continuation rate. Those intervals do not
demonstrate correct stopping for final service. See the
[validation record](validation.md#complete-headless-robust-and-manual-capacity).

The separate conservative fine-state probe starts 256 states in
each lane, executes two half-second segments and the real intervening posterior
transition/phase transport/merge, and charges that entire unit once per actual
half-second segment. Both native and Python template caches are cold for each
unit. This deliberately repeated startup and emission work may withhold a pass;
it does not by itself prove ordinary reception is slower than real time. The
ordinary active, terminal and noise-listening strata are measured separately.
Broad public searches use brief exact frequency tiles plus measured full-count
candidate selection; partial tiles never produce reception decisions.

Recurring public ordinal banks include their fidelity checks and native source
preparation. Workspace accounting bounds simultaneous banks, projector/control
temporaries, worker scratch, input copies and native services before allocation,
and supplements those estimates with sampled parent/child RSS. Memory estimates
and fixed geometry allowances are not allocator or universal statistical guarantees.

Reread and validate the entire manual file on every invocation. Record exact
source, native binary, current list, configuration, machine and allocation
provenance. Partial row selection must remain explicit. Retain only evidence
supporting a current validation claim in ignored `local-evidence/`, and remove
superseded probes after their useful findings are recorded.

### Remote acceleration and local calibration

Remote GPU work can reproduce and investigate selected short receiver strata;
it cannot directly measure this computer's CPU service. A fast estimator must
independently measure the applicable complete receiver work on this computer,
including concurrent fine workers, resident search banks, recurring
preparation and the remaining stages above. Match source, native binary, full
manual list and normalized configuration, CPU topology/affinity/quota,
numerical runtime and actual worker/thread allocation. Use independent local
holdouts to validate extrapolation for each applicable work and memory regime;
missing calibration, stages or holdout support leaves the estimate unresolved.
GPU parity or a matching identity alone supplies none of those qualifications.
Do not reuse older CPU reports as fresh calibration when their required runtime
identity was not recorded.

[`tools/triad_calibration_identity.py`](../tools/triad_calibration_identity.py)
provides an identity guard for future local calibration, not an estimator. It
requires before-and-after observations around both the recorded and current
workloads, using the actual controlled Python and native processes. The caller
must bind the real work and timing to those intervals and independently verify
thread use, work coverage, workspace and holdout accuracy. The identity contains
machine-specific data and must remain local. A mismatch or unavailable required
identity fails closed. Its explicitly enabled conditional mapping mode records a
kernel mapping-device discrepancy and returns a distinct conditional result;
it is not an exact match or a capacity qualification. See the
[GPU diagnostic procedure](triad-performance.md#gpu-fine-work-parity-probe)
and [current findings](validation.md#gpu-fine-work-investigation).

The experimental [`tools/triad_replay_estimate.py`](../tools/triad_replay_estimate.py)
reads the complete current manual list and combines per-row local replay,
calibration and independent resident holdout receipts only when their source,
runtime, work, geometry and allocation gates match. It reports absent or
unmatched rows as unresolved. Its possible result is a conditional nominal
service estimate, not a complete receiver-capacity pass; the separate
worst-state and workspace requirements still apply. Calibration and replay
receipts contain local machine identity and must never be uploaded to the GPU
host. The [validation record](validation.md#gpu-fine-work-investigation)
distinguishes the measured full-list cached assessment from the cold local fit,
replay and independent holdouts required to establish its applicability.

Save the full suite's calibration fits during its ordinary measurement pass
when preparing remote replay for several rows. Choose a common workspace cap
before measurement that fits the computer's visible receiver budget. The
example cap is in MiB.

```sh
mkdir -p local-evidence
python tools/triad_manual_cases.py --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-cases.json --workspace-mib 4096 \
  --sustained-allocation-probe --calibration-receipt-prefix local-evidence/current-fit
```

This saves new private files named `current-fit-N.json` for the current row
numbers. It uses the full worker pilot and canonical one-second timing unit.
The ordinary suite keeps its message/quiet/EOF gate, SNR and transmit-time
diagnostics, and full CPU/workspace reporting. Each unique configuration
prepares one actual observation and runs one canonical absolute measurement.
An unavailable runtime identity or mismatched acquisition fingerprint makes
its fit unresolved while retaining the ordinary measured result. A skipped
or failed message gate leaves suite-captured fits unresolved. Ordered
duplicate rows explicitly reuse the ordinary result and have no independent
fit. Add `--allow-conditional-calibration-identity` only when the documented
conditional mapping assumptions hold. Keep the cap fixed for each replay and
holdout, and preserve any failed or unresolved gate without repeating the fit.

For one row, first use
[`triad_nonfine_calibration.py`](../tools/triad_nonfine_calibration.py) to record
a local fit if a suite fit was not captured. The pilot measures all supported same-stream worker allocations
and binds the selected count to the runtime before fresh inference. Next use
that count for the remote acquisition and local
[`triad_replay_cpu.py`](../tools/triad_replay_cpu.py) measurement. The replay
must come from an independently authenticated complete-acquisition receipt and
match the fit's exact input, work, allocation and workspace geometry. Packet
bounds describe the full calibrated native capture, including any pre-roll.
The original public calendar separately binds the stream scheduler's slot
origin and extent. Replay does not rebase the captured samples.

An experimental Helper-prefix path addresses a cross-host Helper diagnostic
hash mismatch without changing the remote result. A new local fit must have
captured its own prefix identity from the nominal observation before that
buffer is reused for the noise-only probe. Older fits without this anchor
cannot use this path. On the authenticated remote producer, add
`--helper-prefix` and a predeclared `--memory-limit-bytes` to produce a bounded
envelope containing the original fine packet and Helper input. Obtain the
producer's original attestation independently over the authenticated channel;
an attestation included in an envelope does not authenticate itself. Keep both
the envelope and the independently obtained original attestation unchanged.

For local replay, pass the envelope as `--packet`, that independent attestation
as `--trusted-acquisition`, and the new private fit as `--helper-prefix-fit`.
The verifier derives the prefix extent from the local public geometry, checks
it against the independently captured fit anchor and full signal/non-Helper
work identities, then recomputes the canonical Helper locally. It records a
separate verification observation before the fine timer; it does not replace
or relabel the original remote Helper hash. The prefix is capped at 1,000,000
samples and the Helper scratch estimate at 768 MiB, with input, envelope,
native and bank reservations also subject to the selected local workspace cap.
Any absent or incompatible anchor, work, geometry, identity or workspace leaves
the comparison unresolved. Keep the private fit and local timing receipts on
the local computer; do not upload them to the remote host.

When Helper verification is needed for the holdout, provide the same original
evidence with `--helper-envelope PATH --helper-attestation PATH` alongside
`--holdout-fit` and `--holdout-replay`. The holdout verifies this evidence
before its declaration and must still match the fit's actual full signal work.
For the estimator, use the paired `--helper-envelope N=PATH` and
`--helper-attestation N=PATH` options for that row in addition to its fit,
replay and holdout. The estimator recomputes verification from the evidence
rather than accepting a supplied local verification claim. The unchanged
strict match remains the default when these options are not provided. Supplying
the paired evidence explicitly runs verification even if the remote Helper
digest already matches; this permits a bounded exercise of the new path. One
source-matched Groundwave case has exercised that path, with matching remote
and local Helper digests; the differing-digest branch has only controlled-test
coverage. See [the validation record](validation.md#gpu-fine-work-investigation).
This evidence does not establish GPU acceleration, capacity, or transfer to a
new signal.

Predeclare one independent resident holdout by passing the trusted local
receipt paths to the calibration command:

```sh
python tools/triad_nonfine_calibration.py --case-index 7 \
  --exporter build/dev/triad_helper_export \
  --holdout-fit local-evidence/case7-fit.json \
  --holdout-replay local-evidence/case7-replay.json \
  --output local-evidence/case7-holdout.json
```

Use the actual current row number. Include `--allow-conditional` only if
the fit and replay used the documented conditional identity mode. The fit,
replay and holdout run in separate local processes on the same boot. The
holdout binds both exact receipt digests before preparation and pilot timing,
compares the full set of worker candidates again, and reports its exploratory
winner separately from its fresh inference at the fitted allocation. In a
case 7 trial the fit pilot chose two workers and the next pilot chose four.
The fixed holdout repeats actual acquisition, continued fine processing,
recurring banks, stream control, planning, quiet state, codec and worker
lifecycle work, with resident nominal workspace measurements. It skips the
additional cold-state, noise-only, terminal and erasure inference; the full
fit still measures those paths. Its distinct receipt cannot supply a full
fit or worst-state result. This validates repeat timing at the fitted
allocation for the exact sampled realization. It cannot establish the best
allocation, the full CPU ceiling, or transfer to a different seed or path.
The [earlier case 7 result](validation.md#gpu-fine-work-investigation) used
the full-stratum holdout on its recorded source; the shorter holdout needs
fresh source-matched measurements.

Pass the three trusted local files to the estimator with
`--calibration 7=PATH`, `--replay 7=PATH`, and `--holdout 7=PATH`. Use
`--allow-conditional-identity` there only for conditional receipts. Keep a
failed holdout as an unresolved result. The protocol assumes one
predeclared holdout per fit and replay pair, with no retries to select a
favorable outcome. Hashes and same-boot chronology check correspondence
between trusted files. They do not authenticate the files or enforce that
operator assumption. Keep raw receipts private and temporary. Once the
matched validation or unresolved diagnosis is recorded, retain the concise
findings and remove superseded raw evidence.

## Separate sensitivity checks

The bounded [pilot](../tests/triad_manual_sensitivity.py) consumes a freshly
completed, unchanged manual-capacity report and chooses an affordable current
configuration. It tries an independent bit-0/bit-1 pair at the matched model
target, and at most one pair 6 dB higher after ordinary reception failure.
Correct reception includes both scored quiet slots and complete work receipts.
A single-bit dictionary rejection does not invalidate the physical bit endpoint;
these are not whole-text-message trials.

```sh
python tests/triad_manual_sensitivity.py \
  --capacity-report local-evidence/robust-cases.json \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-sensitivity-pilot.json
```

If the original model-point pair succeeds, the separate
[confirmation](../tests/triad_manual_confirmation.py) predeclares one point
3 dB higher and 29 fresh independent AND pairs. It admits bounded waves under
local CPU, memory and elapsed-cost limits, and stops admitting work after a
failed pair or resource/identity gate. Every launched outcome is retained.
All 29 pairs must succeed before the exact one-sided 95% lower bound exceeds
90%. An incomplete prefix has no such qualification, and no result qualifies
literal `fox`, other cases or a minimum SNR threshold. This is a conditional
synthetic-channel operating point. See [exact binomial interval construction](https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm).

```sh
python tests/triad_manual_confirmation.py \
  --capacity-report local-evidence/robust-cases.json \
  --pilot-report local-evidence/robust-sensitivity-pilot.json \
  --exporter build/dev/triad_helper_export \
  --output local-evidence/robust-sensitivity-confirmation.json
```

Parallel independent receives are permitted here solely for sensitivity.
Their timing must never supply same-stream CPU-capacity claims. Use sampled
post-exec native RSS with workspace/runtime allowances: a reaped child's
high-water counter is not simultaneous native-process residency. Linux reports
the largest child's peak and preserves usage across exec; see
[`getrusage(2)`](https://man7.org/linux/man-pages/man2/getrusage.2.html). The current
incomplete confirmation and its repaired monitor are recorded in
[validation](validation.md); do not silently restart its budget or relabel its
five successful pairs as a completed confirmation.

## Physical completion and security

Robust and Fast use observed quiet intervals for physical message completion.
Neither may use a received packet-length field or arbitrary-length header to
choose processing geometry, allocation sizes or the physical endpoint. The
receiver's slot geometry, buffers, candidate budget and source quota are local
bounded configuration. Input EOF, cancellation, quota exhaustion and application
end flags do not establish physical completion.

A quiet decision must come from fully executed received-sample processing.
Incomplete, erased, ambiguous or work-limited searches cannot be counted as
quiet. Hold a possible trailing quiet interval pending the declared end rule;
if DATA resumes, retain the missing interior interval as an erasure and perform
the required reacquisition. Do not fabricate its bit or silently shorten the
message. Decode and present source content only after physical completion and
the applicable source-validity checks. Keep false-end assumptions and end
latency explicit: a sufficiently deep fade can resemble physical absence.

Quiet energy uses only complete orthogonal projection blocks inside the native
public aperture: the current DATA support intersected with the search window
and the latest predecessor/earliest successor signal bounds. These include
public clock, arrival, delay and filter-tail envelopes, never realized channel
truth or a received length. All original input remains in the fine likelihood;
an absent or empty quiet aperture cannot establish silence.

Fast already enforces this boundary in its
[fixed-interval and physical-completion contract](fast-mode.md#fixed-intervals-without-received-lengths).
Its application metadata is interpreted after physical completion and is not a
modem packet-length mechanism. Preserve the
[shared received-content restrictions](security.md#received-content-presentation).
