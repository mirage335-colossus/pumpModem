# Consumer GPU tensor-correlation protocol

This is the predeclared extension of the isolated three-carrier correlation
study. It adds GPU arithmetic paths for consumer cards while keeping the
sampled waveform, clock-addressed encrypted source, interleaving, channel,
oscillator, clock coordinates, coupled channel model and exhaustive receiver
search fixed. Ordinary Robust receiver integration and its unrelated regression
campaign are outside scope. The extension is experimental; it does not change a
receiver default.

## Hardware and arithmetic contracts

The dispatch target is the device's reported CUDA compute capability, not its
product name alone. The CUDA GPU list identifies RTX 2080 Ti as SM75, RTX 3090
Ti as SM86, and RTX 4090 / RTX 4060 Ti as SM89. NVIDIA's architecture guides
describe the format capabilities summarized below. These are hardware
capabilities; a software implementation can be unavailable, unsupported by the
current toolchain, or not verified on the selected device.

| GPU class | Architecture / CC | Dense tensor arithmetic in scope | This study's dispatch boundary |
| --- | --- | --- | --- |
| RTX 2080 Ti | Turing / SM75 | FP16 products with FP32 accumulation; integer INT8/INT4 modes are different arithmetic | Current implementation targets FP16 via the PyTorch CUDA GEMM path. It must report that the selected cuBLAS instruction sequence has not been verified. The maintained Triton tensor route is not advertised below SM80. |
| RTX 3090 Ti | Ampere / SM86 | FP16, BF16 and TF32 products with FP32 accumulation; INT8 products with INT32 accumulation | The explicitly requested FP16/BF16/TF32/INT8 Triton path is eligible after format checks and generated dense MMA inspection. Strict FP32 and FP64 remain available. |
| Cloud RTX 4090 currently available to this campaign | Ada / SM89 | FP16, BF16 and TF32 with FP32 accumulation; FP8 E4M3 and E5M2 with FP32 accumulation; INT8 with INT32 accumulation | This is the actual hardware target presently available for measurement. No throughput result is asserted by this protocol. Use the explicit formats as on Ampere, plus native dense FP8 E4M3/E5M2 only when the installed Triton/toolchain compiles and the generated dense instruction is inspected. |
| Laptop RTX 4090 | Ada / SM89 | Same format family as Ada above | Architecture does not establish power, memory, sustained clock or latency behavior. No laptop measurement is implied by a desktop/cloud RTX 4090 result. |
| RTX 4060 Ti 16 GB eGPU | Ada / SM89 | Same format family as Ada above | Format eligibility does not establish an implementation fits the attached host or benefits from the eGPU link. Measure host-to-device copies and end-to-end compute on the actual host/link before a real-time claim. |

Native floating-point NVFP4/MXFP4 E2M1 tensor arithmetic is a Blackwell-class
capability, outside SM75/86/89. Do not name decoded FP4 values multiplied in
FP16, packed INT4 tensor instructions, or a generic FP32 output as native FP4.
INT4 is an integer dot-product format, not floating E2M1. INT8 here is a
separate explicit policy: scaled/saturated signed integer operands, INT32 dot
accumulation, conversion to FP32, and restored row scale. It must report
saturation, underflow and scale policy.

The implementation labels separate operand storage, multiplication format,
accumulation, output and scaling. `fp16` means rounded FP16 operands and FP32
accumulation/output; `bf16` means rounded BF16 operands and FP32
accumulation/output; `tf32` means explicitly rounded FP32 inputs using TF32
products and FP32 accumulation; `fp8-e4m3` / `fp8-e5m2` use their named input
formats and FP32 accumulation; `int8` uses INT32 accumulation and a named
post-dot conversion. Strict FP32 is a separate reference with TF32 disabled;
FP64 remains an independent sampled-template/product reference. A requested
format with no supported implementation must fail before claiming a timing or
fallback result. `auto` selects an engine for the named format; it does not
select a lower-precision format.

