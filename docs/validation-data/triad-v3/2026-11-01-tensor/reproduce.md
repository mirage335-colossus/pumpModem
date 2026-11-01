# Reproducing the tensor frequency-contraction study

This guide describes the retained run sequence and evidence boundaries. The source tree was based on commit `ebfe649c1f9c0a5bace48594c982111f64c28f41`, with study files changed in the working tree. Reproduce from the exact source bundle and manifests below; the commit alone does not contain the experimental patch or exported binary.

## Archive and environment

- Scientific evidence archive: [`evidence.tar.gz`](evidence.tar.gz), with its exact SHA-256 in [`SHA256SUMS`](SHA256SUMS). It includes initial/fusion launch bundles, final measured Python sources, all score arrays and traces, manifests, checks and an exact CPU exporter. `evidence/evidence-index.json` hashes the contained source/data components; the archive hash binds the index itself. The roughly 57 MB of compressed physical/holdout numeric inputs are regenerated and byte-checked instead of duplicated here.
- CPU exporter binary (`exporter/triad_long`, originally `build/dev/triad_long`): SHA-256 `88ee2b6b90f6233bd6ec46ac299d77b30b742bba361ac3cc12af0124a46fec0a`. Exporter source `tools/triad-long.cpp`: `8bd208887cc0a0290257b9d09db5b2c733535b78a5429ecd592fbe60c4802711`; the fresh campaign source-input digest is `42a9cd3a3a00a2618a1460a8c8d3e8eaa3972ac1b7f92df1c3f0a7b4f433bd89`. `physical-holdout/campaign.json` retains the complete source-input hash list. C++ compiler was Debian GCC 14.2.0, Release; `exporter-environment.json` records linked libraries. This does not retrospectively bind the historical hour/flat exporter binaries.
- Source hashes: `prepared/source-input-hashes.json`, `prepared/fusion-source-hashes.json`, and each final stage’s `driver.json` `source_sha256`. Those per-stage values are authoritative for the source actually run; they differ across edits/stages. Core final implementation hashes are `triad_phase_tensor.py` `3b1a674ac7ace90ddce72c2773c2d1543bcfe8307d7b65ea965ad03a6525ffda`, `triad-resident-campaign.py` `410264ac9f779f932031f948de1a5c63939cc66894a03ed1faa6f3fdeb753d0e`, `triad-optimization-study.py` `47ee069647c996df2a1011b048d4cc23b875fb94f73fccb1614128d833186d05`, and `triad-path-study.py` `2c57112f61bd377a521a1e3af85d2a31b4d165e5498411d77a4c759f9934340c` (holdout path-scoring revision). Prepared driver hashes: screen `2d44f6a25080e6590b3ed54dcf73fb3624482dc019b4a446d546b586af47a57b`, fusion `c6789116a39e815996e350c2ad154d49482bd34b332f1e91f13a5e5a7f9da894`, validation `0172f982f632e5c27b5efe1aa5eb2d7757b7a35304d4d98f335b03cf672aa174`, noise extension `ee472d106aa58f7932104a000b82a5b63efa1696f7a9ddf128ebb0d3c0ba851b`, physical holdout runner `e268e39199d201f9bd0f8d4f248a50b2ffc3953bd431c069fccbd05ab5a36218`, and long-boundary runner `5a8c32100215af3bd09bd15f966595907d98da6870d834e7266d837e3e369889`.
- Input file hashes: hour manifest SHA-256 `a4187a2d8825a26c2fd6b038042fec57261bde01a5e6d3a21813a88f49cea047`; example fresh path-04000 manifest SHA-256 `dd14f7965f800ce3666455e5042d96b3c4695dfcc31a73c04200c2e7bd7d8934`. All holdout-export hashes are in `prepared/holdout-input-hashes.json`; sampled physical input hashes are in `prepared/physical-input-hashes.json`. Per-path manifests retain each exporter argv and path seeds. The fresh holdout binding is above. Historical hour/flat inputs retain the earlier provenance limitation; current exporter hashes are not evidence of their original producer.
- Environment: container `runpod/pytorch:1.5.0-cu1281-torch280-ubuntu2404`; RTX 5090 (CC12.0), driver 595.91.07, CUDA 12.8, Python 3.12.3, PyTorch 2.8.0+cu128, Triton 3.4.0, NumPy 2.5.2, SciPy 1.16.3; see `environment-technical.log`.
- Prepared scripts and exact arguments are retained under `prepared/`. Run them from the bundle root, where `tools/`, `tests/`, `inputs/`, and `results/` are present. Use the matching CUDA/PyTorch environment and a single `cuda:0` GPU. Each bounded stage writes a driver with commands, return codes, wall times, and source hashes.

## Input construction

