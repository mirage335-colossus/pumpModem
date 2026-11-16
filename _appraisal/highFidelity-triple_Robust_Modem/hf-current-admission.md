# Current-waveform admission and protected-message recovery

## Frozen investigation, 16 November 2026

This continuation starts at repository `baa28d5358a748cde76669568a7956c4d8abf0f9`.
It tests whether the current carrier-bound waveform and native admission rules
preserve useful protected-message recovery while rejecting unwanted reception.
The [previous findings](coverage.md#applicability-after-the-admission-and-carrier-key-changes)
remain historical evidence, not results for the new waveform. No production
receiver change or promotion is part of this experiment.

The first stage uses the existing compiled protected-reassembly probe. Each
capture passes through the canonical transmitter, native endpoint chain and
complete receiver once. Copies of its ordered, unfiltered slot scores then
pass through each of the six existing native admission policies, the canonical
message controller and the contained protected-fragment consumer. These are
dependent downstream comparisons, not six independent communications trials.
The unfiltered replay must reproduce actual receiver messages and consumer
results. Selected wanted and foreign-key direct-policy native runs additionally check
the replay against complete reception on identical input.

The baseline is `current`; all six policies are fixed before execution. The
primary comparison is `strict`, with the other tiers reported as conditional
tradeoffs. No threshold coefficient, codec rule or receiver search is tuned to
these observations. GUI visibility remains distinct from headless admission:
the GUI retains baseline decoding, whereas direct admission can introduce
erasures and change completion or decoding.

### Cases and stopping rules

1. Core: `clean`, `fade-byte`, `fade-boundary`, `noise`, `wrong-key`,
   `wrong-key-join`, and `separate`, retaining their original definitions.
2. Fresh holdouts, declared before execution: `fade-holdout` transmits `K`,
   removes public slots 23–24 and uses noise seed `original + 2`;
   `wrong-key-holdout` transmits `K` with that noise seed and a distinct fixed
   foreign key whose bytes are `0x31 + i`.
3. Wanted sensitivity preservation: clean `B`, requested 8-second bits and
   complex noise variance 192, compared with requested 16-second bits and
   variance 384. Native sample rate is 1920 Hz. Thus nominal incident C/N0
   is 10 and approximately 6.99 dB-Hz respectively; actual emitted energy,
   realized duration and received energy are not inferred from these nominal
   labels. This is a static-path duration comparison, not disturbed-HF
   sensitivity qualification. If both points are well above their decision
   thresholds, one declared lower point (8 seconds, variance 768) may locate
   a nearer boundary. No open-ended search follows.
4. Direct-policy parity: repeat `clean` at `strict` on the same input, and
   compare its decisions and message outcomes with the downstream replay.
   After core scoring showed that clean reception exercises no rejection, add
   one `wrong-key`/`strict` full-receiver repeat to exercise the changed decision
   branch. This is a technical equivalence check, not another independent key
   or a threshold-selection trial.

The probe retains its 120-second per-case deadline, fixed public sample limits,
full automatic `W=2H` worker allocation and bounded consumer. Stop dependent
comparisons on an incomplete capture, recovery, source/configuration mismatch,
replay mismatch or deadline failure. Preserve those outcomes as execution or
validity failures. A complete run that fails a communications requirement is
still a valid observation and must not be discarded or relabeled as passed.

The required outcomes are separate: correct complete delivery; accepted wrong
content; false DATA; wanted DATA losses; physical premature completion;
actual attempted/rejected reconstruction; and bounded execution. Byte erasures
are measured from the positioned native frame, since multiple missing bits can
erase one Reed–Solomon byte. Protected recovery does not erase the recorded
premature physical completion, and final rejection does not excuse false DATA.

After these inexpensive checks, the existing active `Ae` outage witness and
selected coupled oblique channel cases may be regenerated to test whether
current inputs preserve the earlier completion defect and predictor regression.
The larger protected/predictive and two-hop duration experiments are conditional
on a useful new discriminator, rather than automatic repetitions of every
historical campaign. Retain the previous calibrated-measurement audit; no new
dataset or disturbed-day occurrence distribution is assumed.

### Coupled physical repair discriminator

Source review identified a feasible additional discriminator: retain the
coupled magnetic/reflection/diffraction model, actual endpoint clock errors and
continuing tone, and compare canonical RS20 reception with the existing
predictive-quiet/fragment-consumer combination on identical current-waveform IQ.
This uses a **new, explicitly slower trajectory**, not a replay of the archived
physical failure. The original field's density period is 24 km and its primary
drift is 8 m/s. An initial feasibility note confused the roughly 267 km path
geometry with the density period; independent review corrected that mistake
before implementation or execution. Merely extending the old 2400-second
limit would cross the original density period and violate its nonrepetition
contract.

The implemented extension fixes drift at 5.5 m/s, a 4200-second maximum horizon,
and the unchanged full table/mesh/frequency/FIR/voltage-error gates. Maximum
traversal is 23.1 km, below one period; actual capture and causal tails must
also pass admission. The public calendar remains unchanged. Freeze RS20 `e`,
requested 8-second bits, incident reference C/N0 30 dB-Hz, path seed 70501 and
noise seed 80501, followed by fresh `a`/noise80547 and full noise/wrong-key
controls if the first repair observation is valid. Both payloads must first
verify as 64-bit canonical protected codewords. Compare baseline and candidate
on one rendered capture per case. Numerical failure prevents communications
admission; a receiver miss remains a failed requirement, not a failed simulator.
The original unprotected `Ae` predictor-regression witness remains a separate
required integrity check, so protected success cannot promote that rule for
unprotected operation.

### Research basis

The reference collection's [native interfaces](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md)
and [statistical protocol](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/86-statistics-and-calculation-checks.md)
support reusing the compiled stack and counting independent episodes rather
than correlated windows or policy replays. The maintained
[admission research](../../docs/triad-stochastic.md#wrong-key-admission) records
both useful discrimination and high-SNR wanted-signal losses.

[Senger et al. (2011)](https://arxiv.org/abs/1104.0576) describe efficient
single-trial reliability-based RS erasure selection, but their calibrated AWGN
symbol probabilities are not supplied by this receiver's competing-bit gap.
The experiment therefore uses the existing native rules without interpreting
that gap as a bit-error probability. [Saha and Ramdas (revised 2025)](https://arxiv.org/abs/2408.14015)
show that robust sequential inference can be possible under explicit
contamination neighborhoods; they do not calibrate an arbitrarily strong
foreign-key waveform as thermal noise. The [October 2026 sequential-reset
preprint](https://arxiv.org/abs/2610.09339) offers economical accumulation under
conditionally valid evidence, with an interval-FDR endpoint distinct from the
probability of any incorrect message. These are potential alternatives if
their assumptions can be established, not guarantees for the present heuristic.

## Evidence and retention

Results will identify native/appraisal source hashes, binary hash, configured
carrier, policy, input digest, case settings and actual work. Raw receipts live
under ignored `local-evidence/hf-current-admission/`; retain them while they
support this unresolved comparison. Remove redundant generated files after
their conclusions and reproducible inputs are superseded. Do not count the
current-source results as calibration of real disturbed-day occurrence rates.

## Current-waveform core and fresh holdouts

The nine frozen core/holdout captures completed with valid observations and
successful unfiltered replay parity. Three actual outage captures recover
exact `B`, `e` and fresh `K` through the existing protected consumer; the native
fragment completion/rejection remains visible. All six admission tiers retain
those three recoveries. Clean `B` and the two separate legitimate messages
also deliver under all tiers, and the complete noise control remains quiet.

The unwanted DATA results are:

| Fixed foreign-key capture | Unfiltered | Guarded | Strict | Very strict | Extreme | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original `B` control | 72 | 26 | 8 | 1 | 0 | 0 |
| Two actual emissions / joining control | 136 | 56 | 14 | 2 | 0 | 0 |
| Fresh key, payload `K`, noise seed | 72 | 29 | 5 | 0 | 0 | 0 |

No policy accepts native or reconstructed content in these controls. Only
the unfiltered joining control actually attempts the combined reconstruction;
it rejects. Suppressing DATA at stronger tiers removes that particular decode
opportunity and must not be reported as another exercised decoder rejection.
The frozen primary `strict` policy therefore **does not satisfy zero false
DATA**, despite its substantial reduction. The two deepest tiers are useful
conditional results here, not calibrated wrong-key rates or HF recommendations.

Two additional full native runs, clean and wrong-key at `strict`, reproduce
the corresponding downstream decisions and message/consumer outcomes on
identical IQ, including all 64 erased unwanted DATA decisions in the first
wrong-key control. These technical repeats are not new independent channels.
The native receiver, current transmission-key, predictive-quiet, protected
consumer and new replay checks passed (five test groups). The first nine
captures took about 329 seconds altogether; the two direct-policy checks
took about 62 seconds on the local Ryzen 5 PRO 5650U, using the full 24-worker
receiver allocation. Those costs are execution observations, not a hardware
capacity qualification.

These first eleven runs use native source
`fc5b4c596aff3866e03a9f2b4c506bec2da3b4eff0d164adcdfcaf61ffade1e6`,
appraisal source
`84c2813a59c0de4bee8ab30eb6b3e9c569d3b9c1212bc0541a5edc431d07a6bc`, and probe
binary `c4cf4374be18b9fac2e3c9051b50da738645e596cd73b6387fc1fb317efff4ab`.
They precede the subsequent physical-extension build; this distinction is
intentional. Commands and all per-policy observations are retained in the
[core](local-evidence/hf-current-admission/core-summary.json),
[holdout](local-evidence/hf-current-admission/holdout-summary.json) and
[direct parity](local-evidence/hf-current-admission/parity-summary.json) receipts.

Reproduction from the repository root:

```sh
python3 _appraisal/highFidelity-triple_Robust_Modem/current_admission.py --stage core
python3 _appraisal/highFidelity-triple_Robust_Modem/current_admission.py --stage holdout
python3 _appraisal/highFidelity-triple_Robust_Modem/current_admission.py --stage parity
```

Use a fresh `--output-dir` under the appraisal's `local-evidence` for a rerun;
the runner refuses to replace retained observations. Its execution-valid flag
does not override the probe's separate failed communications requirements.

The current-waveform [unprotected `Ae` witness](local-evidence/hf-current-admission/active-fade.json)
also completes its intended experiment and **fails the candidate safety
requirement**. Both native receivers admit the same 14 correct DATA bits and
later resumed DATA. The baseline leaves the two interior faded slots ambiguous
and accepts nothing. Predictive quiet changes both to QUIET and accepts `A`
at public stop slot 15 while the input remains open. Both receivers hash the
identical capture
`4cc1baf513670abe41c59b3d260239dbb6a51577420adf55d45ab76ddf2d52d0`.
The paired experiment takes about 291 seconds; the changed wire domain has
not removed the reason to reject general unprotected promotion. The separate
encrypted foreign-key emission preflight passes.

### Wanted sensitivity and duration controls

All six policies deliver clean protected `B` at both frozen duration points:

| Requested / realized bit duration | Complex noise variance | Nominal incident C/N0 | Measured incident DATA energy / noise density | Native run wall time |
| --- | ---: | ---: | ---: | ---: |
| 8 / 10.4 s | 192 | 10 dB-Hz | 7438.7045 | 30.13 s |
| 16 / 20.8 s | 384 | 6.9897 dB-Hz | 7480.9292 | 47.51 s |

This is a measured near-equal-incident-energy comparison: the longer result
has 0.0246 dB more incident E/N0, not an inferred factor from requested
duration. Both have 72 correct DATA decisions, no erasures and normal physical
completion. The six downstream state/codec evaluations together take about
0.17 ms per capture, outside the full native DSP pass. These small timings are
illustrative overhead observations, not a precise microbenchmark.

Because both points have large evidence margins, the predeclared lower point
was executed: 8 requested seconds / variance 768 (approximately 3.9794 dB-Hz).
Every policy fails delivery identically, losing 56 active slots to QUIET and
producing nine premature physical completions. The protected native decoder
and consumer accept no incorrect content. Thus this point exposes a sensitivity
failure while showing no additional threshold-induced loss in this particular
static path. It does not establish a disturbed-HF sensitivity floor, optimal
energy requirement, or receiver distance from a known-channel reference.

These three captures use the same native source as above, with appraisal
source `95bbe06c0298039df34ee280285fbd681316dba869b12d7008e50f562e6011a1`.
Their [weak-pair](local-evidence/hf-current-admission/weak-summary.json) and
[boundary](local-evidence/hf-current-admission/boundary-summary.json) receipts
bind the executable and all raw results. Reproduce with `current_admission.py
--stage weak` and `--stage boundary` after the frozen core checks; use a fresh
output directory. The boundary is a retained failed communications requirement.


## Coupled physical protected-message recovery

The first two physical campaigns use native source
`fc5b4c596aff3866e03a9f2b4c506bec2da3b4eff0d164adcdfcaf61ffade1e6`,
appraisal source
`95bbe06c0298039df34ee280285fbd681316dba869b12d7008e50f562e6011a1`
and executable
`ae05c0c68257c67853b2cfdece923e74e52310300729621a43058d4ccec6964f`.
The configured carrier is 10,000,240 Hz, waveform revision
`independent-apsk-v2-carrier-v1`, CPU reference implementation, Helper off and
24 automatic receiver workers on the 12-logical-CPU Ryzen 5 PRO 5650U. This is
one slowly translating, two-dimensional physical field at the declared 5.5 m/s
velocity, not a calibrated disturbed-day occurrence sample or multiple-hop
qualification. Endpoint TX/RX sample-clock errors are +1/−1 ppm, RF errors
+0.01/−0.01 ppm, and a continuing 0.3-amplitude tone is present. The canonical
calendar retains at least 60 seconds between public starts: requested 8-second
bits realize 11.6 seconds, with 64 transmitted DATA slots and two scored ending
slots across 3912.31 seconds of input. Complete source, path and receiver
receipts preserve the actual sample counts, energy and clocks.

Both paired policies receive the identical rendered IQ and validity mask. The
normal receiver and its fragment consumer are the baseline; the second native
receiver enables the existing predictive-quiet candidate and feeds the same
bounded consumer. No transmission truth enters either receiver or consumer.
The new downstream admission helper reuses ordered scores and the canonical
native rule/controller, reserves 16 MiB outside DSP, accepts at most 304 slots,
and checks exact Unfiltered semantic parity. It does not perform another
history search, alter waveform ordinals, turn admission rejection into QUIET,
or finish a message from EOF.

| Wanted capture | Baseline | Predictive receiver before consumer | Bounded consumer | Six admission tiers |
| --- | --- | --- | --- | --- |
| `e`, noise 80501 | 61 correct DATA, three AMBIGUOUS; no completion | Same DATA, three active QUIET; two codec-rejected fragments | Exact `e`, one native RS decode; three missing bits in two data bytes | All preserve complete recovery; none reject additional wanted DATA |
| Fresh `a`, noise 80547 | 62 correct DATA, two AMBIGUOUS; no completion | Same DATA, two active QUIET; two codec-rejected fragments | Exact `a`, one native RS decode; two missing bits in one data byte | All preserve complete recovery; none reject additional wanted DATA |

Each reconstructed frame spans public slots [0,66) with 64 codeword positions.
The consumer uses the actual rejected fragment events and located gaps, with
no extra DSP. **This demonstrates a complete protected payload repair on the
new detailed channel, including the fresh payload/noise holdout.** It does not
repair the underlying physical segmentation: each candidate still has one
premature completion, and its ordinary native `correct_decoded_complete` stays
false. Those remain separate failed criteria. The fresh `a` is a holdout in
payload and thermal noise, not a second physical realization. The frozen follow-up is triggered by either a DATA loss or a delivery miss.
The three missing DATA bits in `e` therefore require the declared 16-second
physical run even though the consumer recovers the payload; review caught and
corrected an initial plan to skip it on delivery alone. Its result is reported
separately below.

The foreign-key physical capture leaves **9, 6, 5, 3, 3, 1 false DATA decisions**
under Current, Guarded, Strict, Very strict, Extreme and Maximum respectively.
This holds for both quiet policies. No native or consumer content is accepted;
the consumer makes **zero** combined decode attempts, so this control does not
replace the static two-emission negative that actually exercises decoder
rejection. Even Maximum therefore fails the physical zero-false-DATA gate.
The matched no-DATA control retains the continuing tone and final thermal
noise over the entire capture. Both native policies and all six downstream
tiers produce zero false DATA, zero accepted content and no consumer decode
attempt. The preservation campaign completes in 533.67 seconds with no
execution failures; its canonical fresh-`a` delivery failure stays recorded.
The static deepest-tier results above cannot be generalized to this coupled
channel, and none of these counts estimates the GUI's whole-message visibility
rate or a population false-alarm probability.

Receiver service for `e` is 94.17 seconds baseline and 96.54 seconds candidate;
its one combined decode takes approximately 0.244 ms. These are measured
complete service observations for this bounded geometry, not hardware capacity
qualification or a sensitivity claim. Shared source/channel preparation and
rendering take about 41 seconds and are performed once per paired capture.
Each per-policy wall figure includes that common render and must not be added
to the other's wall figure without subtracting it. The first wanted/foreign-key
campaign takes 518.25 seconds total and has no execution failures. It retains
the baseline communication failure and the candidate premature-completion
regression. No RunPod resources were used: these runs exercise the compiled
CPU reference route, for which GPU rental would not accelerate the executed DSP.

### Numerical and receipt validity

The full independent coupled-wave mesh, ordering, frequency, finite impulse
response and cancellation checks execute before receiver admission. No
numerical tolerances or nonrepetition rules were relaxed. Five actual native
plan receipts cover `e`/`a`/noise/foreign-key at requested 8 seconds and the
conditional `e` 16-second follow-up. A longer protected `K` is rejected before
rendering because its 72 DATA bits exceed this physical horizon.

The first native `e` plan was sound but the independent Python reader rejected
it using an old blanket 64-second requested-duration check. The reader was
corrected only for the explicitly admitted protected scenario's 8/16-second
values. The original rejection and receipt remain under
`local-evidence/hf-current-admission/`; wrong 64-second requests and
realized durations below the request are still rejected. This setup correction
is not a simulator accuracy pass; the completed receiver receipts separately
supply the executed physical gates.

Reproduce the frozen paired experiments from the repository root with fresh
output directories:

```sh
python3 _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-protected-oblique-discriminator.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/oblique-discriminator-rerun
python3 _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-protected-oblique-preservation.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/oblique-preservation-rerun
```

The retained [first campaign](local-evidence/hf-protected-oblique-current/run.json)
and [preservation campaign](local-evidence/hf-protected-oblique-current-preservation/run.json)
bind their campaign, executable, source identities, numerical tables and input
digests. Their ordinary campaign summaries continue to describe the baseline;
`status: completed` is execution status, not acceptance of the experimental
receiver. Keep these compact receipts and the preflight failure while they
support the unresolved completion/integrity/admission comparison. Distill and
remove them when superseded; do not retain whole obsolete capture campaigns.


## Implementation decisions supported by these observations

- Keep the existing predictive-quiet rule experimental. Current-domain `Ae`
  repeats its unsafe unprotected acceptance; protected success cannot waive
  that negative control or the physical-completion contract.
- The bounded protected consumer merits further development because it repairs
  two actual detailed-channel messages using one additional codec call and no
  additional history search. Preserve actual rejected events, public positions,
  recovery/session barriers, strict integrity checks, and bounded buffers.
  These are appraisal results; the consumer is not integrated into the GUI.
  GUI integration would additionally require compatible retained rating metadata
  and explicit presentation of recovered protected content.
- Keep native admission and GUI visibility distinct. The new current-source
  comparisons support conditional discrimination, not authentication or a
  universally safe stronger default. Maximum's shorter-duration physical survivor and the
  maintained long-window wanted-signal losses both prevent that recommendation.
  Never turn a DATA veto into physical silence to force completion.
- Account for the protection itself. Protected `e` has 64 active codeword bits
  versus three uncoded bits, a nominal 21.333× active transmission cost at equal
  per-bit duration/power. The baseline/candidate physical comparison holds that
  same protected waveform and energy fixed. It does not claim equal-energy
  superiority over the shorter uncoded message, or a new weak-signal floor.
- The next sensitivity claims must still be made against fixed complete-message
  operating points with negative controls and actual energy/work measurements.
  The static near-equal-energy pair demonstrates preservation on one static
  path; the fixed +30 dB-Hz incident physical case demonstrates repair of completion
  and fragmentation. Neither alone establishes graceful degradation throughout
  the HF GUI range, or replaces calibrated disturbed-event measurements.

## Declared physical duration follow-up

The frozen `DATA or delivery miss` trigger is met by the three missing DATA
bits in the shorter `e` capture. The requested 16-second experiment completes
in 391.11 seconds with all numerical and receipt checks intact. It buys more
energy at the same incident C/N0; it is not the static near-equal-energy test.

| Measured quantity for `e` | Requested 8 s | Requested 16 s |
| --- | ---: | ---: |
| Realized active bit duration | 11.6 s | 23.2 s |
| Input capture duration | 3912.3089 s | 3923.9089 s |
| Incident DATA energy sample sum | 1,422,284.1428 | 2,910,796.4372 |
| Selected physical-path DATA energy sample sum | 181,502.1850 | 371,490.7205 |
| Incident DATA energy / final noise density | 740,772.9910 | 1,516,039.8110 |
| Baseline active DATA losses | 3 | 0 |
| Baseline complete delivery | No | No |
| Predictive receiver premature completions | 1 | 0 |
| Predictive native complete delivery | No | Exact `e` |
| Consumer combined decodes / correct recoveries | 1 / 1 | 0 / 0 |
| Baseline receiver service | 94.17 s | 170.35 s |
| Candidate receiver service | 96.54 s | 168.00 s |

Both have noise variance 1.92. The actual DATA energy increase is approximately
2.047× (+3.11 dB), while candidate service increases approximately 1.74×.
Selected physical-path DATA energy also increases approximately 2.047×; its
energy-averaged attenuation is about 8.94 dB in both captures. This separately
reported component excludes the continuing tone and is not a calibrated
post-receiver field sensitivity rating.
All six downstream admission tiers retain the longer case's direct native
delivery. The 60-second public recurrence explains why doubling active duration
adds only 11.6 seconds to this capture; it does not halve a continuously active
message's throughput cost. No unnecessary repeated history search is introduced
by admission replay or the consumer.

This observation removes the DATA losses and premature completion for the
selected protected message, while the ordinary baseline still cannot finish
under continuing interference. The useful result triggers the already
contemplated longer-duration fresh `a`, foreign-key and tone/no-DATA controls.
Their fixed inputs are in
[the duration-preservation campaign](campaigns/hf-protected-oblique-duration-preservation.json).
The original shorter failures remain evidence; no duration or threshold is
selected retrospectively as a universal default.

The [duration receipt](local-evidence/hf-protected-oblique-current-duration/run.json)
binds the same native source, appraisal source and executable as the shorter
physical campaign. The different input digest and actual energy are retained.

### Longer-duration holdout and complete controls

All three preservation cases finish within their individual 900-second limits,
in 1338.82 seconds total, with valid numerical checks and no execution failures.
Fresh `a` also recovers all 64 DATA bits and completes directly at every tier,
with no erasure, premature completion or consumer decode attempt. Its candidate
service rises from 94.75 to 165.53 seconds (about 1.75×), with approximately
2.047× incident DATA energy. The baseline still cannot finish either wanted
payload despite receiving all its DATA correctly.

| Complete foreign-key capture | Current | Guarded | Strict | Very strict | Extreme | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Requested 8 s: false DATA | 9 | 6 | 5 | 3 | 3 | 1 |
| Requested 16 s: false DATA | 9 | 5 | 1 | 1 | 1 | 0 |

No native or consumer content is accepted by the longer foreign-key control,
and no premature completion occurs. The final longer no-DATA control, which
retains the continuing tone and thermal noise, has zero false DATA, accepted
content and premature completion at all tiers. Both negative controls make
zero combined-consumer decode attempts; they do not replace the separately
exercised combined-decoder negative.

**The longer candidate with Maximum native admission passes all checks in this
finite four-capture repair comparison:** exact direct native delivery of both
wanted payloads, no unwanted DATA or accepted content in either control, no
premature physical completion, and complete bounded/numerically valid execution.
This is a useful demonstrated operating point under the constructed field.
The predeclared primary Strict comparison still fails because one foreign-key
DATA decision survives. Both facts are retained; Maximum was one of the six
fixed alternatives, not a newly tuned threshold or a retrospectively substituted
primary outcome.

The [longer audit](local-evidence/hf-protected-oblique-duration-audit.json) reports
five failed gate observations, one for each non-Maximum tier's foreign-key DATA,
and separately records Maximum's four-case pass. The overall all-tier status
and primary Strict status remain failed. Its
[raw preservation receipts](local-evidence/hf-protected-oblique-current-duration-preservation/run.json)
share the exact binary/source identity above. A complete no-DATA control is one
capture, not a rate estimate; both durations share the physical field and the
longer wanted holdout varies payload/noise, not ionospheric realization.

No automatic GUI/default change follows. The current unprotected `Ae` integrity
failure, other phase-history/sensitivity failures, and known stronger-admission
wanted losses in long Sub-9 windows remain unresolved. These finite positive
results do not supply calibrated disturbed-event measurements, a natural
condition distribution, a whole-GUI reliability qualification, or a new
theoretical sensitivity limit.

## Executable repair acceptance and software validation

`protected_oblique_audit.py` reads the frozen campaigns, run manifests and raw
native receipts, checks their identities and numerical/bounded-execution
contracts, and separately evaluates the nested candidate's combined delivery,
incorrect content, negative-control DATA and premature physical completion.
Exactly one native or consumer delivery is required for wanted messages;
duplicate acceptance fails. A negative control must accept no content, even if
a malformed result labels an accepted message “correct.” The four-case audit
does not transform characterization into predeclared operational qualification.

For the original shorter cases, all twelve wanted tier-delivery observations
pass, but eighteen premature-completion gate observations and six foreign-key
false-DATA gate observations fail. These are repeated policies on four rendered
captures sharing one physical field, not thirty-six independent experiments.
The valid [audit report](local-evidence/hf-protected-oblique-repair-audit.json)
therefore reports failed acceptance and returns exit code 1. Both its separate
delivery successes and its failed gates must be retained.
The same audit's `--longer` profile checks the exact one-plus-three follow-up
campaigns, refuses mixed short/long evidence, and reports each fixed policy
separately. Neither a passing conditional tier nor completed execution replaces
the recorded failed primary comparison.

Review found and corrected two postprocessor defects before acceptance: the
first implementation selected the baseline and incorrectly required native
and consumer delivery together; a later negative-content check could overlook
acceptance labeled “correct.” Focused tests distinguish baseline from candidate,
require exactly one delivery path, and reject both incorrect and misleadingly
labeled negative acceptance. These were reporting defects; no native receipt
or modem outcome was changed. Superseded audit summaries are discarded after
these findings are preserved here; the native failed-case receipts remain.

Recompute with fresh output paths (no DSP is executed):

```sh
python3 _appraisal/highFidelity-triple_Robust_Modem/protected_oblique_audit.py \
  --output _appraisal/highFidelity-triple_Robust_Modem/local-evidence/repair-audit-rerun.json
python3 _appraisal/highFidelity-triple_Robust_Modem/protected_oblique_audit.py --longer \
  --output _appraisal/highFidelity-triple_Robust_Modem/local-evidence/duration-audit-rerun.json
```

Affected native receiver/key, predictive-quiet, protected-consumer, admission
replay and vector-oblique checks pass. The native integration test and 151
runner checks pass, as do three runner provenance/timeout checks and nine
retrospective-audit tests. The two new Python groups are registered in CTest.
The complete physical campaigns exercise the actual numerical admission checks;
software-test passes alone are not used as communication successes. Only
appraisal files changed; no production behavior or default was promoted.
