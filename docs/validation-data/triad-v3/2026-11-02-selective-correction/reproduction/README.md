# Selective-channel correction campaign planner

`make_campaign.py` freezes paired export and score commands. It never launches
them. Its jobs use `triad-lowrate-campaign.py` for the established six-domain
seed derivation and sampled `export-stats --stats-noisy` path, then attach the
new `triad_long` selective-model controls. Run them only against a binary built
from the reviewed source revision; plan metadata records the helper hash and
the binary hash available when the plan is frozen.

The planner compares `legacy-allpass` with `dispersive-dft`, for Each25 and
Each65 modulation, and records one shared Off bypass per geometry. For each
cell, paired path indices reuse source bit, epoch, key, channel, oscillator,
noise, frontend and path seed roots. A public tail floor, computed at the
maximum 0.5 ppm RX clock from the larger legacy IIR decay-plus-planner guard
or dispersive FIR support-plus-guard,
makes the paired captures include the same complete tail. The waveform stages
remain unnormalized; compare pre/post modulation energy as well as decoder
scores.

The corrected DFT delay bank is an explicit **synthetic frequency-selectivity
stress law** when its scale is set to the 0.00075 Hz carrier spacing. The branch
delays then reach about 889 seconds. This is distinct from the example's 5 ms
two-ray excess delay. A short-delay physical channel at this carrier spacing is
expected to have nearly common response: a pure delay needs about 212 seconds
to produce one radian of relative phase between adjacent carriers. The
0.05 Hz Doppler separation determines temporal variation; it does not by itself
create 0.05 Hz frequency selectivity. See the parent study's model discussion
for the full operator and its limits.

## Frozen case families

The short development stage covers sub-9 kHz rates g001/g01/g1, the g001
fast-AM variant, and both earlier high-spread cases (10 Hz with 6 ms and 30 Hz
with 7 ms ray separation/delay). It uses four balanced physical paths per
cell. The fast-AM arm changes ray separation and both modulation rates while
holding Gaussian width fixed, so it is a coupled stress change. The smoke stage
uses only g001, 16 seconds, and two paths per comparison cell. Conditional
scoring checks both listed C/N0 edge values with 16 noise draws
per path and signal/noise controls; those draws are not independent channel
paths and do not establish a rare false-admission tail.

`holdout` can be generated for signal, null, or wrong-key controls with
`--control signal|null|wrong-key`; use the same holdout index range to keep
model/depth/control seed roots paired. It defaults to 64 signal paths and 32
control paths. Use `--cn0` to freeze the selected operating point before
collecting outcomes. Null and wrong-key controls are actual sampled noisy
captures; small control counts cannot establish rare-event false-admission
tails.

`long-development` reproduces the 850-day, −60 dB-Hz, 0.0036 Hz GUI-rate,
0.00075 Hz spacing, 0.05 Hz ray-separation stable-path case with four paths.
`long-replay` uses the old path indices beginning at 80000 and is reserved for
replaying retained legacy seed identities. `long-holdout` starts at a new
holdout index and runs actual-noisy reception for independent paths. The
default is 64 long signal paths and 32 per control when selected; increase
these counts only after measuring
the true export and scoring cost. Even many independent channel paths cannot
empirically establish a 1e-4/day false-admission probability; that requirement
still depends on the calibrated complete search-family model.

All score jobs declare 16 receiver alternatives, 8 duration alternatives, 2
calibration alternatives and 60-second attempt spacing. Development scores
are conditional model screens. Holdout scores consume actual sampled noisy
observations. The nominal timing/epoch bank is a public zero-offset branch,
not blind acquisition. Existing receiver workspaces project a declared
two-ray basis; selective sidebands and hundreds of seconds of dispersive memory
can create receiver/model mismatch. Report correct accepts, wrong-bit accepts,
misses, null admissions, wrong-key admissions, signal energy, retained energy
and confidence intervals separately. Neither model's finite sample is a
universal sensitivity boundary.

## Paired analysis and batched retention

`analyze.py` is the maintained, fail-closed analyzer for paired plans and
completed outputs. It checks source/root/index pairing, full sample calendars,
all expected workspace files and hashes, runner receipts, manifests and saved
score arrays before aggregating correct/wrong/missed/null/wrong-key outcomes.
It records selective-stage pre/post energy separately from noiseless candidate
projection retention and actual noisy receiver decisions. Keep its full output
and bind it to the accepted analysis source and input plan hashes. The analysis
source used for the current 120-workspace audit has SHA-256
`45f4960da40c216c07be8a632182756679a315c5d6ad645506fe102b17b7477e`.

