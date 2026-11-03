# Exact physical-campaign recipes

These are byte-for-byte copies of the executed campaign's preparation and
bounded-runner scripts, frozen plans, configure receipts and focused checks.
[`../README.md`](../README.md) contains the results and their limitations.
This directory contains no sampled arrays and is **not a routine regression
suite**. Read or recount the compact results before repeating physical exports.

| Campaign | Frozen plan | Executed runner | Captures |
|---|---|---|---:|
| Broad configurations | [preflight-003/plan.json](preflight-003/plan.json) | `run-v1.py` | 136 |
| Initial long Each25/65 | [long-preflight-001/plan.json](long-preflight-001/plan.json) | `run-v1.py` | 16 |
| Independent duration/spread followup | [long-followup-preflight-002/plan.json](long-followup-preflight-002/plan.json) | `run.py` | 80 |

The sibling `preflight.json` files preserve native configure responses and
original argument arrays. Original absolute paths in these receipts are
historical provenance; the runnable plans use `{ROOT}`, `{BINARY}`, `{PYTHON}`
and `{OUTPUT}` substitutions. `SHA256SUMS` verifies all 12 copied artifacts.
The scripts were not reformatted or updated during promotion.

## Historical source is required

**An exact replay needs the frozen historical source and native executable
archive, supplied separately.** These recipes do not contain that complete
archive. The [verified preservation bundle](../../preservation/README.md),
`triad-fft-evidence.tar.zst` (SHA-256
`48ace219ff5f3306453a1de647dabece9dce70b7ae779cabfc9aa4bfc2011238`),
contains `ops/triad-fft-source.tar.gz` and `ops/triad_long` alongside the raw
captures. The compact result archive is not a replacement for it. Every plan
binds the same 16 source-file hashes and native executable; the runners check
those identities before and after execution. Do not edit their hashes to make
a later checkout pass.

Key identities are:

| Input | Historical SHA-256 |
|---|---|
| `tools/triad_fft_acquire.py` | `f91265b9d0a6915848be62e2b687982b2b07109d534a8487e137c73377d5b94b` |
| `tools/triad_gaussian_window.py` | `b22fe002864922e44bc7e10000672a163353818f610c4882bae5d62cfe1cf883` |
| Native `triad_long` executable | `9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47` |
| Initial executed runner | `58a448ad4eec00747e8fa5a1b62a92623e3ac026e7cebd08a272f101ef0c4893` |
| Followup executed runner | `1a4029091dafb236922c7ed388f9a7efc15e2cd285f5af08a451efb1877479f0` |

After all runs, the FFT receiver gained one report field documenting FP64
whitening/evidence reductions on the selected device. At promotion, its hash
was `d4a3c2cc39e0068e6aa6aab27d7ee8eeff6cc7db72a790868fe166c3f1d65843`.
Removing that single metadata line in memory reproduces the historical
`f91265b9...` hash; no file was altered for this check. This report-only change
does not retroactively change the source identity of the measurements. The
plans intentionally reject the later file despite unchanged numerical work.
A fresh study on a later checkout requires a newly generated plan and new
provenance, while retaining these historical artifacts.

## Replay commands

First check the copies from this directory with `sha256sum -c SHA256SUMS`.
From the repository containing these recipes, set explicit paths to the
separately retained historical source, its matching executable and a compatible
Python numerical environment. All output roots must be fresh:

```sh
triad_recipe="$PWD/docs/validation-data/triad-v3/2026-11-03-fft-timing/physical/reproduction"
triad_source=/path/to/frozen-source
triad_binary=/path/to/frozen-build/triad_long
triad_python=/path/to/numerical-environment/bin/python
triad_results=/path/to/fresh-results-parent

"$triad_python" "$triad_recipe/run-v1.py" \
  --root "$triad_source" --binary "$triad_binary" --python "$triad_python" \
  --plan "$triad_recipe/preflight-003/plan.json" \
  --out "$triad_results/base" --workers 4 --seconds 2700

"$triad_python" "$triad_recipe/run-v1.py" \
  --root "$triad_source" --binary "$triad_binary" --python "$triad_python" \
  --plan "$triad_recipe/long-preflight-001/plan.json" \
  --out "$triad_results/initial-long" --workers 2 --seconds 2700

"$triad_python" "$triad_recipe/run.py" \
  --root "$triad_source" --binary "$triad_binary" --python "$triad_python" \
  --plan "$triad_recipe/long-followup-preflight-002/plan.json" \
  --out "$triad_results/followup" --workers 4 --seconds 2700
```

The runner uses the source tree's `tools/triad_bounded_run.py`, one numerical
thread per child, 300 seconds per export, 900 seconds per score and a maximum
45-minute campaign admission budget. It records actual file/source hashes,
complete attempted denominators and process-group cleanup. `--only-id` selects
an explicitly named pilot without changing the frozen full denominator.
Output reuse and overwrites are refused. No GPU is requested by these plans.

Default limits are 2 million raw rows, 1024 MiB explicit arrays, 4096 CFO bins,
1 million effective groups and tile512. Only the followup's C850-moderate row
allows 4 million rows/4096 MiB. Four such children can consume up to 16 GiB
explicit arrays plus interpreter and allocator overhead. The actual export's
extent, support, memory and full search bank must pass the runner guards before
scoring; no truth pruning or silent narrowing is permitted. FP32 correlation
bulk coexists with explicitly higher-precision preparation and reductions.

## Preparing a new study

Preparation runs configure only, bounded to 60 seconds. The following produces
new receipts rather than reproducing historical hashes byte for byte; paths,
timestamps/elapsed times and source identities can differ. Keep all three
preparation scripts and both runners together because the optional generators
import `prepare.py` and the followup binds its runner files.

```sh
"$triad_python" "$triad_recipe/prepare.py" \
  --root "$triad_source" --binary "$triad_binary" \
  --output "$triad_results/new-base-plan" --max-configure-seconds 60
"$triad_python" "$triad_recipe/prepare_long.py" \
  --root "$triad_source" --binary "$triad_binary" \
  --output "$triad_results/new-long-plan"
"$triad_python" "$triad_recipe/prepare_followup.py" \
  --root "$triad_source" --binary "$triad_binary" \
  --output "$triad_results/new-followup-plan"
```

Review a new plan before execution. The physical model has continuous sampled
channel/oscillator state, but the receiver uses fixed model-informed ray
delay/Doppler hypotheses and fixed clock/epoch/drift alternatives. The declared
integer timing/CFO bank is complete within those assumptions. The 850/425-day
requests round to longer actual calendars; report actual support and retained
energy. These are stable-ray synthetic observations, not field validation of
multi-year propagation or generic channel tracking. Reused contrast seeds are
paired; the followup's fresh domains are disjoint from the earlier campaigns.
Strict/relaxed gates retain their 1440-attempt, eight-duration, 16-method
allocation. Finite controls and signal counts do not qualify the requested
operational reliability or false-admission probabilities.

[`followup-checks.json`](followup-checks.json) preserves the preparation checks:
runner identity, unchanged default behavior on a retained sampled workspace,
actual resource rejection, frozen counts and seed-domain separation. It is
not a receiver performance result. The promotion itself verified exact copy
hashes and parsed all copied Python/JSON files; it ran no simulation.