The tensor backend applies power-of-two per-row scaling before conversion,
records input nonfinites, saturation and values rounded to zero, then restores
the combined scale after the product. The prescribed preparation and product
flags must preserve FP32 accumulation for FP16/BF16 paths; FP16 reduced
accumulation and reduced-precision partial reductions are disabled. INT32
overflow is bounded by the declared K limit. Changing scale target, tile,
rounding, output conversion or reduction policy creates a distinct arithmetic
mode and requires a new record. Low-precision outputs do not qualify because
the accumulator/output dtype says FP32.

NVIDIA references: [CUDA compute capability by GPU](https://developer.nvidia.com/cuda-gpus),
[Turing tuning guide](https://docs.nvidia.com/cuda/archive/12.8.0/pdf/Turing_Tuning_Guide.pdf),
[Ampere tuning guide](https://docs.nvidia.com/cuda/pdf/NVIDIA_Ampere_Tuning_Guide.pdf),
[Ada tuning guide](https://docs.nvidia.com/cuda/ada-tuning-guide/index.html),
and [cuBLAS narrow-precision data types and compute modes](https://docs.nvidia.com/cuda/cublas/index.html#narrow-precision-data-types-usage).
The implementation uses the documented [PyTorch 2.8 `torch.mm` FP32-output path](https://docs.pytorch.org/docs/2.8/generated/torch.mm.html)
where eligible and [Triton's dense dot lowering](https://triton-lang.org/main/python-api/generated/triton.language.dot.html)
for the verified CUDA path. On Turing, a successful Triton call alone is not
evidence of tensor instructions; the upstream
[MMAv1 retirement / pre-SM80 fallback note](https://github.com/triton-lang/triton/pull/5066)
is why the Turing implementation is separately labeled as the Torch engine.

## Frozen receiver and input comparisons

All precision modes use the same sampled captures and the same complete bank:
four public bit/epoch candidates, three fixed CFO centers (`−0.01, 0,
+0.01 Hz`), and lags 0–2, for 36 correlation hypotheses per input. Correlation
blocks use 256 receiver samples at 64 Hz; the specified 256-second request is
exported with the source's complete output and tails (337.2 s reported duration,
21,889 samples stored). Only the verified exactly-zero 216-sample template
suffix is removed, leaving 21,673 samples / 338.640625 s and 85 scored blocks. The phase/frequency
receiver retains P512, F43, Q3 flat-channel state and FP32 bulk updates with
FP64 reductions where the graph path is selected. Each hypothesis survives
correlation and inference. There is no coarse pruning or top-K list.

The correlation comparison modes are strict FP64, strict FP32, and one explicitly
selected consumer tensor format at a time. All campaign modes share the same
FP32 state-inference path with FP64 probability reductions; `fp64` does not mean
an entirely FP64 receiver. The selected GPU mode and engine are recorded
with the GPU name, compute capability, software versions, generated-instruction
evidence, tile and scale contract. An unsupported Turing graph-inference path
uses the full eager Torch reference when auto inference is selected; changing
receiver states to force a graph to fit is not allowed. The whole-scan graph
requires the declared memory preflight and reports peak allocated/reserved
memory. It must not lower P512, frequency/quadrature grids, capture blocks or
search family to fit a smaller card. If a card cannot run the fixed full
workspace, report a setup/fit limit and retain the full per-capture reference
comparison where feasible.

The predeclared policy-development capture is the existing `boundary-cn-10-r0`
input from the earlier isolated study, reused only to choose candidate
implementations. It is not counted as fresh validation. The new input generator
uses independent `Random(4729019).getrandbits(64)` roots in key, channel,
oscillator, noise and frontend order, with five globally unique roots for every
capture. It produces 128 flat/none-channel captures: 16 at each C/N0 from −12
through −9 dB-Hz (64 total), 32 signal captures at −8 dB-Hz, and 32 null
captures at −10 dB-Hz. Every input records its exporter command, source and
binary hashes, all roots, manifest hash and hashes of basis, observed IQ and
validity files.

These 128 cases screen a frozen policy on new roots; they do not qualify either
operational tail. In particular, 32 independent null captures cannot establish
an any-noise-only-admission probability of 10⁻⁴ per day or 0.01 per 12 hours.
They also cannot resolve 0.1, 0.25, 0.5 or 1 dB sensitivity-loss budgets. A
policy or threshold selected after inspecting these results requires fresh
roots, or a documented selection correction, before final validation. When
multiple formats are viewed and a single winner is selected, include that
selection in the admission family; do not reuse a threshold as though only one
format had been tried. Thresholds stay at the declared default (99% reception,
≤10⁻⁴ any noise-only admission/day) and separately reported relaxed target
(90%, ≤0.01 per 12 hours); the study does not lower either gate to make a mode
look more sensitive.

## Representation and numerical tests

For each selected format, use identical sampled IQ and separately measure
template and observation representation effects. With the actual backend's
decode path, calculate CPU FP64 products for `TᴴX`, `Q(T)ᴴX`, `TᴴQ(X)` and
`Q(T)ᴴQ(X)`, then compare those with the actual selected GPU product. The
fixed-template-only mode deliberately retains strict template Gram matrices;
it diagnoses the score shift but is not automatically the likelihood of
`Q(T)`. Observation quantization is data-dependent and nonlinear. Score actual
quantized sampled observations; do not use a Gaussian projection shortcut to
stand in for quantized noise.

Retain candidate-score deltas, rank and top-candidate changes, likelihood
margins, nonfinite values, scale-group diagnostics, saturation, underflow,
misses, wrong admissions and noise-only admissions. Tie every comparison to the
exact input-file hashes, backend source hash and format contract. Paired modes
on one waveform improve numerical comparison but add only one physical trial.
Repeated timing scans, columns copied to make a larger batch and GPU graph A/B/A
replays are timings or state-reset checks, not reception replications.

The complete software search family includes every candidate, CFO, lag, epoch,
duration, model alternative, repeated acquisition and the actual set of selected
policies. The campaign currently uses a fixed 36-hypothesis point family and
one acquisition every 60 seconds to compute its experimental threshold. A
single-policy operational threshold may be evaluated only for that frozen
policy; use a union bound or a new, correctly calibrated family if selecting
among several modes. Zero observed events in this screening set do not support a
tiny-tail claim. Conditional flat-channel results do not predict performance
across changing physical channels.

## Measurements and reporting

For every format/device report separately and together: source/input load and
hash; sampled simulation/RNG and AES/template preparation; cold backend and
whole-workspace construction; template and observation packing/quantization;
transfers; tensor product with cached operands; both-pack product; resident
exhaustive correlation; inference; final decisions/verification; one-capture
wall latency; repeated independent-trial throughput; acquisition hypotheses
per second; and cost per trial and per policy-qualified result. State which
costs are excluded or amortized. Separate kernel or synthetic GEMM gains from
useful receiver throughput. The existing cache may reuse a ciphertext/template
only when key, epoch, format, source coordinates and waveform identity match;
pack each fresh observation. Do not infer AES-NI dispatch from CPU flags or
omit AES/template creation from cold-job cost.

Report GPU clock/power state, GPU and host memory, CUDA/Torch/Triton versions,
PCIe or eGPU link when present, CPU, batch/tile, capture length, device-memory
peaks and all stage wall/event timings. Use CUDA events, CPU profiling and
Nsight Systems/Compute where permitted. If hardware counters are unavailable,
mark them unavailable; operation-count or static instruction estimates are not
measured utilization, traffic or occupancy. For laptop/eGPU real-time claims,
measure the actual device/host, transfer path, thermal/power limits and sustained
streaming workload. The desktop or cloud RTX 4090 measurement does not stand in
for a laptop 4090 or attached RTX 4060 Ti.

## Reproduction commands

Run these commands from the repository root with the exact source-bound
`build/dev/triad_long`. The generator's `--plan` validates the frozen source and
exporter fingerprints and prints all 128 cases without writing IQ; omit it to
generate captures in a new directory. It refuses to overwrite existing cases.

```bash
ROOT="$PWD"
STUDY="$ROOT/docs/validation-data/triad-v3/2026-11-01-consumer-gpu"
OUT="/path/to/new-consumer-gpu-run"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4

python3 "$STUDY/generate-inputs.py" \
  --source-root "$ROOT" --binary "$ROOT/build/dev/triad_long" \
  --output "$OUT/inputs" --plan
python3 "$STUDY/generate-inputs.py" \
  --source-root "$ROOT" --binary "$ROOT/build/dev/triad_long" \
  --output "$OUT/inputs"
```

Screen all formats on the one development boundary capture before selecting a
frozen holdout policy. The strict FP64/FP32 correlation controls use the same source IQ and
full search. Save each format run under a new output directory:

```bash
python3 "$STUDY/run-campaign.py" --root "$ROOT" \
  --inputs "$ROOT/local-evidence/triad-fp4-20261101/inputs" \
  --output "$OUT/screen-rtx4090" --stage screen --device cuda:0 \
  --pattern boundary-cn-10-r0 \
  --formats fp16,bf16,tf32,fp8-e4m3,fp8-e5m2,int8 \
  --engine auto --packing triton --fp8-partial --phase-states 512 --max-seconds 1200
```

Use representation diagnostics to separate template-only, observation-only
and joint input quantization; these CPU FP64 represented-product comparisons
are excluded from throughput:

```bash
python3 "$STUDY/run-campaign.py" --root "$ROOT" \
  --inputs "$ROOT/local-evidence/triad-fp4-20261101/inputs" \
  --output "$OUT/diagnostic-rtx4090" --stage diagnostic --device cuda:0 \
  --pattern boundary-cn-10-r0 \
  --formats fp16,bf16,tf32,fp8-e4m3,fp8-e5m2,int8 \
  --engine auto --packing triton --fp8-partial --phase-states 512 --max-seconds 1200
```

Measure the fixed-workspace receiver as a separate stage. The seven repeated
graph replays per mode are timing records for one sampled input, not seven
physical captures. The result stores A/zero-B/A, complete replay wall/event
timings and workspace preparation separately.

```bash
python3 "$STUDY/run-campaign.py" --root "$ROOT" \
  --inputs "$ROOT/local-evidence/triad-fp4-20261101/inputs" \
  --output "$OUT/performance-rtx4090" --stage performance --device cuda:0 \
  --pattern boundary-cn-10-r0 \
  --policies fp64,fp32,tensor:fp16,tensor:bf16,tensor:tf32,tensor:fp8-e4m3,tensor:fp8-e5m2,tensor:int8 \
  --engine auto --packing triton --fp8-partial --phase-states 512 --workspace-mib 8192 --max-seconds 1200
```

After fixing the candidate formats, run the fresh 128-input screening split.
Record exact commands/source hashes and preserve any failed capture/policy row.
This is a screening holdout only; further policy selection requires another
fresh seed stream or a declared selection correction. The 1200-second bound is
an example, not a promise the full matrix will finish; increase it to cover all
128 inputs times every policy, or retain and label an interrupted/partial
index as incomplete. A partial run is not a 128-input holdout result.

```bash
python3 "$STUDY/run-campaign.py" --root "$ROOT" \
  --inputs "$OUT/inputs" --output "$OUT/holdout-rtx4090" \
  --stage holdout --pattern '*' --device cuda:0 \
  --policies fp64,fp32,tensor:fp16,tensor:bf16,tensor:tf32,tensor:fp8-e4m3,tensor:fp8-e5m2,tensor:int8 \
  --engine auto --packing triton --fp8-partial --phase-states 512 --max-seconds 1500
```

Every stage creates a new output directory and writes its index after each
policy/input. The index records source hashes, inputs, policy arguments,
failures and whether rows are measurements or physical trials. Inspect per-run
JSON/NPZ scores before comparing rankings. Use independent new roots for a
confirmatory run after selecting the implementation. The recorded thresholds
are experimental calculations, not operational calibration. Preserve sampled
inputs, test source and run results with the GPU driver's and exporter binary's
exact hashes; keep provider billing/account data outside the source tree.

The executed cloud environment was Linux x86-64, Python 3.12.3, PyTorch
2.8.0+cu128, CUDA runtime 12.8, Triton 3.4.0, NumPy 2.5.2 and SciPy 1.17.1,
with NVIDIA driver 595.91.07. These are observed versions, not a guarantee that
every future package combination has the same lowering. The executed shell
set OMP/OpenBLAS limits to four threads. The workspace script separately fixes
Torch to two threads, as recorded in its result; the environment variables do
not override that explicit setting:

```bash
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4
python3 tools/triad-consumer-hardware.py --device cuda:0 --output "$OUT/hardware.json"
python3 tools/triad-consumer-profile.py --root "$ROOT" \
  --index "$OUT/performance-rtx4090/index.json" \
  --output-dir local-evidence/new-profile-fp32 --policy fp32
python3 tools/triad-consumer-tiles.py --root "$ROOT" \
  --output-dir local-evidence/new-tile-sweep --pack-implementation triton \
  --max-seconds 240
python3 tools/triad-consumer-sustain.py --root "$ROOT" \
  --index "$OUT/performance-rtx4090/index.json" \
  --output-dir local-evidence/new-sustain-fp32 --policy fp32 \
  --repeats 4000 --timeout 180
python3 tools/triad-consumer-sustain.py --root "$ROOT" \
  --index "$OUT/performance-rtx4090/index.json" \
  --output-dir local-evidence/new-sustain-fp16 --policy tensor:fp16 \
  --repeats 4000 --timeout 180
python3 tools/triad-consumer-sustain.py --root "$ROOT" \
  --index "$OUT/performance-rtx4090/index.json" \
  --output-dir local-evidence/new-sustain-fp16-b8 --policy tensor:fp16 \
  --repeats 500 --repeat-observations 8 --timeout 180
```

The profile/telemetry helpers record source hashes and exact arguments; read
their help for the sampling and timeout controls. The profile trace is
instrumented and must not replace unprofiled throughput. The sustain helper
counts repeated processing copies, not independent trials. Its subprocess wall
includes startup/serialization; `complete_inside_main_benchmark_seconds`
includes input loading, baseline A/zero-B/A, preparation, graph A/zero-B/A and
timing replays. The sum of complete replay times isolates the steady replay
calls, including CPU decision finishing. Batch latency is the whole call;
batch time divided by eight is amortized service time, not single-capture latency.

The final technical archive contains an exact exporter binary, producer source
snapshots, final and earlier Python source variants, all failed and completed
result records, NPZ scores, PTX/SASS and traces. Holdout JSON provenance and
every input hash are retained; regenerate its raw IQ using the exact exporter.
The original development raw input is included. To examine archived evidence:

```bash
(cd "$STUDY" && sha256sum -c SHA256SUMS)
mkdir -p "$OUT/unpacked"
tar -xzf "$STUDY/technical-evidence.tar.gz" -C "$OUT/unpacked"
EVIDENCE="$OUT/unpacked/triad-consumer-gpu-evidence"
(cd "$OUT/unpacked" && sha256sum -c "$EVIDENCE/SHA256SUMS")
python3 "$STUDY/analyze.py" --root "$EVIDENCE/stage5/consumer-results" \
  --plot-dir "$OUT/plots" --hourly-rate-usd 0.90
```

The external `SHA256SUMS` paths are relative to this study directory; the
archive's paths are relative to its extraction parent. The current source
snapshot is the manifest identity, not a claim that an uncommitted tree has a
new Git revision. Recreating the archive uses `package-evidence.py`; its
default mode validates selection without writing, and `--build` refuses to
overwrite an existing output.

## Frozen numerical organization

The final consumer comparison explicitly requests fused Triton packing. FP8
uses K32 zero-initialized tensor partials with separate FP32 additions; other
formats retain native accumulation. These are explicit experiment options,
not receiver defaults. FP8 input conversion pre-rounds to the exact FP8 lattice
to avoid the SM89 toolchain's intervening FP16 truncation. Product scale
restoration uses one exact FP64 power multiplication and one FP32 rounding.
Packing retains the reference's three-stage FP32 normalization, which can
double-round at a subnormal boundary and is not advertised as ideal `ldexp`.

The holdout uses fixed balanced bits within each SNR stratum and independent
physical seed roots. Main uncertainty bands are pointwise 95% Hoeffding bounds
for independent bounded observations, not unconditionally exact binomial
intervals. Reused captures and timing repetitions are excluded from counts.
The producer-record compatibility repair recognizes the explicit
`consumer-gpu-input-v1` schema without changing any payload, hash check, DSP
policy, seed or threshold; the original pre-scoring setup failures remain
recorded separately.