The analyzer now uses `retention_batch.py` by default for the full-statistics
projection diagnostic. It processes every exported block and candidate in
bounded batches (default 256 blocks, 128 MiB owned-array budget), with
complex128/float64 eigensolves and projections. It preserves the scalar
reference's nominal Gram eigenvalue/nullspace policy, fixed candidate over the
whole observation, rank accounting, and left-to-right block-energy totals. It
does not downsample blocks or omit row intervals. Its report binds the batch
module, scalar reference, every input-array hash, complete block count and
calendar. The batched module SHA-256 is
`5ca0cd5fe9d84bf629999a1621cf5c9799d3fd4e6629e5fc8e84a128676989f9`. This is
an analysis implementation change; it does not change the channel samples,
receiver, or scientific acceptance rules.

Six focused unit tests passed. They cover complete scalar/batch report parity
on a seeded fixture with rank-deficient and erased blocks, per-block
projection/rank parity against raw-row references, nullspace/overflow and
mutated-input failure parity, workspace limits, and that the scalar module is
not mutated. The fixture matches all counts, rank histograms, bindings and
non-numeric report fields exactly; its maximum floating projection difference
was 2e-15. Reproduce these checks with:

```sh
python3 docs/validation-data/triad-v3/2026-11-02-selective-correction/reproduction/test_retention_batch.py
```

For one representative high-spread Each65 workspace with 7,922 blocks, the
scalar check took 7.931 seconds and the batched check 0.1628 seconds (48.72×
within that single matched run). This measures this retention helper only. A
separate bounded 120-workspace analysis completed and audited in 43.859
seconds with all receipt/binding checks passing. The earlier attempt to run
the complete scalar analysis took about 20 minutes and exited with status 143
without producing JSON; its cause is unknown, so it is not a completed timing
comparator and no speedup ratio is claimed against it. Avoid rerunning that
large archived analysis solely to compare runtimes. These projection timings
say nothing about decoder sensitivity, correct reception or false admissions.

The six tests are deliberately short; use them when changing the batched
retention math. For a report, run the existing analyzer on the already retained
workspace set and bind the output to its plan/manifest hashes; do not launch a
new receiver campaign to validate a deterministic offline projection rewrite.

Generate a smoke-plan JSON first, inspect it, then freeze the development and
long plans under fresh output paths. Example:

```sh
python3 docs/validation-data/triad-v3/2026-11-02-selective-correction/reproduction/make_campaign.py \
  --binary build/dev-cli/triad_long --stage smoke \
  --output local-evidence/triad-selective-correction/plans/smoke.json
```

Plan JSON includes bounded per-export commands plus score commands; use the
existing `tools/triad_bounded_run.py` / `run_jobs.py` protocol for execution.
Do not treat plan generation or smoke-path command construction as execution
evidence. The source revision and executable hash must be checked again before
running, and outcomes/failed jobs must be preserved. Portable source archives
without `.git` must pass `--source-revision` with the full original Git hash;
the planner records whether the revision came from that argument or local Git.

## Accepted result digests

The independently checked compact reports are maintained beside this guide:

- [`development-findings.json`](development-findings.json), SHA-256
  `3b31a9d3650abd41c2671e122496766ed1baa4614cfb2c8594688109a1b7244a`.
- [`holdout-summary.json`](holdout-summary.json), SHA-256
  `6be1f6c5f526136ee60db57c8486ae7b3546c95b000eb5ede8b5840069774325`.
- [`oracle-timing-audit.json`](oracle-timing-audit.json), SHA-256
  `fd95fa5ddd4cc59d76ca367afd5001b194bbd1a1c48d6e7cf5a235924401a93f`.
- [`long-summary.json`](long-summary.json), SHA-256
  `d8cff6b148bda39b79d41f805bc850a1b343fdafc7a672fd89a51e2d759e21a9`.
- [`conditional-boundary-summary.json`](conditional-boundary-summary.json),
  SHA-256 `2a18e1eaab192dbff66bc67a88793beef5fcca4ab69b2e59dc3fb5a9fc84da1b`.
- [`benchmark-review.json`](benchmark-review.json), SHA-256
  `acb0a1f9abff8768ce1a13ed0543eb1adfbdef656168f992f89496bafa963f9a`.
- [`compute-build-receipt.json`](compute-build-receipt.json), SHA-256
  `7d34ee055aee21a1f974bfae38b4e444b673b7ea58c5c82aebbee61bb1b66c8b`.

