# Frozen broad physical diagnostics

These recipes cover selected physical configurations, not every Cartesian
combination. The input contains 78 proposals. Zero RF shift and a shared
baseband/shift clock remain unsupported topologies; native rate/FHSS rejections,
capture exclusions, receiver-bank exclusions, numerical failures and timeouts
are retained as explicit results. A missing or failed result is not a success.

`make_plan.py` creates exact argument vectors, seeds, job identities, source
hashes and a build/archive binding. It never launches experiments. `run_plan.py`
executes only one explicitly selected stage, with fresh outputs, owned child
process groups and no automatic scientific retries. `analyze.py` verifies
receipts and score archives, independently recounts strict/relaxed decisions,
and measures post-erasure signal energy for evaluation. Keep the raw `.scores.npz`
files: neither a JSON outcome summary nor rounded printed scores suffice for
later threshold or precision comparisons.

The default system Python may lack NumPy/SciPy. Use the numerical-study
interpreter (`/tmp/triad-weak-env/bin/python` locally), or a remote environment
with NumPy, SciPy and the existing receiver's PyTorch dependencies. Only the
generator and runner are standard-library-only.

## Stages and scientific scope

| Stage | Frozen jobs | Meaning |
| --- | ---: | --- |
| `configure` | 81 | 76 supported-topology proposals plus five erasure schedules; native checks decide eligibility |
| `export` | 178 | Two balanced independent actual-noisy signal paths per proposal, ten paired erasure paths, eight null and eight wrong-key controls |
| `actual` | 356 | Two methods separately score each exact sampled observation |
| `conditional` | 912 | 24 preselected paths × 19 C/N₀ values × two methods; four signal and four null noise draws per job |
| `benchmark` | 16 | Two paths × two methods × CPU/CUDA × FP32/FP64 |

Each method has its own job so one method's numerical failure does not erase
the other's result. The 24 conditional paths are selected before outcomes,
balanced across bit labels, and reuse fixed noise addresses across methods and
C/N₀. These repetitions do **not** provide independent physical paths or 99%
reception qualification. The null and wrong-key physical controls use fresh
seed addresses and include C/N₀ 80 and 200 stress cases; wrong-key controls are
not Gaussian noise-only trials.

The main actual screen uses C/N₀ 20 dB-Hz and a requested eight seconds, with
the source's native minimum pattern/tile geometry determining actual duration.
The internal-wait case requests 32 seconds. This is a failure-localization
screen, not a claim that eight seconds reaches all sensitivity targets.

The physical source retains independent timing, oscillator, spread and
modulation effects. The tight timing setting is 10 ms sync / 1 ms region /
zero offset / 70 ms audio error. The default case explicitly samples arrival
over ±7.035 seconds. Both use a **nominal** arrival/clock/epoch receiver bank;
they do not search those complete uncertainties. This exposes compression
and acquisition failures. A known-alignment diagnostic must explicitly set
the physical prior to `--arrival-prior-seconds 0 0`; substituting hidden truth
as the receiver arrival is not part of this plan.

Static FFT uses the public oscillator CFO envelope times margin, provided it
fits below the retained block-center Nyquist. Otherwise it is marked
`unsupported_receiver_grid`. Gaussian-window uses zero residual common CFO.
Neither method tracks stochastic oscillator trajectories. Preset Doppler
separation remains a stable two-ray parameter; the Gaussian width is zero
except for three explicitly renewed-Gaussian proposals. Source basis taps
retain the preset's effective delay/Doppler parameters.

For active modulation, the core plan raises waveform Fs to at least a power
of two above 64× effective modulation Doppler (or explicit Gaussian width),
and retains the previous higher Fs. The receiver block has 16 samples except
for preserved ultralow-rate block durations. This is a sampling heuristic,
not proof of exact band limitation or adequate compression. Nominal IF is an
oscillator-reference coordinate here, not validated real-audio/VLF placement.

FHSS is still a source envelope model. The five explicit receiver-IQ loss
schedules are none, adjacent dwells straddling a private pattern-tile boundary,
a middle adjacent pair, and seeded approximately 10%/25% schedules. All five
reuse the same two bit-specific physical seed sets. Their 40-second request
rounds to five complete 23-dwell tiles. These are **not** validated RF retune
cycles. Actual lost samples and signal energy must be measured from exported
valid rows/energy; echoes and channel state continue through erased dwells.
Whole-bit Symbol Burst spacing and internal silent tiles are separate cases.

## Freeze and two-case pilot

First run the small recipe checks; they do not run a modem campaign:

```sh
triad_py=/tmp/triad-weak-env/bin/python
triad_repro=docs/validation-data/triad-v3/2026-11-03-broad/reproduction
"$triad_py" "$triad_repro/test_plan.py" -v
```

Create a fresh plan only after runtime inputs have settled. The supplied build
provenance must report passed checks and bind the actual source archive. The
runner additionally verifies the selected binary and nested bounded-check
outcome, child exit, and empty process group. New recipe files can be uploaded
as a small supplement to that archive; their exact contents are also bound by
the plan's individual file hashes.

