# Frozen appraisal coverage matrix

This matrix records frozen cases and reviewed observations; its case status is
limited to the receipts identified in each row. Native renderer assertions,
receiver outcomes, idealized oracle calculations, and execution forecasts answer
different questions and are reported separately. No natural-condition
probability, field calibration, or universal sensitivity floor follows from
these deterministic cases or small fixed-seed holdouts.

## Applicability after the admission and carrier-key changes

Documentation/source audit: **16 November 2026**, repository
`f048f9fbf75b46a7a7754fb88e95ba305117e101`. The failure guides, candidate
source and frozen campaign definitions are versioned repository records.
Raw receipts linked under `local-evidence/` are ignored local artifacts, not
included in a fresh checkout. The guides preserve the substantive outcomes,
recipes, source identities and limits; consult a retained receipt when locally
available, and regenerate missing evidence under an explicitly identified
source when needed. Neither kind of record automatically describes this newer binary.
In particular, the latest predictive/protected results bind native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
This audit changes documentation only; it reruns no communications campaign.

### Wrong Key Admission: distinguish decoding from visibility

The authoritative [admission equations and evidence](../../docs/triad-stochastic.md#wrong-key-admission)
and [GUI contract](../../docs/gui-contract.md#modems-and-current-availability)
describe two different uses of the same scalar competing-bit rule:

- **GUI:** acquisition and decoding always use `current` (Unfiltered).
  The dropdown retrospectively filters retained Signals/Files records; a
  completed rated record remains visible when at least half its baseline DATA
  bits pass, with ties visible. Pending, Helper, non-Robust and unrated records
  remain visible. Changing the choice does not rerun DSP/FEC or change original
  decoded content; Unfiltered restores records still retained in memory.
- **Direct native/headless admission:** stronger policies run after ordinary
  scoring/fallback/quiet checks. Rejected DATA becomes AMBIGUOUS, not physical
  QUIET, without a new search or worker. This can change later message assembly
  and FEC outcomes, unlike the GUI visibility choice.

The tokens are `current`, `guarded`, `strict`, `very-strict`, `extreme` and
`maximum`. Their GUI reference fractions are respectively unfiltered, 2/3,
1/3, 1/10, 1/100 and 1/1000 **remaining wrong-key DATA relative to Unfiltered**;
the compact word “reduction” does not mean the fraction removed. The last two
are extrapolated model targets. The displayed approximately 1/2/3/6/6 dB costs
are scoped HF visibility estimates, not universal penalties, probabilities or
authentication. The rule uses the gap between the two operational bit scores
and the competing score's excess above the ordinary threshold. It is not the
old fixed variance penalty or the normalized-covariance diagnostic below.

Existing native admission studies and the later reversible-history study are
recorded separately in [maintained validation](../../docs/validation.md#reversible-wrong-key-admission-history--15-november-2026).
Their empirical successes and contrary cases must both inform a choice:
the recorded clean 96-minute Sub-9 case loses wanted admissions as C/N0 rises
under stronger policies; in the GUI this hides content reversibly. The deep
holdout tiers have zero observed foreign survivors but simultaneous upper
retention bounds of 12.12%/8.19%, not verified 1%/0.1% rates. These studies do
not establish the majority-bit browser rule's whole-message error rate or
reproduce this appraisal's detailed disturbed-HF failures.

The rate/sensitivity and 256-call holdout studies follow the carrier-key
revision. One explicitly archived exception is also useful: the new scalar
rule was applied offline to reproduced **old-waveform** protected-aperture
scores and rejected the wrong-key observation while preserving its wanted
pair. The [admission validation](../../docs/validation.md#wrong-key-admission--15-november-2026)
records the original-library/header recipe and its missing scratch-binary
hashes. This is a historical-score discriminator, not a current-waveform or
complete-message repair, and not hash-bound certification.

The appraisal `run.py`/`src/main.cpp` currently has **no
`--wrong-key-admission` option**; its receiver configurations use the native
default `current`. Do not pass the GUI option to an old campaign and assume
the requested tier ran. A future direct-policy appraisal needs an explicit
configuration/receipt extension. Its baseline must remain Unfiltered. The
predictive-quiet candidate also requires ambiguity *below* the ordinary
presence bound: native admission-rejected DATA retains positive presence
evidence and is not automatically eligible for that quiet correction.

### Configured carrier keys: regenerate waveform-dependent evidence

Commit `f674f9aeac3136917996a08b07f57496dfbe195d` introduced
`independent-apsk-v2-carrier-v1`. The [wire contract](../../docs/crypto.md#canonical-robust-waveform-purposes)
binds **one configured nominal physical carrier**, baseband plus Shift, rounded
with `Q = max(10 Hz, inner_rate_hz * added_symbols / 10)`. It appends the
binary64 value `Q * round(configured_hz / Q)` in the specified order; nonnegative
halfway values round upward. Same rounded value deliberately shares a context;
very close settings across a boundary can differ. Both peers need the new
derivation and matching rounded carrier; there is no old-key fallback.

This is not a new key for each lane index, hop, Doppler shift or CFO search
candidate. All three DATA lanes and their child derivations share the nominal
context. The receiver still generates templates from the fixed public format
and applies its ordinary CFO/clock transformations. A new cross-profile or
persistent cache must include the waveform revision and relevant full format,
including carrier context; existing source-owned caches already have that
immutable scope. Do not add a crypto search over every CFO bin or reuse an
old-domain template by frequency rotation.

The domain change alters **all old DATA realizations**, including plain,
Amateur and synthetic zero-origin fixtures. The three appraisal source callers
already supply physical carrier: direct scenarios use the declared RF center;
native endpoint-chain scenarios and the two standalone message probes include
RF Shift plus DATA IF. Their migration received syntax checks, **not appraisal
campaign reruns**, as recorded in [carrier-key validation](../../docs/validation.md#configured-carrier-waveform-keys--15-november-2026).
Helper metadata and scheduling are unchanged, but composite/Helper amplitudes
can depend on recalibrated finite DATA power; unchanged Helper metadata does
not establish identical complete IQ or energy.

That same validation preserves a relevant new-domain counterexample: the
strong 1737-Hz source scored with neighboring 1725-Hz configuration still
admits DATA under the baseline (log evidence 1792.68 against 21.4934). The
prespecified one-fifth-amplitude control separates matching and foreign
reception. Thus frequency separation can help without supplying reliable
strong-foreign rejection. Later admission-control studies are additional
source-bound evidence, not reruns of the older appraisal matrix.

### Reuse map for failures and contemplated implementations

| Preserved finding / implementation status | What to carry into the changed modem |
| --- | --- |
| **Wrong DATA/content and skipped evidence:** the native admitted-model comparison repairs selected GUI cases but accepts a wrong-key message and misses a fresh longer payload. [Repair investigation](hf-receiver-repair.md), [coverage expansion](hf-coverage-expansion.md). | A hidden GUI row does not repair its baseline DATA or decoded content. Preserve raw decisions, accepted wrong content, physical completion and browser visibility separately. Recreate the complete-message and wrong-key cases with current keys before recommending the comparison hook. |
| **Phase/history and time-energy cliffs:** independent trajectories, mode changes and nonwhite noise expose new misses; additional phase/gain/bridge candidates improve selected bits or messages without a general sensitivity result. [Ensemble](hf-ensemble.md), [inference/propagation](hf-inference-and-propagation.md), [correlated presence](hf-correlated-presence.md). | Rebuild native templates and waveform energy references, retain public prior weights and all search work, and compare complete messages at declared energy/duration points. Higher admission margins are an additional possible loss, not evidence that phase mismatch is resolved. Preserve private known-channel diagnostics as optimistic references, not deployable receivers. |
| **Predictive quiet: tested conditional candidate, rejected for general promotion.** Five ending failures improve; an active fade makes uncoded `Ae` become accepted `A`. [Methods and regressions](hf-predictive-completion.md). | Preserve separate causal frequency proposal/confirmation, amplitude training, validation, both quiet bands and coherent veto. Finite training gives aligned-signal information `MN/(M+N)`, saturating at fixed `M`. Recheck new-domain complete IQ, clocks/tone interactions and the active-fade negative together; neither GUI hiding nor stronger DATA admission makes this a generally safe completion rule. |
| **Protected reassembly: demonstrated bounded recovery, not qualified operation.** Actual rejected fragments recover `B`/`e`; the combined negative invokes decoding and rejects, but retains 61 false DATA and fails its exact two-erasure pattern. [Contract and receipts](hf-protected-reassembly.md). | Keep actual native events, public located erasures, recovery/session barriers, the 304-position bound and one existing codec call. Direct admission can add erasures and alter fragments: rerun whole messages, not just codec inputs. GUI-only filtering leaves native events intact; integrating reconstructed records would also need explicit compatible baseline rating metadata. Retain the reported nominal coding cost (21.333× short-`e` airtime/energy at equal power), not an equal-energy gain claim. |
| **Complete-outage ambiguity and fragment acceptance:** identical received histories can represent a genuine end or a continuing hidden transmission. [Framing witness and protected-codec checks](hf-fragment-integrity.md). | The identifiability argument survives when its same-context/shared-prefix assumptions hold. Regenerate the numerical witness under the new domain. A dropdown, extra search or frequency binding cannot distinguish identical observations. Protected coding can constrain later reconstruction, with its cost and residual false-acceptance limits explicit. |
| **Fixed variance inflation and physical-prior directional guards: rejected proposals.** A wanted signal can lose acceptance as SNR rises. [Variance counterexample](hf-predictive-completion.md#rejected-wrong-key-filter), [directional counterexample](hf-predictive-completion.md#rejected-directional-data-veto). | Keep the high-SNR coefficient/mismatch arguments and the original observed counterexamples. They are not the formula implemented by Wrong Key Admission, so do not transfer their exact scores or proof to that rule. The current relative-gap rule has its own measured long-window counterexamples in maintained validation. |
| **Normalized covariance and held-out prediction: diagnostic / conditional research.** A real wrong-key aperture separates from its wanted control, but no operational veto is qualified. [Normalized evidence](hf-normalized-null.md). | Preserve fixed covariance shapes, native projection/rank, full-lattice prior mass and same-model known-white positives. A structured wrong-key waveform need not satisfy the spherical null, and directional evidence can saturate. Reconsider held-out keyed prediction only with disjoint fit/score data and a defensible nuisance model; the new keystream does not provide one automatically. |
| **Simulator validity and measured-data limits:** failed interpolation/refinement checks, coupled physical models, and public recordings with incomplete calibration remain documented. [Validity findings](hf-validation.md), [multiple-hop and data audit](hf-multihop.md). | Key-independent solver checks can remain applicable when their source, geometry and tolerances are unchanged. Waveform-coupled FIR/support, antenna cancellation, received energy and communications checks need current inputs. New keys/admission controls supply none of the missing transmitted-waveform, timing, gain/noise or disturbed-event calibration. |

The implementation alternatives and observability limits in
[_ref native implementation options](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md)
and [noise/equipment guidance](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md)
remain part of this interpretation. They separate partial-fade inference from
impossible arbitrary-outage discrimination, and noise power from covariance
and frontend calibration. Existing research references in each investigation
retain their stated model assumptions; a new control label does not extend them.

### Evidence needed before updating an implementation recommendation

1. Record source/binary hashes, pattern revision, configured and rounded carrier,
   Rate/Added Symbols, key mode/direction, epoch/ordinal, policy and receiver
   path. Regenerate TX and receiver references together; preserve old receipts
   under their old identities. Do not relabel an old IQ digest or rescore old
   scalar logs as a new-waveform experiment.
2. Separate raw DATA/AMBIGUOUS/QUIET and completion/content from direct admission
   and GUI visibility. Retain both bit scores, ordinary threshold and minimum
   gap, required admission margin, baseline DATA count and per-tier passing
   counts/availability. Quiet and erasure counts are not the GUI denominator;
   missing evidence is not an inferred rejection or confidence value.
3. Follow the staged [experiment protocol](experiment-protocol.md): cheap
   generator/context and scalar-policy checks first, including same-bucket and
   cross-boundary settings and a foreign carrier inside the declared CFO
   search. Then use matched complete
   wanted/foreign/noise cases, the relevant active-fade/integrity regression,
   fresh trajectory/payload holdouts and the scoped duration/energy comparison.
   Freeze budgets and criteria before execution; do not search until a known
   failure disappears. A GUI-only history change needs its retention/tie/live
   update checks, not a fictitious new receiver campaign.
4. Preserve all public hypotheses, `W=2H`, memory bounds, recurring preparation,
   unsuccessful work and physical quiet completion. Source inspection of an
   unchanged geometry is not refreshed service or sensitivity calibration.
   Only new applicable evidence can mark a failure repaired or recommend a
   candidate for broader use; software checks alone do not do so.

## Source-bound investigation record

The pre-carrier-v1 [receiver-repair investigation](hf-predictive-completion.md) records
ten paired complete observations and a separate active-fade discriminator.
**The candidate fails the broader integrity criterion and cannot be promoted
as a general repair.** The active-fade test sends `Ae`, whose first thirteen
bits form the complete shorter `A`, removes wanted signal in slots 13–14 and
resumes correct DATA in slot 15. Both receivers still have open input. On the
same IQ, the baseline reports two ambiguous slots and no message; the candidate
reports two quiet slots and accepts one wrong, premature `A`. The new branch is
actually exercised, every admitted DATA bit remains correct, and the acceptance
assertion intentionally fails.

Within the ten paired observations, the revision estimating frequency from
three prior gaps repairs oblique `A`, `B` and prospectively selected fresh `D`,
evolving-field `C` and two-hop `E`. Each preserves thirteen correct DATA
decisions unchanged and replaces both ending ambiguous decisions with scored
quiet; all five matched baselines miss completion. Both policies deliver the
endpoint-Off `A`, and the complete noise control has no DATA or accepted
content. The duration checks preserve the +6 dB-Hz/69.6-second miss and
+3 dB-Hz/139.2-second delivery in both policies. The wrong-key replay retains
three false DATA decisions in both policies; the candidate's physical
completion is rejected for an unqualified leading message prefix, with no
accepted content.

Earlier revisions restored the original bright clock/interference `A` and
preserved two successful references but missed `B`; those failures remain
recorded. A separate strong in-band tone produces false DATA in physically
quiet wanted slots, preventing the predictor from running. A variance guard
and a directional guard are rejected for demonstrated/theoretical high-SNR
rejection floors. The five scoped completion repairs do not remove the newly
exposed integrity regression or establish general HF qualification.

The [protected-fragment extension](hf-protected-reassembly.md) adds seven standalone complete
native signal-chain observations. It recovers exact RS20 `B` and fresh `e` after
two-slot interior fades, preserving the original premature native completion
and two rejected fragments in each case. The bounded consumer inserts located
erasures and calls the existing codec once, without repeating acquisition.
Clean, full-noise and separate-message controls pass. The first wrong-key case
produces **72 false DATA decisions**, a rejected native frame and no recovered
content. The added wrong-key joining case exercises one combined decode, which
rejects the joined span; **136 false DATA decisions persist**, and neither
native nor consumer content is accepted. Both wrong-key requirements remain
failed. No production promotion, general false-join bound, equal-energy gain or
real-time capacity qualification follows.

The separate [combined predictive/protected observation](hf-protected-reassembly.md#completed-wanted-observation)
preserves 62 correct DATA decisions on matched IQ. All four baseline ambiguous
slots—interior 13/14 and ending 64/65—become candidate quiet through actual
accepted causal predictions. Two actual native codec rejections feed one
64-position, two-erasure decode, recovering exact `e` at slot 66 without
incorrect accepted content. One premature physical completion remains. The
849.620-second execution costs 21.333 times the nominal uncoded active airtime
and incident energy at equal power; it does not repair the unprotected
counterexample or establish equal-energy sensitivity. A raw-receipt audit
checks the stronger ending-activation requirement that the archived native
summary did not enforce. The original baseline timeout and first wrong-key
setup failure remain ungraded.

The corrected complete wrong-key observation actually exercises all four
causal quiet conversions, two native codec rejections and one 64-position
combined decode, which rejects without accepted content. **Both policies still
produce 61 false DATA decisions.** Native slot 43 remains ambiguous in both,
so the combined attempt has three bit erasures instead of the frozen two.
The exact two-erasure requirement and wrong-key discrimination both fail;
`branch_covered` and `consumer_acceptance_check` remain false. The observed
three-erasure rejection is retained separately from those failed criteria,
not reclassified as a pass or an unexecuted decoder. Combined-route
qualification remains failed. The cheap completion and new key-preflight
CTest checks pass; they do not qualify these communications requirements.

The [normalized-covariance diagnostic](hf-normalized-null.md) adds two
aperture diagnostics using an analytic channel. The wanted observation is correct
native DATA, while the wrong-key observation is already **AMBIGUOUS** in the ordinary
receiver. Their directional evidence comparison is not a false-DATA repair.
A separate matched static native-slot pair reproduces actual wrong-key false
DATA: the normalized scores separate it from its wanted control. This supplies
a concrete discriminator, but neither a qualified wrong-key null model nor an
operational veto. These component results do not establish complete-message behavior.

The preceding [coupled multiple-hop extension](hf-multihop.md) carries all retained
polarization/diffraction orders through two Es reflections and a lossy ground
bounce, alongside a competing transmitted-through-Es F return. Both frozen
physical fields pass mesh, order, frequency, moving-angle and actual FIR checks.
The primary includes sampled subsecond deep carrier cancellation and longer
band-energy fades; the independently fixed holdout does not reproduce that
deep-fade criterion and is not retuned. The initial under-resolved and
fixed-angle approximations remain explicitly rejected.

The extension has **sixteen unique strict complete observations**: eight correct
deliveries and four misses among twelve wanted cases, plus four negative controls.
The initial +20/69.6-second and +17/139.2-second pair delivers at nearly equal
incident E/N0; the weaker 0/−3 pair misses every active bit. A fixed prospective
follow-up finds twelve correct bits and one erasure at +6/69.6 seconds, but
complete delivery at +3/139.2 seconds with nearly equal incident E/N0 and about
1.3% less propagated E/N0. This positive duration comparison does not establish
a general sensitivity floor or natural-condition probability.

The impaired native endpoint case recovers all thirteen bits but fails to
finish; its matched Off case delivers. The strong wrong-key control produces
thirteen false DATA decisions, one rejected codec completion and no accepted
message. The other three negative controls stay quiet. Existing failures remain
failed requirements. A corrected endpoint setup refusal and an original metadata
recovery are retained separately; the repeated input is not an independent case.
The final finite qualification and exact-window diagnostics are detailed in the
[experiment record](hf-multihop.md).
This selected local planar two-hop/one-hop construction and public-data audit
materially extend the earlier gap; general curved-Earth hop populations and
calibrated broadband disturbed-event measurements remain unqualified.

The preceding [intrinsic-evolution extension](hf-vector-evolving.md) changes density
mode magnitudes and relative phases during each message in the coupled 3D
Maxwell calculation. Both fixed fields pass 270-solve numerical admission,
including direct checks at scanned temporal minima. Four prospectively declared
+20 dB-Hz complete-delivery requirements pass, and both complete negative
controls stay quiet. The +15 short and nearly equal-incident-energy +12 long
cases each miss twelve of thirteen bits; the longer case receives about 9% more
total signal energy without delivering. Exact decision-window energy references
keep those observations separate from an idealized known-channel benchmark.
One frozen +17.5 midpoint recovers eleven bits but misses two consecutive slots,
splitting the message into codec-rejected fragments. It narrows this fixed
realization's complete-message bracket to +17.5 failure / +20 success.

The native clock/interference combination at +30 dB-Hz fails its declared
delivery requirement: all thirteen DATA bits are correct, but both ending slots
are ambiguous. The matched endpoint-Off case completes. This extends the
combined-effects regression to an evolving field without claiming a new
deformation-specific failure or a receiver repair. The original receipt-reader
stop, its correction and the repeated validation observations are retained.
That preceding minute-scale Es extension alone did not cover rapid/deep
F-region or coupled multiple-hop behavior; the subsequent extension above
addresses selected cases. Neither provides calibrated disturbed-event measurements.

This increment has eleven unique complete observations: five delivered messages
and four delivery misses among nine wanted observations, plus two quiet negative
controls. Two additional original executions are retained as validation recovery;
they are repeats, not extra independent cases.
The expanded [43-cell qualification](hf-vector-evolving.md#finite-qualification-and-preservation-of-earlier-failures)
validates every receipt and preserves all original 32 gate outcomes. Five of
six declared delivery anchors succeed; the combined endpoint case fails. Older
binary identities remain historical, and the previous integrity and false-DATA
failures are retained. A legacy-held descriptor regression discovered by this
audit is repaired and recorded separately from communications outcomes.

The preceding [3D spatial extension](hf-vector-3d.md) retains both transverse
coordinates in the coupled Maxwell solve. The stronger fixed field requires
wider numerical support; its initially rejected approximations remain evidence.
A [machine-checked finite qualification](hf-vector-3d.md#executable-qualification-requirements)
now binds individual receipts and refuses to turn execution completion, stale
binaries or missing coverage into qualification. The
[native outage witness](hf-fragment-integrity.md#exact-native-input-ambiguity-under-an-outage)
additionally establishes identical-IQ premature completion before EOF.

The extension runs **eleven complete native observations**: five correct
deliveries and four misses among nine wanted observations, plus two negative
controls with no DATA or accepted message. The strong combined clock/interferer
case again recovers every active DATA bit but cannot finish; its matched Off
control delivers. The [32-cell audit](hf-vector-3d.md#executable-qualification-requirements)
keeps the older integrity and false-DATA failures as failed requirements and
does not invent operational delivery promises for sensitivity probes.
Its [energy-scale reference](hf-vector-3d.md#information-reference-at-the-correct-observation-scale)
also prevents pooling an entire known message from being misreported as a
demonstrated practical per-slot receiver gain. No receiver repair is claimed.

The preceding [coupled vector/oblique extension](hf-vector-oblique.md) materially
changes the earlier environmental gap: magnetic polarization, transverse
diffraction and oblique reflection now share one bidirectional Maxwell
operator. Five physical variants pass independent reductions, flux checks,
refinement, 54 assembled-table comparisons each, and actual stream tests.
An under-resolved initial pilot is rejected and retained. The primary contains
a sampled −25.22 dB antenna-power minimum within the frozen message.
This is a slow translating, two-dimensional sporadic-E construction, not a
general 3-D, multiple-hop or calibrated disturbed-day channel.

Its **21 complete native observations** include twenty strict receipts and one
explicitly metadata-audited receipt; the original campaign stop is preserved.
Eighteen wanted observations yield four correct complete messages, one
incorrect accepted `i`, and thirteen without delivery. Three negative controls
accept no message; the strong direct wrong-key control produces three false
DATA decisions. One wanted observation is an exact diagnostic replay, and
several others are matched brackets or ablations, so these are not independent
statistical success trials.

The new field reproduces the fragment mechanism as `B`→`i`, despite every
admitted DATA bit being correct. A distinct bright native clock/interference
case recovers all thirteen active bits correctly but marks both ending quiet
slots ambiguous, so **no message completes**. Its matched bright endpoint-off
control succeeds. The result establishes a combined-interaction failure;
it does not separately identify clocks or the continuing tone as the cause.
Direct primary erasures decrease 11→9→4→0 at incident C/N0 0/+10/+20/+30.
The equal-incident-energy doubled-duration case still fails, but sees less
selected-antenna E/N0, so it is not an equal-received-information comparison.
The matched private diagnostic distinguishes conditional bit information from
presence evidence and does not establish a receiver-only miss in its sampled
slot. These communications failures are unresolved.

The [new public-data audit](hf-research.md#transverse-diffraction-and-usable-measurement-audit)
inspects the McMurdo archive directory and representative pickle products,
plus a bounded Ottawa prefix from the Cameron 2026 deposit. The sampled data
are real sounding-parameter products, not established calibrated continuous
complex channel recordings. The full payloads were not exhaustively inspected.
That preceding software coverage is 36 groups, with the full original run, its
catalog-test correction, affected follow-up checks and automatic waveform
test separately recorded in the [build summary](README.md).

The [correlated-presence continuation](hf-correlated-presence.md) now repairs
the exact lens `B` failure at unchanged energy/duration and the matched
separated-screen `A` failure at +11 dB-Hz. It evaluates all public timing
atoms and retains normalized channel-law and CFO priors. Fresh payload and
ambient controls deliver, and both complete negative controls stay quiet.
The original separated-screen `B` still accepts a wrong space after interior
fades, so the candidate fails the general accepted-content gate. The +10
shadow remains below threshold; the +11 success is a fixed new bracket,
not a reclassification of that failure or a measured 9 dB sensitivity gain.

The duration follow-up admits the existing 139.2-second case without changing
laws, thresholds or public hypotheses. It preserves correct delivery at nearly
equal incident energy and records all fifteen noise slots as quiet. Together,
the current inference and integrity continuation adds fourteen complete
receiver observations and two standalone shadows: seven of nine wanted rows
deliver correctly, one accepts incorrect content, and one delivers nothing;
all five negative controls have zero DATA and accepted messages. These are
selected, sometimes matched observations, not a population success rate.
The software checks pass 34/34 groups, followed by the affected checks for the
larger duration envelope; no communication failure is reclassified as a pass.

The [integrity continuation](hf-fragment-integrity.md) reproduces that
fragment mechanism using the canonical codec and rejects all 7,282 proper
contiguous fragments of the protected RS20/RS60 `B` frames. These are synthetic
scored-slot component checks. Separately, the complete native RS20 lens trial
repairs a missing bit and delivers `B`, with both long negative controls
quiet. It uses 5.538 times the incident energy and 4.992 times the total
capture duration of the unprotected lens case; it is not an equal-energy
coding gain or a demonstrated repair of the separated-screen fragment case.

The [transverse lens](hf-wave-optics.md) and
[separated plasma regions](hf-phase-screens.md) now test focusing, diffraction
and deep destructive interference from prescribed density fields. Independent
quadrature, spatial and frequency refinement, finite-thickness comparisons and
off-grid null witnesses check the actual causal waveform before receiver
admission. The refined multiple-screen holdout contains a sampled fade of
about 45 dB; the finite-thickness comparison also demonstrates that physical
approximation error can exceed numerical error. These are scalar forward-wave
models on an ideal unfolded path, not calibrated oblique, vector, multiple-hop
or general evolving-Maxwell solutions.

These additions expose an accepted incorrect message with FEC off and without
any incorrect DATA-bit decision: the fresh multiple-screen field interrupts `B`, and the
receiver accepts a space from a surviving fragment after mistaking interior
fades for terminating silence. The primary field still loses an active slot
at +10 dB-Hz. The single-lens primary instead has a positive near-equal-energy
duration example, while its fresh off-center holdout loses a bit. Complete
payloads, physical completion, negative controls and conditional known-mean
diagnostics are therefore separate gates; aggregate correct-bit counts cannot
establish message integrity or graceful sensitivity.

The [native endpoint sandwich](hf-wave-optics.md#complete-receiver-and-endpoint-interaction-protocol)
combines the lens with actual TX/RX clocks, oscillators and a continuing RF
interferer. Its wanted cases succeed, but public uncertainty substantially
increases receiver work and its original noise control is time-censored.
The original censor and interrupted parent command remain recorded alongside
the fixed-budget continuations, which complete both noise and wrong-key
captures with all fifteen slots quiet and no accepted message. These are
service observations, not a local
CPU-capacity qualification. The
[recorded-failure diagnosis](hf-recorded-diagnosis.md) separately preserves
exact input identity and locates evidence losses on the archived Gannon miss;
private known-channel information is not an implemented receiver repair.

The preceding wave/scattering extension completed **29 receiver observations and three standalone
Gannon diagnostics**. The receiver observations include matched replications:
eleven wanted observations deliver correctly, twelve fail their complete
endpoint, and six negative controls remain quiet. The one incorrect-space
case is observed twice on exactly matched input, not on two independent
fields. One original native noise deadline and one interrupted attempt remain
separate. These heterogeneous, deliberately selected observations are not a
failure-rate estimate. Its 32-group software check passed without
reclassifying any communication failure.

The fixed additional brackets recover the primary multiple-screen message at
+20 dB-Hz after a +10 dB-Hz miss. The fresh field still fails at +10 dB-Hz,
with two active erasures and no accepted message; its earlier 0 dB-Hz
incorrect acceptance remains an unresolved defect. An exact gain-variance
witness closes a separate recorded-input diagnostic gap: its selected fixed
cell scores 5.203 after public coordinate weighting against a 21.493 presence
reference. This does not justify a repair and does not exclude untested cells.

The [magnetic/long-record expansion](hf-magnetoionic.md) directly addresses two
previously absent mechanisms. A vector cold-plasma operator couples reflection,
absorption, dispersion and fixed-antenna polarization loss; independent mesh
and profile checks expose and then resolve a deep-null interpolation defect.
Two exact hour-long storm-era voltage records add real temporal structure with
explicit recording-noise and bandwidth limitations. The tilted-field receiver
cohort completes two of six wanted messages; all five wanted parallel-field
messages and three of four recorded-voltage messages complete. The parallel
duration pair succeeds with essentially equal incident energy and 4.7% less
received energy at twice the actual bit duration. The three wrong-key controls
produce seven, thirteen and thirteen false DATA decisions respectively, but
no accepted messages. A missed recorded
slot has strong conditional known-waveform information; that private isolated
diagnostic does not establish a complete-message receiver repair.

The tilted first-slot reference instead has only 9.09% ideal isolated presence
detection at the declared false-alarm target. One fixed 30 dB-Hz follow-up
recovers the complete message with identical source/calendar/clean energies,
narrowing that configuration's bracket to 20 fails / 30 succeeds. This extension
totals 22 complete-receiver observations and four private diagnostics; numerical
probes and software checks are separate.

The [inference and propagation follow-up](hf-inference-and-propagation.md)
adds 32 completed, uncensored observations (15 complete receivers and 17
component/private/shadow diagnostics). It separates phase-reachability and
rate-support restrictions; its fixed bridge recovers individual bits but no
additional complete long message. Clean/noisy native comparisons retain GUI
misclassification without thermal noise. Independent sampled full-band and
finite-FIR checks admit a coupled cutoff/absorption waveform for modem tests.
The fixed one-ray receiver recovers `A` at incident C/N0 = 40 dB-Hz, fails at
30, 20 and 0 dB-Hz, and stays quiet in the matched noise control. This paired
10 dB complete-message bracket characterizes that configuration, not optimal
sensitivity, a natural failure rate or an evolving-Maxwell solution.

The [receiver follow-up](hf-receiver-repair.md) now tests complete-message
repairs on exactly matched captures, fresh payload/trajectory controls, and
scoped complete-service costs. Four archived GUI wrong messages recover under
the opt-in all-admitted comparison, but the fresh payload still fails and a
wrong-key sequence changes from codec rejection to an accepted false message.
A separate normalized phase filter recovers the physical-domain edge and a
fresh trajectory. After diagnosing more than 98.9% representable clean
coherence in a long weak miss, a fixed additional transition prior recovers
that complete message. A new long trajectory still fails, even though its
selected first-slot shadow passes. These are useful, finite causal results; they
do not qualify a general receiver policy or a natural-condition failure rate.

The fixed receiver also passes the previously failed +3 dB edge bracket, but
still fails its longer near-equal-energy bracket. The suite now explicitly
reports completed negative-control failures and accepted false messages,
separately from wanted-message failures and censored execution. A software
pass or a selected-slot improvement cannot conceal these message outcomes.

The preceding [physical/operating expansion](hf-coverage-expansion.md) adds a frozen
physical-parameter design, actual canonical GUI clocks and a static coupled
cutoff/absorption reference. It finds another failed energy/time boundary and
wrong accepted GUI content, including a fresh physical realization. Its scope
separates complete reception, shadow branches, numerical physics and admission;
passing one layer does not qualify the others. Same-IQ diagnostics find correct-bit
evidence in the skipped broader branch on the original failed GUI observation
and a fresh path's first wrong slot. Those component results alone do not
qualify a changed policy; the subsequent complete results and their
counterexamples are recorded separately above.

The earlier [independent HF ensemble](hf-ensemble.md#executed-evidence-and-findings)
adds multiscale/random-knot trajectories, independent placement and noise,
competing paths, fixed SNR/time brackets and known-binary diagnostics. All 31
rows executed uncensored; twelve exercise the complete receiver. It caught a
near-equal-energy long-TID failure, repeated premature completion through mode
changes, and false DATA in nonwhite-noise controls. It also preserved contrary
evidence: native reception recovered one bit which the standalone shadow
missed. This strengthens failure-finding coverage; it does not establish that
most natural HF failures or the entire GUI range are covered.

Campaign budgets are tier-specific: propagation, receiver-controls, and weak
holdouts each cap at 8,000,000 samples, 120 seconds per episode, and 1,800
seconds aggregate; EME caps at 1,000,000 samples, 120 seconds per episode, and
1,000 seconds aggregate; VLF caps at 70,000,000 samples, 1,200 seconds per
episode, and 1,900 seconds aggregate. Episode overrides may impose stricter
limits. These original campaigns used no automatic retries or outcome-dependent stopping. Limits
censor work; they do not count as observed communication failures. Paired cases
share seeds only where stated; matching seeds does not imply matched received
energy when channel or C/N0 changes.

The five initial and two follow-up campaigns executed all 53/53 declared
episodes without execution failures or censoring: `propagation-breadth` 16/16
in 226.042 s (7 valid incomplete communications), `receiver-controls` 12/12 in
145.310 s (2), `weak-holdouts` 6/6 in 34.551 s (5), `eme-episodes` 7/7 in
123.972 s (5), `vlf-reference` 7/7 in 750.383 s, `noise-ablation` 3/3 in 6.290
s, and `eme-discriminator` 2/2 in 28.470 s (1). The runner's complete-message
failure count does not include slot-only misses such as VLF phase wander.
These are finite descriptive observations, not pooled success rates or
reliability estimates. Their reports and source-bound receipts are under
[`local-evidence/`](local-evidence/).

The original HF expansion's 112 frozen native campaign episodes all completed
uncensored. The phase follow-up consists of 9 oracle diagnostics, 6 wanted full-message
holdouts (4 correct, 2 failed/rejected), and 1 noise-only receive case with no
DATA or physical completion. Helper campaigns send `e`; neither pair produced a
narrowed frequency proposal. The 2 s AWGN case passed coarse/onset support but
used final zero-prior fallback; the O/X cases found no coarse pilot. The equal-energy
case had 100 quiet decisions out of 184 active-bit decisions. See [HF
validation](hf-validation.md) for detailed outcomes, original failures,
receipts, and model limits. Results remain conditional descriptions, not
reliability estimates, and are not pooled with the original 53 episodes.
External competitor channel/capture (36) and replay (15) cases remain separate.

The later [public phase-search continuation](hf-public-search.md) adds public
CFO/affine/nonlinear likelihoods and an opt-in complete-receiver branch. The
[phase-refinement continuation](hf-phase-refinement.md) adds a physical-template
diagnostic and a bounded, fixed-prior phase proposal/refinement candidate. The
matched physical diagnostics show substantially more known-template evidence
than the current sinusoid on the retained failed slot. The initial blind recovery
failure exposed a coarse CFO sidelobe; after preserving an unsuccessful CFO-seed
repair, a bounded sub-bin segment-CFO breakpoint refinement passed the later
full 17-group suite. The two frozen shadows then completed: the off-grid case
recovered charged phase evidence, while the new-only branch missed the long weak
case despite its correct timing. The 15-episode full-receiver cohort completed in 560.536 s with 3 failures among 13 wanted episodes; the refined 512-request/−6 dB-Hz case recovered slot 0 but then ended prematurely after quiet slots 1 and 2. Fresh, compound-ray and CFO-edge cases passed, with no demonstrated incremental gain on baseline-passing fresh/compound cases. Follow-up same-input physical templates scored 133.257/115.251 on the two long weak slots versus winning public cells at 50.725/30.3869; a frozen staged-coherence shadow raised the two saved weak-slot combined true-bit log-evidence scores to 105.7873 and 88.65537 with coherence .9882/.9804 while preserving the short case exactly. The six-episode receiver cohort completed uncensored with 3/3 staged wanted messages passing, including the prior off-grid failure; the fresh long baseline failed while its staged same-IQ pair passed. Noise and wrong-key controls stayed quiet. The deterministic cap-edge fallback passed in the 17-group suite; it retains the old branch at fixed prior weight and excludes new-family evidence when planned work exceeds quota. Allocation, cancellation, runtime and memory-pressure failures do not trigger this fallback. Three final-source shadows match the previous staged source exactly in IQ hashes, evidence, timing, work and bounds; the complete-message cohort remains bound to its earlier source. This does not establish a speedup or population sensitivity. Keep failures and later outcomes source-bound in
[HF validation](hf-validation.md). These tests use configured-calendar,
Helper-off CPU reception with proper white noise; a two-ray actual channel is
included against a declared single-tap public model. They do not establish
smooth sensitivity, arbitrary multipath recovery, or general compute scaling.
None is pooled with the earlier 112 episodes.

| Source / mechanism | Observable | Receiver case IDs | Required outcome and evidence classification |
| --- | --- | --- | --- |
| Fixed phase lattice and four-second frequency bridges on independent physical trajectories. [Methods/results](hf-inference-and-propagation.md) | Private clean path certificates, phase versus rate support, matched public evidence, whole messages and negative controls. | `hf-phase-structure`, `hf-phase-bridge-shadow`, `hf-phase-bridge-receiver`, `hf-bridge-support`. | Long cases show phase-reachability loss; the fast boundary shows rate-support loss. The candidate gains a bit in each of two long cases but no new complete message; retained TID, fast edge and fresh `A` pass. Noise/wrong-key controls stay quiet. Private relaxed maxima are diagnostics, not normalized receiver evidence or attainable blind sensitivity. |
| Phase-factored scalar reflected-port response along a complete coupled D/F path, realized as a 129-tap native FIR. [Methods/results](hf-fullwave.md#phase-factored-causal-operator) | Independent frequency/profile witnesses, finite-FIR retardation bound, preserved physical loss, continuous history and tails. | `high_fidelity_fullwave_kernel_probe --phase-factored-profile-refinement`, `--kernel-table`. | Direct interpolation controls fail; phase factoring passes sampled full-band gates at 65 knots / 128 profile holdouts. The 120 s finite-FIR bound passes and the 1 s control fails. This does not qualify continuous-profile errors, evolving Maxwell physics, dynamic magnetic mode coupling or strong scattering. |
| The admitted coupled reflected waveform through the canonical receiver with a fixed one-ray prior. [Outcomes](hf-inference-and-propagation.md#complete-coupled-waveform-outcomes) | Full-span component energy, correct/quiet/wrong DATA, complete payloads, matched noise and charged preparation. | `hf-fullwave-coupled-receiver`, `hf-fullwave-bright-controls`, `hf-fullwave-boundary-midpoint`. | All six observations complete uncensored. At incident C/N0 0/20/30/40, correct DATA counts are 0/1/5/13; only 40 completes `A`. No wanted row accepts wrong content; noise is quiet. Arrival coverage spans all FIR lags but does not make the receiver's likelihood a matched dispersive model. The paired 10 dB completion bracket is not optimum sensitivity or a probability curve. |
| Frozen eight-parameter coupled eikonal domain plus placement; eight-point stratified design, static/max-rate anchors and a fresh parameter/placement/message holdout. [Protocol/results](hf-coverage-expansion.md) | Fixed whole-domain public envelope; screen/native parity, full message outcomes, nearby energy/time points and gain-rank cost. | `hf-domain-screen`, `hf-domain-receiver`, `hf-domain-edge-receiver`, `hf-domain-edge-diagnostic`, `hf-domain-edge-brackets`, `hf-coverage-long-miss`. | Interior screens and four wanted messages pass; the maximum-rate edge fails native reception and both fixed brackets. Full-timing score/IQ parity distinguishes the actual missed slot. Known-channel information and high short-block clean retention do not establish achievable blind reception. Only eight of 64 pair-bin cells per axis pair are sampled; no continuous-domain guarantee. |
| Canonical GUI preparation and native StreamingChannel: actual RF/audio errors, HF Worst / Each65, margin, Helper and arrival. [Protocol/results](hf-coverage-expansion.md) | Actual prepared 10.4 s bits/minute calendar, realized clocks versus public bounds, correct/wrong accepted bytes, quiet controls, source/energy identity and bounded work. | `hf-gui-admission`, `hf-gui-coverage`, `hf-gui-branch-holdouts`; separate wider-audio auroral canonical plan; `hf-gui-varying-pair` and conditional `hf-gui-varying-fresh`. | Margin3/40 dB-Hz accepts space instead of `e`, whereas its matched20 row passes. Midpoint/fresh-noise/fresh-path rows also accept wrong content. Off, Margin1, Helper and broad-arrival cases pass; noise controls remain quiet. Native affine admission skips the broader branch on wrong bits; same-IQ component scores are separate from any operating policy repair. One narrow-audio auroral plan is unavailable; a wider-audio plan admits but does not receive. No all-GUI or combined detailed-ionosphere/clock qualification. |
| Static scalar collisional Maxwell/Helmholtz column with vacuum ports; constructed D/F profiles across cutoff. [Methods/results](hf-fullwave.md) | Complex reflection/transmission, absorption, independent analytic limits, mesh convergence and qualified local phase slope. | `high_fidelity_fullwave_probe`, fourteen fixed profiles. | All14 complex responses converge;13 reflection delays reported, one reflection null withheld. Resonant feedback below precision is rejected. No dynamic channel, magnetic mode conversion, 3D scattering or receiver result is implied. |
| Sub-9 kHz, 200 ft-class T antenna and magnetic receiver reference planes; calculated lumped RLC envelope with independent detuning. [Model](models.md#sub-9-khz-antenna-envelope), [Fable 81](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/81-scenarios-sub9.md) | Antenna current response and separate magnetic input C/N0; physical horizon, active DATA decisions, quiet-slot completion, CPU/render cost and censor state. | Observed all seven VLF campaign cases: `vlf-rrc-phase-cost`, `vlf-tapered-phase-cost`, `vlf-tapered-bright-message`, `vlf-tapered-weak-static-slot`, `vlf-tapered-weak-wander-slot`, `vlf-tapered-detuned-render`, `vlf-rrc-bright-slot`; historical `vlf-reference`. | The VLF campaign executed 7/7 in 750.383 s with zero execution failures, under its 1,900 s aggregate cap. Two diagnose-mode pulse-cost episodes are mechanism observations, not communication trials: RRC used 2,304 direct basis columns versus 2,304 interpolated tapered columns and three native fills; fine-checkpoint wall sums were 0.556 s vs 0.01599 s. The +20 dB-Hz tapered full message decoded `e` with ordered slots `0,0,1,quiet,quiet` and completed correctly in 64.690 s after 64,806,069 samples/33,753.16 physical seconds. At −18.42995 dB-Hz, the matched static first-slot case decoded the correct DATA bit in 20.268 s; the two-hour phase-wander match had equal wanted energy but was classified quiet while wanted in 64.577 s, with supplemental fine models too slow for the short block. An ideal known-vector presence calculation gives miss 8.18e−12 at the declared false-alarm target; its exact waveform, timing, phase and amplitude knowledge is not the blind receiver's bit-detection contract. The detuned channel-only aperture render took 9.717 s and lost 0.09154 dB, bracketed by the declared adiabatic response. The +20 dB-Hz RRC first-slot case returned correct DATA bit 0 over all 12,966,069 samples, but took 567.458 s (558.212 s receiver control/wait; isolated receiver service was not measured and DSP could overlap rendering) and is a one-slot observation, not full-message completion or capacity qualification. A historical full VLF receive timed out before scoring; the tapered full-message receipt is specific to its calculated channel and does not calibrate field sensitivity. No LWPC/LMP or local field calibration. See [VLF validation and receipts](validation.md#frozen-full-duration-vlf-campaign). |
| 1 MHz groundwave, LFMF smooth-earth land/sea reference levels and a separate nighttime competing-ray hypothesis. [Model](models.md#groundwave-snapshots-and-competing-path), [Fable 82](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/82-scenarios-groundwave.md) | Preserve source/default C/N0 for land/sea comparison; record declared RF/noise planes, DATA/quiet decisions and full completion. Night ray strength, delay and estimator mismatch remain labeled synthetic. | Observed in `propagation-breadth`: `mf-land-reference`, `mf-sea-reference`, `mf-land-weak` (−16 dB-Hz), `mf-night-competition`; earlier `mf-groundwave`, `mf-outage`. | In the fixed episode, land and sea defaults both completed; weak land did not complete correctly, with three active slots classified quiet and no wrong bits; the constructed night competitor completed. Land/sea used **no C/N0 override**. See [campaign receipt summary](local-evidence/comprehensive-propagation-breadth/report.md). These are four finite observations, not geographic availability or sensitivity estimates. Night ray remains synthetic. |
| 30 MHz groundwave LFMF endpoint, sea and land. [Model](models.md#groundwave-snapshots-and-competing-path), [Fable 82](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/82-scenarios-groundwave.md) | Same source/default level and complete receiver outputs; weak-land conditional level; wall/work and completion/censor state. | Observed in `propagation-breadth`: `hf30-sea-reference`, `hf30-land-reference`, `hf30-land-weak`; historical `hf-groundwave`. | Sea and land defaults completed in the fixed episodes, while weak land at C/N0 0 did not complete correctly; three active slots were quiet with no wrong bits. Sea/land used **no C/N0 override**. One episode per condition cannot estimate sensitivity. |
| 1–30 MHz skywave: stationary quiet (0/.5 ms, 0.1 Hz) and disturbed (0/7 ms, 30 Hz) two-tap F.1487/Watterson comparators; separate equal-power disturbed NVIS delay and DRM6 multipath structures. [Model](models.md#hf-standard-and-nonstationary-controls), [research reconciliation](research-reconciliation.md#evidence-to-model-map), [Fable 83](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md) | Native covariance/model checks are separate from receiver reception. For each receive episode record complete correct/incomplete status, physical completion, bit/null/ambiguity counts, work and censored status at fixed C/N0. | Observed in `propagation-breadth`: `hf-quiet-reference/weak`, `hf-disturbed-reference/weak`, `hf-nvis-large-delay/weak`, `hf-drm6-reference/weak`; observed N=3 holdouts `hf-disturbed-weak-holdout-9601..9603`. Earlier historical `hf-skywave`. | All four fixed 20 dB-Hz reference episodes completed. All four fixed 0 dB-Hz weak episodes failed correct completion with three active slots quiet and no wrong bits. Holdouts produced 0/3 correct completions in disturbed HF at C/N0=0 and 1/3 in burst noise at C/N0=8. These outcomes preserve the observed holdout spread but are descriptive only, not probability or confidence estimates. NVIS and DRM6 preserve distinct delay structures; the fixed episodes do not establish a smooth sensitivity trend. See [campaign receipt summaries](local-evidence/comprehensive-propagation-breadth/report.md) and [weak holdout report](local-evidence/comprehensive-weak-holdouts/report.md). |
| HF absorption/recovery transition: disturbed two-ray episode with a defined 40 dB signal-only fade and return. [Model](models.md#hf-standard-and-nonstationary-controls), [Fable 83](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md) | Slot decisions across fade and recovery, premature physical completion, resumed DATA slots, final codec result and channel state continuity. | Observed `hf-transition-reacquisition` in `propagation-breadth` uses reference message `A` to span recovery; earlier `hf-outage` used a different fade control. | The slot receipt shows DATA resumes at slot 4 as the fade ends; slots 5–12 are wholly post-fade DATA, with one reacquisition. The full message still fails integrity with one missing slot, followed by two scored quiet slots and physical completion; no premature completion is flagged. It distinguishes post-fade recovery from successful message delivery and process failure. This is one constructed episode, not recovery probability or an ionospheric frequency-of-occurrence model. See [native slot receipt](local-evidence/comprehensive-propagation-breadth/015-hf-transition-reacquisition.stdout.json). |
| Adverse HF profile expansion: fitted Watterson components; O/X characteristic-mode projection and cancellation; coherent-trough phase; analytic vertical-ionosphere eikonal; mode absence/recovery; finite measured complex-envelope record; phase-history, FEC, and Helper controls. [Research reconciliation](hf-research.md), [experiment map](hf-experiments.md) | Preserve complex phase, component power, vector projection, finite support and continuous state. Record receiver DATA/quiet/error, public-aperture diagnostics and full-message outcomes; keep private known-channel oracle measurements separate. | The five initial campaigns (88 episodes), phase follow-up (16), native FEC (3), equal-energy (1), default Helper controls (2), and capture-lead Helper controls (2); 112 total. Fourteen preset IDs are listed in [hf-experiments](hf-experiments.md). | All 112 episodes completed uncensored. The phase follow-up has 9 oracle cases, 6 wanted-message holdouts (4 correct, 2 failed/rejected), and 1 noise-only case with no DATA or physical completion. Helper trials transmit `e`; neither pair produced a narrowed frequency proposal. The 2 s AWGN case passed coarse/onset support, then used zero-prior frequency fallback; O/X cases found no coarse pilot. Equal-energy results include 100 quiet decisions out of 184 active bits. Outcomes remain heterogeneous and descriptive; details, failures and limits are in [HF validation](hf-validation.md). Competitor results are separate. |
| Phase-mismatch diagnosis and bounded public refinement candidate. [Protocol and theory limits](hf-phase-refinement.md) | On identical selected IQ, compare a post-scoring native full-prefix physical template with the winning public phase cell; candidate acceptance requires fixed old/new/timing/CFO prior accounting, bounded work, matched input hashes and complete native quiet-slot outcome. | Two exact-hash physical diagnostic episodes completed. The first refinement CTest failed at a blind, noiseless off-grid recovery assertion after 89 checks; a bounded endpoint-breakpoint CFO repair then passed 107 focused assertions and the full 17-group suite. The final-metadata two-case shadow completed in 19.585 s; the 15-episode receiver cohort completed in 560.536 s with three failures among thirteen wanted episodes. | On the previously failed off-grid slot, the known physical template yielded raw log LR 150.650 and the winning sinusoid 35.170, with clean coherent-energy fraction 0.2054. On the earlier recovered motion control, corresponding LR values were 146.210 and 125.365, coherence² 0.8651. These are same-noise, known-template diagnostics, not admission bounds. The off-grid shadow raised new-family true-bit log evidence from 21.038 to 130.081, while the long weak shadow's combined evidence was 93.347 versus 94.040 old-family evidence and new-family-only evidence was 15.206. Follow-up physical-template LR remained high on the long weak failure (133.257 and 115.251); a bounded staged-coherence shadow then raised its combined true-bit log-evidence scores to 105.7873 and 88.65537 while preserving short-case behavior. These conditional shadow values do not establish message completion; the frozen six-episode receiver cohort completed with all three staged wanted messages passing, the matched fresh old-bank baseline failing, and both negative controls quiet. See [HF validation](hf-validation.md). |
| Earth–Moon–Earth at 5.76 GHz: finite frozen spherical disk/patch delay–Doppler approximation, with distinct weak/strong, outage, and severe-width hypotheses. [Model](models.md#eme-coupled-kernel-and-numerical-limits), [Fable 84](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/84-scenarios-eme.md) | Model metadata supports up to a 360 s episode at 1920 samples/s; native reference-message geometry determines actual receive horizon and full-message tracking, quiet completion, decode, ambiguity/misses, resource use and wall censoring. | Observed `eme-disk-strong-full-message`, `eme-disk-weak-full-message`, `eme-patch-strong-full-message`, `eme-patch-weak-full-message`, `eme-severe-strong-full-message`, `eme-severe-weak-full-message`, `eme-disk-noise-only`, `eme-disk-high-energy`, and `eme-disk-wrong-bit-oracle`; earlier `eme-disk-slot` bright/weak diagnostics. | The seven full-message cases executed in 123.972 s with zero execution failures; five were valid incomplete communications. At C/N0=30 dB-Hz, patch completed `e`, while disk and severe each decoded a wrong third DATA bit (` ` rather than `e`) yet reached physical completion with no integrity rejection. At 10 dB-Hz, disk had three ambiguous slots, patch three quiet slots, and severe two ambiguous plus one quiet slot; none physically completed. Noise-only had five quiet slots, no false DATA, and no physical completion. At C/N0=60 with the disk seed9501, the wrong third bit persisted and was still accepted as ` ` with physical completion. For the original seed9501 at C/N0=30, the selected bit-2 known-channel oracle had D²=17,867.858068, noise variance 1.92, and reported P(error) numerically underflowed to 0; this oracle knows the exact channel/waveforms and is not a blind-receiver bound or a receiver outcome. Separately, a 10 s reference EME smoke render directly verified 19,200 samples and positive received signal energy in 5.993 s; this selected check is not a rerun of the full integration-smoke campaign or a receiver test. These results expose strong-case integrity and weak-case detection gaps but do not estimate probabilities. Reference `e` geometry spans 269.47 s; the 1M-sample cap permits it without a receive-mode duration override. The frozen sphere is not a whole-disk calibrated IQ replay. See [full-message report](local-evidence/comprehensive-eme-episodes/report.md), [selected discriminator report](local-evidence/comprehensive-eme-discriminator/report.md), and native receipts. |
| Receiver-local compound colored/impulsive noise and continuous swept frontend blocker. [Model](models.md#noise-and-equipment-controls), [Fable 85](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/85-scenarios-noise-and-equipment.md) | Wanted/noise-only false DATA and physical completion, quiet decisions, blocker acquisition/continued quiet behavior, receiver state and cost. | Observed in `receiver-controls`: `bursts-wanted-reference`, `bursts-wanted-weak`, `bursts-noise-only`, `frontend-blocker-wanted`, `frontend-blocker-noise`; observed holdouts `noise-bursts-weak-holdout-9701..9703`. | For seed 9301, the C/N0=32 wanted burst case did not complete correctly and had two false DATA decisions, while the paired C/N0=8 case completed; the C/N0=32 noise-only control had five false DATA decisions but no physical completion. The three fresh C/N0=8 holdouts produced one completion and two non-completions. This fixed-seed nonmonotonic contrast and N=3 descriptive holdout do not establish a smooth sensitivity or reliability trend. Both frontend wanted/noise cases did not complete and had no false DATA. These digital controls do not qualify analog front-end hardware or storm statistics. See the [receiver-control report](local-evidence/comprehensive-receiver-controls/report.md) and [holdout report](local-evidence/comprehensive-weak-holdouts/report.md). |
| Helper acquisition and public-hypothesis integrity; wrong key and ordinal. [Protocol](experiment-protocol.md), [Fable 83](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/83-scenarios-skywave.md) | Helper candidates/acquisition, false acquisition, selected public ordinal, DATA decisions and complete-message outcome. | Observed in `receiver-controls`: `helper-reference`, `helper-noise-only`, `helper-wrong-key`, `reference-wrong-ordinal`; historical `integration-smoke` Helper and noise controls. | Wanted Helper completed; noise-only Helper had no false DATA or physical completion. With a wrong key, the receiver made two false DATA decisions and one physical completion, but integrity rejected the message and no accepted message resulted. The wrong-ordinal control produced no physical completion. Record public receiver behavior separately from evaluator truth. See the [receiver-control report](local-evidence/comprehensive-receiver-controls/report.md). |
| AWGN/compound-noise false-DATA ablation, selected after the seed-9301 noise-only control produced false DATA. | Noise-only receiver output under white-only, white+AR(1), and white+shot process controls; false DATA and physical completion. | Observed `noise-white-baseline`, `noise-white-ar1`, and `noise-white-shot` in `campaigns/noise-ablation.json`. | This matched three-case diagnosis preserves seed9301, reference waveform, C/N0=32, and geometry while removing one component at a time. White-only produced five quiet decisions; white+AR(1) produced five ambiguous decisions; white+shot produced five false DATA decisions. None physically completed. This is a selected-mechanism counterexample, not independent confirmation or a false-alarm-rate estimate. See the [ablation report](local-evidence/comprehensive-noise-ablation/report.md). |
| Earth–Moon–Earth disk high-energy and selected-bit discriminators, selected after the seed-9501 C/N0=30 disk message accepted a wrong third bit. | Receiver full-message outcome with 30 dB less white noise; separately, known-waveform/channel bit-2 oracle comparison at the original C/N0. | Observed `eme-disk-high-energy` and `eme-disk-wrong-bit-oracle` in `campaigns/eme-discriminator.json`. | Executed in 28.470 s with no execution failures. The C/N0=60 receiver case reuses disk seed9501 and still accepted the wrong third bit with physical completion. The oracle case uses the original C/N0=30 seed9501, `oracle_slot=2`, D²=17,867.858068 and reported P(error) numerically underflowed to 0; it is a selected known-channel comparison, not an optimal unknown-channel bound, receiver outcome, or independent success-probability sample. See the [discriminator report](local-evidence/comprehensive-eme-discriminator/report.md). |
| AWGN physical fade, marked capture gap, and transport EOF. [Physical completion contract](../../docs/robust-manual-capacity.md#physical-completion-and-security), [historical pilot](validation.md#reference-weak-signal-pilot) | Paired message-A seed, active/quiet decisions before and after disturbance, resumed DATA, physical completion count, and EOF status. | Observed in `receiver-controls`: `fade-interior-resume`, `marked-gap-interior-resume` (same seed), `transport-eof-no-completion`; historical `awgn-declared-gap`, `mf-outage`, `hf-outage`. | The paired physical fade generated two physical completions, one premature, then five resumed DATA slots; capture gap generated no physical completion and five resumed DATA slots. EOF produced no physical completion. A real fade can resemble message end and cause premature completion, so report physical scored absence separately from marked capture invalidity and EOF. See the [receiver-control report](local-evidence/comprehensive-receiver-controls/report.md). |

## Reporting boundary

For every executed row, retain the campaign and episode identity, source and
binary hashes, compiler/backend/arithmetic/device identity, scenario and model
version, seed, public receiver settings, declared reference plane and C/N0,
planned sample/time horizon, actual samples and physical duration, channel
power/fade/tail diagnostics, receiver slot/bit/null/ambiguity/false-DATA and
physical-completion outputs, per-stage work and memory, wall time, and exit or
censor reason. Public receiver output is not the private evaluator truth or an
idealized oracle. Keep those as separate fields and never infer equal inputs
from equal decisions.

Use the native compiled appraisal and canonical modem only. Frozen mechanism
checks establish that a declared synthetic model was rendered; they do not
validate a local path or rare-event rate. Receiver checks establish the outcome
of their declared episodes, not a smooth sensitivity curve. Explain deviations
from ideal time/energy trends with source geometry, identifiability, detector
thresholds, finite coding, state loss and computational censoring kept distinct.
