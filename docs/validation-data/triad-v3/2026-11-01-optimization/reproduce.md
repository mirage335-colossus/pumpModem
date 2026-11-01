# Reproducing the isolated triad GPU study

This guide reproduces selected, bounded experiments from the isolated three-carrier study. It does not alter the ordinary Robust receiver. Follow the study scope and acceptance rules in [the optimization protocol](../../../triad-gpu-optimization.md). The GPU likelihood benchmark consumes a fixed exported candidate bank and disjoint block statistics; it is not blind acquisition, a full sampled-IQ receiver, or an operational false-admission qualification.

## Environment and provenance

Use a clean checkout of the desired revision and record the dirty state before running. The 2026-11-01 evidence was produced with Python 3.12.3, PyTorch 2.8.0+cu128, NumPy 2.5.2 and SciPy 1.16.3; CUDA runs additionally recorded CUDA 12.8 and device/runtime versions in each JSON result. These versions describe the recorded run, not a universal requirement. Install the project's documented build dependencies, build the isolated executable, and use an environment containing the study's Python dependencies:

```sh
git rev-parse HEAD
git status --short
sha256sum tools/triad-long.cpp tools/triad-optimization-study.py \
  tools/triad_phase_frequency.py tools/triad_phase_optimized.py
./build.sh
sha256sum build/dev/triad_long
python3 --version
python3 -c 'import torch, numpy, scipy; print(torch.__version__, numpy.__version__, scipy.__version__); print(torch.version.cuda)'
nvidia-smi
```

The campaign schemas differ: inspect each result JSON and its source/provenance fields instead of assuming every tool records the same metadata. Retain the JSON and any `.scores.npz` sidecar. The maintained fixed input archive contains `inputs/flat` and `inputs/hour`; verify and extract it before replay:

```sh
sha256sum docs/validation-data/triad-v3/2026-11-01-optimization/fixed-inputs.tar.gz
# Expected: d63eb20b1fe7af6197682a7df2f19d966a5efbff4ed7db3146dd9069b7db7c67
mkdir -p /tmp/triad-fixed-inputs
tar -xzf docs/validation-data/triad-v3/2026-11-01-optimization/fixed-inputs.tar.gz \
  -C /tmp/triad-fixed-inputs
find /tmp/triad-fixed-inputs/inputs/hour -type f -print0 | sort -z | xargs -0 sha256sum
find /tmp/triad-fixed-inputs/inputs/flat -type f -print0 | sort -z | xargs -0 sha256sum
```

The `triad-block-stats-v1` manifest explicitly does **not** bind the source or exporter binary: record the exporter source and `build/dev/triad_long` hashes alongside generated inputs. Hash all generated input files, for example:

```sh
find local-evidence/triad-optimization-20261101/holdout-hour-hf-coloured/path-02000 \
  -type f -print0 | sort -z | xargs -0 sha256sum
```

Do not reuse an output directory: the campaign driver requires a new directory and stable global path indices. Preserve raw JSON, archives, and score sidecars; do not bind results only to rounded tables or plot files.

## Preserved source and evidence packages

The maintained directory includes:

- `source-overlay.tar.gz`: the final isolated study sources and reference dependencies, to overlay on base revision `75f3af291100d513a8bc20bff9f26d259b51ed71` in a separate checkout. `source-files.sha256.json` binds each file and the measured exporter binary.
- `fixed-inputs.tar.gz`: exact historical hour/flat input bytes. Their hashes establish identity, not missing exporter provenance.
- `evidence.tar.gz`: 1,479 raw result, score, trace, command, source-snapshot, input-manifest and check files. `evidence-files.sha256.json` verifies every member. It excludes cloud account identifiers and the large sampled-input arrays.
- `artifacts.sha256.json`: hashes of maintained report artifacts, including the three archives.

Extract evidence into a new directory; its roots are `gpu/results`, `pro6000/results`, `mig24/results`, `physical`, `holdout-hour-hf-coloured`, `checks`, `adc-bound`, and related diagnostic folders. `reproduction-drivers/` contains the exact bounded cloud experiment loops, using `/workspace/triad-opt` and `/workspace/triad-env/bin/python`; adapt those two local paths explicitly when replaying elsewhere. Earlier runner versions are retained under `source-snapshots/`, because final source hashes must not be substituted for historical measurements.

