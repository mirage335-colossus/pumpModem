# HF inference structure and coupled propagation

Continuation against `af0f52299bcafb99722ea7069e4fd5f2444f13d3` on
14 November 2026. Each protocol below was recorded before its native outcomes. It follows
the unresolved findings in [receiver repair](hf-receiver-repair.md), with one
native execution lane and no changes to production policy or native hooks.

The completed continuation contains 32 bounded observations: 15 complete-receiver
trials and 17 component/private/shadow diagnostics, plus separate numerical
physics probes. It identifies distinct phase-reachability and rate-support
restrictions, retains the GUI discrimination failures on clean IQ, and feeds an
admitted coupled cutoff/absorption waveform through the canonical receiver.
The phase candidate gains individual bits but repairs no additional complete
long message. The coupled waveform recovers `A` at incident C/N0 = 40 dB-Hz
and fails at 30, 20 and 0 dB-Hz; its matched noise control stays quiet.
These results narrow concrete failure mechanisms and a sensitivity bracket;
they do not establish coverage of most unusual real HF days or the desired
general time/energy trend. Exact operating limits and outcomes follow below.

## Frozen diagnostic questions

`campaigns/hf-phase-structure.json` repeats three exact private apertures:
fresh long failed slot 1, historical TID slot 0, and maximum-rate edge slot 1.
Each has a 180-second / twelve-million-sample cap, 540 seconds aggregate.
Existing public evidence must remain unchanged. Clean traceback measures the
grid path's jumps, reversals, phase residual and prior cost, and compares with
the analytic physical carrier. A witness alone does not establish a necessary
jump count. A second fixed max-plus objective subtracts
`lambda = sqrt(Eclean*Etemplate)/(64*groups)` per jump; its optimum gives a
conservative lower bound on jumps needed to reach 99% of the original best
real-projection power (sqrt(0.99) times its real correlation). This finite-lattice
criterion is distinct from the fraction of all received clean energy. Numerical
margins are practical floating-point guards, not interval arithmetic proofs.
Independent on-grid, half-bin, quarter-bin and smooth-chirp controls test the
representation. This is evaluator-only work, not a receiver likelihood.

`campaigns/hf-gui-clean-native.json` freezes three rows: wrong-key slot 2,
fresh `A` slot 5, and noise-only exact-zero clean control. Caps are 120 seconds
each, 360 seconds aggregate. The thermal-off twin starts from sample zero,
preserves channel/source/clock state and validity, and uses one noisy-selected
native coordinate bank per key. Clean and noisy scores share that key's bank.
The actual transmitter key is private evaluator information. Native branch
evidence is available; a varying-model residual/posterior is not. These rows
diagnose discrimination without changing acceptance thresholds or decisions.

## Frozen phase-factored propagation preflight

The full-wave probe's `--phase-factored-profile-refinement` retains both full
physical trajectories, five offsets (-864, -768, 0, 768, 864 Hz), 5/10 m meshes
and the existing 0.005 complex-error gate. Its sole new interpolation method
removes the analytic D-layer carrier phase, interpolates the remaining complex
response, and restores the interpolated knot phase. Reflection uses two D-layer
traversals and transmission one. Neither the profile span nor loss is normalized.
The phase at a test midpoint is interpolated from knots; the exact midpoint
solution remains an independent reference. Complex residual interpolation
remains defined at zero amplitude.

Freeze 16, 32 and 64 profile intervals, at most 2,580 RF solves and 180 seconds.
Retain direct complex interpolation as the matched control. Analytic D-layer
quadrature uses 400 cells, compared with 800 at knots, midpoints and 33 irrational
profile positions, with a 1e-6 rad convergence gate. Exact-node reconstruction,
native/conventional phase signs and an independent weak-plasma reference are
unit checks. No separate arbitrary phase-interpolation gate is substituted for
the unchanged final complex error. A successful preflight is only permission to
qualify full-band kernels and a slowly evolving surrogate; it does not validate
a dynamically evolving ionosphere.

## Research reconciliation and limits

The existing [reference review](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md) distinguishes stationary
Watterson validation from unusual nonstationary conditions and warns that
sounder no-return is detection-conditioned, not proof of zero transfer. These
experiments retain that distinction. The coupled D/F path is constructed, not a
measured flare law. Phase-factor interpolation preserves the static solver;
dynamic admission must additionally address propagation time, kernel memory and
phase rate. A small Doppler/carrier ratio alone is insufficient.

