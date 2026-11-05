# Long-duration and Helper boundaries

This note reconciles the 850-day standalone triad study with the scalar
known-waveform calculation for the fixed Helper lead-in. It is a boundary
assessment, not a qualification of multi-day Helper-assisted reception.
Compact numerical inputs and source identities are in
[`bound-evidence/ideal-presence-reference.json`](bound-evidence/ideal-presence-reference.json).

## What the 850-day result does and does not establish

The full sampled campaign in
[`2026-11-03-broad/long_followup`](../2026-11-03-broad/long_followup/README.md)
ran 136 standalone v3 cases: 128 fresh signal observations (four configurations
with 32 each), four noise-only controls, and four wrong-key controls. Each
signal configuration had 16 cases per bit. The calendars were genuinely 850
days long; they were not short captures rescaled to the same energy. These runs
did **not** contain the Helper transmitter or Helper acquisition stage. Their
observed successes therefore cannot establish Helper-assisted acquisition,
Helper-to-DATA clock transfer, or multi-day session recovery.

The 32/32 result per signal configuration implies a pointwise one-sided 95%
lower bound of about 91.1% for the equal-bit mean success probability, assuming
independent trials within each bit stratum and a fixed success probability per
stratum. This bound does not require the two bit strata to have equal success
probabilities: the probability of all successes is `(p0 p1)^16`, and by
AM-GM it is no larger than `((p0+p1)/2)^32`. The separate 16/16 result for
each bit has a one-sided 95% lower bound of about 82.9%. Four null and four
wrong-key controls are diagnostics, not evidence for a rare operational
false-admission rate. This finite seeded evidence is useful for standalone
feasibility in the tested channel law, but does not show a 99% reception
guarantee.

The new
[`multiweek_chunk_equivalence_checks`](../../../../tests/test_triad_v3_channel.cpp)
test requests a 38-day v3 sampled channel stream at effective sample rate
`1/64 Hz`, with a sub-9-kHz-best channel (`0.001 Hz` Doppler, `0.005 ms` delay),
Each65 selective perturbation, and one-day ray-coherence time. It reads the
complete tile-rounded extent, including source and channel-tail samples, once
in 257-sample chunks and once in 4096-sample chunks; it requires exact sample
and validity-mask equality, continuous modulation through the tail, and a
bounded reported channel workspace below 1 MiB. This is a useful multiweek
chunk/state-continuity check, but it has noise disabled and is not a receiver
trial. It does not model oscillator error, Helper transmission or acquisition,
DATA reception, restart/checkpoint recovery, or an operational session.

One earlier sampled-validation result reached 172,871.2 seconds (about two
days); this is not a maximum-duration claim. A separate two-day flat-spread
throughput case processed 11,064,065 samples in 55.308 seconds on its recorded
host. These remain separate results from both
the 38-day deterministic chunk-equivalence check and the 850-day standalone
follow-up. Multi-day checkpoint/restart and operator-facing progress remain
unimplemented. No 38-day chunked **Helper-assisted reception** was demonstrated.
A duration accepted by a planner or a scalar energy calculation is not such a
test.

## Fixed Helper lead-in: favorable presence ceiling

The operating schedule fixes the Helper lead to 95 400-ms dwells (38 seconds):
90 observation dwells (36 seconds), then five detection dwells. The current
calculation uses only that 36-second observation window. At DATA
`C/N₀ = −60 dB-Hz`, the inherited fixed power examples imply:

| Fixed example | Helper `C/N₀` | 36-s Helper `E/N₀` (`ρ`) | Ideal one-candidate `P_D`, `α=10⁻⁴` | Ideal one-candidate `P_D`, `α=.01` |
| --- | ---: | ---: | ---: | ---: |
| EME: 15 kW Helper / 40 W DATA | −34.2597 dB-Hz | 0.0135 | 0.000189 | 0.01531 |
| HF: 40 W Helper / 4 W DATA | −50 dB-Hz | 0.00036 | 0.000111 | 0.01074 |

These are deliberately favorable, single-hypothesis signal-*presence*
references: one known complex waveform, known phase and channel, white complex
Gaussian noise, and no timing/frequency/epoch/model/repeated-attempt search.
They use unit received-energy gain relative to the stated nominal C/N₀.
Selected fading or additional modulation can change realized received energy;
that change must be accounted separately from receiver mismatch and is not
silently credited to these reference numbers.
They are neither measured Helper decoder rates nor DATA bit-error
probabilities. In particular, the probability that a known waveform is present
does not establish the separation of the two keyed DATA patterns. The Monte
Carlo vectors validate this scalar sufficient-statistic calculation only;
they are not waveform or receiver trials.

For one such known candidate, let `ρ=(C/N₀)T` (linear units), use
`Z ~ N(0, 1/2)` under noise and `Z ~ N(sqrt(ρ),1/2)` under signal, and threshold
the standardized score at `Φ⁻¹(1−α)`. Then