```sh
mkdir -p /tmp/triad-evidence-replay
tar -xzf docs/validation-data/triad-v3/2026-11-01-optimization/evidence.tar.gz \
  -C /tmp/triad-evidence-replay
MPLCONFIGDIR=/tmp/triad-mpl python3 \
  docs/validation-data/triad-v3/2026-11-01-optimization/render-hardware.py \
  --evidence /tmp/triad-evidence-replay --output /tmp/triad-hardware-plots
```

The 300-path sampled-input archive remains in ignored local evidence (77,894,068 bytes, SHA256 `d6e237c57d5aa15ed9f1c257ac474b5fc2854665f0d4d81c430e95a9b5730326`). Its exact input-file hashes, generation command, source/binary hashes and seeds are preserved in the maintained evidence archive, and the next section regenerates those paths. Physical converter inputs can be regenerated with `physical/run_screen.py`; its manifest records each exact command. No persistent cloud disk is needed to retain this evidence.

The final documentation/plot tools run locally with Python 3.13.5, NumPy 2.5.4, SciPy 1.18.1, Torch 2.8.0+cpu and Matplotlib 3.11.2. Runtime GPU versions above remain those of the measured CUDA jobs. Building with another compiler or dependency version may change the exporter binary hash; record this as a new reproduction, not the original binary.

## Sampled-waveform holdout paths

The recorded fresh holdout used 300 independently seeded paths, balanced over source bits, with a 1-hour duration and HF-colored oscillator/channel profile. This is a CPU export campaign; it does not itself run a receiver or validate a false-admission tail. A reproducible local command is:

```sh
python3 tools/triad-state-campaign.py \
  --binary build/dev/triad_long \
  --output local-evidence/triad-optimization-20261101/holdout-hour-hf-coloured-repro \
  --paths 300 --seconds 3600 --profile hf-colored --split holdout \
  --cn0 -18 --jobs 4 --start-index 2000 --stats-noisy
```

Use the generated per-path directory as the input to the GPU benchmark, such as `.../holdout-hour-hf-coloured-repro/path-02000`. Keep the remaining paths for independent-path summaries rather than treating multiple batches over one path as independent physical trials. The campaign's seeds are derived from stable path indices; save the generated command and hashes. For a lower-cost smoke run, reduce `--paths` and use a distinct development output directory and split. Do not substitute a smoke run for the holdout.

## Paired GPU timing and score runs

Use exactly the same input path, model options, precision, batch, seed, split, and observation-preparation policy for paired backend comparisons. Run reference and candidate organizations in alternating order over repeated timing reps; keep each JSON and score archive. The v2 B8 strict-FP32 graph record used this exact invocation (replace the input and output paths with local copies of the same hashed input and a new output basename):

```sh
python3 tools/triad-optimization-study.py /path/to/hour-export \
  --device cuda:0 --precision fp32 --batch 8 --memory-gib 12 \
  --warmup-blocks 4 --cn0 -18 --seed 170051 --backend graph \
  --output-scores --output local-evidence/triad-optimization-20261101/graph-b8-repro.json
```

The benchmark input files are `/tmp/triad-fixed-inputs/inputs/hour` from the hash-verified archive above. Change only `--backend reference|optimized|graph` or the declared organization variable being studied. Set `--observation-preparation reference|cached` deliberately when testing that variable; the recorded command above uses the default reference preparation. Preserve the complete family shape and candidate accounting from the JSON. If using `--compare-report`, provide the prior JSON **and** its `.scores.npz`; the script rejects incompatible input/model settings. `--batch` is the number of conditional observations, not extra independent channels. `--actual-observation` scores the exported noisy sufficient projection only when its C/N0 matches; it still is not full sampled-IQ acquisition.

`--precision fp32` enforces strict FP32 inputs/products with TF32 disabled and highest float32 matmul precision, while the finite-state log-evidence reduction remains FP64. `--precision fp64` is the high-precision numerical reference. Neither is a calibrated operational threshold. The JSON's stage timings separate preparation, simulation/RNG, transfer, construction, inference, and verification where applicable; use the recorded complete-job time for end-to-end throughput.

For resident graph-FP64 (strict FP32 bulk plus FP64 reducer) and an 8-batch campaign, the recorded configuration was:

