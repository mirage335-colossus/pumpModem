# Independent HF trajectory appraisal

Frozen 14 November 2026, before executing this continuation's new channel cases.
This is a bounded failure-finding design, not an estimate of the probability of
successful reception on ionospheric days. The receiver and its phase-template
bank remain unchanged. The native channel generator takes no receiver-bank
parameters or search results.

## Questions and physical scope

The previous single-period successes left nearby and interacting failures
unqualified. This continuation varies the physical density history, its
placement relative to a message, and selected mode/noise/Helper combinations.
The [research synthesis](hf-research.md) and
[measured-data audit](hf-experiments.md) distinguish the supporting observations
from these constructed trajectories. In particular, sensor cadence and unrelated
path maxima do not establish a joint measured time law.

| Stratum | Frozen native construction | Interpretation |
| --- | --- | --- |
| `hf-ensemble-tid` | Vertical cold-plasma parabola, bottom 253 km, half-thickness 50 km, critical frequency 3.8 MHz; 12 kHz / 271 s / .57 rad and 13 kHz / 1013 s / −1.11 rad variations | Two contextual timescales, independent of the detector's 90–180 s sinusoid bank; not a fit to simultaneous measurements |
| `hf-ensemble-changing-rate` | 245 / 52 km, 3.73 MHz; 11 kHz / 397 s / 2.19 rad plus bounded 14 kHz C2 random knots every 113 s | Reproducible nonperiodic rate changes; knot seed is separate from noise; derivatives vanish at knots, unlike a measured turbulence law |
| `hf-ensemble-modes` | Changing-rate primary plus 305 / 65 km, 3.72 MHz secondary, 6 kHz / 503 s / −.77 rad plus 6 kHz knots every 173 s; fixed continuous fade/recovery gates | Distinct effective reflection regions with coherent interference; not a solved stack of vertically screening layers or magnetoionic O/X rays |
| `hf-ensemble-modes-noisy` | Same two-ray geometry, fixed colored/shot-noise stress | Canonical receiver only; proper-white-null phase evidence is unavailable |
| `hf-ensemble-ox-helper` | Existing full-band Jones O/X construction, visibility gates, common .72 Hz frequency offset and colored/shot noise | Canonical Helper interaction; constant residual frequency is an oscillator-offset surrogate, not a diffusion or sample-clock model |

Each eikonal derives phase delay, group delay and dispersion from the same
critical frequency. Admission uses the declared global frequency/rate bounds,
the occupied source band, monotone retarded time and the interpolation tail,
not extrema observed in one realization. Rays are not renormalized after fades.
The visibility schedule is an imposed stress, not inferred from the density
law. A helper-bearing eikonal which exceeds the existing dispersion-band limit
must remain unavailable; the separate O/X case does not remove that limit.

`--hf-trajectory-seed` selects the immutable knot field;
`--hf-trajectory-offset-seconds` shifts the analytic channel's physical clock
without changing capture preroll, the source calendar or the additive-noise
clock. `--noise-seed` changes only the additive-noise domain. The ordinary
`--seed` still selects stochastic path realization. Resolved laws, seeds,
placement and a channel descriptor hash are recorded by the native probe.
Legacy runs with no overrides retain their existing source/noise conventions.
These are different axes; extra noise seeds are not extra ionospheres.

## Frozen hierarchy and stopping rules

1. Run deterministic physical/integration/runner checks and native `plan`
   admission before communications. No waveform is rendered in Python.
2. [`hf-independent-screen.json`](campaigns/hf-independent-screen.json): six
   selected-slot screens at requested 128 s and 0 dB-Hz (TID placements 0/74 s;
   changing-rate seeds 20101/20102 at 0/83 s; modes 20103/20104 at 0/121 s),
   plus TID/changing-rate at 512 s and −6 dB-Hz and one noise control. All use
   root seed 30101 and noise seed 40101 for paired comparisons. Slot 1 is fixed
   before seeing outcomes. Cap: nine trials, 100 s each, 900 s aggregate.
3. [`hf-independent-receiver.json`](campaigns/hf-independent-receiver.json):
   three 128 s complete messages (`e` for TID/changing-rate; `A` for modes so
   active transmission crosses fade and recovery), one fresh changing-rate realization
   (20107), placement (197 s), message (`A`), root (30107) and noise (40107),
   then same-geometry noise/wrong-key controls. Five canonical-only rows cover
   colored-noise/mode wanted/null, Helper/O-X wanted/null, and the existing
   disturbed HF profile. Their requested 16 s / 12 dB-Hz geometry keeps this
   interaction screen bounded. Cap: 11 trials, 180 s each, 1500 s aggregate.
