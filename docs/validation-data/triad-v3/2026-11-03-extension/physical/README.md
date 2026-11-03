# Bounded physical followup

This is an isolated triad experiment definition, not a routine regression or a
claim of successful reception. It closes selected gaps in the
[broad study](../../2026-11-03-broad/README.md): full timing acquisition for
middle/random missed dwells, real gaps inside a bit, rate/spreading contrasts,
zero translation, a shared endpoint clock, and explicitly independent added
modulation with zero ray separation. Ordinary Robust integration stays outside
scope.

The default paired screen contains ten cells, each with two signal bits and
one control-only paired null. The six scheduling cells are middle dwells11/12,
the frozen random10% and random25% masks, internal waits of three tiles,
rate1200/Added1 and rate36/Added100. Four non-FHSS cells compare None with
explicit added-modulation rate0 or0.1Hz, zero shift with8270Hz oscillator
reference, and5.8GHz shift driven by the baseband reference. The depth is Each65.
The timing bank retains both endpoints and zero over the tight±0.0905s prior;
its3dB spacing is the native random-chip/infinite-RRC approximation, **not** a
finite waveform loss certificate.

The actual paired seed roots and baseline arguments are read from the retained
`acquisition/plan-coarse.json`, not reconstructed from a prose command. The
independent suite predeclares16 balanced signal paths plus one null and one
wrong-key path per cell. These small cells do not qualify99% reception or a
rare daily false-admission rate. Conditional reuse and the three receiver
methods must not be counted as independent receptions.

## Prepare and inspect

`prepare.py` never launches a simulation. Without `--freeze`, the output is a
draft that `run.py` refuses to execute. Freeze only after the native and scorer
changes have passed their applicable review/checks. Source hashes bind the
native inputs and runner separately from the scorer, and the executable hash
is explicit. This is identity binding, not an independent compilation
attestation; retain the actual build command and its output separately.

```sh
STUDY=docs/validation-data/triad-v3/2026-11-03-extension/physical
PARENT="$STUDY/baseline-input.json"
python3 "$STUDY/prepare.py" --parent "$PARENT" --root "$PWD" \
  --binary "$PWD/build/dev-cli/triad_long" --freeze --output FRESH_PLAN.json
python3 -m unittest discover -s "$STUDY" -p test_plan.py
```

The compact checked-in input retains only the two baseline export definitions,
their original definition hashes, and the original plan hash. It requires no
old raw captures to upload. For a cloud rebuild, run the freeze command on that
host **after** its build, binding its own executable hash. Use the same source
and scripts; never edit a frozen plan to substitute a different binary hash.

Use repeated `--cell NAME` for a bounded submatrix. `--suite validation`
selects the independent seed schedule. `--cn0 6` or `--cn0 12` prepares a
lower-power physical replay; it does not rescale old noisy data. `--seconds 80`
prepares a genuine duration change, which may alter the source calendar and
cost. Keep every resulting plan and output root distinct. Resource predictions
below apply only to the default requests.

The additional `--cell max-spreading-rate0p1` case is **opt-in** and leaves all
ten default cell definitions and seed indices unchanged. It selects rate0.1Hz,
Added1000,128samples/s, noFHSS, an8second minimum request, HF-best/Each65,
30MHz shift,1725Hz nominal IF and independent OCXO profiles at margin1. The
two-ray receiver uses0.5ms/0.1Hz taps and128sample blocks. Native geometry
rounds that short request to approximately3,842seconds of source time; its
public tight bank has15timings and the full-calendar CFO bank is about95bins.
These are configuration/resource observations, not measured decode success.
Prepare separate paired plans at `--cn0 -23` and `--cn0 -16`; only a promising
paired result warrants `--suite validation --cn0 -16`. Do not treat the
8second request as8seconds of actual energy allocation.

## Execute separate stages

```sh
TRIAD_PYTHON=/tmp/triad-weak-env/bin/python
"$TRIAD_PYTHON" "$STUDY/run.py" --plan FRESH_PLAN.json \
  --root "$PWD" --binary "$PWD/build/dev-cli/triad_long" \
  --python "$TRIAD_PYTHON" --output-root FRESH_OUTPUT \
  --run-dir FRESH_CONFIGURE --stage configure --jobs 1 --global-seconds 120
```

Then run `--stage export` in a fresh run directory with
`--prior-results FRESH_CONFIGURE/results.json`. Start with
`--only-id loss-middle-paired-00-b0-signal`, then the internal-wait pilot,
before scheduling all exports. Export child bounds are600seconds normally and
900seconds for internal waits. A stage needs enough remaining time for its
full declared child bound plus cleanup; otherwise the job is retained as
`not_started_deadline`.

The `acquire` stage requires the configure and export receipts, supplied with
repeated `--prior-results`. It performs three declared methods on each same
actual noisy observation:

- `window`: the existing62.5ms independent-window model.
- `global`: one constant complex-gain observation, with zero residual CFO.
- `global-frequency`: the same global model with a public CFO bank spanning
  the nominal oscillator bound, maximum spacing `1/(4T)` for the complete
  sampled calendar. The grid is applied to original block correlations before
  reblocking, and every timing/frequency pair enters the normalized mixture.

The frequency reuse approximation has its own enforced0.01dB maximum original
block phase-arc loss. That does not certify the complete continuous-channel
likelihood. No simulator frequency, arrival or energy selects a receiver
hypothesis. Clock-rate and keyed-epoch hypotheses remain zero in this matrix;
successful rows are **not** full multidimensional acquisition qualification.

The strict/relaxed gates preserve1440attempts/day,8duration alternatives and
16method alternatives. The result independently recounts correct admissions,
wrong admissions, misses, null/wrong-key admissions and each actual gate.
Post-erasure signal energy is evaluation-only. The runner inventories raw
outputs, rechecks source/executable identities, retains unsuccessful jobs and
never overwrites or implicitly reruns a previous result. Exit0 means the
selected jobs were accounted for with stable inputs, not that all jobs or
receptions succeeded. Inspect `counts` and every job status.

## Cost and remaining exclusions

The earlier cloud host measured143–146seconds for one455-group,46second FHSS
export (~260MB). Its export loop is CPU work; a GPU scorer does not remove that
cost. The new internal-wait source lasts156.4seconds, suggesting roughly
500seconds/~0.9GB per export before measurement. Rate1200/Added1 has153groups
and44seconds; rate36/Added100 has455groups and54seconds. The complete30-export
paired screen is roughly one serial CPU-hour and6GB under those assumptions.
These are estimates, not new benchmarks. Pilot before choosing concurrency;
default runner concurrency is one, maximum24, with512MiB declared per child.

The unchanged no-loss and cycle-edge full-bank workspaces can be rescored
separately without new exports. They cannot be exactly remasked into the new
loss cases because retained blocks straddle dwell edges and contain no rawI/Q.
FHSS is source-envelope shaping plus receiver I/Q erasure, not physical RF
retuning. Internal waits are actual silent tile intervals; the whole-bit
`--symbol-burst 3` calendar alone would not exercise those gaps.

The default several-second timing range at600fine symbols/s would require
about35,200timings on this3dB grid and exceeds both the native10,000-group cap
and the current scorer's5,000-base-group cap (10,000bit candidates). The
configure stage checks both limits before approving an export dependency.
The plan preserves that exclusion explicitly. Automatic1MHz/30MHz rate
geometry also remains outside this sampled matrix. Do not replace either with
a narrower truth-based bank and label it tested.