```sh
python3 tools/triad-resident-campaign.py /tmp/triad-fixed-inputs/inputs/hour \
  --output local-evidence/triad-optimization-20261101/resident-graph-fp64-repro.json \
  --backend graph-fp64 --observation-preparation cached --receiver-lifetime campaign \
  --device cuda:0 --precision fp32 --batch 8 --batches 8 --cn0 -18 \
  --seed 170111 --split development --phase-states 512 --relative-phase-states 8 \
  --taps 2 --nominal-dt 4 --frequency-bound .02 --frequency-std .003 \
  --frequency-correlation 1000 --phase-diffusion .00872665 --memory-gib 12 \
  --workspace-memory-mib 512 --cpu-threads 2 --max-blocks 0 \
  --attempt-spacing 60 --duration-alternatives 1 --selection-methods 1
```

The matched equal-work holdout comparison scores 192 observations as 24 batches of 4 for each organization; the graph command consumes reference scores for paired comparison:

```sh
python3 tools/triad-resident-campaign.py /tmp/triad-fixed-inputs/inputs/hour \
  --output local-evidence/triad-optimization-20261101/equal192-reference-b4-repro.json \
  --backend reference --observation-preparation cached --receiver-lifetime campaign \
  --device cuda:0 --precision fp32 --batch 4 --batches 24 --cn0 -18 \
  --seed 170121 --split holdout --phase-states 512 --relative-phase-states 8 \
  --taps 2 --nominal-dt 4 --frequency-bound .02 --frequency-std .003 \
  --frequency-correlation 1000 --phase-diffusion .00872665 --memory-gib 12 \
  --workspace-memory-mib 512 --cpu-threads 2 --max-blocks 0 \
  --attempt-spacing 60 --duration-alternatives 1 --selection-methods 1

python3 tools/triad-resident-campaign.py /tmp/triad-fixed-inputs/inputs/hour \
  --output local-evidence/triad-optimization-20261101/equal192-graph-fp64-b4-repro.json \
  --compare-report local-evidence/triad-optimization-20261101/equal192-reference-b4-repro.json \
  --backend graph-fp64 --observation-preparation cached --receiver-lifetime campaign \
  --device cuda:0 --precision fp32 --batch 4 --batches 24 --cn0 -18 \
  --seed 170121 --split holdout --phase-states 512 --relative-phase-states 8 \
  --taps 2 --nominal-dt 4 --frequency-bound .02 --frequency-std .003 \
  --frequency-correlation 1000 --phase-diffusion .00872665 --memory-gib 12 \
  --workspace-memory-mib 512 --cpu-threads 2 --max-blocks 0 \
  --attempt-spacing 60 --duration-alternatives 1 --selection-methods 1

python3 tools/triad-resident-campaign.py /tmp/triad-fixed-inputs/inputs/hour \
  --output local-evidence/triad-optimization-20261101/equal192-graph-fp64-b12-repro.json \
  --backend graph-fp64 --observation-preparation cached --receiver-lifetime campaign \
  --device cuda:0 --precision fp32 --batch 12 --batches 8 --cn0 -18 \
  --seed 170121 --split holdout --phase-states 512 --relative-phase-states 8 \
  --taps 2 --nominal-dt 4 --frequency-bound .02 --frequency-std .003 \
  --frequency-correlation 1000 --phase-diffusion .00872665 --memory-gib 12 \
  --workspace-memory-mib 512 --cpu-threads 2 --max-blocks 0 \
  --attempt-spacing 60 --duration-alternatives 1 --selection-methods 1
```

The B12 × 8 run has the same observation count but a different batch-shaped random draw order; treat it as equal-count timing, not a paired numerical comparison. The three recorded paired repetitions used seeds 170121, 170122 and 170123 with distinct outputs. For future sweeps, `--batches` accepts up to 32. Batch-size throughput is conditional workload throughput; it does not by itself establish reception probability or cost per target-qualified result. Do not combine results from different channel/oscillator designs into one throughput point.

## Precision and ADC experiments