```text
P_D = Φ(sqrt(2ρ) − Φ⁻¹(1−α))
ρ_required = 0.5 [Φ⁻¹(P_D) − Φ⁻¹(α)]²
```

For DATA at −60 dB-Hz with unit useful-energy fraction and no outage, this
presence-only reference requires 75.33 days for 90% detection at a **single
test** `α=.01`, 144.71 days for 90% at `α=10⁻⁴`, and 211.50 days for 99% at
`α=10⁻⁴`. The 38-day point has `ρ=3.2832`, yielding only 59.33% or 12.37%
ideal one-candidate presence probability at those two thresholds respectively.
These are energy floors under ideal knowledge, not required or sufficient
durations for the actual triad receiver. Useful energy loss, burst duty cycle,
channel outages, nuisance estimation, bit-pattern distance, search multiplicity
and false-admission accounting can increase required elapsed time. Conversely,
the numbers do not prove that a practical detector must be worse by a specific
amount; that gap needs a sampled receiver experiment.

The fixed lead makes a separate operational distinction: the existing 36-second
observation is far below the ideal presence requirement at these DATA levels
and power ratios. Extending DATA observation to tens or hundreds of days does
not itself lengthen or improve the fixed Helper acquisition window. A different
Helper power allocation, repeated Helper observations, or a different
acquisition architecture would need its own energy and false-admission budget
and fresh validation; the scalar upper reference alone cannot choose among
them.

There is also a severe acquisition-compute scaling reference at the 850-day
span. For an ideal single complex tone at Helper `C/N₀=30 dB-Hz` observed for
36 seconds, the asymptotic unknown-phase frequency CRLB gives sigma
`5.70745e-5 Hz` and a normal 95% half-width of `1.11864e-4 Hz`. This is an
optimistic estimator reference (known amplitude; no Helper line splitting or
channel fitting), not measured estimator coverage. Separately, the rectangular
coherent-integration grid spacing for a 0.22 dB maximum bin mismatch over 850
days is `3.40414e-9 Hz`. The CRLB 95% interval spans approximately 65,723 such
cells. Thus even the strong Helper pilot reference leaves a large fine-grid
search burden over a 850-day coherent interval; coarse-to-fine narrowing and
phase/channel uncertainty must be measured rather than assumed away. These
figures do not imply a required uniform exhaustive search: grid mismatch,
waveform shape, finite observation, prior information, and the estimator all
affect the actual acquisition design.

A separate plan-only HF boundary replay used rate 1.2631579 Hz and reported a
38-second pulse halo against the 38-second lead, with `pulse_halo_fits_lead`
false. The original geometry classifier had accepted the exact-equality case;
independent review found the missing native sample guard, and the repaired
classifier/test added that guard. This is a planner geometry rejection, not a
measured radio margin or evidence about multi-day processing.

## Representation and model limits at long duration

At 18 kHz, exporting every complex FP64 sample plus one validity byte for 850
days would require 1,321,920,000,000 samples and 22,472,640,000,000 bytes
(22.47264 decimal TB), about 10,465 times the 2 GiB fixture limit. The existing
standalone v3 route avoids that raw-file requirement with streamed sufficient
statistics. The default 18-kHz sampled-export profile has a 3,600-second requested-
active-duration input limit; tile rounding can produce a longer actual bit
and capture. Separate source-power and 2 GiB export caps still apply. These
are concrete boundaries of that sampled-export path,
not an information-theoretic limit and not evidence that a multi-day
Helper-aware streaming architecture cannot be built.

There is not yet one validated architecture that jointly provides all of:
multi-rate continuous physical shared-channel propagation, the fixed Helper
lead and its acquisition uncertainty, multi-day state continuity/checkpoint
recovery, and the full DATA receiver search. Existing sampled long runs and
Helper planning/presence calculations cover different pieces. Stitching their
results together as if they were one end-to-end experiment would overstate the
evidence. The next decisive work is a bounded streaming Helper-plus-DATA
prototype with shared oscillator/channel state, explicit outage and erasure
accounting, checkpoint/restart tests, and independent signal/null/wrong-key
cohorts at declared operational false-admission budgets.

## Provenance and correction record

The compact numerical reference preserves selected fields from
`local-evidence/triad-helper-extended/long-limits/result.json` and records its
SHA-256. Its independent review is
`local-evidence/triad-helper-extended/long-limits/REVIEW.md`. The first
unit-error artifact was rejected: days had been treated as seconds. The
`original-unit-error/first-result.json` file is a **reproduction** of that
failed calculation, not the untouched first output. The original output hash
was reported as `8ab91e69947a2aa5840c8da3503345dd50151e0b693bd3c6f0092d4181995dd6`;
it is retained as an agent-reported identity only. The corrected calculation
uses explicit seconds derived from days. The correction validates arithmetic
and the ideal scalar formula, not a modem result.

No runtime tests or receiver runs were performed to prepare this note.