4. Follow up at most two failures, selected in manifest order among completed
   wanted screens below the public presence/separation references, or complete
   message misses when no screen fails. Censoring is not a signal miss. For
   each: replay the exact failing selected slot with the private native
   physical diagnostic, and inspect a fixed nearby bracket: +3 dB at the same
   requested duration, −3 dB at the same duration, and twice the duration at
   −3 dB relative to the original. At most eight component trials, 100 s each,
   800 s aggregate. Preserve all rows; do not search for a favorable threshold.
   At most two corresponding complete-message +3 dB or doubled-duration
   follow-ups, 180 s each / 360 s aggregate, if component evidence supports
   admission. This conditional stage is recorded separately from the frozen
   screening cohort. No receiver tuning is part of this campaign.

The exact shadow predicate uses the combined old/new bit log-evidence: subtract
`ln(2)` for its bit mixture and `ln(4)` for the outer supplement weight, compare
with the reported presence reference, then require the **signed correct-bit**
gap to be positive and at least the separation reference. A confident wrong bit
is a miss. These are component references; canonical line/null gates and its
other evidence can still produce a different native decision. The contained
`ensemble.py` consumes the source-bound screen receipts and writes the fixed
eight-row maximum follow-up manifest without rendering signals. It refuses to
overwrite an existing manifest.

Total maximum: 30 native trial rows and 3560 s of declared campaign wall caps,
excluding cheap plans and correctness checks. One intensive local native job
at a time, default full `W=2*H`; explicit sample cap eight million per trial.
Cloud acceleration is warranted only for a supported native GPU workload.
Independent simultaneous simulations would not measure receiver throughput.

**Causal amendment after the nine-row screen, before its additional run:**
the first long single-ray miss (`tid-long`) retained only the 1 ms timing
atoms, while its physical path is near 2.12 ms. The frozen first-two-failure
rule selected the two mode cases. Add one separately labeled
[`hf-independent-long-causal.json`](campaigns/hf-independent-long-causal.json)
row (100 s cap) with identical input and **full public timing**, plus the
private known-binary diagnostic. This tests timing omission against signal or
phase-model loss; it is not an independent holdout or a retuned campaign.
Require the selected-IQ hash to match the original. If this row is censored,
one selected-timing physical diagnostic may follow under a separate 100 s cap;
otherwise no fallback run. The amended ceiling is 32 rows / 3760 s, and all
original rows and their selection order remain unchanged.

After all eight mode brackets missed the weighted screening references, their
conditional complete-message follow-ups were not triggered. Completion review
identified one necessary distinction: a long shadow miss need not imply native
message failure. Reassign one of the two unused 180 s complete-follow-up
allowances to [`hf-independent-long-receiver.json`](campaigns/hf-independent-long-receiver.json):
the exact long TID `e` episode, the normal operational search, and selected-IQ
digest at slot 1. This post-screen causal row must match both long shadows;
preserve a native success if canonical evidence rescues the shadow miss. It
does not increase the amended ceiling or alter the frozen screen.

The native long row failed slots 0 and 2 but **rescued slot 1**. Therefore the
slot-1 known-channel result is not a diagnosis of its missed native bits.
Before any further diagnostic, reassign the unused 100 s censored-full-grid
fallback allowance to [`hf-independent-native-miss.json`](campaigns/hf-independent-native-miss.json):
the first actual missed active slot (0), full public timing and private known
binary. It uses identical declared source/channel/noise context; the receiver
saved a digest for slot 1 only, so no direct native slot-0 digest equality is
claimed. This targets a different causal question and retains the 32-row /
3760 s ceiling. No more follow-ups are scheduled in this continuation.

The duration bracket is **operational**: same launch, longer exposure to the
same continuous channel. A longer slot traverses different physical history;
there is no monotonicity assumption or binary search. Record actual emitted
duration, signal energy, noise variance, public search costs and resource
status. Do not substitute requested duration for realized energy or label a
fixed-time energy deficit a receiver implementation defect.

## Endpoints and coverage limits

Selected-slot likelihoods are screening evidence, never a received message.
Complete receive uses native DATA/null decisions, exact payload/framing,
interior erasures, and two scored physical quiet slots. Preserve false DATA,
premature completions, wrong bytes and integrity rejection separately.
Private physical twins run only after public search and must not supply truth
to acquisition. Their conditional known-channel scores can diagnose mismatch;
they do not remove public acquisition, channel uncertainty or completion costs.