## Completed 850-day sample test

The accepted long audit ran a finite −60 dB-Hz, 850-day stable-path sample
case using the complete channel and selective-filter tails. In the paired
replay, legacy and corrected Each25 and Each65 all produced 4/4 correct
admissions at both strict and relaxed gates. A fresh corrected-model set also
produced 4/4 at each depth. Do not pool these independent and paired groups.
Each 4/4 result has only 2/2 paths per bit; the source-bound equal-bit
one-sided 95% lower bound is 47.3%. Fresh null and wrong-key controls had 0/2
admissions for each depth/gate, whose one-sided upper bound is 77.6%. The
result demonstrates finite-model feasibility, not a calibrated sensitivity
boundary, 90%/99% reliability, minimum duration, or an operational false-alarm
rate.

The full source-bound summary separates received signal energy from the
receiver's noiseless nominal-basis projection. Across four replay paths,
corrected Each65 post/pre signal energy was 1.246 while the correct-label
projection retained 0.693 of post-stage energy (0.864 of pre-stage energy).
Legacy Each65 was 1.245, 0.726 and 0.904 respectively. Despite the lower
corrected projection fraction, the bounded receiver admitted all four correct
bits; the small sample does not establish why. Two noiseless 0.25/0.5-Hz
sample-rate refinements changed correct projection by about −0.0042, but their
stochastic continuous-time paths/noise and host binaries were not coupled, so
they are descriptive checks rather than convergence evidence.

## Conditional boundary screen (diagnostic only)

The accepted [conditional boundary summary](conditional-boundary-summary.json)
reconciles 120 workspaces and 30 GPU cells. Each cell uses four fixed physical
path realizations with 64 conditional noise draws per path at each of four
tested C/N₀ points. The 256 rows per point are therefore clustered repeats,
not 256 independent channel trials; the summary supplies no pooled path-level
confidence interval and is marked `scientific_qualification: false`.

For corrected high30 at +20 dB-Hz, Each25 had 159 correct / 67 wrong / 30
missed admissions in 256 conditional trials at either gate. Each65 had
165 / 91 / 0. At +16 dB-Hz, Each65 had 168 / 81 / 7 at both gates. These
conditional counts expose a serious wrong-bit admission mode at the tested
high-spread points; they are not population error probabilities. Corrected
high10 Each65 remained path-clustered: at +14 dB-Hz there were 64 correct,
zero wrong and 192 missed from 256, with only one of four physical path
strata contributing correct admissions. Timing/frequency/epoch acquisition
remained nominal or unsearched, so the results do not establish a Doppler
limit or general sensitivity floor. The conditional sweep has no wrong-key
controls, and zero null admissions does not calibrate the requested
continuous-operation false-admission rate.

## CUDA scoring benchmark

`benchmark-review.json` is a single-run paired CPU/GPU arithmetic and timing
review. It verifies all six jobs and their frozen inputs, and reports zero
differences in sampled two-bit winners or strict/relaxed admissions for the
tested FP64 and `FP32-min` paths. On an NVIDIA RTX PRO 6000 Blackwell, CUDA
static-FFT method timers were 2.47–4.07× faster than CPU FP64 and Gaussian
method timers 1.04–1.21× faster. Whole-scorer wall time improved only
1.01–1.08×; conditional-noise simulation/UCB consumed 83–93 seconds of the
92–107-second scorer runs, with CPU covariance preparation retained. Jobs ran
sequentially once in fixed order. These ratios are descriptive, not a repeated
benchmark or general GPU speedup guarantee. No full waveform generation,
blind acquisition or long-duration export acceleration was measured.

The `FP32-min` path performs complex64 FFT/GEMM, with TF32 disabled and
score reductions, physical simulation, UCB, whitening and covariance
preparation using FP64 as declared. Across 2,048 trial vectors per comparison,
the maximum scaled score difference was 7.7e-6 and maximum absolute score
difference was 7.72e-5, without a sampled winner or admission change. CUDA
memory figures are framework allocation counters, not whole-process memory.

