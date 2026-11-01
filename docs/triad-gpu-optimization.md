# Three-carrier GPU optimization protocol

This is the frozen comparison protocol for optimizing the isolated three-carrier
v3 study. It defines measurements and acceptance gates before selecting a faster
implementation. It does not change the default receiver, the waveform or wire
behavior, or integrate with the ordinary Robust receiver. The v3 signal,
encryption, clock-derived keystream, interleaving, channel and streaming
contracts remain in the [v3 study guide](triad-v3.md); use its shared sampled
channel models and preserve oscillator, FHSS, echo and filter state across
chunks and hop guards.

## Success definition

The primary objective is at least **5× more useful, end-to-end GPU work per
wall second** than the current GPU implementation on the same declared task
and device where available. Report same-device and cross-device comparisons
separately. A
useful result is a fully verified reception decision that meets the applicable
sensitivity and operational admission gates below. Count a capture or complete
search family once. Do not count pruned, approximate or partially scored
hypotheses as exhaustive work. Report the number of fully scored hypotheses
separately from captures, and include pruning only when its misses against the
exhaustive reference are measured near the sensitivity boundary.

Compare each candidate with the current implementation using identical sampled
observations, keys, templates, nuisance assumptions and search families. Keep
strict FP32 and FP64 reference modes. The operational default is at least 99%
correct reception and at most `10^-4` probability of **any noise-only
admission** in a 24-hour period. Also report the explicitly relaxed target of at
least 90% correct reception and at most 1% probability of any noise-only
admission in 12 hours. Both probabilities cover the full deployed family and
all repeated attempts, not one candidate score or one detector call.

For each target, construct useful-throughput versus sensitivity-loss curves at
approximately 0.1, 0.25, 0.5 and 1 dB relative to the reference with the same
nuisance assumptions. These are comparison budgets only. Label any candidate
using a budget as experimental; do not silently change the default receiver.
Estimate reception thresholds from a prespecified SNR sweep and independent
validation around the crossing. Report confidence intervals on the sensitivity
difference. If the interval cannot resolve a budget, label it unresolved.

Keep these receiver comparisons distinct:

1. **Known-channel waveform discrimination:** sampled waveform and noise with
   the channel and alignment given to both implementations. This is a useful
   numerical and kernel reference, not a receiver sensitivity claim.
2. **Matched unknown-phase/channel reception:** identical observations and the
   same declared nuisance model, rank and noise assumptions for both receivers.
   This isolates implementation loss from model choice.
3. **Full acquisition and operational admission:** complete declared timing,
   frequency, epoch, clock, channel, duration and method family, with the
   maximum and repeated attempts included in one calibrated noise-only event.

Report numerical implementation loss separately from model mismatch and
information discarded by the algorithm. A matched-subspace projection may be
used as a sufficient statistic only while its linear-Gaussian assumptions hold.
Quantization, clipping, AGC, nonlinear interference suppression and similar
processing must be applied to actual sampled observations before comparing
receivers; do not validate those paths using only a projected Gaussian vector.

## Frozen workload ladder

The following workloads establish screening shapes and labels, not new
operational qualification. Reuse archived inputs only when their exact source,
input bytes, models and seeds match the current comparison. The existing
P512/F43 phase-frequency state model uses 512 phase states, 43 frequency
states and 24 amplitude quadrature settings. Screen batch sizes 1, 8 and 32
where memory permits. Record OOM, fallback and unsupported modes as outcomes;
do not silently substitute a smaller batch or different math mode.

| Workload | Required comparison | Interpretation |
| --- | --- | --- |
| Resident numerical kernel | Same cached input and transition tensors; strict FP32 and FP64; candidate layouts and batched operations | Kernel time only; do not call this pipeline throughput. |
| Resident inference | P512/F43/Q24 at batches 1, 8 and 32; include synchronization and output materialization | Measures device-resident model throughput and per-capture latency. |
| Sampled streaming | Same exported path and waveform through the existing streaming path; include a short capture and an actual one-hour or longer coordinate-preserving capture | Measures simulation plus streaming reception. Repeated short captures are not equivalent to an hour/day observation. |
| Exhaustive acquisition | One tractable and one broad finite bank; score every declared timing/frequency/epoch/clock/channel/bit candidate; compare direct, FFT, hybrid and cached forms where supported | Reports complete family size and captures/s as well as hypotheses/s. Approximate front ends need paired exhaustive checks around the sensitivity boundary. |
| Sustained independent trials | Batch independent physical paths with balanced bit values and stable seed-to-path assignment | Measures throughput over changing physical realizations; a batch of noise replicas on one path is conditional evidence only. |
| Long observation | Preserve genuine hour/day absolute sample coordinates and continuous channel/oscillator state through chunk boundaries | Reports total samples, elapsed observation time, wall time, peak memory and streaming factor. |