[Chum et al. (2018)](https://doi.org/10.1186/s40623-018-0976-4) provides
measured flare Doppler/absorption context with finite sampling and periods of
signal loss. D-region absorption and higher-altitude phase changes need not
follow the same driver or timing. Consequently the shared D/F parameter here
is a named constructed sensitivity branch, not a fitted flare distribution.
Recent power or Doppler summaries in the reference review cannot substitute
for a calibrated long complex channel record.

[Conti et al. (2024 preprint)](https://arxiv.org/abs/2404.05344v1) uses continuous
circular phase, but its Wiener/AWGN coded-QAM model does not establish an HF
frequency prior. Its reported multimodality and phase slips caution against
assuming a single circular approximation solves these failures. Continuous
particles are another candidate, but normalized importance weights and suitable
resampling are essential for a valid likelihood estimate
([Andrieu et al., 2010](https://doi.org/10.1111/j.1467-9868.2009.00736.x)).
Naive data-dependent pruning can destroy unbiasedness; corrected rejection
control can require variable work
([Kudlicka et al., 2020](https://arxiv.org/abs/1910.09527)). Neither estimator
unbiasedness nor a high-energy private path proves practical blind sensitivity.

Raw receipts in `local-evidence/hf-phase-structure`, `hf-gui-clean-native`, and
`hf-inference-propagation` support these unresolved questions. Retain them until
their claims are superseded and distilled here; then remove redundant outputs.

## Initial numerical outcome

The four focused native/runner groups passed (1.63 seconds combined). The
phase-factored preflight passed at 64 intervals in 11.614 seconds / 2,580 RF
solves. Maximum coupled reflection/transmission errors were 0.001553785 and
0.001776631; D-only reflection error was 0.000212681. The matched direct
interpolation errors were 0.119486883, 0.044761457 and 0.100879052. The largest
400-to-800-cell relative-phase change was 5.29e-10 rad. No profile span, error
gate or loss convention changed to achieve this outcome. The raw receipt
retains build identity and flags full-band, slowness and communications as
unqualified.

## Frozen full-band follow-up

Before full-band outcomes, retain the same complete coupled D/F path and
65 knots. Use a 256-frequency design at 5 m; check 65 fixed irrational
frequencies at every knot and at two independent non-knot positions in each
interval. Five fixed profiles (q=0,.25,.5,.75,1) additionally compare 512
frequencies and 10 m meshes. Both physical ports are evaluated per solve.
The design costs at most 32,070 new RF solves (34,650 including preflight),
with an external 240-second cap. Original 0.005 complex-response and 1e-6
discarded-impulse-energy gates remain unchanged. No gain normalization or
reduction of the physical profile span is allowed. Failure withholds table
admission; an unqualified table cannot feed a receiver.

The proposed finite native operator has 129 taps and an explicit 32-sample
modelling delay, with retained history/tails and continuous physical profile
time. Its slowness check bounds the difference between output-time and
per-tap-retarded conventions in incident-amplitude units. For each tap, bound
|dh/dq| by its maximum over all profile intervals before summing against
|lag−32|/1920. For a clamped half-cosine of duration T, multiply that sum
by pi/(2T); require at most 0.005. This bound applies to these two finite-FIR
surrogates, not to a distributed, time-evolving Maxwell solution.

Freeze candidate durations 120, 240 and 480 seconds plus a deliberately fast
1-second control, independently of decoding outcomes. Report a separate
0.1-rad phase-coordinate flight proxy, with undefined/ill-conditioned phase
near nulls explicit. The absolute output bound retains those nulls. Existing
flight-time and weak-scattering limitations in
[Gherm et al. (2005)](https://doi.org/10.1029/2004RS003093) remain applicable;
passing a numerical proxy does not validate strong scattering or magnetoionic
mode coupling.

## Diagnostic outcomes and receiver experiment

All three private phase rows completed in 41.212 seconds. For fresh long /
historical TID / maximum-rate edge, best clean grid paths had 258 / 245 / 78
jumps and 215 / 234 / 42 successive jump reversals; analytic carriers had
7 / 4 / 3 reversals. The fixed certificate lower-bounds jumps needed to
retain 99% of best discrete real-projection power at 60 / 54 / 13. Feasible
penalized witnesses use 190 / 186 / 47 jumps. These are finite-lattice
statements, not physical jump requirements. Four-second averaging reduces
fresh-long frequency RMS error from .02550 to .006256 Hz. This supports a
representation penalty diagnosis without claiming it explains every miss.

The three GUI clean diagnostics completed in 20.445 seconds. Noisy evidence
and IQ hashes match the earlier decisive apertures exactly. Fresh `A` slot 5
(true 0) has affine log-evidence difference (1 minus 0) −653.62 noisy and
−819.75 clean, while short-varying incorrectly favors 1 by +853.17 and
+764.70. Wrong-key slot 2 (transmitted 1) still favors false 0 under short
varying on clean IQ (difference −124.00). The private actual-transmitter-key
bank strongly favors true 1 (+26681.09 clean). This shows useful waveform
discrimination remains when that key is supplied, not that an implementable
blind receiver can use unknown-key truth. Separate key banks also include
acquisition differences. Noisy-selected coordinates are fixed for each
clean/noisy comparison; independent clean acquisition is not tested. The
noise-only clean capture is exactly zero. These outcomes rule out fresh
thermal randomness as necessary for these fixed-bank failures and argue
against a threshold-only repair.

Before candidate outcomes, freeze `hf-phase-bridge-shadow.json` (seven rows,
180 seconds each / 1,260 aggregate) and `hf-phase-bridge-receiver.json`
(ten rows, 180 seconds each / 1,800 aggregate). These include the two existing
misses, retained TID and fast-edge cases, independent long physical root 60105
with placement 887 s, and fresh 13-bit `A` root 60106 with placement 1237 s.
The two holdouts have paired complete legacy baselines. Their full parameters
are in the frozen manifests and were selected before bridge outcomes. Noise
and wrong-key controls share the old fresh-long geometry. Existing thresholds
and complete-message quiet-slot rules remain unchanged.

The opt-in bridge uses four-second adjacent-frequency ramps between the same
33 endpoints and 32 phase states. Exact normalized edge sums keep all paths,
including partial terminal edges; block-center phase defines the actual
block-constant likelihood model. It uses fixed .5 stay/.25 neighbor mass,
reflected at frequency boundaries, and the existing two gain radii. The
supplement mixture is .5 refined family, .25 original correlated family,
and .25 bridge (with sticky: .125 walk + .125 sticky). The native outer
weight remains .25. Faster changes retain their existing support, though
reducing their prior mass can still regress a marginal case; the fast-edge
control checks this tradeoff. No truth is supplied to receiver inference.

The seven bridge shadows completed in 58.981 seconds. The fixed mixture
passes the retained TID and both new wanted holdouts; neither negative control
passes. It still misses the historical fresh-long and equal-energy edge
apertures. The phase bridge alone has weighted presence 30.053 on TID and
26.256 on the fresh long holdout, versus 25.956 and 19.378 for the existing
correlated mixture. Complete-message results remain a separate gate.

Source analysis after these outcomes identified a stronger support restriction:
`p−4*k mod 8` is invariant at four-second boundaries. With frequency fixed,
each initial class reaches four boundary phases spaced by pi/2. A half-bin
analytic control does not establish arbitrary fractional-frequency coverage.
Freeze `hf-bridge-support.json` before private support outcomes: three matched
apertures (historical fresh-long slot 1, equal-energy edge slot 0, TID slot 0),
180 seconds each. Reuse the existing clean statistics and compare max-real
projection for the original 97 edges against two separate support relaxations:
phase increments ±one phase bin (291 edges) or frequency edges ±two bins
(159 edges). Cache at most 159 clean correlations per block; cap the extra
work at 1,431,000 products / 2,625,600 max-plus terms / 256 KiB. The phase
relaxation allows discontinuities and is explicitly not a receiver proposal.
No noisy maximum, physical truth, or optimized relaxation enters reception.

The ten complete-receiver rows all executed within their caps (776.640 s
aggregate). Both negative controls produced zero DATA and zero messages.
The bridge retains complete TID, fast-edge and fresh `A` recovery. The paired
legacy `A` run also succeeds. The older fresh-long miss now recovers its last
bit and retains an interior erasure instead of ending prematurely, but still
cannot decode. The near-equal-energy edge still misses its first bit. On the
new long holdout, legacy misses all three bits; bridge recovers the selected
middle bit, then ends prematurely through the missed final bit. Neither
accepts a payload. Thus the improved shadow does not establish a new complete
long-message repair. There are four wanted-message failures among the eight
wanted rows including baselines; these are heterogeneous development results,
not an estimated error probability.

All measured execution here is local native CPU, Ryzen 5 PRO 5650U, six cores /
twelve logical threads, H=12/W=24, Debian 13 and GNU 14.2 Release. Hardware
metadata is retained beside the logs. Whole invocation timings include source
rendering, preparation, receiver fill/drain and optional private diagnostics;
they are not complete-service capacity calibration. There was no GPU execution.
The three trace replays preserve all archived public/path scores and capture
hashes exactly across differing source identities.

## Admitted coupled waveform follow-up

The full-band probe completed 32,070 RF solves in 167.505 seconds. All 65
knots and 128 independent profile holdouts passed the frozen sampled gates.
Largest reflection/transmission holdout errors were .00135655/.00154850;
frequency-refinement and mesh witnesses were below 2.91e-6 and 2.03e-5.
Discarded impulse energy was below 2.15e-8. The 120-second transition passes
the separate finite-FIR retardation bound (.00365593/.00248644); the one-second
control fails (.438711/.298373). Chunk invariance, direct convolution and full
tail checks passed. This is sampled static numerical qualification plus a bound
between two defined FIR conventions, not a full evolving-plasma error bound.

Freeze `hf-fullwave-coupled-receiver.json` before modem outcomes: one complete
canonical waveform component and two complete receivers at input C/N0 20 and
0 dB-Hz, all using the reflected port, full q=0..1, fixed transition start 60 s
and first admitted duration 120 s. Root/noise seeds 64001/74001, payload `A`,
requested 64 s per bit, twelve-million-sample and 300 s per-row caps, 900 s aggregate.
Each CLI process rebuilds and requalifies the same immutable table once; its
32,070 solves and preparation time count against that wall cap. This setup
cost is distinct from receiver service. The native source must span at least 540 s
(the latest declared candidate transition endpoint), and actual source bounds
must fit ±768 Hz before numerical preparation.

Public arrival is [1,129]/1920 s, covering the entire retained reflected FIR
support, including the 32-sample modelling delay. A 128-sample filter
halo retains precursors and tails; the actual reflected output tail is 129
samples, charged once in physical quiet planning. No additional rays, Helper,
clock errors, frontend or nonwhite noise are combined into this fixture.
Incident/reflected energies before, during and after the transition are
reported separately. Physical attenuation is retained; fixed input C/N0 must
not be relabeled as post-channel sensitivity.

The fixed receiver uses varying-channel processing, zero configured delay
spread, zero frequency-search bound and a 1 Hz atmospheric bound. Zero spread
selects a one-ray receiver prior facing a dispersive FIR. The public arrival
range and observation halo preserve delay-search and sample support; they do
not make that likelihood a matched dispersive-channel model. Nonzero native
spread would add fixed delayed-ray anchors, not all 129 FIR coefficients.
This batch characterizes the declared configuration, not the best supported
receiver configuration or an information-theoretic sensitivity limit. The
private physical kernel is never supplied to receiver inference.

Before private bridge-support acceptance, its analytic constant-carrier unit
failed the phase-relaxation expectation. The three-row campaign was mistakenly
launched before inspecting that failed test result and was stopped with exit 130;
its initial output is unqualified and retained separately from any corrected
run. The same test invocation also rejected the new full-wave component
manifest's receiver-only `control` field; that field was removed from the
channel-mode row before any full-wave modem outcome. Neither failure is a
communications result.

Independent completion review, before any full-wave modem outcomes, identified
an essential bright-control gap: endpoint reflection power is approximately
4.1e-4, giving only about 2.6 energy/noise per late 64 s bit at input C/N0 = 20.
Failure there would not isolate receiver mismatch. Freeze the separate
`hf-fullwave-bright-controls.json`: wanted input C/N0 = 40 and its matched
noise-only control, otherwise identical A64 geometry, 300 s each / 600 aggregate.
Retain all original 20/0 outcomes. Noise keeps the source schedule metadata
but requires exactly zero incident/reflected energy, without altered noise
calibration. These cases are fixed before their receiver outcomes.

Independent review rejected the initial proposed arrival upper bound 38 samples
before any full-wave modem outcomes: known near-cutoff group delays plus the
modelling delay can exceed it, and an observation halo does not enlarge delay
hypotheses. The final declared bound covers all 129 retained FIR lags. It is
intentionally broader than a vacuum-flight estimate; no received peak or decode
outcome selected it. Source/quiet support and the fixed 300 s row caps remain.

The failed private-support unit was traced to an unsupported analytic test
expectation, not a DSP change: a quarter-bin carrier cannot be represented
arbitrarily accurately by one-bin phase offsets. The correction compares
against an independently constructed allowed four-edge path (94.6044% power)
and keeps the independent short exhaustive reference, plus a separate near-bin
control. All 22 then-run appraisal groups excluding runner/integration passed
in 26.35 s, including the corrected support test. Subsequent full-wave timing
and noise-control changes receive their own final affected validation.

## Matched support discrimination

The corrected private support campaign completed all three rows without
execution failures. Maximum clean real-projection power fractions are:

| Same private aperture | Original four-second bridge | Phase-offset relaxation | Wider-rate relaxation |
| --- | ---: | ---: | ---: |
| Historical fresh-long failed slot 1 | .884116 | .974030 | .924083 |
| Near-equal-energy edge failed slot 0 | .210784 | .293511 | .895609 |
| Retained long-TID slot 0 | .898326 | .982372 | .926534 |

These fixed comparisons identify different restrictions: phase reachability
matters strongly for the two long trajectories, whereas frequency-change
support dominates the fast boundary. All 32 initial phases are searched;
therefore the best absolute-correlation power of each finite model is at most
the displayed real-projection optimum divided by cos²(pi/32), capped at 1.
That small angular margin cannot explain the large original-model deficits.
The phase relaxation explicitly permits jumps and neither relaxation includes
a normalized prior or receiver decisions. They are not proposed sensitivity
gains. A further public receiver design must address support and probability
allocation together, with new holdouts; choosing another segment duration to
fit these three apertures would not qualify it.

## Execution tiers and retained evidence

Cheap appraisal correctness groups and the five-frequency physical preflight
remain the first gates. The full-band table and complete coupled-waveform
campaigns are targeted qualification work for this operator, not a routine
exhaustive test requirement after unrelated edits. Each current CLI process
requalifies its table; the measured preparation cost is explicit. Shared table
reuse within a future bounded campaign could reduce that setup cost, but has
not been implemented or credited as receiver throughput.

The final full appraisal invocation passed 24 of 25 groups and failed only the
integration fixture's unsupported assumption that short requested bits imply a
short source calendar. The corrected test keeps the admitted long-calendar
case and independently checks rejection of a genuinely short source. The
affected integration and runner groups then passed in 54.01 s. The 98 runner
unit checks pass. Original failed logs, the corrected checks and campaign
receipts retain distinct source/binary identities; no failed gate is relabeled
as a successful original invocation.

After the fixed 20 dB-Hz failure and 40 dB-Hz complete success, exactly one
intermediate observation at 30 dB-Hz was frozen on the identical
source/channel/noise root (`hf-fullwave-boundary-midpoint.json`, 300 s,
12 million samples). It narrows the descriptive 20 dB bracket and checks
intermediate wrong-content behavior without changing the model, receiver
thresholds or duration. There was no further bisection in this batch.
Common noise roots make this a paired
energy comparison, not an independent success-probability sample.

## Complete coupled-waveform outcomes

All six declared observations (one component and five receivers) completed
uncensored with valid receipts: [original cohort](local-evidence/hf-fullwave-coupled-receiver/report.md),
[bright/noise pair](local-evidence/hf-fullwave-bright-controls/report.md), and
[fixed midpoint](local-evidence/hf-fullwave-boundary-midpoint/report.md).
Actual native geometry is 69.6 s per bit,
13 transmitted bits and 15 scored slots. Source stop is sample 2,208,229 and
the complete capture has 2,554,138 samples at 1920 Hz. All observations use
the full transition and identical public geometry; the component and 20 dB-Hz
receiver also have exactly matching input hashes.

| Incident C/N0 (dB-Hz) | Correct DATA / 13 | Active slots called quiet | Correct complete message | Premature endings |
| ---: | ---: | ---: | --- | ---: |
| 0 | 0 | 13 | No | 0 |
| 20 | 1 | 12 | No | 1 |
| 30 | 5 | 8 | No | 3 |
| 40 | 13 | 0 | `A` (`65`), quiet slots 13 and 14 | 0 |

No wanted trial makes a wrong-bit DATA decision or accepts wrong content.
The weak fragmented messages are codec-rejected. The matched 40 dB-Hz
noise-only trial has 15 quiet decisions, zero DATA and zero accepted messages;
its clean incident/reflected energy is exactly zero in every region.

At 30 dB-Hz, slots 0, 1, 2, 5 and 10 produce correct DATA. All 13 active
slots favor the correct bit in their computed native evidence, including
misses. The eight missed slots have correct-minus-wrong log-evidence gaps of
approximately 17.47–26.46 and presence scores of 9.77–20.72 against the unchanged 21.493
threshold. This identifies a configured presence/framing boundary, not absent
bit information, a justified threshold reduction, or best attainable blind
sensitivity. There is a descriptive 10 dB complete-message bracket between
30 and 40 dB-Hz on this single paired realization. No success probability,
continuous monotonicity, longer-duration repair or general graceful-degradation
claim follows from it.

The physical attenuation is substantial and remains in the waveform:

| Physical region | Incident energy | Reflected energy |
| --- | ---: | ---: |
| Before transition | 115250.443 | 3026.235 |
| During transition | 153529.331 | 1583.305 |
| After transition | 1478295.385 | 839.330 |

The after-transition waveform energy ratio is about 5.68e-4 (−32.46 dB).
This integrated result supersedes the approximate carrier-only endpoint
ratio used to justify the bright control. Incident C/N0 values in the table
must therefore not be compared directly with post-channel mode sensitivities.

The three original full-wave observations took 577.578 s in aggregate, the
bright/control pair 394.119 s, and the fixed midpoint 198.371 s. Each native
invocation spends approximately 167.5–167.8 s constructing and qualifying the
same table. This preparation is included in the row caps and wall times;
it is not receiver throughput or evidence of a real-time computational ceiling.

## Provenance, conclusions and retention

The six full-wave observations share final appraisal source identity
`6cc5e7b4ed99c45b3147de6525836e25b6aab82435b2ff16a5592c58970027b4`
and binary SHA-256
`fba6bbe4d4b4710cd0eb10989901a06effdbcba126c30fdae228c45073889f4c`.
The canonical native identity remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
Earlier phase and GUI diagnostics retain their own source/binary identities;
the 32 observations must not be described as one unchanged-binary campaign.
The initial support-campaign launch was interrupted before producing results
and is excluded from those 32 observations.

All six full-wave table coefficient hashes are
`c6a473d0230a748c4daf34318ad2b1928a748239da7cc7192de05bfec6c6c543`.
This hashes ordered knot coefficients for both ports as little-endian IEEE-754
double real/imaginary pairs. Source identity additionally binds the unwrapped
phase interpolation; the coefficient digest alone is not a signature of every
between-knot response.

Hardware and original/final validation logs are in
`local-evidence/hf-inference-propagation/`. Per-campaign native JSON, frozen
inputs, campaign hashes and reports are in the correspondingly named
`local-evidence/hf-phase-structure/`, `hf-gui-clean-native/`,
`hf-phase-bridge-shadow/`, `hf-phase-bridge-receiver/`, `hf-bridge-support/`,
`hf-fullwave-coupled-receiver/`, `hf-fullwave-bright-controls/` and
`hf-fullwave-boundary-midpoint/` directories. Retain these receipts while the
phase/support failures, GUI discrimination failures and coupled-channel
sensitivity bracket remain active investigations. When superseded, distill
the replacement evidence here and remove redundant raw outputs. The original
failed validation logs remain distinct from corrected passing checks.

All new implementation, tests, manifests and documentation are contained in
this appraisal directory. Production defaults and the approved native hooks
were unchanged; this continuation used no RunPod resources. All 25 appraisal
check groups are covered by the 24 original passes plus the affected fixture
repair rerun, not by an originally all-green full invocation.

The next receiver design must jointly address phase/rate support and prior
mass rather than fit another duration to these apertures. GUI clean-input
misclassification still needs a whole-message repair with wrong-key controls.
The new coupled channel enables further receiver comparisons with physical
loss and dispersion intact, but this one-ray configuration is not an optimum.
General dynamic magnetoionic coupling, strong scattering/caustics, long
measured disturbed-day records, and an orders-of-magnitude advantage over
other modes remain unqualified. These gaps remain explicit requirements;
the new numerical and communications passes do not close them.
