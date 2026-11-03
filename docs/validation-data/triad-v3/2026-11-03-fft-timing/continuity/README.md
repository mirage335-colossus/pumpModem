# Intermediate continuity windows on five retained failures/controls

All **30 receiver jobs** produced valid results: six fixed model choices on
each of five existing captures. Independent review rehashed the 30 restored
input files (218,612,706 bytes), matched all 15 historical baseline reports,
recomputed timing/frequency likelihood mixtures and residual-variance upper
bounds, and recounted both admission gates. The recalculated mixture scores
and Gamma variance bounds had zero discrepancy with the saved reports.
The [review summary](summary.json) preserves outcomes, source/input hashes,
restore provenance and limitations. No waveform or receiver code was changed.

The intermediate windows correct the selected wrong-bit admission and reject
both selected wrong-key controls. **Existing whole-observation reception
already did both.** The selected missed-hop failure remains missed by every
new choice. This supports reusing the existing continuity settings when
appropriate; it does not justify a new receiver algorithm or default.

## Fixed experiment and acquisition coverage

The five post-failure selections are the 6 dB-Hz random25% missed-hop signal,
the 12 dB-Hz None/Each65 wrong-bit signal, one corresponding null, and the
None/Each65 and shared-microwave/Each65 wrong-key signals. Their earlier
results are in the [physical extension evidence](../../2026-11-03-extension/physical/evidence/README.md)
and [scheduling followup](../../2026-11-03-extension/physical/evidence/additional-README.md).
These are reused development cases, not independent validation observations.

Each capture uses the same six combinations: receiver Gaussian coefficient
width **B = 0 or 0.2 Hz**, with **0.5, 2 or 4 second** windows. Four original
31.25 ms blocks are summed into each 125 ms block before scoring. The width
is a receiver covariance parameter; it does not change the physical ray
separation, selective modulation, waveform or noise. Full observed complex
statistics are reused; clean means and simulation energy are not receiver
inputs.

All new choices retain the earlier public CFO bank and complete timing bank:

| Capture class | Timing positions | CFO positions | Effective groups |
| --- | ---: | ---: | ---: |
| Random25% missed-hop signal | 455 | 3 | 1,365 |
| None/Each65 signal, null and wrong-key | 7 | 3 | 21 |
| Shared-microwave wrong-key | 7 | 91 | 637 |

The review checked the complete public parameter lists and selected template
indices against the old frequency-search reports, rather than checking group
counts alone. No winning hypothesis is selected using simulator truth.
The microwave case retains its separately documented **0.02 dB** original
block phase-loss allowance; other cases retain **0.01 dB**. CFO rotation
still occurs at original block timestamps before reblocking, so these
frequency-approximation bounds are unchanged.

## Outcomes and scores

Every table entry is the winning-bit log score. Gates remain **22.72106339
strict** and **17.42274602 relaxed**. In every new model the two signal
captures choose their correct raw bit; the missed-hop signal remains below
both gates. Both wrong-key columns remain below both gates. The null also
remains rejected, with scores from −29.086 to −12.162.

| B, Hz | Window, s | None/Each65 signal | Random25% signal | None/Each65 wrong-key | Microwave wrong-key |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0.5 | 95.598 | −6.296 | 5.266 | 13.567 |
| 0 | 2 | 117.994 | −2.761 | −0.476 | −3.143 |
| 0 | 4 | 126.466 | 1.717 | −7.044 | 1.143 |
| 0.2 | 0.5 | 95.661 | −6.163 | 6.912 | 15.016 |
| 0.2 | 2 | 118.798 | −1.038 | −1.348 | 4.059 |
| 0.2 | 4 | 121.372 | 0.588 | −0.961 | −0.344 |

The original 62.5 ms receiver admitted the wrong bit at score 50.213 and the
two wrong-key signals at 28.068 and 49.796. Its random25% capture selected
the wrong raw bit but rejected it. The existing whole-observation/CFO
receiver already selected the correct None/Each65 bit at 130.238, rejected
both wrong-key controls, and selected but missed the random25% bit at 9.067.
Thus none of the intermediate choices rescues the unresolved admission or
improves the demonstrated outcome over that existing receiver.

The six new repetitions of one signal are not six independent successes;
the six repetitions of each control are not additional independent controls.
The worst new wrong-key score, 15.016, is only 2.407 nats below the relaxed
gate. This selected screen establishes neither rare wrong-key rejection nor
a general correct-reception probability.

## Calibration and causal limits

The review derived both gates from the declared 1,440 attempts/day, eight
duration alternatives, 16 score-method slots and two bit alternatives.
Strict calibration uses the existing stationary proper-complex Gaussian
residual model; each effective timing/CFO group receives its allocated
variance-bound failure probability. No injected/known noise variance is used.
Every reported upper bound was independently checked as residual energy
divided by the corresponding Gamma lower quantile.