Use one selected workload as the end-to-end 5× objective and name it before
optimization. Also report the other applicable workloads so a kernel gain does
not hide preparation, transfer, RNG, simulation, scoring or verification cost.
Use the same correctness gate and family size for the baseline and candidate.
If a workload cannot meet 5×, provide its measured stage times and an
Amdahl-style achievable ceiling; do not relax its decision gate.

After shape and numerical screening, prioritize paired cases near the
correct-bit and wrong-candidate boundaries. Cover flat, coupled two-ray and
frequency-selective channels; phase and colored frequency/phase noise; timing,
CFO, clock drift and epoch ambiguity; interleaving, Added Symbols, bursts and
dwell erasures; and known versus estimated noise covariance. Vary representative
rates, bandwidths, block lengths and search sizes without forming a Cartesian
product. Preserve separate known-alignment and exhaustive-acquisition labels
through every result and plot.

The organization screen should include cached templates, FFTs, Grams and
channel bases; direct correlation or GEMM; FFT and overlap-save acquisition;
hybrid coarse/refinement search; real-valued complex decomposition; fused
scoring and reductions; persistent allocations; CUDA graphs or asynchronous
pipelines; and independent-trial multi-GPU sharding. Test search partitioning
when a measured shape or memory limit justifies it. Keep exhaustive family
accounting for every pruning variant. Record unsupported approaches with the
reason; advertised device peaks are not measured candidate performance.

## Timing and resource accounting

For each run record separately and together, where applicable:

- source/template generation, channel-basis construction, FFT/Gram/factor
  preparation, compilation and cache warm-up;
- CPU input preparation;
- waveform/channel simulation and random-number generation as separate time
  entries, even when one routine performs both;
- host-to-device and device-to-host transfers, allocations, graph capture,
  synchronization, and idle gaps;
- acquisition, inference, candidate reduction, calibration/gating and
  independent verification;
- complete-job wall time, host CPU time, GPU-resident time, peak host/GPU
  memory, and sustained streaming factor.

Report kernel-only, resident-pipeline and complete-job results separately.
State warm-up and repetition counts, synchronization method, whether timing
uses CUDA events, and whether any other process shared the GPU. Report single
capture latency (including its chosen preparation amortization), sustained
independent-trial throughput, exhaustive hypotheses per second, and streaming
throughput in samples/s and real-time factor. Separate cold preparation from
cached operation. If preparation is reused, state the reuse count and break-even
job count; do not omit it from single-capture latency. Define **cost per
validated result** as the actual measured compute and storage cost divided by
the number of independently validated, correctly received captures meeting the
same gate. Report uncertainty and zero-result cases as undefined/infinite, not
as zero cost. Keep provider account and billing identifiers in private
operational records, outside this repository.

Record actual profiler observations separately from estimates: GPU model and
partition, driver/runtime/library versions, SM and tensor-core activity,
memory/cache traffic, occupancy, register pressure, achieved arithmetic rate,
kernel launch and synchronization time, CPU preparation, transfers and idle
gaps. Include Nsight Systems/Compute and CUDA event traces when available.
Label operation counts and inferred arithmetic intensity as estimates. If a
counter or profiler is unavailable, name it and leave that field unavailable;
low allocation or a CPU speedup does not establish bandwidth use or GPU
utilization.

Compare RTX 5090, full RTX PRO 6000 Blackwell and appropriate MIG partitions
only when each is available under the current cloud authorization. Record the
actual GPU name and partition profile, host CPU/RAM, precision support and
fallback behavior, transfer limits, run cost and deletion status. A MIG slice
is a partition of compute, cache and memory resources. Do not infer its speed
from VRAM size or advertised sparse/low-precision peak rates. Do not rent until
the runnable, hash-bound experiments are prepared and locally checked.

## Precision and physical input checks

Select precision independently for each important stage. Keep a recorded table
with stage, storage dtype, multiply mode, accumulation/reduction dtype,
implementation location, measured time share, strict-reference difference,
ranking/rank changes, misses, wrong admissions and null admissions. At minimum
consider template energy/normalization, covariance or Gram preparation,
whitening/noise estimation, QR/SVD and rank decisions, long reductions and
likelihood margins, phase/frequency state normalization, threshold/tail
calculations, and long-duration sample/time/phase coordinates.

Verify actual input, multiply and accumulation precision and any backend
fallback. Distinguish quantized templates from quantized observations.
Screen every supported mode among FP64, strict FP32, TF32, FP16, BF16, FP8,
FP4, integer and block-scaled representations. Record unsupported hardware or
library combinations and actual fallback behavior.
Evaluate lower-precision products with FP32 accumulation and short partial
accumulations with higher-precision final reduction where supported. Test
scaling, clipping, saturation and selective recomputation explicitly. FP32
outputs or accumulators do not prove FP32 input accuracy. Record strict FP32,
FP64 and each mixed mode by explicit name; never enable TF32 or another reduced
multiply mode under a strict-FP32 label.

