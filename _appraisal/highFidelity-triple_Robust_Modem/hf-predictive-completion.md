# Predictive interference and physical completion

For the newer Wrong Key Admission control and configured-carrier keystreams,
read the [current applicability/reuse map](coverage.md#applicability-after-the-admission-and-carrier-key-changes).
The numerical observations here retain their original source and waveform;
GUI visibility, direct native admission and raw completion are distinct.
The new competing-bit rule is not either rejected guard described below.

This investigation targets the retained clock/interference completion failures.
The canonical receiver admits all thirteen wanted DATA bits in the bright
oblique, evolving and coupled two-hop examples, but continuing interference
leaves both ending observations ambiguous. It must still finish from two
scored physical quiet slots. Transport EOF, the transmitted payload and the
evaluator's message length cannot supply completion evidence.

## Rejected wrong-key filter

Before implementing a new DATA rejection rule, the fixed variance-inflation
proposal was checked against a successful disturbed-HF observation. The
then-current native receiver delivers `e`, with DATA `001` and two scored quiet
slots. Its full 506,441-sample IQ/validity digest and score statistics match
the archived input exactly despite the source identity change.

The proposed fixed 9/8 diffuse-null correction subtracts
`max(0, R/9 - d*log(9/8))` from native DATA log evidence, where `R` is energy
in the tested band and `d` is its dimension. On the three correct bits the
original scores are 42.5632, 88.4844 and 127.1555. The hypothetical penalties
are 171.7425, 179.9652 and 213.1228; all three decisions would be lost.
No alternate receiver was executed. The frozen baseline is
[`hf-null-guard-preflight.json`](campaigns/hf-null-guard-preflight.json), with
the source-bound calculation retained in
`local-evidence/hf-null-guard-preflight/fixed-rho-rejection.json`.

This is a substantive rejection, not a threshold-tuning problem. Increasing
signal power makes both the native evidence and the penalty grow quadratically
in voltage; a legitimate mismatched channel can have the smaller evidence
coefficient. The rule can therefore reject that signal increasingly strongly
as SNR rises. It is not promoted as a repair for wrong-key false DATA.

## Candidate and statistical limits

The candidate uses strictly earlier public inter-observation gaps to estimate
a continuing coherent interferer. Frequency selection, complex amplitude
training and prospective validation must use separated data. Current wanted
observations must not fit the interference amplitude. All canonical DATA
searches and thresholds run unchanged; only eligible ambiguous observations
can receive an additional physical-quiet test.

For an independently fixed frequency, let `M` earlier samples estimate the
complex amplitude and let the current observation have `N` samples. Subtracting
the independently predicted tone leaves noise covariance
`nu * (I + u*u^H/M)`, where the current tone vector has norm squared `N`.
Whitening scales its unit direction by `sqrt(M/(M+N))`; it preserves the
orthogonal directions. This retains a nonzero wanted component at exactly the
interferer's frequency, unlike current-observation line projection, which
would erase that component entirely. The loss in that direction is explicit
and must appear in the diagnostic.

The fixed-frequency covariance law is unconditional over amplitude-training
noise. It does not certify a selected or drifting frequency, contaminated
training, impulsive noise, or a mismatched covariance. The candidate also
requires a separate coherent-residual veto, prospective validation and both
canonical quiet-band checks. Missing or invalid public gaps and failed
stationarity checks preserve ambiguity. Passing these tests is not a general
false-completion probability bound.

The first implementation treated the selected frequency as exact. Review
rejected that inference before physical testing. The replacement splits each
of two earlier gaps into an independent frequency proposal and confirmation
interval. Gaussian complex-mean disks and an amplitude upper bound certify
the local main lobe before wrapped phase constraints define a continuous
frequency interval. The frozen exclusion budget is `1e-6` per prediction,
allocated over ten past-data events. This is a confidence-set coverage budget
under the stated stationary-line/white-noise model; it is **not** a false-end
or false-message rate.

Nine frequency nodes cover the interval. At each node, amplitude prediction
uses a third earlier gap and retains the aligned direction. The scorer requires
the worst coherent and two native band norms to pass, including an analytic
second-derivative bound between nodes. That bound includes both the current
projection coefficient and the independent amplitude's frequency dependence.
Ambiguous aliases or unsupported intervals refuse the correction. The intended
property is that, except for confidence-set exclusion, accepting quiet is no
more permissive than testing the true frequency with the fixed-frequency
predictor. This construction still requires numerical and complete-message
checks; the formula alone is not receiver qualification.

Finite training also imposes a real cost: for a wanted amplitude increment
parallel to the interferer, information scales as `M*N/(M+N)`. At fixed `M`
it saturates as the current aperture `N` grows. Canonical DATA processing has
no such new penalty, but this quiet-only candidate cannot establish the desired
general graceful integration trend.

The distinction follows the training assumptions in adaptive detection:
[Kelly (1986)](https://doi.org/10.1109/TAES.1986.310745), the mismatch analysis
of [Besson (2021)](https://arxiv.org/abs/2012.01182), and the treatment of
composite nulls in [Grünwald, de Heide and Koolen (2024)](https://academic.oup.com/jrsssb/article/86/5/1091/7623686).
The local reference findings in
`_ref/highFidelity-triple_Robust_Modem/Astra-6/50-noise-interference-and-frontends.md`
also caution against treating measured noise power as complete covariance or
front-end calibration. Public disturbed-event observations remain subject to
the calibration limits recorded in [the multiple-hop investigation](hf-multihop.md).
Confidence-set treatment of nuisance parameters has a classical precedent in
[Berger and Boos (1994)](https://doi.org/10.1080/01621459.1994.10476836);
that precedent does not validate this particular numerical implementation.

A further literature cross-check includes
[Wang et al., *Signal Processing* 239 (2026), 110247](https://doi.org/10.1016/j.sigpro.2025.110247).
Its publisher abstract and model preview allow uncertainty in a rank-one
interferer's known subspace, but require that subspace to be linearly independent
of the signal subspace and require noise-only training with matching covariance
structure. Those assumptions do not cover wanted energy aligned with this
continuing tone. This is why that work's CFAR result cannot justify deleting the
aligned component here. The full article was not accessible in this audit.
[Yin et al. (2025 preprint), Section II](https://arxiv.org/html/2503.02214v1)
likewise explicitly assumes independent zero-mean complex Gaussian training and
test disturbances sharing one covariance, with target-free secondary samples.
Its iterative Bayesian/ML detector is a credible alternative under that model;
it does not supply the missing structured-wrong-key or contaminated-training
guarantee. These restrictions reconcile the newer literature with the local
noise/frontend findings rather than treating an adaptive detector's name as a
general robustness claim.

## Prospective acceptance and bounded work

1. Native component checks compare predictive whitening with the rejected
   projection operation. Cover stationary interference, increasing aligned
   wanted energy, frequency/phase/amplitude changes, two tones, a current-only
   burst, invalid samples and contaminated training. A wanted direction must
   not vanish, and failed assumptions must not force quiet.
2. Render the fixed bright oblique endpoint capture once. Replay its identical
   sealed IQ and validity through the canonical receiver and the candidate,
   in process, with the same full worker budget. Require exact payload and
   two physically scored ending quiet slots, unchanged DATA evidence, and
   separate complete-service and memory records.
3. If the discriminator succeeds, test the matched endpoint-Off control,
   a fresh payload/realization and the evolving/two-hop endpoint failures.
   Preserve complete noise/wrong-key controls, an interior-fade/resumption
   check, and the existing near-boundary duration comparison.
4. Stop a candidate on a theoretical rejection floor, lost wanted direction,
   false completion, changed DATA decisions or unaccounted execution. A
   failed candidate is not a receiver repair. Do not expand expensive physical
   runs before the cheaper component discriminator is resolved.

No production defaults change. The spool is temporary and removed on close;
paired replays are one channel realization, not two independent trials.
Candidate training scans that sealed spool before receiver construction, with
all preparation and I/O charged to its complete-service result. Consequently
training does not add a numerical worker while the full native executor runs.
Every prediction still uses only gaps earlier than its own public observation.
This batch arrangement does not qualify online preparation latency.
Source-bound receipts and the rejected-filter calculation serve this current
repair investigation. Remove redundant outputs after the conclusions and
unresolved regression inputs have been preserved. RunPod is unnecessary for
the initial bounded native component and replay checks; no GPU result is
inferred from these CPU observations.

The initial component assertion used a 64.34567 Hz second tone outside its
expanded ±40.75 Hz DATA band. Its exclusion was expected behavior, not a
receiver failure. The exact failed log is retained; that tone is now an
out-of-band coexistence control. Separate in-band second-tone, burst and
contaminated-training cases verify the intended rejection behavior, with native
band visibility checked first. No detector threshold changed to repair that
fixture error.

## Native preservation observation

The first paired complete-receiver observation passes strict receipt validation.
Both policies deliver `e` with three correct DATA decisions followed by two
scored quiet slots, no false DATA and no premature completion. The complete
input digest is `fb241ca737d734f791a7f7870190b897fdbd97904d7f79da8f8ed0443e6392fb`,
identical to the successful disturbed-HF reference used to reject the variance
veto. Canonical and candidate service take 7.8793 and 8.1770 seconds; candidate
training contributes 0.00250 seconds. The candidate reports 1,277,177,889 bytes
of joint reserved capacity, with the unchanged 24-worker/12-logical-thread
allocation. These are single complete-service observations, not a capacity or
timing-regression qualification.

The frozen case is
[`hf-predictive-quiet-preservation.json`](campaigns/hf-predictive-quiet-preservation.json).
The raw paired receipt and complete runner report are in
`local-evidence/hf-predictive-quiet-preservation/`. Its appraisal source identity
is `e0fa6ebe42c416ab7508a47581ebc64a366100e97ae18eb7dc2229c246d608f9`;
the unchanged native identity is
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
This establishes preservation and paired execution, not a completion repair.

## First bright-oblique comparison: incomplete correction

The first identical-input bright-oblique comparison is a valid communications
failure for both policies. Every DATA decision is correct and unchanged. The
candidate changes ending slot 13 to quiet: its coherent upper envelope is
14.279 below 21.493, and its legacy-band envelope is 15,897.36 below 16,237.85.
Ending slot 14 remains ambiguous, so neither receiver accepts a message.

Slot 14 has valid earlier training, but the first candidate excluded every
observation in which the native scorer had already retained a coherent line.
The native post-line DATA evidence is −9.558, below threshold, and its raw quiet
energy remains excessive. The independent earlier-gap predictor never runs.
Review found that exclusion unnecessary: this predictor still uses raw IQ and
its own past-only uncertainty set, never the native current-slot line fit.
The follow-up removes only that eligibility exclusion and adds retained-line
positive and unexpected-second-line negative component controls. DATA, validity,
coherent-residual and both band gates remain intact. The original partial result
is not relabeled successful.

The original receipt is in `local-evidence/hf-predictive-quiet-discriminator/`,
bound to appraisal source `e0fa6ebe42c416ab7508a47581ebc64a366100e97ae18eb7dc2229c246d608f9`.
Canonical and candidate complete service take 143.2272 and 146.0796 seconds;
training takes 0.0561 seconds. The 311.0264-second campaign includes one shared
physical render and both services. A follow-up on this same frozen realization
is a repair comparison, not another independent environmental sample.

## Retained-line follow-up

The focused retained-line controls pass, and the repeated bright-oblique
observation now delivers the exact `A` through the candidate. The canonical
baseline still does not complete. Both policies recover the same thirteen DATA
bits; the candidate then scores quiet at slots 13 and 14, with no wrong DATA,
false DATA or premature completion. The coherent envelopes are 14.279 and
17.213 against the unchanged 21.493 threshold. Both native band checks pass.
The second slot's original retained-line log evidence is 6,370.822; that
current-slot fit is reported but never used to predict or remove interference.

The full input digest is
`8ac3d98c847b207ad16708b10718aecd2c34a14e027a7818fbaadd2b6e38e917`.
The receipt is in `local-evidence/hf-predictive-quiet-retained-line/`, bound to
appraisal source `a45799af0dbf74339d752587b7e456de8d24dfb8d89a043f849f6a7bd2479d3f`.
Baseline and candidate services take 166.134 and 133.293 seconds, with joint
reserved capacity 3,133,793,461 and 3,166,441,589 bytes. This single-run timing
difference is not evidence that the added operation speeds up the receiver.
The local host is an AMD Ryzen 5 PRO 5650U with Radeon Graphics and about
15 GiB system RAM; these trials execute the native CPU path with 24 software
workers and 12 effective logical threads. No GPU or capacity qualification is
claimed.

The frozen [control batch](campaigns/hf-predictive-quiet-controls.json) begins
with wrong-key reception and stops on increased false acceptance, premature
completion or loss of a previously delivered message. Its first wrong-key
observation retains three false DATA decisions in both policies and accepts no
message in either. One candidate ending slot remains ambiguous on that fresh
noise realization. This is a preserved wrong-key failure, not successful
wrong-key discrimination or evidence of generally reliable completion.

The complete four-case batch runs in 1,357.087 seconds with no execution or
paired-regression stop. Both policies preserve the matched-Off `A`; neither
accepts content in the full-noise or wrong-key controls. The wrong-key control
retains three false DATA decisions. The fresh `B` remains a complete-message
failure in both policies despite thirteen correct DATA bits. Its two candidate
coherent residual envelopes, 26.592 and 25.985, exceed the unchanged 21.493
threshold. The predictor therefore leaves both final slots ambiguous. The
full-noise control likewise leaves its second ending slot ambiguous. These
are observed limitations of the bounded predictor, not execution failures.

The four paired captures are four environmental/control observations, not eight
independent trials. The additional policy shares exactly the same rendered
samples. Their raw receipts and the strict runner report remain in
`local-evidence/hf-predictive-quiet-controls/`, bound to appraisal source
`a45799af0dbf74339d752587b7e456de8d24dfb8d89a043f849f6a7bd2479d3f`.
No detector threshold is relaxed after seeing these failures. The candidate
has demonstrated one focused completion repair, preservation of two successful
references, and incomplete generalization to fresh noise/payloads.

## Longer public baseline: fixed before its receiver trials

The fresh-`B` failure motivates a more precise frequency estimate without
relaxing any residual threshold. For public slot `i >= 5`, the next candidate
uses gaps `i-4` and `i-1` when both public gaps exist and their confirmation
centroids are at most 360 seconds apart. Otherwise public geometry selects the
original `i-2` and `i-1`. This choice is made before observing samples. If the
chosen long pair fails calibration or has unresolved phase aliases, it does
not fall back to a successful short pair.

The already computed adjacent pair supplies only a consistency veto: a unique
short tube wholly disjoint from the long tube refuses prediction. An unavailable
or ambiguous short tube neither replaces nor narrows the selected interval.
The selected pair retains its original ten-event `1e-6` exclusion budget;
the veto can only reduce acceptance. The same nine nodes, interpolation bounds,
finite-training whitening, held validation and native thresholds remain.
No extra frequency search is required because each public gap was already
calibrated once. Longer spacing improves phase resolution but increases exposure
to nonstationarity and wrapped-phase ambiguity; those risks remain explicit.

Native component checks pass for an activated longer baseline, aligned wanted
increments, old-gap phase/frequency changes, unresolved aliases, excessive public
separation, missing adjacent fits, and failed selected fits without fallback.
The strict receipt reader independently reconstructs every selected gap from
public observation and adjacent-signal exclusions. Its new rule marker keeps
earlier adjacent-only receipts distinct from the revised policy.

The fixed [discriminator](campaigns/hf-predictive-separated-discriminator.json)
runs wrong-key and the matched fresh `B` before conditional preservation of
the original `A`, full-noise control and matched-Off case.
The inexpensive disturbed-`e` paired preflight already passes with unchanged
DATA and complete delivery in both policies (16.706 seconds including both
services). It validates native/reader integration; the old capture is not a
fresh environmental observation.

The two separated-pair comparisons complete in 628.56 seconds. Neither accepts
content; wrong-key false DATA remains three, and the matched `B` still misses
completion. Both ending frequency fits refuse unresolved/inconsistent/wide
intervals. The longer baseline alone therefore fails its delivery objective;
its conditional preservation campaign is not executed and its superseded unused
manifest is removed. These receipts remain in
`local-evidence/hf-predictive-separated-discriminator/`.

The next fixed construction uses three existing gap witnesses: old `i-4`,
middle `i-2`, recent `i-1`. It retains all bounded feasible aliases for the long
and short pairs, intersects their interval unions, conservatively merges touching
intervals, and only then requires a unique interval within the unchanged width
cap. Fifteen primitive tail events each receive `1e-6/15`; shared use of the
recent witness does not require independence between the two derived constraints.
Early two-witness slots use that tighter event budget conservatively. A missing
middle witness or remaining aliases refuse correction; there is no fallback
chosen from successful fits. Raw gap searches and DATA thresholds are unchanged.

Its component checks include a deterministic long-pair alias resolved by the
short pair, a genuinely unresolved alias, actual public slot selection and
nonstationary/missing witnesses. Receiver results are kept separate from these
mathematical/software checks. The fixed joint
[discriminator](campaigns/hf-predictive-joint-discriminator.json),
[preservation/holdout](campaigns/hf-predictive-joint-preservation.json) and
[independent-field](campaigns/hf-predictive-joint-fields.json) stages are
conditional; later stages require earlier complete-message success and no
acceptance regression. Three-gap stationarity is a stronger nuisance assumption,
and finite-amplitude-training saturation remains.

The joint discriminator completes both comparisons in 625.54 seconds. On the
matched `B` capture, the ordinary receiver still misses completion; the candidate
delivers exact `B` with unchanged thirteen DATA decisions and two scored QUIET
slots. Each ending fit reduces three older-pair aliases to one joint interval.
The worst coherent residual bounds are 5.112 and 5.251 against 21.493; both
independent band-energy checks also pass. This is a repair of the previously
failed `B` example, not a new independent holdout after the revised construction.
The complete captured IQ/validity hash is
`4627954e8b16eeee4627ddced2f84625becc36f525e18df74fed99b471118af0`.

On the wrong-key comparison, both policies retain three false DATA decisions.
The candidate scores ending silence and produces one physical completion, but
the native decoder rejects its missing leading prefix; neither policy accepts
content. This is an acceptance-preservation result, not a false-DATA repair.

The `B` baseline/candidate service times are 122.888/125.865 seconds, including
spool verification and candidate training, with common rendering charged once
at 20.574 seconds. Their peak reserved totals are 3.136/3.176 GB; both preserve
24 workers. The candidate training scan takes 0.060 seconds and its two ending
tests total 0.126 seconds. These single paired services do not establish a
throughput regression bound or real-time capacity. Receipts are retained in
`local-evidence/hf-predictive-joint-discriminator/`, bound to appraisal source
`a9d1e09dc4295d99d9f6e5b3f027cf18acef07898a8abd93b09f43faa051f4de`
and executable SHA-256
`4d2b218d4d25e722ee7e0dc385d078d3359091592ba511b9fd89e7fb2021263d`.
The frozen preservation and prospective holdout stage completes all four cases
in 1,221.60 seconds, with no DATA or content-acceptance regression:

| Case | Ordinary receiver | Candidate | Service seconds, ordinary/candidate |
| --- | --- | --- | ---: |
| Original impaired `A` | Misses completion | Exact `A`, 13 DATA + 2 QUIET | 152.30 / 133.22 |
| Full noise control | No DATA or content | No DATA or content | 182.96 / 161.63 |
| Matched endpoint-Off `A` | Exact `A` | Exact `A` | 116.50 / 139.61 |
| Prospectively fresh `D`, noise seed 80541 | Misses completion | Exact `D`, 13 DATA + 2 QUIET | 143.14 / 117.33 |

The fresh `D` and its noise seed were specified before executing the joint
candidate; the other cases are preservation observations. Candidate peak
reserved totals range from 3.163 to 3.217 GB. Timing varies in both directions,
including a slower Off comparison, and is not a calibrated overhead estimate.
The unchanged 24-worker allocation and every complete service remain recorded
in `local-evidence/hf-predictive-joint-preservation/`, using the same executable
and source identity as the joint discriminator. The independent evolving-plasma
and coupled two-hop stage also completes, in 1,122.49 seconds:

| Independent constructed field | Ordinary receiver | Candidate | Service seconds, ordinary/candidate | Shared render seconds |
| --- | --- | --- | ---: | ---: |
| Evolving plasma with clocks/tone, `C` | 13 correct DATA, two ambiguous endings; no message | Exact `C`, unchanged DATA + 2 QUIET | 143.08 / 123.22 | 86.12 |
| Coupled two-hop with clocks/tone, `E` | 13 correct DATA, two ambiguous endings; no message | Exact `E`, unchanged DATA + 2 QUIET | 131.61 / 132.99 | 505.07 |

These repeat already frozen physical failures on the same inputs rather than
adding new physical parameter sampling. Candidate joint memory peaks are
3.200/3.274 GB, with 24 workers. Both physical numerical gates and every strict
paired receipt check pass. The ending coherent residual envelopes are
3.535/6.389 for `C` and 4.653/3.018 for `E`, against 21.493, and both quiet-band
checks pass. Whole-input hashes are
`3d47455821b2703b7ace6ec54868ea4810acd762e07789c12038885af125a1ac`
and `3b66de295263dae72e4ddafe2adeb23975b11087ebba09bdaffccaafd9d88e12`.
Receipts are in `local-evidence/hf-predictive-joint-fields/`, with the same
binary/source identity as the preceding stages. The campaign's canonical
top-level required-reception counters still fail; the candidate's success is
recorded separately and does not rewrite the baseline failures.

The original prospective duration commitment requires a new paired run despite
the source-level restriction to ambiguous-to-quiet changes. Historical baseline
receipts do not measure the candidate's service or resource effects. The fixed
[duration manifest](campaigns/hf-predictive-joint-duration.json) therefore reuses
the exact +6/64 and +3/128 two-hop boundary inputs, retaining their
characterization status and the known shorter miss. Its 1,500-second per-case
allowance covers two services and one shared render, with no additional physical
search or threshold change. Both duration comparisons complete in 1,129.60
seconds and preserve the original decisions and message outcomes. At +6/69.6 s,
both policies retain the one quiet erasure and reject the incomplete message.
At +3/139.2 s, both deliver exact `E` with thirteen correct DATA slots and two
scored quiet slots. This is preservation of an existing favorable duration
comparison, not a new candidate sensitivity gain.

Short baseline/candidate service is 21.284/22.619 seconds and long service is
43.103/44.364 seconds; shared renders take 494.571/503.211 seconds. Candidate
joint memory is 3.209/3.380 GB, retaining 24 workers. Input hashes are
`244bcc480583678063981214a662f02e77a1c6076f3bf68d3fea051eeddee57d`
and `e22067c26fcd89d50c1eb45c1f9250074b24a9e32a199a0807f91c3c4f38bd51`.
All receipts remain in `local-evidence/hf-predictive-joint-duration/`, using
the same binary/source identity as the other joint stages. The active
interior-fade witness remains a separate acceptance check.

## Rejected directional DATA veto

A second considered alternative removes the observation's radial magnitude and
compares complex angular Gaussian covariance models. For one fixed covariance
component, with whitened energy `R`, explained energy `J`, log determinant `L`
and complex dimension `d`, its log likelihood ratio against a uniform direction
is `-L - d log(1-J/R)`. This follows the complex angular Gaussian density in
[Ito, Araki and Nakatani (2016), Eq. 10](https://www.eurasip.org/Proceedings/Eusipco/Eusipco2016/papers/1570256519.pdf).
That acoustic source-separation paper supplies the density, not an HF null model
or a wrong-key false-alarm guarantee.

Scaling the observation while holding this covariance fixed leaves its direction
unchanged. That invariance does **not** establish graceful physical C/N0 behavior:
with fixed physical gain priors and decreasing noise variance, the covariance
itself changes. If a wanted signal retains a nonzero fraction outside the model,
`J/R` tends to a value below one while `L` grows without bound. The directional
score eventually tends to minus infinity. This is a theoretical rejection floor,
not just an insufficient empirical gain.

A deterministic native-kernel/dense-covariance countercheck passes. Holding the
same IQ and gain prior fixed, with 1% of energy in the modeled span, gives:

| Noise variance | Directional log ratio | Known-white Gaussian log ratio |
| --- | ---: | ---: |
| 1 | −2.7383 | −3.0350 |
| 0.01 | −7.2800 | −6.6019 |
| 0.0001 | −11.8845 | 87.7934 |
| 0.000001 | −16.4896 | 9,983.1883 |

These are fixed-IQ covariance evaluations, not four random channel observations.
The Gaussian score initially has an Occam-penalty dip, then increases in the
high-SNR regime; the directional score keeps falling. The proposal is rejected
as a general DATA veto. Its unused whole-aperture search implementation, tests
and temporary target were removed after this result; exact source hashes and the
small output remain in `local-evidence/hf-repair-preflight/directional-countercheck.*`
until the associated false-DATA investigation is resolved. The first temporary
build lacked a tools include path; the corrected build and actual countercheck
succeeded. No receiver behavior was changed by this experiment.

## Continuing-tone integrity discriminator: false DATA blocks the predictor

The existing native identical-IQ outage witness still passes in 3.43 seconds:
the legitimate space and the prefix of a continuing ` B` transmission are
indistinguishable through the shorter message's physical completion deadline.
Both are accepted as a space before EOF. This remains a framing limitation.

A new, separately frozen `--predictive-tone` mode adds a continuing 17 Hz,
0.3-voltage interferer after the wanted-signal outage, with noise variance
0.0001. The 8-second requested bit geometry, sample rate, sample cap and native
calendar remain fixed. This test was intended to exercise ambiguous-to-quiet
completion while preserving the identical received prefixes. Its expected
completion assertions **fail** in 8.03 seconds: both ordinary and candidate
receivers classify all five slots as DATA, `00010`, and accept no message.
The two physically absent wanted slots have native log evidence about
1,477,227 and 1,306,299 against 21.493. They are false DATA caused by the
in-band interferer, not missing waveform samples or a simulator setup error.

Earlier-gap tone training succeeds, but the predictor preserves the native DATA
decisions and never runs its ambiguous-to-quiet branch. The failed test therefore
establishes an additional interference limitation; it does not demonstrate either
a new incorrect-message acceptance or its prevention by the candidate. The
original intended witness was not obtained, and its assertions remain failed
rather than being retuned. The unchanged identical-IQ argument still prevents
treating improved quiet detection as a general uncoded integrity repair. Both
receipts, execution/binary hashes and failed assertions remain in
`local-evidence/hf-predictive-integrity/` while these receiver failures are open.

### Required active interior-fade witness

The 17 Hz failure does not exercise predictive quiet during an interior fade.
A separate frozen `--predictive-active-fade` mode therefore uses the successful
oblique `A` public geometry, actual clock errors, continuing 100 Hz RF-offset
tone and noise settings, with a simple fixed-delay path in place of the oblique
physical solver. Native `Ae` has the thirteen-bit `A` prefix and three further
bits. Only the wanted propagation path is muted across public slots 13 and 14;
the continuing interferer remains, and both complete receivers consume resumed
DATA slot 15 before input closure. One immutable, hashed IQ capture feeds both
policies. The fixed limits are 12 million samples, 600 seconds and the full
automatic worker allocation; no amplitude/frequency search is authorized.

This is an acceptance test for the changed completion branch, not a new HF
propagation model. New premature completion or wrong accepted content fails the
candidate. If the predictor does not convert both faded observations, or resumed
DATA is not correctly received, the intended branch remains uncovered. A valid
prefix followed by two scored quiet slots necessarily permits early native
decoding; the run must determine whether this candidate realizes that risk.
The original no-tone indistinguishability witness and failed 17 Hz witness
remain separate evidence and are not replaced by this construction.

The single frozen run completes in 269.30 seconds and **fails the candidate's
integrity acceptance criterion**. The branch is fully covered: both policies
recover the thirteen-bit `A` prefix and actual resumed DATA slot 15 correctly,
and both consume the same 2,726,973 samples while input remains open. The
ordinary receiver leaves slots 13/14 ambiguous and accepts no message. The
candidate converts both to QUIET and accepts `A` at public stop 15, although
the actual transmitted message is `Ae`. It introduces one premature completion
and one wrong accepted message. This is a new candidate regression relative to
the ordinary receiver, not merely a repeated pre-existing false acceptance.

Every setup, shared-input, public-geometry, full-worker, validity, causal-training
and resumed-DATA check passes. Only
`candidate_does_not_introduce_premature_or_wrong_content_acceptance` fails.
The nonzero probe exit and failed assertion remain intact; the fixture is not
retuned and its communications failure is not registered as a passing software
correctness check. The five repaired genuine endings therefore **do not qualify
this candidate for general use**. The protected-fragment experiments provide
separate evidence about integrity-bearing formats; they do not retroactively
prevent acceptance of this unprotected prefix.

The shared IQ hash is
`9f405d795839f7c33624cb69fc9e72fdcb26e5b19c8d822d0ac4cf4903455c05`.
Baseline/candidate service takes 139.096/117.446 seconds, with 12.311 seconds
of common rendering; those single measurements are not a speedup claim.
Native reserved peaks are 3.303/3.357 GB, with the 43.63 MB immutable capture
included in the charged process footprint. Both use 24 workers. Candidate
training takes 0.063 seconds before its receiver executor starts.
Raw output and execution receipt are retained in
`local-evidence/hf-predictive-active-fade/`, bound to appraisal source
`73cba1e799daa6c94cbaefde39b81c640bffa037b9cb99ff64047108db925309`
and executable SHA-256
`f418437f93a1d1ef70471ca35ac51ad899ad833654c294185dfff59628ae29d6`.
The unchanged native source hash remains the one recorded above. Retain this
minimal failure witness while the completion/integrity boundary is unresolved.