```sh
triad_root="$PWD"
triad_plan=local-evidence/triad-broad-coverage/plans/core-frozen.json
triad_provenance=/tmp/triad-broad-cloud/build-provenance.json
triad_archive=/tmp/triad-broad-cloud/frozen-final/triad-broad-source.tar.gz
"$triad_py" "$triad_repro/make_plan.py" --root "$triad_root" \
  --build-provenance "$triad_provenance" --source-archive "$triad_archive" \
  --output "$triad_plan"
```

On the execution host, set paths to the verified source tree, matching remote
binary, build provenance and archive. Start with two configure jobs:

```sh
triad_out=local-evidence/triad-broad-coverage/core
triad_binary=build/dev-cli/triad_long
"$triad_py" "$triad_repro/run_plan.py" --plan "$triad_plan" \
  --stage configure --root "$triad_root" --output-root "$triad_out" \
  --binary "$triad_binary" --python "$triad_py" \
  --build-provenance "$triad_provenance" --source-archive "$triad_archive" \
  --run-dir "$triad_out/pilot-configure" --jobs 2 --global-seconds 120 \
  --only-id configure-rate-0.001 --only-id configure-spread-hf-worst
```

Then export the corresponding two paths using a fresh run directory and that
configure receipt. The exact same job identities must never be relaunched:

```sh
"$triad_py" "$triad_repro/run_plan.py" --plan "$triad_plan" \
  --stage export --root "$triad_root" --output-root "$triad_out" \
  --binary "$triad_binary" --python "$triad_py" \
  --build-provenance "$triad_provenance" --source-archive "$triad_archive" \
  --run-dir "$triad_out/pilot-export" --jobs 2 --global-seconds 700 \
  --prior-results "$triad_out/pilot-configure/results.json" \
  --only-id rate-0.001-b0-signal --only-id spread-hf-worst-b0-signal
```

Actual score IDs prepend `actual-` and append `-static-fft-ucb` or
`-gaussian-window-ucb` to an export ID. Pass the export receipt as
`--prior-results`; each method consumes the same workspace. For the remainder
of a stage, explicitly select only still-unexecuted IDs and include all earlier
receipts. A repeated ID, changed prior bytes or mismatched plan is rejected.
For a fresh complete campaign, omit `--only-id`. Run configure, export, actual,
conditional and optional benchmark as separate stages, handing completed
dependency receipts forward. Use at most two local or twelve remote children.
Do not overlap stages or infer that GPU availability makes CPU preparation
parallelism free.

## Resources and interpretation

Core exports are bounded by two million sampled rows, 256 MiB statistics,
256 MiB native workspace and 300 seconds. Scores have 0.5 GiB explicit array
budget and 120 seconds. These are per-child declared bounds, not total process
RSS limits; Python/framework memory and solver workspace are additional.
Each stage also has an explicit wall-time dispatch deadline. The external
cloud owner must enforce the total instance lifetime/spend across stages.

With dimension four, retained arrays use about 424 bytes/block, or roughly
53 MB at two million rows and 16-sample blocks; the declared export bound
still decides eligibility. Two parent diagnostic pilots took approximately
0.35/2.08 seconds to export and 0.98/0.95 seconds to score 1,171/660 blocks.
Those pilots used explicit known alignment and are **not** campaign results.
High-rate blocks can be far more expensive. The complete core's configured
child timeouts sum to about 58 CPU-hours, approximately 4.8 hours at ideal
12-way concurrency before cleanup; actual costs require the selected pilot
and stage receipts. Unsupported and dependent jobs skip explicitly.

The optional `--tier long` is a separate plan: rates .001/.01 Hz ×
Sub9 best/worst × two fresh bits, Each65, GPSDO OCXO, 500 kHz RF, margin one,
850 genuine calendar days and −60 dB-Hz. It has four configure, eight export
and sixteen actual score jobs. Fs is .25/.5 samples/s, block duration 1,000
seconds, export bound 128 million rows/2 GiB and 1,200 seconds. Approximate
retained array volume is 31 MB/path before tails. This long tier intentionally
uses a different sampling/compression choice; it requires relevant refinement
evidence and does not validate renewing fading, arbitrary oscillator tracking,
long-duration null/wrong-key behavior or near-bound reliability. It is never
launched by the core plan.

Analyze only after collecting the selected receipts and all hashed outputs:

```sh
"$triad_py" "$triad_repro/analyze.py" --plan "$triad_plan" \
  --output-root "$triad_out" --receipts "$triad_out/actual/results.json" \
  "$triad_out/export/results.json" "$triad_out/configure/results.json" \
  --output "$triad_out/independent-analysis.json"
```

The strict target is 99% correct and at most 10⁻⁴ noise admissions/day; the
relaxed target is 90% correct and at most 1%/12 hours. The same strict residual
noise-UCB is retained at both gates. Halving the horizon and increasing score
alpha 100-fold lowers the relaxed log gate by `log(200)`. These are
predeclared finite-family thresholds, not empirical qualification or anytime
stopping. The analyzer keeps processing failures in attempted opportunity
denominators (signal failures count as missed opportunities; unresolved
controls prevent qualification), separates unstarted work, and never pools
heterogeneous configurations to manufacture a reliability claim.