Include plausible 16-bit ADC and DAC paths with declared effective resolution,
full-scale level, headroom, analog noise/distortion, quantizer step, gain,
dither amplitude and placement, clipping and overload recovery. Apply DAC
quantization before the channel; apply ADC quantization after the declared
analog filtering, noise, interference and gain. Include signal/noise level
sweeps, in-band/adjacent/narrowband/impulsive interference, deep fades and
FHSS occupied-hop collisions, retuning errors and guards. Preserve echoes and
filter tails. A 16-bit converter does not prescribe 16-bit DSP arithmetic.

## Calibration, seeds and confidence

Freeze the exact search family before threshold calibration. It includes all
timing, frequency, epoch, clock, drift, channel, bit, duration, receiver-method
and repeated-attempt choices, plus any adaptive coarse/refinement selection.
Noise-only admission means any family member passes during the stated horizon.
For dependent looks, use a justified family bound or empirical calibration of
the complete schedule; do not multiply pointwise error rates as though looks
were independent. Include calibration work and its cost. If a policy or
threshold is selected after inspecting results, reserve a fresh holdout or use
an explicit selection-adjusted analysis.

Use separate deterministic seed domains for development, threshold calibration
and final holdout, and record all root and derived seeds. Assign channel,
waveform/key, oscillator, noise and trial identities independently of batch or
shard position. Use paired observations and identical physical realizations
for numerical comparisons, then use fresh independent waveform, channel,
oscillator and noise seeds for final validation. Report conditional fixed-path
results separately from performance across paths; do not pool correlated SNR
sweeps or reused noise as independent trials. Preserve all misses, wrong-bit
acceptances and null admissions with one-sided confidence intervals.

Zero-event empirical evidence is limited: approximately 29,956 independent
day-equivalent null trials are needed for a one-sided 95% upper bound below
`10^-4` per day; approximately 299 independent 12-hour-equivalent trials are
needed for a 1% bound. These counts do not make mismatched or correlated trials
valid. State any analytic tail assumptions separately. A small zero-event
sample must not be reported as proving either operational target.

## Reproduction record and current status

Every reported point must have a compact machine-readable record containing:

```json
{
  "source_revision": "",
  "dirty_source_sha256": {},
  "input_manifest_sha256": "",
  "command": [],
  "environment": {"os": "", "python": "", "torch": "", "cuda": "", "driver": "", "gpu": "", "partition": ""},
  "workload": {"receiver": "", "search_family": "", "hypotheses": 0, "capture_seconds": 0, "batch": 0},
  "precision": {"storage": "", "multiply": "", "accumulation": "", "tf32": false},
  "seeds": {"development": "", "calibration": "", "holdout": "", "waveform": "", "channel": "", "oscillator": "", "noise": ""},
  "counts": {"correct": 0, "wrong": 0, "missed": 0, "null_admitted": 0},
  "timing_seconds": {"preparation": null, "transfer": null, "simulation": null, "rng": null, "acquisition": null, "inference": null, "verification": null, "complete_job": null},
  "throughput": {"hypotheses_per_second": null, "captures_per_second": null, "samples_per_second": null, "streaming_factor": null},
  "calibration": {"target": "", "family": "", "horizon_seconds": null, "threshold": null, "method": "", "confidence_interval": null},
  "profile_counters": {},
  "cost": {"currency": "USD", "amount": null, "validated_results": null},
  "status": "unmeasured",
  "limits": []
}
```

Missing measurements are `null` or `unavailable` with a reason. Do not replace
unknown values with estimates without labeling them. Preserve source, input,
script, output and profiler-trace hashes; command, seed and environment
versions; unsuccessful configurations; and all failed or incomplete checks.
Retain private billing details outside the repository.

At protocol freeze, the archived GPU studies establish useful workload-specific
acceleration and numerical comparisons but do not provide the end-to-end 5×
campaign, the sensitivity/throughput curves, a calibrated acquisition result
across the stated default and relaxed gates, a complete profiler breakdown, or
the requested 5090/PRO 6000/MIG and cost comparison. Mark each of those
acceptance targets **unmeasured** until new evidence fills it. The 1 November
state-study source snapshot matches the current `tools/triad-long.cpp`, while
its `final-source.sha256` records a different hash for that file; preserve this
as a provenance limitation. The 31 October hash set is historical and differs
from the current checkout in three files. Do not reuse either record as proof
that a new campaign ran against the exact current source and input hashes.

Reproduction entry points and their limits are listed in the [v3 study guide](triad-v3.md).
The maintained [31 October validation record](validation-data/triad-v3/2026-10-31/README.md)
and [1 November state-likelihood record](validation-data/triad-v3/2026-11-01-state/README.md)
remain separate evidence sets; neither is silently promoted by this protocol.

## Recorded campaign

The [1 November 2026 measured campaign](validation-data/triad-v3/2026-11-01-optimization/README.md)
records corrected timings, sampled holdouts, hardware comparisons, failures and
remaining qualification gaps. Its primary conditional performance workload is
the full 929-block hour-path P512/F43/Q24 receiver. Complete operational
acquisition throughput remains unqualified.