The archive retains the exact long-form exporter; rebuild the project with `./build.sh` only when separately validating a replacement binary. The development hour input was exported with the exact argv in `prepared/inputs/hour/manifest.json`: version-3, encrypted/interleaved waveform, HF two-ray channel, 3.6-symbol/s rate plus 10 added symbols, 64-Hz statistics, four-second blocks, 3,600 requested active seconds, `cn0=-24`, key seed 11001, channel seed 21001, epoch 1800000000, and relative oscillator terms phase diffusion `.00872665`, white FM `.002`, random-walk FM `.000001`, flicker FM `.002`, white PM `.01`. The manifest retains the complete argv, model fields, generated file sizes and hashes. The flat microdiagnostic input and its manifest are retained separately under `prepared/inputs/flat/`.

The fresh sampled holdout set contains 128 version-3 HF two-ray hour paths, indices 4000–4127, at `-18 dB-Hz`, with actual sampled noisy observations retained. Each `physical-holdout/path-XXXXX/manifest.json` records its key/channel seeds, exporter argv, oscillator/channel configuration and observation hashes. `prepared/holdout-input-hashes.json` binds the exported files. Use these exact artifacts for reproduction; do not substitute the earlier 300-path development set. Gaussian projection supplies the conditional noise sweeps, projected companion checks and the two-day linear diagnostic. Actual sampled ADC/interference observations are never replaced by that shortcut.

## Run stages

Prepare the CPU inputs before renting a GPU. From the repository root, unpack the retained evidence and build a fresh work directory (the names below must not already exist):

```sh
study_report="$PWD/docs/validation-data/triad-v3/2026-11-01-tensor"
mkdir /tmp/triad-tensor-replay
tar -xzf "$study_report/evidence.tar.gz" -C /tmp/triad-tensor-replay
study_evidence=/tmp/triad-tensor-replay/evidence
study_work=/tmp/triad-tensor-replay/work
mkdir "$study_work"
tar -xzf "$study_evidence/gpu-bundle.tar.gz" -C "$study_work"
python "$study_report/replay-exports.py" --evidence "$study_evidence" \
  --output "$study_work" --binary "$study_evidence/exporter/triad_long" \
  --group physical --jobs 4
python "$study_report/replay-exports.py" --evidence "$study_evidence" \
  --output "$study_work" --binary "$study_evidence/exporter/triad_long" \
  --group physical-holdout --jobs 4
```

The replay helper refuses an unknown exporter hash, fresh-directory collisions, changed physical metadata or any mismatched numeric component. It stores the relocated invocation and generated manifest under `work/replay/`, then restores the original immutable input manifest only after these checks. Its single-path checks reproduced all six numeric files exactly for fresh `path-04000` and nonlinear `hour-adc16-fs32-overload`. The remaining 136 replays have not been rerun merely for packaging; their original generation and hashes are archived. A rebuilt exporter (`./build.sh`) needs separate byte validation before replacing the retained binary.

On the GPU, activate the version-matched environment above. The initial development screen and fusion sequence use their exact respective source snapshots; install the final measured overlay only afterwards:

```sh
cd "$study_work"
python run-screen.py
cp "$study_evidence/prepared/run-attribution.py" .
cp "$study_evidence/prepared/inspect-isa.py" .
python run-attribution.py
tar -xzf "$study_evidence/fusion-overlay.tar.gz" -C "$study_work"
python run-fusion.py
cp -a "$study_evidence/source-final/." "$study_work/"
cp "$study_evidence"/prepared/run-*.py "$study_work/"
cp "$study_evidence"/prepared/inspect-*.py "$study_work/"
python run-validation.py --stage noise --max-seconds 900
python run-noise-extension.py --stage noise --max-seconds 360
python run-validation.py --stage physical --max-seconds 900
python run-physical-holdout.py --max-seconds 900
python run-long-boundary.py --max-seconds 450
python run-validation.py --stage profile --max-seconds 180
python run-validation.py --stage latency --max-seconds 180
python inspect-fused-isa.py
```

Copy `run-attribution.py` into the work root before executing it, because prepared drivers resolve inputs relative to their own file. The actual sequence used that location. Each stage records child argv, return codes, parent wall times and source hashes. Output directories must be fresh. `run-screen.py` already runs native tests and all 216 microcases. `inspect-isa.py`/`inspect-fused-isa.py` require the CUDA toolkit's `nvdisasm`; `run-attribution.py` records the unavailable-counter failure rather than suppressing it.

The measured microdiagnostics snapshot lacks one later descriptive JSON field: the current tree explicitly labels that both FP64-reference operands are converted during each timed call. Its calculations are unchanged; the archived measured source is authoritative for reproducing original file hashes.

`run-screen.py` compares graph-FP64 against `strict-padded`, `tf32`, `tf32x3`, `bf16`, `fp16-scaled`, and `bf16-split2` at batches 1/4/12, with tile widths 64/128/256 where configured. Screen seed is 180301; it processes four noise batches per job. It first runs the CUDA tensor unit tests and 216 microcases (six modes × batches 1/4/12 × tiles 64/128/256 × four posterior patterns). `run-fusion.py` uses B12 × 8 batches (192 total observations = 96 H0/H1 pairs), three development repeats with seeds 180311–180313, and baseline bracketing runs. It also includes unfused split BF16 at 64 columns and fused split BF16 at 128 columns. The two frozen tensor policies for subsequent validation are the fused 64-column variants.