`tools/triad_precision_probe.py` accepts a sampled `triad-long-basis-v1` export (manifest or directory). Use the exact archived `inputs/flat` export from the verified fixed-input archive above. The recorded input hashes are `manifest.json` `2d6c159101c0d4fca5e3ab5b50f5f0d7a360851950e1afe81b564888fd226ae7`, `basis.c64` `1a9c9f99da156acc550428efa89ea84fcee567a18a6964b366d5053911740025`, and `signal.c64` `cf3cbb9b2d4f3b11f7805a206f1ad4202714810ee326421467acaf5dc8673998`. Verify hashes before reusing it. The so-called FP64 reference uses FP64 arithmetic on the exported complex64 templates; it is not a separately generated FP64 waveform reference. Its fixed-window comparisons use paired actual sample-domain noise, but do not exercise blind acquisition. For a compact paired precision sweep:

```sh
python3 tools/triad_precision_probe.py /tmp/triad-fixed-inputs/inputs/flat \
  --device cuda:0 --trials 1024 --seed 194999 --split holdout \
  --cn0-grid=-1,0,.25,.5,.75,1,1.25,1.5,2 \
  --modes strict-fp64-reference,complex64-strict,fp16-template-storage,bf16-template-storage,fp16-observation-storage,bf16-observation-storage,fp16-products-fp32-output-attempt,bf16-products-fp32-output-attempt,bf16-real-products,tf32-requested \
  --memory-gib 12 \
  --output local-evidence/triad-optimization-20261101/precision-holdout-repro.json
```

Check each mode's recorded dispatch/support status; “requested” does not mean the device actually executed that multiply mode. Observation quantization must be scored on the quantized sample observations. Do not infer a safe policy from average score agreement. In the current campaign, the comparison budgets of 0.1, 0.25, 0.5 and 1 dB remain unresolved; no reduced-precision policy has been selected for the default receiver.

The fixed-family ADC bound can be reproduced separately on a sampled basis export:

```sh
python3 tools/triad_adc_bound.py /tmp/triad-fixed-inputs/inputs/flat \
  --trials 256 --batch 16 --seed 195071 --split holdout \
  --cn0-grid 0,0.5,1,1.5,2 --bits 16 --full-scale 32 512 65536 \
  --output local-evidence/triad-optimization-20261101/adc-bound-holdout-repro.json
```

Its deterministic rounding-error argument yields an ideal-arithmetic union bound for an unclipped, fixed, rank-one, known-window template family; observed ADC rail codes are rejected. It does not certify floating-point roundoff, clipping/saturation error, interference, estimated covariance, AGC, retuning, dither, full acquisition, or the 24-hour operational tail. The campaign's empirical converter screen used actual sampled paths: at 16-bit and the tested high-headroom levels it found very small projection perturbations, while lower full scale and interference produced clipping/overload. Those paired fixed-path projection measurements are **not** a multi-path sensitivity result and do not validate the theorem outside its assumptions.

## Profiling

Use the standalone attribution tool on a bounded workload; its instrumentation perturbs timing:

```sh
python3 tools/triad-profile.py INPUT --output local-evidence/triad-optimization-20261101/profile.json \
  --device cuda:0 --backend graph --batch 8 --blocks 16 \
  --phase-states 512 --precision fp32 --cn0 -18 --seed 170051
```

The JSON and Torch profiler trace attribute CPU and GPU work. Nsight Systems was unavailable in the recorded environment. Nsight Compute was present, but hardware performance-counter access failed with `ERR_NVGPUCTRPERM`; consequently SM/tensor-core utilization, achieved DRAM/cache traffic, and hardware occupancy were not measured. Do not turn allocation size, estimated operation counts, or kernel time into hardware utilization claims. Re-run profiling on a permitted device if those counters are needed.

## Interpretation limits

The isolated GPU campaign does not establish the default operational target (99% correct reception and at most `1e-4` probability of any noise-only admission in 24 hours), nor the relaxed 90%/1% per 12 hours target. The result curves report only the empirical scope and confidence intervals they support. A small fixed-path or zero-event sample cannot establish an extreme false-admission tail. Acquisition-family maxima, repeated attempts, independent physical-channel variation, non-Gaussian front ends and unsupported tail assumptions remain separate qualification work. The 0.1/0.25/0.5/1 dB budgets are sensitivity-comparison goals, not an authorization to change the default receiver.

The final reducer includes a metadata-only correction: avoided widening-copy
traffic is zero for already-FP64 inputs. Its CUDA arithmetic is unchanged from
the measured `08c3446f…` module, whose exact snapshot is preserved. The final
metadata path passed seven CPU checks; it was not rerun on a rental after
cleanup. Native GPU checks and timing claims bind to their recorded prior hash.