| Boundary | Coverage in this continuation | Still unqualified |
| --- | --- | --- |
| Physical trajectory | Multiscale and nonperiodic critical frequency, fresh seed and shifted launch, competing effective rays | Strong scattering, caustics, absorption/collisions, cutoff crossings, a calibrated disturbed-day distribution |
| Phase supplement | Actual multi-ray H1 mismatch with white proper-complex H0; fixed configured calendar | Helper, nonwhite H0, sample-clock error, broad declared delay/Doppler searches |
| GUI conditions | Existing 7 ms / 30 Hz statistical disturbed profile compares a delay/width scale, with Gaussian Doppler tails | GUI default Margin 3 implies 21 ms / 90 Hz support, already beyond the supplement's ≤25 Hz and zero declared spread scope; no exact preset or all-GUI claim |
| Payload/placement | Fresh `A` content and later position, full physical completion | Unknown epochs/ordinals, long-message mode and arbitrary real-world packet population |
| Measured channel | Existing finite Ding complex envelope and a new primary-source availability audit | No new long disturbed complex channel capture; no tiling or stretching the 41 s record |
| Performance | Exact observed work, memory and wall time on identified local hardware | Complete-stream real-time capacity, other receiver devices, superiority to other modems |

Receipts and logs live under ignored `local-evidence/hf-independent-*` only
while they support the current unresolved failure/boundary findings. Distill
resolved findings here and in [coverage](coverage.md) before removing redundant
outputs. Keep one current compatible build; do not preserve a copied receiver
or obsolete implementation.

## Executed evidence and findings

All **31 rows completed without execution errors or censoring**, in 433.631 s
aggregate native campaign wall time on a Ryzen 5 PRO 5650U, 12 visible logical
CPUs, default 24 software workers, 15,677,412 KiB system RAM, Linux x86-64 and
GNU 14.2 Release. This includes 12 end-to-end receiver trials and 19 component
trials. Independent cases were run sequentially. No GPU was rented: the
experimental phase scorer requires native CPU execution, and this bounded
campaign completed locally in about 7.2 minutes. These are trial durations,
not complete-stream capacity qualification or hardware forecasts.

All campaign receipts bind to appraisal source
`606d0da325808d648a7b186c686034166ef2687e29300793d7e2e1dd4d27d438`,
probe `285fcd0d9834206402f3baa930da82d58a33d7f0d961563880c62d737ea245ee`,
and unchanged canonical native source
`1749313fcabbf16527ac687c5cf4947acde3d00dca6bcd364b87d453b6d79a16`.
The full 18-group correctness check passed in 74.95 s; the final analyzer/runner
checks passed again after the canonical-hash/null-control repair and rebuild
([full checks](.build/hf-independent-checks.log),
[final Python checks](.build/hf-independent-final-python-checks.log)).

| Campaign | Rows / wall time | Result and scope |
| --- | ---: | --- |
| [Frozen screen](local-evidence/hf-independent-screen/report.md) | 9 / 30.462 s | Four short single-ray cases cleared the charged references. Both mode placements and both long/weak single-ray screens missed. Noise stayed below the presence reference. |
| [Frozen native receiver](local-evidence/hf-independent-receiver/report.md) | 11 / 249.825 s | Short TID, short changing-rate and fresh trajectory/197 s placement/13-bit `A` passed. Mode `A`, noisy modes, O/X Helper and disturbed diffuse wanted cases failed. Negative controls are retained separately below. |
| [Mode boundary follow-ups](local-evidence/hf-independent-boundaries/report.md) | 8 / 35.638 s | Exact original observations plus fixed ±3 dB and doubled-duration/−3 dB brackets. None cleared both weighted screening references. |
| [Full timing, long slot 1](local-evidence/hf-independent-long-causal/report.md) | 1 / 18.891 s | All 39 public timing atoms; physical bit information remains strong, public score remains weak. Exact IQ digest matches the original shadow. |
| [Native long TID](local-evidence/hf-independent-long-receiver/report.md) | 1 / 86.188 s | Message failed: active slots `quiet, DATA(0), quiet`, then two quiet slots. One premature completion and integrity rejection; no accepted message. Slot 1 was rescued despite the shadow miss. |
| [Actual native miss, slot 0](local-evidence/hf-independent-native-miss/report.md) | 1 / 12.627 s | Full-grid evidence reproduces the native supplemental score to floating-point rounding; private known-binary information is strong. |

### A new time/energy failure, with the native rescue preserved