The final fresh-noise stage uses holdout split, noise root 180331, C/N0 values -26, -24, -22, -20, and -18 dB-Hz, B12 × 32 noise batches, and all three fixed policies. The extension uses separate root 180371 at -17 and -16 dB-Hz. Each point has 384 independent noise trials for H0 and 384 for H1; H0/H1 are paired, and the same underlying noise is reused across C/N0. Do not count correlated C/N0 points as independent channel trials or combine the separate roots as independent physical paths.

The latency stage in `run-validation.py --stage latency` is a resident-run first-use measurement with `--batch 1 --batches 1`; that is **two paired H0/H1 observations, not one actual capture**. `physical_capture_seconds` is the simulated hour-design duration. It does not mean that one unique recorded physical capture was acquired for this latency run. The stage uses three paired policy rounds with seeds 180351–180353. All nine latency jobs, three final profile jobs and six long-boundary jobs completed with exit code zero. Both development and final profile traces use B12 and 64 updates, not a full hour.

The fresh physical-holdout stage scores the same 128 actual sampled noisy observations with all three fixed policies, in eight batches of 16 paths, using scorer seed 180361. The distinct `validation-physical-v2` development stress stage covers 10 actual-observation folders, three policies each, at -18 dB-Hz with seed 180341. `run-long-boundary.py` uses the linear two-day baseline input at -28 and -26 dB-Hz, seed 180381, one paired Gaussian-projected diagnostic per level and policy. These two-day trials are diagnostic, not reception-rate estimates.

## Tests and known failures

- `tests/test_triad_phase_tensor.py --device cuda:0` passed in the screen and fusion drivers. It checks fused/separate agreement, including fractional first advection. The microdiagnostic report records 216 executed cases and zero case failures.
- All recorded final noise-v2, noise-high-v2, and physical-v2 child runs returned 0 (15, 6, and 30 runs respectively). The sampled-path driver completed 24 jobs covering 128 paths under three policies. Native validation passed 10 CPU checks, 24 CUDA product checks, 16 fused-product checks and six graph modes. Focused standalone tests passed: resident 6, optimization 4, state 2 and path 4. Python compilation and `git diff --check` passed; no C++ change or ordinary Robust regression run is claimed.
- The initial bounded noise driver attempt failed during parent serialization with `AttributeError: list object has no attribute stage` after only its first child. `driver-failure.json` records the shadowed stage variable and repair. Its lone baseline report is excluded from final timing and independence counts; the corrected run is `validation-noise-v2`.
- The completed noise-v2 SSH transport returned 143 after all 15 child reports had recorded exit code zero; remote process inspection found no remaining Python process. The cause is unresolved. `noise-transport-status.json` preserves that distinction.
- Nsight Compute's single counter probe exited 1 with `ERR_NVGPUCTRPERM`. GPU performance counters were not available, so achieved SM/tensor utilization, occupancy, DRAM/cache traffic and hardware arithmetic throughput were not measured. Nsight Systems was unavailable. PTX/SASS and compiler metadata do not replace those measurements.
- Direct BF16, TF32 and scaled-FP16 modes failed the predeclared development likelihood-error/bias screen and remain unsuccessful candidates. The two passing numerical-screen policies are not operationally calibrated receivers.

The 0.1, 0.25, 0.5 and 1 dB sensitivity-loss budgets remain unqualified. These conditional noise and sampled-path results do not certify rare false admissions, all-channel performance, or universal sensitivity equivalence. See `protocol.md` for the frozen decision rules and model limits.

## Re-rendering

Raw evidence is already arranged for local analysis; no GPU or input regeneration is needed to reproduce the tables and plots. With NumPy, SciPy and Matplotlib installed:

```sh
MPLCONFIGDIR=/tmp/triad-mpl python "$study_report/render.py" \
  --evidence "$study_evidence" --output /tmp/triad-tensor-figures --usd-hour 1.197
python "$study_report/analyse-validation.py" \
  --evidence "$study_evidence" --output /tmp/triad-tensor-figures/validation-analysis.json
python "$study_evidence/prepared/analyse-profile.py" \
  --evidence "$study_evidence" --output /tmp/triad-tensor-figures/profile-analysis.json
```

The summary files preserve exact metric values, report hashes, statistical scopes and absent-stage checks. Figure coordinates use the true C/N0 values; coincident policies are not offset to suggest sensitivity differences. Their paths will reflect the selected extraction/output directories. `SHA256SUMS` binds the maintained report artifacts, and the archive index binds all raw evidence and source snapshots.