The **six models are separate score alternatives**, not a six-model
likelihood mixture. Within each model, the complete timing × CFO bank is
uniformly mixed. All six new choices have exactly identical residual
calibration arrays for a given capture because they share reblock4 and the
same public basis/bank; their score alternatives fit within the declared
16 method slots.

The three historical baselines use different reblocking and frequency
calibration families. This review does **not** infer a joint operational
best-of-nine guarantee merely from `selection_methods=16` and
`calibration_segments=2`. Nor does a fixed Gaussian-noise calculation establish
wrong-key rejection, correct-bit reception, adaptive stopping or unlimited
post-data tuning. The cases themselves were selected after earlier failures.

Reblocking changes the retained projection, residual degrees of freedom and
coefficient-center approximation as well as the coherent window. The new
models also include CFO alternatives absent from the original short-window
and zero-CFO baselines. Consequently this is not a pure window-length
ablation; the old full-CFO report is the relevant acquisition-matched
comparison. The physical sampled channel was unchanged, but a block-center,
independent Gaussian-window model is still not its complete likelihood.

The evidence supports the concern that unnecessarily short independent
channel fits discard useful continuity. It does not show that nonzero B is
needed here: both widths yield the same admissions, and B = 0.2 does not
rescue the missed-hop case. More elaborate inference is not warranted by
this screen alone. Existing whole-observation/CFO processing remains the
simpler successful choice for these stable cases; arbitrary renewed fading
still requires separate evidence.

## Pilot handling, execution and source identity

The first null pilot exited successfully and produced a score, but the
probe's validation harness initially rejected its report because of a
field-name mismatch. The retained score was then revalidated after a
harness-only correction; the pilot was **not rescored**. Its current status
file was rewritten and the other jobs then ran. The original invalid-status
snapshot is absent from the score directory, so that original notice is not
independently recoverable from these files. The reviewed source explicitly
records the revalidation path; the retained pilot output independently passes
the input, bank, outcome and calibration checks. This setup failure is
separate from a receiver failure or an extra statistical trial.

The reported **103.838 s** is the resumed batch clock and excludes the
already-retained **2.216 s** pilot child. Their sum is **106.055 s**, excluding
the harness-debug interval; it is not a complete original end-to-end wall
measurement. Across the 30 unique jobs, child elapsed times sum to 205.996 s
and reported receiver times to 153.167 s. At most two jobs ran concurrently,
each with one CPU thread, a 90 s child timeout, a 600 s global deadline and
a 1 GiB declared array budget. Peak process RSS was not measured.

The new signal/null/HF-control receiver costs were about 0.36–0.49 s;
microwave controls took 1.71–4.38 s and the missed-hop signal 11.49–27.71 s.
The historical runs used a different host/session, so their lower elapsed
times are not a controlled hardware comparison. No GPU speedup is claimed.
Bulk arithmetic is FP32 with explicit FP64 small preparation, whitening and
evidence reductions.

Every score binds the receiver entrypoint SHA-256
`1e060eb53a411513598a01bd84cc204133056cabdab366c255b66d6b6123b104`.
The initial audit-time five-file Python helper closure matched the historical
source-variant closure. The probe did not record all helper hashes immediately
before/after execution; this is narrower evidence than contemporaneous
loaded-code attestation. After that initial audit, a separate worker changed the Gaussian helper from
`0e262ea3...` to `b22fe002...` for batched one-block-window SVD processing.
The coordinator identified this expected concurrent change; these continuity
runs use 4/16/32 blocks per window and were not rerun. The summary preserves
the historical closure and the later variant separately. The restore receipt
binds the original archive and selected file identities; the review rehashed
all 30 restored files rather than relying solely on nominal metadata.

## Archived recipe and optional reproduction

[probe.py](probe.py) is a byte-identical archival copy of the actual local
driver, including its pilot-revalidation handling. Its original relative
directory defaults assume `local-evidence/triad-continuity/probe.py`;
**do not execute this maintained copy directly**. Restored statistics and
the old compact score reports must already exist. For exact source reproduction,
use an isolated checkout with the historical helper closure in the summary;
the original driver itself checks only the entrypoint hash. This optional wrapper
uses a fresh output directory and leaves the preserved originals untouched:

```sh
/tmp/triad-weak-env/bin/python - <<'PY'
import importlib.util
from pathlib import Path
import sys

root = Path.cwd()
path = root / 'docs/validation-data/triad-v3/2026-11-03-fft-timing/continuity/probe.py'
spec = importlib.util.spec_from_file_location('continuity_probe', path)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
probe.ROOT = root
probe.LOCAL = root / 'local-evidence/triad-continuity-reproduction'
probe.RESTORED = root / 'local-evidence/triad-continuity/restored'
probe.SCORES = probe.LOCAL / 'scores'
probe.PYTHON = Path(sys.executable)
probe.RECEIVER = root / 'tools/triad_group_acquire.py'
probe.main()
PY
```

This is an optional bounded research screen, not a routine regression
prerequisite. No scoring was rerun for this independent review. Ordinary
Robust receiver integration and its regression campaign remain outside scope.
