# Opt-in comparison of admitted native evidence

The [matched GUI replays](../hf-coverage-expansion.md) identified two erroneous
affine admissions under HF Worst, Each 65%, and Margin 3 at 40 dB-Hz. The
same-IQ short-varying scorer preferred the transmitted bit in both provisional
failures. One message had another wrong bit beyond its inspected slot. The
paired 20 dB-Hz slot reached the short-varying branch through the native
fallback and scored correctly. These observations identify a branch-selection
failure and leave the complete-message result of a policy change open.

## Proposed native exception

The [patch](hf-native-admitted-evidence.patch), subsequently explicitly approved
by the user and applied, adds one internal
`receiver_detail::score_slot_comparing_admitted_evidence` entry point in
`src/triad_receiver_driver.hpp` and `src/triad_receiver.cpp`. An APP-only
`--hf-compare-admitted-models` selection can pass it to
`StreamReceiverFixture::make_with_executor` with the normal sequential and
overlap workspace functions. The canonical scorer still owns preparation,
scoring and classification. The fixture uses the unchanged stream scheduler,
full worker budget, configured calendar, ordered commits, codec, and two
scored quiet slots. Existing callers retain the fallback policy, including
the previously authorized experimental supplement. The new entry point has
no extension callback and cannot combine with that supplement.

With a built-in alternative planned, the comparison scores each admitted
ordinary model before publishing a slot. These are affine, short varying,
coherent gain, and drift according to their existing native planners. The
mixture keeps the same full-grid normalizations, equal model prior weights,
presence threshold and bit separation. A planned branch with no retained
evidence contributes zero at its original weight. Native plan exclusions
remain binding. A conditional presence crossing goes through the existing line
gate even if the first affine decision would have been DATA. If a line is
retained, all admitted residual likelihoods run before the same gate makes
its final decision. The ordinary and line-conditioned prior weights, line
normalization, and subsequent quiet protection remain unchanged.

Every stage remains sequential inside the slot on the same executor. The
existing workspace calculation already reserves the maximum ordinary or
residual stage, with the admitted lease held through slot completion; no new
simultaneous scratch or second acquisition is needed. Admission and
cancellation failures retain their native handling. Unsupported geometry,
including the Off case with no alternative plan, follows the unchanged path.
Neither computed scores, transmitted content, nor load decide which admitted
ordinary models are compared. Native proposal and model limits still apply.

## Alternatives and cost

Always evaluating only the short-varying branch is cheaper and directly tests
the two reproduced bit errors. It would still let a large incorrect score
bypass another admitted likelihood, including drift in these very fixtures.
Running the full admitted comparison first measures that competing-model
failure without choosing a new SNR or confidence trigger on the known misses.
The short-varying-only shadow remains useful as a causal diagnostic. A
future reduction in mandatory work needs a separate proof of an applicable
upper bound on the skipped alternatives or an independently validated
public-geometry restriction. An amplitude or gap threshold by itself does
not bound mismatched structured evidence.

The 40 dB-Hz standalone short branch took 0.152 s and 0.083 s at 24 workers on
the affected original and fresh slots. Earlier noise controls on this host
already ran drift at about 0.30–0.41 s per observed Helper-off slot and
4.20–5.37 s with Helper. These are measured component intervals from those
flights. Drift, available coherent gain, and retained-line residuals can
make mandatory comparison more costly than the short shadow. Unchanged
peak scratch quotes do not imply unchanged whole-stream service or backlog.
The proposal is opt-in so these effects can be observed before considering
any product change.

## Validation before adopting any policy

1. Apply only after approval of the two native files. Review the exact patch
   and run bounded checks of native Off/legacy parity, varied-model execution,
   admitted coherent gain, retained coherent line with and without separable
   keyed DATA, invalid capture validity, cancellation and low-memory refusal.
   The existing native line fixture in
   `tests/test_triad_native_receiver.cpp` provides the deterministic
   line-only and coexistence reference. Any new fixture code stays inside the
   appraisal. Run applicable native Robust and APP checks with both source
   identities recorded.
2. Before decoding candidate results, freeze the bounded receive manifests.
   Match current-build baseline and candidate capture hashes, source message,
   public geometry, sample validity, endpoint clocks and noise seeds. Reuse
   the original 40/20 pair, midpoint 30, fresh-noise 40 and fresh-path 40
   failures and controls. Run complete stream controls covering Off, Margin 1,
   Helper, noise and wrong key. Keep a separately seeded physical/noise
   realization with a different message and more active positions as a
   prospective holdout. Use the declared sample/time limits and retain
   censored, invalid and adverse results.
3. Measure exact accepted bytes, wrong bits and erasures, false DATA,
   premature endings, both scored quiet slots, per-branch work, peak admitted
   workspace and throughput on the same full stream. A historical wrong
   message is repaired only by correct complete bytes in the candidate
   receive run on the matched capture. A successful component or only one
   corrected bit establishes less. Keep the other known physical failures and
   scope limits in the appraisal record. The bounded tests do not qualify an
   untested population, a hardware ceiling, or automatic deployment.

## Patch identity

Base checkout `42798a98bdc0bae32387870ec4aa55179798974c`.
Base `src/triad_receiver.cpp` SHA-256
`1edf0106802bbb736dcbf81e6eea948ca2be0b9f83787f25d3a87a8a84d9ae3a`.
Base `src/triad_receiver_driver.hpp` SHA-256
`ce04e2f75a909b42bc58784fa14821a7d8c6785f84575c1b49b0d30cbfc5957a`.
`git apply --check` verifies applicability. Native execution requires the
approved applied source and APP wiring.

The exact approved patch SHA-256 is
`f19888d7683fcce3df628cb48cd3d49a6fe68605be7cdb82ad032cffeea1a0f5`.
The resulting native source identity is
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
See the [executed comparison](../hf-receiver-repair.md) for complete recoveries,
fresh failures, a wrong-key acceptance regression and measured cost. The
proposal's mechanism is implemented; its broader deployment is not qualified.
