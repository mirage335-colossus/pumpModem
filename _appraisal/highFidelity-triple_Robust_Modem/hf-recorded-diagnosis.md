# Diagnosis of the recorded Gannon slot miss

The [initial observations](hf-magnetoionic.md#long-observed-voltage-stress-cases)
found that slot 2 of the 20:00 recorded-voltage replay favored the correct
bit by 15.0065 nats but scored quiet. Native presence was 9.7155 against a
21.4934 threshold. The later slot 3 passed presence and the codec rejected
the message with one erasure. The earlier isolated-bit measurement gives
the known recorded-multiplier binary distance squared divided by added
noise variance as 482.67. Short independent gain fits had a substantial
likelihood penalty. Those measurements used an isolated emission.

## Frozen matched-input checks

The [campaign](campaigns/hf-gannon-recorded-failure.json) runs two rows once
using the single local execution lane, automatic CPU resources and a
reference RRC waveform. The limits are 90 seconds and 3,000,000 capture
samples per row, with 180 seconds aggregate. Both rows fix the 20:00
placement, payload `A`, 20 dB-Hz incident C/N0, waveform/path seed 67001,
added-noise seed 77001 and requested 64-second bit duration. The physical
DATA duration is 69.6 seconds.

1. Repeat the ordinary complete receiver with the existing optional digest
   of slot 2. Require the archived 2,553,902-sample IQ and validity SHA-256
   `75e061b172a99556a30f599b17dd44bbfb3adc4af29169b424065bc87d30fed1`
   and the native miss before attributing a result to that observation.
2. Render the same full native source from sample zero through the stop of
   slot 2. Search the full public timing lattice with the existing fixed
   phase families and requested refinement. Require the same selected IQ
   and validity digest and full aperture `[345584,480302)`. After the
   public search, compare evaluator-only full-message templates that differ
   in the selected wire bit. Record the normalized binary distance, realized
   signed likelihood ratio, selected public cell's clean projection and
   raw likelihood, and the prior-weighted evidence of the evaluated families.
   Report branch availability and work limits as observed.

The fixed phase filter and bridge require a declared ±1 Hz geometry. The
recorded replay uses the original ±5 Hz model allowance. The measured
voltage still multiplies the waveform, including the recording instrument's
noise. The private comparisons condition on that deterministic replay
multiplier and independent synthetic added noise. Their binary alternative
holds the other bits fixed. A full-template energy measurement alone has no
matched absent-target template and therefore does not establish presence
sensitivity. A low clean projection at a noisy-selected public cell measures
that cell's mismatch. Excluding an entire family would require more evidence.

## Matched results

Both rows passed strict receipt validation in 27.04 seconds of runner wall
time on the local 12-logical-CPU system with 24 configured workers. The
ordinary receiver rendered the archived full IQ and validity hash. It
repeated the slot 2 quiet score and correct-bit preference exactly,
completed physical framing and rejected the payload after one active-slot
erasure. Coherent-gain, short varying and drift scoring had all run at
the failed slot.

The shadow rendered 480,302 prefix samples. Its selected 134,718-row
aperture and validity mask had digest
`79cb27e6704813b995c9e95cec7231152b880481410ab4bd7070b60e70b11dd2`,
identical to the digest from the ordinary receiver. The private
full-context templates give `D²/v = 482.67377`, numerically equal to
the archived isolated-bit distance at this aperture. The observed
correct-versus-wrong known-mean log likelihood ratio is 476.65574.
The known wanted template energy divided by added noise variance is
243.05573. These quantities condition on the recorded multiplier and
hold the other wire bits fixed.

| Public shadow family | Presence log evidence | Best correct-bit single-cell log LR | Clean energy retained at that cell |
| --- | ---: | ---: | ---: |
| Constant frequency | 7.441 | 20.131 | — |
| Fixed 97 sinusoidal models | 4.155 | 20.789 | 13.03% |
| Retained refinement cells | −2.141 | 21.946 | 13.34% |
| Equal fixed/refined mixture | 3.464 | — | — |

The families above all favor bit 1. The shadow evaluated 52 public
timing atoms and 4,951 CFO cells over the configured ±5 Hz range, with
5,045,637,120 logical FFT row-stages including refinement. Requesting
refinement uses the operational block geometry and leaves the exhaustive
affine shadow comparison unmeasured. The canonical drift branch ran at
the failed slot. The full shadow used 3.75 seconds from source setup
through private template comparison, with 2.14 seconds in the public search.

The winning fixed public cell retains 61.51% of the clean energy when
each 0.25-second block gets an independent complex gain. Its wanted-bit
log likelihood ratio becomes −509.92 with the fixed unit gain variance and
279 nonzero block ranks. With the unpartitioned gain, the two bitwise
maxima bound even an optimistic pre-prior presence at 19.437, 20.096 and
21.252 for constant, fixed sinusoidal and evaluated refinement cells
respectively. This bound uses the existing gain prior and the available
cell set. A continuous family or a different segmentation could have
additional support.

A scalar prior-variance change on these same unpartitioned cells cannot
reach 21.493. With `a = E/v` and normalized squared correlation `q`,
the cell log likelihood ratio at variance `τ` is
`-log(1+τa)+qτa/(1+τa)`. For `τ ≤ 1`, it can improve on the
original value by at most `log(1+a)`, at most 8.854 nats over all
the reported template energies. The same additive bound holds after
summing the fixed prior masses and averaging the bits. It gives optimistic
presence bounds of 16.295, 13.009 and 6.713 respectively. For
`τ ≥ 1`, the score decreases because the observed `q` is less than
`1+a` for every scored cell, as bounded by each timing atom's reported
maximum and the common per-atom template norms. This eliminates a
gain-variance-only repair on the evaluated banks. The temporal gain
model, discarded refinement cells and native branch decomposition remain
separate questions.

Recomputing normalized correlations from all 214 reported timing/bit
maxima confirmed the derivative condition and the presence bounds.
Direct evaluations at eight lower and higher prior variances checked the
inequality against the serialized maxima.

Contained configure, targeted build and all three focused checks passed
(Gannon replay, public receiver, strict runner with 114 cases).
The campaign binary SHA-256 is
`39d308f07f851cf3942618c84ae7b13d0e2af9e40134eafba378c77afbadec1d`.
The native source identity remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
The [frozen input](local-evidence/hf-gannon-recorded-failure/frozen-input.json)
and [run report](local-evidence/hf-gannon-recorded-failure/report.md)
bind both completed rows to that invocation.

## Frozen native coherent-gain comparison

The [one-row follow-up](campaigns/hf-gannon-native-comparison.json) keeps
the exact selected-aperture digest above, with a 45-second wall cap and
500,000 capture samples. It adds one private physically propagated
counterfactual with only the selected DATA support muted. The full native
message source still generates every sample and preserves its other
DATA intervals. Compare conditional wanted and absent means on the
actual noisy aperture. Keep their separation and observed log likelihood
ratio distinct from the binary comparison.

Use the native coarse proposals from the actual noisy slot and the
existing native coherent-gain routine, with one coordinate bank and
noise variance for both actual IQ and the private wanted mean. Report the
outer coordinate correction separately from the internal branch score.
The branch mixes independent and persistent gains over fixed powers and
constant frequency sectors. Its reported score does not resolve the
independent and persistent contributions. The private mean uses the
observed replay multiplier with synthetic additive noise disabled.
For this Helper-off tracked branch the correlations consume raw IQ.

The native .5-second block uses a fallback diffuse width of .35 Hz and
one-step gain correlation 0.85974. For the 702 observed-voltage knots
strictly inside each aperture, the magnitude of the normalized .5-second
lag correlation is 0.21939 in slot 2 and 0.10638 in the successful slot 3.
A constant carrier rotation cannot alter this magnitude. This comparison
describes the recorded multiplier, including receiver noise and mixed
transmitters. It cannot identify the physical gain covariance or isolate
the cause of the difference in native decisions. Without known phase and
with finite coherence, the information available to a blind detector can
be much weaker than this private known-mean comparison.

## Native coherent-gain outcome

The one frozen row completed in 4.865 seconds and passed strict
receipt validation. Its prefix digest matches the earlier full-timing
shadow and its selected digest matches both earlier rows. The two private
bit templates and their likelihoods reproduce exactly. The new private
absent template mutes source samples `[345600,479749)` before preroll,
reads the channel continuously through sample 480,302 and exactly
preserves all received samples before the aperture. Its earlier
received signal-energy sum is 151,572.445. The absent mean is exactly
zero on the selected aperture. The known conditional presence distance
is `D²/v = 243.05573`, with an observed wanted-versus-absent log
likelihood ratio of 234.49169. This separates the recorded stress's
known presence information from its binary template separation.

The native coherent-gain branch uses seven noisy-selected coordinates,
45 frequency states and 140 blocks with nominal duration 0.5 seconds.
Both branch calls use the same score variance and 1,616,616 native
basis rows. The native coordinate log-mass correction is −9.94164.
The table gives each presence score as the log average of its two
bitwise evidence values.

| Input and score stage | Bit 0 log evidence | Bit 1 log evidence | Presence log evidence |
| --- | ---: | ---: | ---: |
| Actual noisy IQ, before coordinate correction | 7.411 | 19.598 | 18.904 |
| Actual noisy IQ, after coordinate correction | −2.531 | 9.656 | 8.963 |
| Private wanted mean, before coordinate correction | 4.755 | 9.785 | 9.098 |
| Private wanted mean, after coordinate correction | −5.187 | −0.157 | −0.844 |

The native branch still falls short of the 21.493 presence reference
before coordinate correction and the outer mixtures. The public
coordinate cost compounds the loss. Removing synthetic added noise
weakens this branch on the selected bank at the same likelihood variance.
This is an observed realization effect in a fixed-bank comparison.
The private mean does not establish an attainable blind score.
The branch aggregates independent and persistent gain likelihoods
and supplies no separate clean projection witness, so the experiment
cannot apportion the loss between those assumptions. These results do
not justify a threshold reduction or a qualified receiver candidate.
Any future change needs an independently placed replay with fresh payload
and noise, a matched complete-receiver pair, and fresh noise-only and
wrong-key controls before an improvement claim.

The first shared targeted build stopped at unresolved phase-screen
symbols in the native-chain link, before any checks or comparisons
ran. The registered phase-screen target resolved the setup dependency.
The same targeted build then passed, as did the Gannon replay,
native-chain, public receiver, and strict-runner groups (4/4).
The comparison binary SHA-256 is
`75e05d97739408ac55e899d99f8669c2e995f2ae2ca80f36455b841bc7cfc380`.
The [frozen input](local-evidence/hf-gannon-native-comparison/frozen-input.json)
and [receipt report](local-evidence/hf-gannon-native-comparison/report.md)
record the separate appraisal build and work.

## Gain variance in independent short blocks

The unpartitioned variance bound does not carry over to independent
gains per block. For a common variance `τ` the 0.25-second partition
sums `-log(1+τaᵢ)+zᵢτaᵢ/(1+τaᵢ)` over blocks, where `aᵢ`
is reference energy divided by noise variance and `zᵢ` is noisy
projected energy divided by that variance. The archived winner retains
279 nonzero blocks, `Σaᵢ=6999.531`, `Σzᵢ=415.465`, and the
unit-variance log likelihood −509.919 for the wanted bit. The
accumulator discards individual block norms and projections after
adding their contributions.

As an equal-reference-energy approximation, the continuous maximum is
25.371 at `τ≈0.01950`, before public priors or marginalizing the
unknown bit. At the native tracked branch's fixed variances 1/16,
1/256, 1/512 and 1/4096 the corresponding approximate scores are
−9.453, 10.998, 6.060 and 0.826. The geometric midpoint 1/64 between
the two largest variances gives 24.722. The native tracked branch
actually uses its own 0.5-second geometry, and its public configuration
has no variance-only override.

These approximate scores cannot exclude or establish detection at
another block variance. For example, positive block norms of 12 and 50
in two blocks and 25.04524 in each remaining block, with only the
first two normalized projected energies nonzero, can match the four
recorded quarter-second aggregates exactly and yield 51.954 at 1/64.
This is a mathematical example showing that the four aggregates have
lost information, with no claim that it occurred in the archived IQ.
The selected cell maximizes a different unpartitioned score.
An exact fixed-variance mixture would need the actual per-block
sufficient statistics, fixed model and coordinate masses over the
evaluated public bank, and independent negative controls before any
operational interpretation. The −509.919 result applies to the unit
variance actually tested.

### Frozen selected-cell variance check

The [single-row follow-up](campaigns/hf-gannon-gain-variance.json) keeps
the archived slot 2 full-prefix and selected-aperture digests, with a
45-second wall limit and 500,000 capture samples. It uses the same
noisy-selected fixed and refined public cells. For both bits at each
cell it scores the existing .25, 1, 4, 16-second and full-aperture
partitions at fixed gain variances 1/4096, 1/512, 1/256, 1/64, 1/16
and 1. The existing history accumulator adds these likelihoods before
it discards each block's sufficient statistics. The original unit
variance result is preserved entry for entry.

Score the actual IQ, private wanted mean, and same-noise residual
at the original additive noise variance. The residual correlation is
the difference of the actual and private mean correlations. Both public
cells were selected from the actual noisy observation, so the residual
is a conditional same-realization reference. Compute the equal mixture
by averaging likelihood ratios across bits, partitions and variances.
For the fixed cell, also apply the mass of that one timing, CFO and
sinusoidal-model coordinate in the original full public bank. The
refined cell has a descriptive uncharged mixture. Only a charged fixed
cell contribution reaching the existing presence reference triggers
a separately costed full-bank proposal and independent controls.
One exact rerun completes this check.

The rerun completed in 4.36 seconds and passed strict receipt validation.
Both IQ and validity digests, the canonical native source digest, the two
selected coordinates and projections, the unit-variance partition scores,
and the known binary and presence quantities agree exactly with the
original comparison. The fixed coordinate has log mass −17.0333 from
52 timings, 4,951 CFO cells and 97 sinusoidal models.

| Retained cell and weighting | Actual IQ | Private wanted mean | Same-noise residual |
| --- | ---: | ---: | ---: |
| Fixed, equal bit/partition/variance mixture | 22.236 | 24.020 | −0.952 |
| Fixed, including full public coordinate mass | 5.203 | 6.987 | −17.985 |
| Refined, equal bit/partition/variance mixture | 23.244 | 24.748 | −0.917 |

At the fixed cell, the exact quarter-second wanted-bit score reaches
24.190 at variance 1/64 on the actual IQ, versus −509.919 at unit
variance. At 1/64 the private mean and same-noise residual separately
score −50.017 and −15.968. The actual score depends on this particular
added-noise realization and its projection with the wanted mean. The
largest actual component among the declared alternatives uses the
full aperture at 1/256, with log LR 25.257 at the fixed cell and 26.373
at the refined cell.

The charged actual contribution 5.203 is below the predeclared 21.493
reference. The selected-cell result ends this follow-up under its
continuation rule. Other coordinates under the new prior remain
unevaluated. The already aggregated coherent-search likelihoods lack
the block projections needed to evaluate those cells in aggregate.
The existing public correlations use 29-sample blocks, which the
fixed 480-sample quarter-second boundaries can split. A full-bank
evaluation would require new work accounting for those boundaries and
all public timing and frequency hypotheses, plus separate negative
controls for any receiver proposal.

The [frozen run](local-evidence/hf-gannon-gain-variance/frozen-input.json)
and [receipt report](local-evidence/hf-gannon-gain-variance/report.md)
record executable SHA-256
`0ce90649bfe7cb3f20f38e13fba66f7f5357eea04414b2b4233434af1d7d18a4`.

## Sources and retention

The record provenance comes from the source deposit and the receiver method
paper. The quiet-HF Gaussian-tap literature motivates distinct fading priors
without calibrating this disturbed recorded-voltage example.
[Gannon dataset](https://zenodo.org/records/20466479),
[GRAPE methods](https://doi.org/10.5194/essd-15-1403-2023),
[Watterson channel model](https://its.ntia.gov/publications/details?pub=3106).

Retain campaign receipts under `local-evidence/hf-gannon-recorded-failure/`,
`local-evidence/hf-gannon-native-comparison/`, and
`local-evidence/hf-gannon-gain-variance/` for the matched failure and
model-support review. The last receipt preserves the fixed-grid component
scores needed to assess a later block-gain proposal. Distill any superseding
matched results here before cleaning up redundant receipts.