The short TID message at requested 128 s / 0 dB-Hz passed. At requested 512 s /
−6 dB-Hz it failed. Actual bit duration increased from **139.2 to 522 s**;
whole-message transmitted energy divided by noise variance changed from
419.1168 to 394.6009, a ratio .9415057 (**−.26177 dB**). Received-energy/noise
ratio changes by the same amount to within a microdecibel in this lossless
single-ray construction. This is near equal total normalized energy, not an
absolute transmit-power or path-loss claim. Wall time rose from 16.50 to
86.05 s; rendering and receiver processing overlap, so these are not isolated
DSP costs or capacity measurements.

The selected long slot-1 shadow's correct-bit log-evidence was .1530; the full
39-timing shadow gave 3.0230, still below the charged presence reference. Its
known-context binary distance `D/v=260.8923` gives conditional equal-prior bit
error `1.6364e−30`. **The native receiver nevertheless recovered that bit.**
Its failure was at slots 0 and 2, so the slot-1 shadow is not presented as a
diagnosis of a missed native decision.

The final diagnostic targets the first actual miss, slot 0. The native callback
already closed the 13 proposed timing atoms to all 39 public timing atoms;
the earlier manifest phrase “selected operational timing” does not mean that
native admission omitted the other 26. Its correct-bit combined log-evidence
was **−.9034942096458711**, versus **−.9034942096458713** in the independent
full-grid diagnostic. Its private binary `D/v=260.0570`, observed correct-vs-
wrong log likelihood 247.6078, and conditional bit error `2.0197e−30` show
substantial bit information under the exact known context. The refined
winning cell retained only **.07243** of clean coherent energy. Actual native
slot-0 work included 3,845,873,664 FFT row-stages (old plus refinement), with
233,716,496 bytes of reported search workspace; that workspace is not total
receiver/process memory.

Thus this failure is not explained by missing public timing atoms or an
absence of known-context binary information. It does **not** uniquely separate
the fixed phase family's mismatch from proposal/optimization losses, establish
an optimal unknown-channel receiver, or prove that this private reference can
be reached at the same compute cost. The slot-0 replay has matched source,
trajectory/noise, calendar and full-context identities plus score parity;
only slot 1 had a separately saved native IQ digest.

### Different mode failures require different interpretations

The mode `A` episode spans 13 active slots. Descent occurs in slot 1, slot 2
lies wholly in the trough, slot 3 spans recovery, and nine active slots begin
after recovery. There were eight active quiet decisions, three premature
completions and three integrity rejections, with no wrong bits or accepted
message. Four of the nine wholly post-recovery slots returned correct DATA.
Recovery of some later bits is distinct from successful message delivery.

| Screen trajectory / slot 1 | Original correct-bit log-evidence | +3 dB | −3 dB | Twice requested duration / −3 dB | Known binary `D/v`; conditional error at original |
| --- | ---: | ---: | ---: | ---: | --- |
| Modes A, placement 0 | 1.0217 | 20.0169 | −3.3153 | −4.2975 | 87.1233; `2.0541e−11` |
| Modes B, placement 121 s | −4.0460 | −4.7023 | −3.4165 | −.2820 | 8.6210; `.01894` |

These scores precede the bit-mixture and outer supplement weights; none passes
the charged screen. The original physical-diagnostic IQ digests match the
screen exactly. Modes A exposes a large public-search/known-context gap;
Modes B has appreciably less distinguishable information even for the private
reference. Longer integration crosses different parts of the fade/recovery,
and its actual duration is 266.8 s rather than exactly twice 139.2 s. It must
not be described as a matched channel-energy experiment. These shadow messages
are `e`; the longer native mode test is `A`, so their selected-bit IQ is not
claimed identical.

### Null and interaction failures remain visible

The white-noise and wrong-key phase-receiver controls produced no false DATA
or messages. In contrast, both canonical nonwhite-noise controls (modes and
O/X Helper) classified all five scored slots as DATA. Their wanted companions
had two false DATA decisions in the physical quiet slots and wrong active bits
(two for modes, one for Helper). None accepted a message or reached correct
physical completion. This confirms that the proper-white-null phase supplement
must not simply be enabled on these inputs; it does not estimate a natural
false-alarm rate or establish a unique component of the combined noise as the
cause. The separate 7 ms / 30 Hz diffuse control returned three ambiguous
active slots followed by quiet, with no wrong/false DATA or accepted message.

The main remaining receiver questions are broader phase evolution with bounded
model/prior cost, calibration of nonwhite/impulsive-null decisions, and message
survival across physical fades. Existing native block-gain/history statistics
and segmented proposals are plausible starting points, but require explicit
null-model and complete-work validation; adding more historical sinusoidal
templates alone is not a demonstrated general remedy. Measured long-channel,
caustic/strong-scattering, clock-error and broad GUI qualification remain open.
