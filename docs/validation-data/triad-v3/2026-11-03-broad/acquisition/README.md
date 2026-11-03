# Public timing-bank recovery followup

This small followup addresses large nominal-arrival failures found by the broad
screen. It reuses the existing grouped-export and calibrated group receiver;
it adds no receiver algorithm. Selection occurred after the broad outcomes,
so these paired diagnostics are not independent reliability confirmation.

Five cells are fixed: rate 360 / Off, HF-worst / Each 65 at rate 360, default-wide
rate 36 / Each 65, and paired forty-second FHSS requests with no loss or dwells 22/23
loss. Each has the original two signal seeds and one null made by changing
only the bit 0 capture's control. Do not count this as three independent paths.
The FHSS experiment represents the transmitter envelope and receiver I/Q
erasures, not physical RF retuning.

The public timing banks include both endpoints and zero. Their maximum steps
come from the native 0.5 dB random-chip/RRC recommendation: 109 groups for the
tight rate 360 cases, 837 for wide rate 36, and 1077 for FHSS Added ×10. That
recommendation is not a finite-pattern loss certificate. A 1 ms step would
be too coarse for the FHSS waveform's 600 fine symbols/second/carrier.
The full finite bank enters the normalized bit likelihood mixture; no
simulator timing is used to choose or prune a candidate.

FHSS exports use 128 samples/block and 2 blocks/window to retain the original
62.5 ms physical likelihood window while keeping each grouped export below
2 GiB. Other cases retain their original 16-sample blocks and 16-block windows.
This projection change is explicit. The scorer also reports the fixed
zero-arrival group's result from the **same** new workspace, using the
full bank's calibration and admission gates. It does not select the best
group for that baseline. Export receipts evaluate total post-erasure signal
energy and calendar agreement with the parent capture, after simulation.

Common CFO, sample-clock rate and keyed epoch are not searched here. The
selected OCXO bounds produce small phase excursions over the chosen windows;
the plan records the worst common CFO cycles per window. The corrected
selective operator and independent physical oscillator draws remain active.
The receiver is still an independent-window Gaussian approximation, with
explicit B=0, rather than the full sampled-channel likelihood.

## Prepare without executing

Use the accepted core plan and independently recounted actual analysis:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-broad/acquisition/prepare.py \
  --root "$PWD" --core-plan CORE_PLAN.json --analysis ACTUAL_ANALYSIS.json \
  --output FRESH_PLAN.json
python3 -m unittest discover \
  -s docs/validation-data/triad-v3/2026-11-03-broad/acquisition -p test_plan.py
```

The physical plan binds the native source/binary build proof separately from
the numerical scorer. After accepting the scorer version, freeze its four
source hashes in a fresh manifest. This allows a reviewed numerical fix to
reuse already exported physical observations:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-broad/acquisition/prepare.py \
  --root "$PWD" --scorer-manifest-only --output FRESH_SCORER.json
```

## Run bounded stages

`run.py` accepts `configure`, `export`, or `acquire`. It requires the original
source archive and successful native build-provenance record; their hashes
must match the immutable plan. It checks source and executable hashes before
and after execution, verifies all reused receipt/output hashes, bounds every
child, and retains failures. No retry or overwrite occurs implicitly.

```sh
TRIAD_PYTHON=/path/to/numerical/environment/bin/python
"$TRIAD_PYTHON" docs/validation-data/triad-v3/2026-11-03-broad/acquisition/run.py \
  --plan FRESH_PLAN.json --stage configure --root "$PWD" \
  --binary "$PWD/build/dev-cli/triad_long" --python "$TRIAD_PYTHON" \
  --build-provenance BUILD_PROVENANCE.json --source-archive SOURCE.tar.gz \
  --output-root FRESH_OUTPUT --run-dir FRESH_CONFIGURE_RECEIPTS \
  --jobs 2 --global-seconds 300
```

For exports, choose a fresh run directory,
set `--stage export`, and add
`--prior-results FRESH_CONFIGURE_RECEIPTS/results.json`. For acquisition,
set `--stage acquire`, provide both configure and export receipts using
repeated `--prior-results`, and add `--scorer-manifest FRESH_SCORER.json`.
The default stage bound is 900 seconds, maximum 1800; children are bounded at
180 seconds. Begin with `--only-id` for one predeclared pilot if resource
behavior is uncertain. Other stages must receive all relevant pilot and
remaining receipts; selected job IDs cannot be silently rerun.

The receiver currently uses one CPU thread per child. Its tiny per-window
GEMMs are not presumed to benefit from a separate GPU process. All fifteen
exports are bounded individually at 2 GiB and 2 million sampled rows; these are
ceilings rather than measured storage/time. A timeout remains incomplete
evidence. A stage exit 0 means its selected jobs were accounted for without
source mutation, **not** that every native job or reception succeeded. Inspect
`counts`, individual status, `acquisition_summary`, and the source-variant hash.

The primary outputs separate correct admissions, wrong admissions, misses,
and null admissions at the existing strict and relaxed gates. Two signals
and one paired null per cell cannot qualify 99% reception or a rare daily
false-admission rate. Wrong-key controls and new independent paths remain
separate experiments.
