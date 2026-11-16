# Bounded protected-fragment recovery

The [admission/carrier-key applicability map](coverage.md#applicability-after-the-admission-and-carrier-key-changes)
explains how to reuse these source-bound results with the newer modem. GUI
filtering does not change native fragments; direct DATA admission can add
erasures, and the new waveform domain requires fresh complete-message inputs.
Neither change retroactively repairs or invalidates the recorded observations.

This appraisal-only experiment reuses the native RS20 redundancy to recover a
short message after an interior fade has already split its physical reception.
The canonical receiver, its scored two-quiet-slot completion rule, DATA search
and original completion receipts remain unchanged. The consumer publishes a
separate recovered candidate only at a later actual physical completion.

## Contract and research reconciliation

The earlier [identical-input outage witness](hf-fragment-integrity.md#exact-native-input-ambiguity-under-an-outage)
prevents a receiver from distinguishing an uncoded finished prefix from a
continuing message hidden by a complete outage at that deadline. More compute
cannot resolve identical observations. Existing error-control redundancy can
instead constrain a later reconstruction, with its transmission cost explicit.

[ECSS-E-ST-50-01C, §6.1 and §8](https://ecss.nl/wp-content/uploads/standards/ecss-e/ECSS-E-ST-50-01C31July2008.pdf)
separates Reed–Solomon correction from correct frame synchronization. Its
telemetry codes and synchronization markers differ from this modem; their
undetected-error claims do not transfer here. This implementation therefore
does not treat an RS decode, CRC32 check or a few unrelated-fragment controls
as authentication or a universal false-join bound. It also preserves the
public-calendar, located-erasure and physical-completion requirements in
`_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md`.

The native [consumer](src/hf_protected_reassembly.cpp) holds one RS20 short-frame
hypothesis in a fixed array of at most 304 public bit positions. It accepts
only short `codec_rejected_message` events that actually reached physical
completion. It validates chronology, classification counts, bit values,
local erasure locations and the exact two-slot quiet suffix. The first coded
position is `event.stop_public_slot - 2 - bit_slots.size()`: the native episode
origin can include one skipped leading quiet slot and is not necessarily the
first bit's address.

Every gap between eligible fragments becomes located null erasures, including
the earlier two quiet slots. At the later completion the existing native
`decode_message_bits(..., rs20)` runs once on that single bounded span. There
is no search over alternate starts, lengths, suffixes or selected fragments.
Native accepted content clears pending state and is never replaced. Recovery,
session/key/calendar changes, input close, inconsistent chronology and the
fixed position deadline also clear it. EOF and cancellation cannot decode it.

This addresses recovery after a split, not prevention of the premature physical
completion. Both outcomes must remain visible. It neither repairs uncoded
fragment acceptance nor changes production behavior.

## Predeclared staged checks

The component test first uses the actual native message controller and codec
with supplied scored slots. It covers within-byte and across-byte two-slot
erasures, a nonzero public origin and skipped leading quiet, clean accepted
messages, unrelated rejected fragments of a legal combined size, valid-message
separation, expiry, reset, unsupported codecs and malformed/overflow metadata.
It passes locally; these checks alone are not signal-chain recovery evidence.

The separate [native probe](src/hf_protected_reassembly_probe.cpp) freezes six
single-case runs before execution:

| Case | Input and required outcome |
| --- | --- |
| `clean` | Native RS20 `B` completes; consumer does no reconstruction. |
| `fade-byte` | `B`, wanted signal absent in public slots 10–11; retain two rejected native fragments and recover exact `B` afterward. |
| `fade-boundary` | Fresh `e` and noise seed, outage at slots 15–16 crossing a byte boundary; recover exact `e`. |
| `noise` | Entire wanted emission absent; no false DATA or accepted native/consumer content. |
| `wrong-key` | Mismatched transmitter/receiver keys; no false DATA or accepted native/consumer content. |
| `separate` | Independent legitimate space and `e`, with two physical quiet slots between; both complete natively without joining. |

Run clean first; an execution, wrong-content or delivery failure stops dependent
expansion for diagnosis. Each case has a 120-second deadline and 10 million
sample limit, or 16 million for the separate messages. The initially considered
2-million cap cannot contain the canonical minimum 60-second recurrence and
72-bit RS20 `B` at the retained 1,920 Hz sample rate. Lowering the rate to 240 Hz
would violate the source's declared ±287.5 Hz band. The revised caps were set
from those source constraints before any native run. Samples are rendered and
hashed incrementally, not stored as a complete capture.

In these original standalone cases, each public observation boundary is drained
before the next becomes ready, so a native recovery barrier cannot enter and
exit unnoticed. The full `W=2H` worker allocation remains available within each
slot. This staged scheduling does not qualify backlog throughput. Receiver,
consumer and channel memory, source/binary identities, complete IQ digest and
wall costs are reported. The combined experiment below records its original
serialized attempt separately from a bounded retry with overlapping windows.

The scenario is a simple constructed outage with known-white final receiver
noise and Helper off. It does not reuse, repeat or extend a finite detailed-HF
field beyond its validated horizon, and is not an equal-energy comparison to
uncoded `B` or a disturbed-day sensitivity qualification.

## Native observations

The six complete signal-chain observations produce:

| Case | Observed native / consumer outcome |
| --- | --- |
| Clean | Exact native `B`; no reconstruction, 31.29 seconds. |
| Within-byte fade | Two codec-rejected native fragments, one premature completion; consumer reconstructs exact 72-bit RS20 `B` with two located erased bits, 31.16 seconds. |
| Across-byte holdout | Same split failure; consumer reconstructs exact 64-bit RS20 `e` with two located erased bits, 28.11 seconds. |
| Noise | No DATA, native content or reconstruction, 31.58 seconds. |
| Wrong key | **72 false DATA decisions**, one native codec rejection, no native or consumer accepted content, 32.38 seconds. |
| Separate messages | Both legitimate messages complete natively; no reassembly, 56.36 seconds. |

The wrong-key requirement remains failed. The batch stops at this assertion;
diagnosis shows that the consumer merely retains the rejected 72-position frame
and performs no combined decode. It did not cause or repair these native false
DATA decisions. After that diagnosis, the independently predeclared
separate-message preservation check runs without changing any threshold and
passes. This continuation tests valid-message separation, not a repair of the
wrong-key requirement. Five cases pass their declared requirements; one fails.

The two successful reconstructions take about 59 and 37 microseconds of consumer
service, with one native codec call apiece and a fixed 65,536-byte consumer
reservation. The source is still RS20: `B` requires 72 transmitted bits rather
than the uncoded 13, and `e` requires 64 rather than 3. This recovery uses existing
redundancy; it is not an equal-energy sensitivity gain. Original premature
physical completions remain in both receipts. The clean run reserves about
425 MB in the native receiver and renders 8,488,803 samples incrementally with
the unchanged 24-worker/12-logical-thread allocation. These costs do not establish
real-time throughput under an accumulating backlog.

These observations bind to native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`,
appraisal source `c6ef2f65f59494d5cc6a79bb5c0ed95ee97a95e72b2ef8657f7617ea5a18e7ca`
and probe binary `bf713e311a1fcb1c990c86c0c9e2cf92b58a284c85a9a7bf990724fb82e2b5b8`.

### Wrong-key joining path

Review identified that the first wrong-key control never reached a combined
decode. A separately frozen `wrong-key-join` case transmits RS20 `B` at slot 0,
leaves eight public quiet slots, then transmits RS20 `e` at slot 80. Both use
the same fixed distinct transmitter key; the receiver uses the amateur key.
The joined 144-position span is a legal RS20 extent, so this control exercises
the new acceptance opportunity rather than merely retaining a fragment.

The complete native run takes 69.60 seconds and 16,783,203 streamed samples,
within its prospectively set 18-million-sample/120-second limits. Both native
frames are rejected. The consumer actually tries one 144-position decode with
eight located erasures and rejects it; neither path accepts content. The
`consumer_acceptance_check` passes. Nevertheless, **all 136 transmitted slots
produce wrong-key false DATA**, so the overall operational requirement remains
failed. That failure is not converted to success by the successful rejection.
Its execution receipt binds the amended probe binary and source separately from
the six earlier trials. This is one structured false-join control, not a
universal CRC, address discrimination or false-acceptance probability bound.

## Combined predictive completion and protected recovery

Separate codec successes do not qualify the new completion path. The
[active unprotected fade](hf-predictive-completion.md#required-active-interior-fade-witness)
introduced a wrong accepted `A` during transmitted `Ae`, so a fixed combined
experiment uses native RS20 `e` with the same clocks, continuing interferer,
public 69.6-second bit geometry and wanted-only outage at slots 13/14. All 64
coded positions and the final two physical quiet slots are observed. Input
remains open; source length and payload are evaluator information only.

The paired baseline and candidate use one immutable IQ capture, full `2*H`
workers and the existing native codec. The fixed limits are 12 million rows and
1,800 seconds per paired case, including source rendering, both services and
consumer work. The original attempt serialized successive public observation
boundaries to expose recovery and order actual native message events before
consumer cursor advancement. It exhausted the 1,800-second budget during the
baseline, before the candidate or consumer could be assessed.

The positive `--predictive-protected-fade` must activate on both interior and
ending quiet pairs, retain two native codec-rejected fragments, perform one
64-position combined decode with two located erasures, and deliver exact `e`
only at the final physical completion without incorrect accepted content.
Only success permits the matched `--predictive-protected-fade-wrong-key` run.
That negative must exercise the actual combined path and accept no content;
its false-DATA requirement remains independent. No parameter sweep is allowed.
The 64 coded positions versus three uncoded `e` positions imply 21.33 times
the nominal active airtime/incident energy at equal per-bit power; actual
emitted and post-fade energy are recorded. This is an integrity/recovery
experiment, not an equal-energy sensitivity comparison.

### Original timeout and bounded scheduling retry

The original `wanted` attempt took **1,800.060 seconds** by its execution
receipt. The baseline reached input sample **10,330,204** and next public slot
**59**, then was cancelled. The diagnostic calls that first policy
`active-fade native`; this is the ordinary baseline, not the predictive
candidate. Three setup assertions passed, but the execution assertion
failed. The candidate never executed, native event receipts were omitted by the
original exception path, and there is **no communications grade** for this
attempt. Its appraisal source is
`8d5e29d577b263069c1e5cdb135942c019b2e075ce7f34510d0b286dd3733462` and
probe binary is
`96a1b666bce71999003d9ac96e83bdcf7b757659902fe3c1dc2bbd6037dbc279`.

The separately identified retry restores overlapping native ready windows
without changing the 66-slot capture, worker allocation, sample cap or wall
budget. It retains every canonical slot receipt and drains actual native
message events. Its narrow recovery exclusion requires valid, gapless input
starting at sample zero, the complete fixed horizon, and the actual native
RS20 configuration: a 304-coded-position short limit, a 610-slot episode limit
and a 192-position long marker. Those bounds exceed this 66-slot fixture;
they do not justify recovery exclusion for general streams. The exclusion
also requires contiguous accepted input progression without a validity mask
and no native recovery or terminal state. Complete `all_valid` slot receipts
alone would not prove validity between observation windows. There is no
general recovery-generation report on which to base a broader claim.

Only after **both policies complete and their full receipts validate** may the
consumer run. Each policy must retain all 66 canonical slots with their actual
geometry and validity. Actual native event origins must begin at zero and each
subsequent origin must equal the preceding physical-completion stop. Any
episode-origin break withholds consumer execution, including a benign rearm;
this conservative restriction belongs to this fixed witness, not the general
adapter. Eligible actual events are then consumed before advancing through the
corresponding retained public slots. No synthetic completion is substituted.
The event-origin check alone does not prove that events cover the final
horizon; the wanted acceptance check separately requires the exact two actual
events and a recovered span ending at slot 66.

The revised receipt reports `reassembly_scope`,
`overlapping_ready_windows_enabled`, and per-policy
`protected_native_receipts_verified`, `contiguous_native_event_origins`,
`ready_windows_may_overlap` and `protected_consumer_started`. The 610-slot
bound is checked in source preflight; the scope receipt reports the short limit,
long marker and fixed horizon. A timeout now reports `status=time_limit`,
`failing_policy`, `baseline_completed`, `candidate_started`,
`candidate_completed=false` and `communications_assessed=false`, together with
actual partial receipts, the input digest, cursor, counts, timing and unchanged
caps. An unstarted candidate is null and the consumer remains unstarted.
These are separately source/binary-bound attempts, not a change in the original
timeout result. Only the completed retry below counts as a paired channel
observation; its scheduling change does not establish real-time capacity.

### Completed wanted observation

The retry completes in **849.620 seconds** by its execution receipt and passes
the wanted-case assertions. Both policies receive all 66 public observations
with input still open. Both preserve **62 correct DATA decisions**. The
baseline leaves slots 13, 14, 64 and 65 ambiguous and completes no message;
the candidate scores all four quiet. Each change actually attempts and accepts
`predictive_quiet_passed_both_bands`, with every reported frequency, amplitude
and validation interval ending no later than its target observation starts.

The candidate retains two actual `codec_rejected_message` events, covering
`[0,15)` and `[15,66)`. Their trailing-quiet coordinates are episode-local:
`[13,14]` and `[49,50]` respectively, with the latter mapping to public slots
64/65. The consumer makes **one native 64-position decode with two located
erasures** and recovers exact `e` only at the final completion at slot 66.
Neither policy accepts incorrect content. The candidate's first physical
completion remains premature; protected recovery does not erase that event
or repair the unprotected `Ae`→`A` counterexample.

The archived binary's ending-pair summary checked QUIET alone. Before the
conditional negative, a separate saved audit checked the stronger frozen
requirement directly from the raw receipts: baseline AMBIGUOUS becomes
candidate QUIET at both 64/65, attempted and accepted status is present, and
all training intervals are causal. It also verifies the paired IQ and DATA
evidence, complete 66-slot geometry, both actual rejections, the exact final
recovery and absence of incorrect content. Thus the observed wanted result
meets that criterion. The later probe revision below adds the same
ending-activation predicate without changing this archived positive receipt.

| Recorded cost | Wanted paired observation |
| --- | --- |
| Rendering | 52.259 seconds |
| Baseline / candidate service | 414.413 / 381.435 seconds |
| Total native wall time | 849.553 seconds |
| Workers / hardware threads, each policy | 24 / 12 |
| Reported peak native reservations, baseline / candidate | 5.181 / 5.215 GB |
| Shared input | 11,367,008 complex rows; 181,872,128 bytes |

These paired runs use the native C++ CPU path on an AMD Ryzen 5 PRO 5650U,
with 12 logical CPUs and 16,053,669,888 bytes of system RAM, built with GNU
14.2 in Release mode. They execute no GPU work. Their service measurements
are observations of this fixed workload, not a general receiver-capacity pass.

The input hash is
`c063b2476649006c2b046c91bd03b02e3b55f0e2faaf749be6d22496e5a3c1ad`.
The 64 coded versus three uncoded positions cost **21.333 times** nominal active
airtime and incident energy at equal power. Actual emitted and post-fade
wanted energy sums are 8,600,923.657 and 8,331,133.638 respectively; an uncoded
equal-energy trial was not measured. This receipt binds appraisal source
`fb25b823db85fb0ca93554d899992ed73ec7f90723c7da22f739f4e477160724`, native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`, and probe binary
`d0c25c33d7773d15dbb5a0603572bda7f3fb341969b02deab69c3c2d2a406731`.

The conditional wrong-key command then stops after **0.016 seconds**, before
rendering or receiver DSP, with `Confidentiality requires independently supplied
Serpent and AES roots`. The fixture supplied only one 32-byte root despite
enabling encryption. Three setup assertions pass, but no communications
assertion executes; this is **not a negative-control result**. The fixture
prerequisite must be corrected before an actual negative can be assessed.

### Corrected key preflight

The revised fixture supplies two explicit public 64-byte roots: ascending AES
bytes `a5..e4` and Serpent bytes `31..70`. The first 32 AES bytes, `a5..c4`,
preserve the original distinct waveform seed. Its bounded
`--predictive-protected-key-preflight` passes **all four assertions** in
**0.033 seconds** by the execution receipt, rendering 4,096 encrypted native
DATA rows. It checks the RS20 length, supported independent confidentiality
roots and unchanged distinct waveform seed, native encrypted rendering, and
the predeclared 15-second cap. **No receiver runs and no communications outcome
is assessed** by this preflight.

The same revision requires actual baseline AMBIGUOUS→candidate QUIET prediction
at both ending slots in the native acceptance predicate and reports
`candidate_ending_quiet_pair_predicted`. It requires `attempted=true`,
`accepted=true` and causal preceding training intervals. The
saved raw audit additionally checks the exact successful prediction-status
string; the native predicate does not compare that string. The default
completion CTest passes in 3.32 seconds;
that check and the independent review do not substitute for the full negative.

The preflight binds appraisal source
`9efbc7dbc3c276d9e0a5597c7beb313bbe5e2676b76317c559f71a5096e6c271` and probe binary
`c2a995e7fd07fbbe96c91106d0618c4c2acc39ef0171cfe4185a300255a74e3b`;
native source remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
The original positive source/binary and the failed setup attempt remain
separately recorded. The corrected actual wrong-key run uses the original
1,800-second cap in `local-evidence/hf-predictive-protected-fade-fixed-key/`.

### Completed wrong-key observation: rejection with failed requirements

The corrected negative completes both native policies in **957.481 seconds**
by its execution receipt. Both score all 66 public observations with input
still open, on the same hashed IQ. Both produce **61 false DATA decisions**.
The candidate actually changes slots 13, 14, 64 and 65 from baseline ambiguous
to quiet, with accepted predictions and causal preceding training intervals.
It retains two actual codec-rejected events, `[0,15)` and `[15,66)`, and the
bounded consumer makes **one 64-position native decode attempt**. That attempt
rejects the combined frame; neither native policy nor the consumer accepts
content. Consumer service takes 0.831 milliseconds.

The attempt has **three located bit erasures**, rather than the frozen two:
in addition to faded slots 13/14, slot 43 remains ambiguous in both policies
with reason `coherent_line_in_public_data_aperture`. The predictor did not
introduce that extra erasure. Positions 13, 14 and 43 occupy two RS byte
positions, within the configured two-parity-byte erasure capacity; this is an
actual decode rejection, not a refusal based solely on excessive erasures.
The receipt does not identify the internal codec step that rejects it, so it
does not establish a particular CRC outcome.

**Both failed requirements remain failed:** the exact two-bit-erasure pattern
was not obtained, and wrong-key DATA discrimination failed. Consequently the
original `branch_covered` and `consumer_acceptance_check` values remain false,
and the process intentionally exits 1. The separately saved observation audit
records that the real combined attempt executed and rejected; it does not
rewrite those assertions as passes. The combined route remains unqualified.

Rendering takes 52.852 seconds, baseline service 457.743 seconds and candidate
service 445.335 seconds; total native wall time is 957.399 seconds. Each policy
uses 24 workers on 12 hardware threads. Reported peak native reservations are
5.308 GB and 5.608 GB respectively. The shared capture again contains
11,367,008 complex rows and 181,872,128 bytes, with input hash
`67fad8e52e72c82803476cb1caab991a840e4f66d5df13494049adbcf3c535eb`.
The complete negative binds the preflight's appraisal source `9efbc7db…` and
probe binary `c2a995e7…` given above. It is one paired observation, not two
independent channel trials or a general false-acceptance bound.

After registering the cheap preflight as CTest
`high_fidelity_hf_protected_key_preflight`, both it and
`high_fidelity_hf_completion_ambiguity` pass in **2.91 seconds** total
(0.03 and 2.87 seconds respectively). Its appraisal source, after adding only
the CTest registration, is
`de6e069161f13adc6f9e5b7d5500c7613f39d980ee2de3d085d210cf30b0981b`.
The final-check binary is
`77f8fe9e9b35caedcd90fe73c1633d13436a8824da33d35fa6964df9aab2855e`;
these small checks do not relabel the separately bound negative or remove its
failed communications requirements.

## Reproduction and evidence lifecycle

Build the contained target `high_fidelity_protected_reassembly_probe`, then run
`.build/bin/high_fidelity_protected_reassembly_probe --case CASE` from this
directory. Component checks are `high_fidelity_hf_protected_reassembly` in CTest.
For the combined probe's cheap default and fixture prerequisite checks, run
`ctest --test-dir .build -R '^high_fidelity_hf_(completion_ambiguity|protected_key_preflight)$' --output-on-failure`
from this directory. These do not run the full paired communication modes.
Receipts and execution hashes belong in `local-evidence/hf-protected-reassembly/`.
Keep those small receipts while the recovery/false-join question is unresolved;
replace them with a later qualifying record and concise findings when superseded.
No full IQ recording or copied native source is retained.
The combined modes use `high_fidelity_completion_ambiguity_probe` instead;
the original timeout receipts remain in
`local-evidence/hf-predictive-protected-fade/`, and the separate scheduling retry
uses `local-evidence/hf-predictive-protected-fade-parallel/`. Retain both with
its saved wanted audit and key-preflight receipts. The corrected negative uses
`local-evidence/hf-predictive-protected-fade-fixed-key/`. Retain these bounded
records with the unprotected counterexample until this combined operating route
is qualified, rejected or superseded. Captured IQ is released on exit.