The byte-preserved [`compute-build-receipt.json`](compute-build-receipt.json)
records the RunPod source archive (9,995,721 bytes), development cache archive
(213,386,044 bytes, 1,030 verified files), and local/remote binary hashes. It
has no transfer-duration telemetry, so no upload time is claimed. The receipt
records the isolated triad build and focused tests as 14/14 passed each;
ordinary Robust receiver integration/regression was not part of this study.
Its `source_file_manifest_sha256` field does not match the SHA-256 of the
manifest at its `evidence_paths.source_manifest` because they identify two
different JSON manifests: the full/private manifest is
`4850e98e66b819a519742fe6c9df2f85b575078577e4b36fa30ce0db082383d5`, while
the public/source-only manifest is
`0820d8c4a07f08dadbb3858d5d3de2e036360f94835c95e1af14feb3e79b4ec4`. The
two manifests have exactly the same 1,759 path/hash/size inventory entries
and both identify source archive SHA-256
`6675cba4b22055f066e6fd53c691f7562bd5852280e61f130fbf10141e5af7a6`. The byte-preserved
[manifest-hash erratum](receipt-manifest-hash-erratum.json), SHA-256
`10e561b5b3c0a52f1ce3cdae3d632e77a26211da1a1d084bdececdef13e34a9d`,
clarifies the receipt labels/reference. Runtime file hashes otherwise
reconcile; this was a provenance naming mismatch, not a source mismatch. The
original receipt remains unchanged. The temporary Pod is stopped and deleted;
the RunPod console showed zero active GPU, CPU, storage and hourly spend
resources.

## Frozen evidence archive and restoration

The full immutable evidence archive is
`local-evidence/triad-selective-correction-final/evidence.tar.gz` (2,388,133,591
bytes, SHA-256
`d1bb149f2fb0d19004102054fbcb5451cd3caf423b5b451063b8d5580bf26ac1`). Its
compact result receipt is [`archive-result.json`](archive-result.json),
SHA-256 `7d6bfa2c1c58eecaec5f3fff8e848f1bac88258fe4f4eca6fc2de428b6668841`.
It records 13,173 retained source files, including 8,610 raw files totaling
3,747,396,672 bytes, with zero omitted. Plans, plan generators, receiver
outputs, score arrays, runner receipts and GPU packages are in this archive;
do not regenerate plans or rerun campaigns just to inspect the accepted
results.

Restore the archived relative tree under the expected ignored evidence root:

```sh
mkdir -p local-evidence/triad-selective-correction
tar -xzf local-evidence/triad-selective-correction-final/evidence.tar.gz \
  -C local-evidence/triad-selective-correction
```

The archived paths then include
`local-evidence/triad-selective-correction/plans/gpu-boundary/`,
`.../analysis/`, `.../gpu-results/` and the saved waveform/workspace trees.
The benchmark reproducer above requires the restored
`gpu-results/benchmark/`, `plans/gpu-boundary/boundary-final/` and the eight
original corrected Each65 workspaces; it consumes them without rerunning GPU
jobs. The 850-day summary inputs are the accepted `long-replay`, `remote-long`
and `remote-controls` audits named in `long-summary.json`. Preserve the frozen
plan hashes and manifest receipts when moving the archive. The copy manifest
and final archive identity are bound by `archive-result.json`.

Key frozen plan hashes are:

- `plans/gpu-boundary/boundary-final/boundary-execution-plan.json`:
  `280411003fe22a8d8079af7ccc397e11ecc7a1fa13c472f513918ba4d6e30797`.
- `plans/gpu-boundary/boundary-final/benchmark-score-jobs.json`:
  `84e03abf933ecc1fd5886131f10d0eb11b63d409c68983c574fccd898b431a9e`.
- `plans/gpu-boundary/remote-long-final/long-execution-plan.json`:
  `d724e1ab43a47a5952a447cbe076e1004e09b3d27b2f3001f7e62339ef07093d`.
- `plans/gpu-boundary/oracle-timing-final/oracle-timing-plan.json`:
  `4495ba1b47de17bcaa6b0032bf1eccea74c78f7239ab5cb1a91204eb4900361b`.

The benchmark reviewer is reproducible only with its retained extraction and
the eight original g1/high10 corrected Each65 workspaces. From repository root,
with the frozen `gpu-boundary/boundary-final/benchmark-score-jobs.json`, set
the single-thread environment and run:

```sh
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 NUMEXPR_NUM_THREADS=1 \
PYTHONDONTWRITEBYTECODE=1 /tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-02-selective-correction/reproduction/benchmark_review.py
```

The script checks the frozen source manifest, plan, package and runner outputs;
it writes a new ignored `analysis/benchmark-review/review.json`. The recorded
input paths use the original `/home/user/___quick/p/_cur/dataPump/pumpModem/`
absolute prefix. A relocated checkout needs those immutable path aliases or
an explicitly reviewed path remapping. Running this script does not rerun the
six benchmark jobs. The maintained review preserves the original `/tmp`
reproducer path; rerunning from this folder changes that path field only.
