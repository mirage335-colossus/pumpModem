# Reproducing the acquisition study

Start with the smallest check that can distinguish the proposed change.
The [study record](../README.md) separates classification, acquisition,
noise-only admission and wrong-key rejection. The
[development workflow](../../../../triad-development-workflow.md) and
[isolated build stages](../../../../building.md#isolated-three-carrier-research)
define proportionate checks. Large research batches are not mandatory
regressions for a small likelihood, documentation or numerical change.

Run these commands from the repository root. Use a Python environment with
NumPy, SciPy and PyTorch; CPU PyTorch suffices below. Set `TRIAD_PYTHON` when
the system interpreter lacks those dependencies. Every experiment writes to
a fresh scratch directory and uses one BLAS/OpenMP thread.

```sh
triad_python="${TRIAD_PYTHON:-python3}"
export triad_study_repo="$PWD"
export triad_study_scratch="$(mktemp -d "${TMPDIR:-/tmp}/triad-acquisition.XXXXXX")"
triad_repro=docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1

./build.sh test triad --cli --jobs 2 --build-jobs 2
"$triad_python" -m unittest discover -s tests -p test_triad_covariance_reference.py
"$triad_python" -m unittest discover -s tests -p test_triad_group_acquire.py
```

This selects the focused isolated triad group and affected numerical helpers,
not the ordinary Robust receiver's unrelated regression campaign. Select
additional suites when changing their components. These are example commands,
not a claim that a fresh test run accompanied this documentation edit.

For changes to Gaussian-window preparation, the post-campaign cache check used
the following three suites (33 checks total); no new physical campaign was
needed for that exact reuse change:

```sh
"$triad_python" -m unittest discover -s tests -p test_triad_gaussian_window.py
"$triad_python" -m unittest discover -s tests -p test_triad_group_acquire.py
"$triad_python" -m unittest discover -s tests -p test_triad_conditional_sweep.py
```

## Small public basis and cheap classification check

The saved [reference basis plan](inputs/reference-basis-plan.json) contains
the complete scientific argv. Its original binary/output paths are historical.
For a fresh local export, change **only argv[0] and the `--output` value**.
Preserve seeds, key/epoch, timing, waveform, taps, sampling and all other
scientific arguments. The wrapper below records the translated command and
binary hash, then bounds the export to 60 seconds plus bounded cleanup.

```sh
"$triad_python" - <<'PY'
import hashlib, json, os, pathlib, subprocess, sys
repo = pathlib.Path(os.environ['triad_study_repo'])
scratch = pathlib.Path(os.environ['triad_study_scratch'])
plan_path = repo / 'docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction/inputs/reference-basis-plan.json'
original = json.loads(plan_path.read_text())['argv']
argv = list(original)
argv[0] = str(repo / 'build/dev-cli/triad_long')
output_index = argv.index('--output') + 1
argv[output_index] = str(scratch / 'public-basis')
assert all(a == b for i, (a, b) in enumerate(zip(original, argv)) if i not in (0, output_index))
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
(scratch / 'basis-command.json').write_text(json.dumps({
    'argv': argv, 'source_plan_sha256': sha(plan_path),
    'binary_sha256': sha(argv[0]), 'changed_indices': [0, output_index]}, indent=2) + '\n')
subprocess.run([sys.executable, str(repo / 'tools/triad_bounded_run.py'),
    '--cwd', str(repo), '--log', str(scratch / 'basis-export.log'),
    '--status', str(scratch / 'basis-export-status.json'),
    '--timeout', '60', '--max-seconds', '60', '--term-grace', '3', '--', *argv], check=True)
expected = '376f2fe4cfc30b7da313a88bf80ab7cc291b4f774c42cc713c54441a1401f09a'
actual = sha(scratch / 'public-basis/basis.c128')
assert actual == expected, (actual, expected)
print('Matched accepted public basis:', actual)
PY

"$triad_python" tools/triad_bounded_run.py \
  --cwd "$PWD" --log "$triad_study_scratch/confirmation-smoke.log" \
  --status "$triad_study_scratch/confirmation-smoke-status.json" \
  --timeout 60 --max-seconds 60 --term-grace 3 -- \
  "$triad_python" "$triad_repro/gaussian_gpu_confirmation.py" \
  --basis-dir "$triad_study_scratch/public-basis" \
  --sampling-law quadratic --precision fp64 --device cpu --threads 1 \
  --output "$triad_study_scratch/confirmation-smoke.json"
```

The driver defaults to **256 trials per bit**, a wiring/numerical check. It
does not make the fixed million-trial confirmation claim. Its default raw
FP32 mode is deliberately overridden: the primary inference uses quadratic
FP64 only. A changed output location changes the manifest's command metadata;
the basis bytes must still match the expected hash. If they do not, preserve
the discrepancy and diagnose the source/build/platform difference before
comparing against the frozen reference. Do not silently replace the hash.

When reproducing the accepted **fixed-count confirmation**, run a separate
fresh output with the frozen count and seed:

```sh
"$triad_python" tools/triad_bounded_run.py \
  --cwd "$PWD" --log "$triad_study_scratch/confirmation-primary.log" \
  --status "$triad_study_scratch/confirmation-primary-status.json" \
  --timeout 60 --max-seconds 60 --term-grace 3 -- \
  "$triad_python" "$triad_repro/gaussian_gpu_confirmation.py" \
  --basis-dir "$triad_study_scratch/public-basis" \
  --sampling-law quadratic --precision fp64 --device cpu \
  --trials-per-bit 1048576 --batch 4096 --threads 1 --seed 2026110221 \
  --output "$triad_study_scratch/confirmation-primary.json"
```

The accepted run took 7.9847 seconds on one CPU thread. Different hardware may
exceed this 60-second example ceiling; a timeout is an incomplete reproduction,
not an inference result. Inspect the fresh report's source/basis/manifest
hashes, completion, frozen-count flag and four declared confidence bounds.
The experiment is known-sync/key, Off-selective Gaussian classification over
0.125 seconds; it does not test admission or a multi-day receiver.

For the initial independent grids and block8/16/32 comparisons, use
[`run_reference.py`](run_reference.py). Its `--pilot` selects a smaller model
screen; `--validation-grid` selects the saved finer numerical experiment.
Keep those diagnostic screens separate from the fixed primary confirmation.
The authoritative `scipy-final-full.json`, `scipy-final-validation.json` and
`quadratic-fp64-primary.json` hashes are listed in the [study](../README.md).
Older receipts with the incorrect known-channel Q normalization are retained
as superseded evidence, not interchangeable baselines.

## Freeze a full campaign without executing it

The checked [`make_plan.py`](make_plan.py) interface requires explicit saved
input plans for portable reproduction. The following writes plans only:

```sh
"$triad_python" "$triad_repro/make_plan.py" \
  --root "$PWD" --binary "$PWD/build/dev-cli/triad_long" \
  --python "$triad_python" \
  --oracle-plan "$PWD/$triad_repro/inputs/oracle-plan.json" \
  --pilot-plan "$PWD/$triad_repro/inputs/pilot-plan.json" \
  --output-dir "$triad_study_scratch/campaign-plan"
```

Add `--configure-only` to emit just the four cheap configuration jobs. Plan
generation launches neither exports nor scorers. Before execution, inspect
their native configure results, row/byte/memory ceilings, complete source and
binary provenance, fixed search family and independent seed domains. Saved
plans contain historical absolute paths: `--root`, `--binary` and `--python`
can target a verified remote checkout/interpreter. Source fingerprints and
that remote binary's actual build/execution provenance must still be bound;
changing a path string is not proof of matching source or samples.

The full study contains **172 exports and 200 score jobs**, around **114 GB**
of exported arrays. It is a deliberate research campaign, not the routine
test command above. Plan generation and cheap configuration do not authorize
or require executing it. Reuse matching exported observations for model,
reblock, precision and cache comparisons; a different projection basis needs
new projections or retained physical samples. The grouped export has no
cross-group Gram, so independent group-noise resampling is unsupported.
Keep incomplete/failed jobs and selective raw-array retention explicit.

[`analyze_campaign.py`](analyze_campaign.py) audits frozen plans, receipts,
manifest/array bindings and score outputs. Its result is source/input bound;
replaying an archived JSON does not verify missing raw bytes. Preserve all
wrong-bit accepts, misses, null admits and wrong-key admits separately.

## Optional consistency replay and followup recount

[`pattern_consistency.py`](pattern_consistency.py) is a separate experimental
postprocessor of an already completed actual-noisy full-bank result. It keeps
the winner and its selected group fixed, recomputes that group's per-window
scores, and applies an AND guard. It needs the **complete saved grouped
workspace**, not just a manifest or collector inventory. This example uses
one of the four locally verified original retained workspaces and bounds the
replay to 60 seconds:

```sh
"$triad_python" - <<'PY'
import hashlib, os, pathlib, subprocess, sys
repo = pathlib.Path(os.environ['triad_study_repo'])
scratch = pathlib.Path(os.environ['triad_study_scratch'])
saved = repo / 'local-evidence/triad-acquisition-improvement/retained/remote-root/local-evidence/triad-acquisition-improvement'
name = 'paired-high30-each-65-cn8-p120002-b16-signal'
workspace = saved / 'campaign/workspaces' / name
report = saved / 'campaign/scores' / (name + '-r1.json')
required = ['manifest.json', 'group_gram.c128', 'group_observation.c128',
            'received_energy.f64', 'intervals.u64', 'valid_rows.u64']
assert all((workspace / n).is_file() for n in required), 'full retained workspace required'
digest = hashlib.sha256(report.read_bytes()).hexdigest()
driver = repo / 'docs/validation-data/triad-v3/2026-11-02-acquisition/reproduction/pattern_consistency.py'
subprocess.run([sys.executable, str(repo / 'tools/triad_bounded_run.py'),
    '--cwd', str(repo), '--log', str(scratch / 'consistency.log'),
    '--status', str(scratch / 'consistency-status.json'),
    '--timeout', '60', '--max-seconds', '60', '--term-grace', '3', '--',
    sys.executable, str(driver), '--workspace', str(workspace),
    '--full-score', str(report), '--full-score-sha256', digest,
    '--doppler-width-hz', '30', '--output', str(scratch / 'consistency.json')], check=True)
PY
```

The tool records both thresholds, but **`ln(100)` is the frozen primary
rule; zero is diagnostic**. The accepted fresh signal outcome is 15/16 under
the primary rule versus 16/16 for both the base receiver and diagnostic zero
rule. Do not switch the primary threshold after seeing that difference.
This is a window-bit-switch proxy, not authentication or a calibrated
wrong-key Bayes factor. It only removes existing admissions and leaves the
production receiver/defaults unchanged. See the [study](../README.md) for
the preserved base wrong-key failures, fresh control outcomes, extra replay
cost and wide uncertainty.

The [duration plan generator](make_duration_extension.py) also launches
nothing. Supply its frozen parent explicitly when reproducing the plan:

```sh
"$triad_python" "$triad_repro/make_duration_extension.py" \
  --parent-plan "$PWD/local-evidence/triad-acquisition-improvement/campaign-plan/campaign-plan.json" \
  --output-dir "$triad_study_scratch/duration-plan"
```

It enforces the parent's listed source hashes and emits the 240-second stage
plus a separate conditional 480-second plan. Emitting the latter does not
select or execute it. The accepted four-case 240-second result uses actual
source elapsed time 246.4 seconds, compared with 123.2 seconds for the parent;
source tile rounding, complete capture tails and measured channel energy
remain distinct from requested duration. These four paired paths and 16
fresh validation paths are separate populations.

To reproduce the **archived followup audit**, retain the frozen main analysis
and extract the compact followup package under its recorded relative tree.
The following recount uses JSON and score/window NPZs plus collector
inventories; it performs no receiver inference or physical simulation:

```sh
triad_followup="$PWD/local-evidence/triad-acquisition-improvement/followup-retained"
triad_followup_root="$triad_followup/remote-root"
triad_followup_plans="$triad_followup_root/local-evidence/triad-acquisition-improvement"
"$triad_python" tools/triad_bounded_run.py \
  --cwd "$PWD" --log "$triad_study_scratch/followup-audit.log" \
  --status "$triad_study_scratch/followup-audit-status.json" \
  --timeout 60 --max-seconds 60 --term-grace 3 -- \
  "$triad_python" "$triad_repro/analyze_followups.py" \
  --plans "$triad_followup_plans" \
  --base-analysis "$PWD/local-evidence/triad-acquisition-improvement/analysis/main-campaign.json" \
  --evidence-root "$triad_followup_root" --remote-prefix /workspace/triad \
  --build-provenance "$triad_followup_plans/build-provenance.json" \
  --inventory "$triad_followup/retained-inventory.json" \
  --inventory "$triad_followup/metadata-inventory.json" \
  --receipt "$triad_followup_plans/duration-plan/240-export-plan.json=$triad_followup_plans/duration-extension/export-240-run/results.json" \
  --receipt "$triad_followup_plans/duration-plan/240-score-plan.json=$triad_followup_plans/duration-extension/score-240-run/results.json" \
  --receipt "$triad_followup_plans/pattern-consistency/screen-plan.json=$triad_followup_plans/pattern-consistency/screen-run/results.json" \
  --receipt "$triad_followup_plans/pattern-consistency/remaining-plan.json=$triad_followup_plans/pattern-consistency/remaining-run/results.json" \
  --receipt "$triad_followup_plans/final-validation-plan/export-plan.json=$triad_followup_plans/final-validation/export-run/results.json" \
  --receipt "$triad_followup_plans/final-validation-plan/score-plan.json=$triad_followup_plans/final-validation/score-run/results.json" \
  --receipt "$triad_followup_plans/final-validation/guard-plan.json=$triad_followup_plans/final-validation/guard-run/results.json" \
  --final-guard-plan "$triad_followup_plans/final-validation/guard-plan.json" \
  --output "$triad_study_scratch/followup-audit.json"
```

This auditor binds exact archived helper, base-analysis, plan and source
hashes; it is not a generic acceptance tool for an arbitrarily modified new
campaign. It suppresses incomplete strata and retains the original base,
primary `ln(100)` and diagnostic zero decisions separately. The accepted
result is `analysis/followups.json`, SHA-256
`849763dd13f77fac913c62741f441f767ac0433f74d5a4f8c9f029d978045a70`.
The 1,118-member compact package and all 72 members of its separate nine-workspace
raw archive were verified locally. Those raw checks are recorded separately in
`followup-retained/raw-verification.json`; the inventory-backed statistical
audit itself does not read every raw array. Fresh raw representatives were
selected before their outcomes; the additional original wrong-key capture was
selected afterward for diagnosis. A separate full workspace for the fresh
signal rejected by the primary guard was also retained and verified after
that failure, as documented in the study.

## Scratch prior-truncation finding; production unchanged

The retained scratch experiment is
`local-evidence/triad-acquisition-improvement/prior-truncation/`:
`run_prior_truncation_v2.py`, `source_v2/`, and
`results/weak-physical-window-v2/summary.json`. It changes private copied
helpers only. It drops lowest complete prior-eigenvalue clusters while
discarded positive coefficient trace stays within epsilon of the total;
epsilon zero keeps all positive modes. This is not a production CLI flag.

One retained actual-noisy high30/Each65, 8-dB-Hz signal capture was rescored
with block16, a fixed 0.25-second window, all 109 arrival groups, FP32 bulk
and FP64 preparation/finishing:

| Prior trace budget | Max retained rank | Receiver seconds | Single-run ratio vs epsilon 0 | Bit log scores | Max discarded candidate signal trace |
| ---: | ---: | ---: | ---: | --- | ---: |
| 0 | 64 | 105.8643 | 1.00× | 13.75849 / −5.91609 | 0% |
| 0.001 | 52 | 80.0655 | 1.32× | 13.76404 / −5.91558 | 0.07772% |
| 0.01 | 40 | 60.6346 | 1.75× | 13.72502 / −5.84144 | 1.13614% |

Every variant still chose bit 0 and **missed both gates**. The saved miss is
not turned into a success by the speedup. A 1% prior trace budget is not a
1% signal-energy bound—the measured candidate trace loss exceeded 1%—and
neither number bounds sensitivity loss. These one-capture, single-run
measurements motivate a later independent sensitivity/cost comparison, not
default truncation. Production defaults remain unchanged.

In that scratch trace, `factor_trace_summary.group_count=28` counts **tiles**:
`ceil(109/4)=28`, including the final partial tile. It does not mean that 81
arrival hypotheses were pruned. The actual geometry, mixture and paired
comparison retain all **109 groups**. Interpret trace counters against their
producer and full candidate inventory before drawing acquisition claims.
