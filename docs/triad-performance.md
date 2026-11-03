# Isolated three-carrier receiver performance regression

Use [`tools/triad-performance.py`](../tools/triad-performance.py) for short,
source-bound performance checks of the experimental sampled FFT acquisition
receiver and Gaussian-window likelihood helper. It compares a fixed source
reference with the candidate on the same machine. It also records independent
hardware-health measurements so a slower CPU, operating system or GPU stack
cannot disappear into a receiver-normalization factor.

This suite does **not** qualify the ordinary Robust receiver, native v3 receiver
throughput, radio sensitivity, or every experimental receiver. The native
`triad_long` executable supplies common physical fixtures; its generation time
is recorded separately from the measured Python receiver. Follow the
[development workflow](triad-development-workflow.md) for functional and
sensitivity tests and the [precision guide](gpu-precision-controls.md) before
changing arithmetic. Historical multi-day and Monte Carlo campaigns remain
research evidence, not routine performance-regression prerequisites.

## Run the bounded suite

Reuse a compatible native build, or build only the fixture exporter:

```sh
./build.sh configure --cli --build-dir build/dev-cli -- \
  -DBUILD_TESTING=ON -DDATAPUMP_COMPILER_CACHE=OFF
cmake --build build/dev-cli --config Release --target triad_long --parallel 2
python tools/triad-performance.py \
  --binary build/dev-cli/triad_long \
  --output local-evidence/triad-performance/run-001.json \
  --device cpu --threads 2 --stress-seconds 5 --budget-seconds 600
```

Use a Python environment containing NumPy, SciPy and PyTorch. The
[CI workflow](../.github/workflows/triad-performance.yml) pins its own numerical
stack; record local package versions rather than assuming a matching version.
CUDA tensor probes additionally use the existing Triton backend when the device
supports the requested format. Unsupported formats are reported explicitly.

Choose a fresh output name: the runner refuses to overwrite previous evidence.
The report contains per-backend records, source and input hashes, raw paired
timings, arithmetic qualification, full-workload estimates and gate decisions.
Sibling backend reports and process logs retain failure details. Fixture caches
are bound to the exporter binary, relevant sources, configuration and content
hashes; mismatched captures are regenerated.

The preferred measurement budget is **five minutes per backend**; the hard
measurement ceiling is **ten minutes**. The default `--budget-seconds 600`
includes hardware probes, fixture preparation, receiver startup, qualification
and measurement. Dependency installation and native compilation are separate.
An external process-group deadline covers work blocked inside native libraries;
timeout is a failed/incomplete run, never a passing abbreviated result.

| Option | Purpose and limit |
| --- | --- |
| `--device cpu`, `cuda`, or `auto` | Measure one backend, or CPU plus an available CUDA backend sequentially. The time budget applies to each. |
| `--profile ci` | Required bounded physical cases plus a multi-block Gaussian helper case. |
| `--profile quick` | One physical diagnostic plus the helper; explicitly incomplete for normal CI coverage. |
| `--profile sustain` | Additional configuration coverage and outer repetitions; the same hard deadline remains. |
| `--hardware-baseline FILE` | Compare raw kernels with a saved, comparable measured profile. An absent or incompatible profile gives `hardware_baseline_unavailable`, not a hardware-health pass. |
| `--target-profile FILE` | Record a measured target profile for future transfer work. This alone does not establish a cross-architecture runtime interval. |

## What is measured

The [case manifest](../tools/triad-performance-cases.json) declares the physical
fixtures, source plans and bounds. Normal coverage includes tight and broad HF
arrival priors, HF 30-Hz/7-ms spread with Each 65% modulation and missed FHSS
dwells, free-running crystal/RF oscillator uncertainty, and a low-rate
−60-dB-Hz point-sampled observation spanning about 850 source days. The extended
profile adds a 5.8-GHz case. These long simulated calendars do not require the
same wall time: the low sample rate is part of that explicit physical model.
They are not compressed high-rate observations or validation of arbitrary
long-term propagation.

Every FFT measurement retains the complete actual sample array, FFT length,
integer timing lattice, masks, template/Gram preparation, rank handling and
edge refinement. Only the number of declared CFO alternatives is abbreviated.
Frequency subsets include the prescribed center/endpoints; the full target
bank remains in the report. Reduced-bank scores are **performance-only** and
must not be interpreted as full-search admissions. Clock-rate, keyed-epoch and
general evolving-phase acquisition are outside this adapter.

The second adapter measures the real Gaussian-window helper on deterministic
algebraic PSD/shared-observation fixtures, including multiple windows and
numerical qualification controls. These fixtures isolate a different
preparation/scoring workload; they are not sampled propagation experiments.

The receiver reference is the immutable commit
`242ea15fa032ee117b9113a7f3cd07cc6e9f0254`. Reference and candidate modules load
in separate persistent interpreters, under the same thread limits, inputs and
harness. Their execution order alternates across outer repetitions. Do not
automatically replace this reference with a newly passing candidate: a baseline
change needs a reviewed comparison and retained evidence. Report the harness,
application source and fixture binary identities separately.

## Hardware and arithmetic qualification

The hardware layer gives each warmed kernel a nominal five-second measurement
window and records first-use/setup costs separately. CPU families cover FP32
and FP64 GEMM, complex64/complex128 FFT, memory copying, phase products and
reductions, and small batched SVD. CPU FP16/BF16 dispatch probes are separate:
a tensor dtype is not proof that specialized tensor instructions executed.
CUDA ordinary arithmetic and explicit tensor-format probes are also separate;
the latter report backend instruction evidence, packing/scaling and wider
arithmetic exceptions. They do not make the FFT receiver tensor-capable.

Use completed CUDA work, not just asynchronous host dispatch time. Retain
process CPU time, wall time, device event time, array sizes and throughput.
CPU process time per wall second is a utilization clue, not an instruction-set
efficiency measurement. Kernel throughput is meaningful for the measured
shape/cache regime; a small cached product or copy does not establish peak
tensor occupancy or DRAM bandwidth. Missing hardware counters stay missing.
One five-second block is not a collection of independent statistical trials.
The optional `nvidia-smi` sampler records device-busy and memory-busy percentages,
clocks, power and temperature during the measured windows. These are device-wide
driver telemetry, not measured SM occupancy, tensor occupancy or per-process
utilization; the driver's averaging windows can overlap the requested window.

The current Torch 2.8/Triton stack on the tested RTX PRO 6000 Blackwell Server
emitted FP16 tensor instructions for strict requested FP8 probes. The instruction
check therefore reports those probes as **unsupported**, with no fallback
throughput attributed to FP8. Device capability or an FP8 input dtype alone does
not establish native FP8 execution. Preserve this explicit result when comparing
profiles from another compiler, driver or GPU.

The receiver chooses between its **implemented FP32 and FP64 policies**, after
comparison with the fixed FP64 reference. Qualification checks numerical
results and discrete decisions before selecting the faster eligible policy;
the choice is frozen before held-out timing measurements. Sensitive
preparation/reductions may remain FP64 and must be reported. Strict FP32 does
not silently enable TF32. Failed numerical agreement is a correctness failure,
not a performance tradeoff. Lower/tensor arithmetic needs a receiver adapter
and its own qualification before it can become eligible here. FP32/FP64
selection is qualified on the declared numerical corpus and a small CFO bank;
all measured larger banks also compare candidate and reference at the selected
precision. This is not universal FP32 equivalence to FP64 for every unmeasured
alignment or receiver. No arithmetic choice is justified by speed alone.

## Full-workload estimates and regression decisions

The cost model is deliberately narrow:

\[
T_{\mathrm{full}} = T_{\mathrm{prepare}} + T_{\mathrm{fixed\ score}}
                    + F_{\mathrm{full}} T_{\mathrm{per\ CFO}}.
\]

All terms describe the original full sample/timing geometry. The model is fit
to actual wrapped receiver calls, including result construction, rather than
an internal timer that omits finishing work. The full-time prediction covers
receiver preparation and scoring for a loaded observation. Interpreter startup,
fixture generation/loading and provenance hashing are recorded separately;
they count against the suite budget but are not included in that resident
receiver prediction. Do not present it as cold end-to-end radio latency.

Before timing, a work-count guard compares declared training plus validation
CFO evaluations with direct full-bank work. At 80% or more, it selects direct
measurement. The 33-CFO tight and broad HF cases therefore run directly: their
earlier estimation plans required 117 and 103 evaluations respectively,
against 99 for three direct repetitions. The 992-CFO free-crystal case retains
abbreviation: three repetitions of 1/124 training and 249 validation CFOs use
37.7% of direct CFO work. This is a work-count saving, not a promised wall-time
speedup. The extrapolation limit is eight times the largest training bank.
Larger withheld banks test scaling; reports retain actual bank counts and
complete-target measurement counts. Small banks are measured directly.

Each case has at most two attempts, with the original result retained. Invalid
scaling requires a direct full-target retry. A valid but inconclusive large
case receives one fresh attempt under the same work-count guard. If both
sources' initial full-runtime estimates
are below one second, the retry instead uses seven direct full-target
repetitions. A supported regression is never retried in search of a passing
result. Neither the retry nor precision selection may tune against a holdout
until it passes.

The default model check permits
`abs(predicted - observed) / observed <= 0.20`. This engineering allowance is
not a statistical coverage claim. Inverting that convention gives actual-time
factors `[1/1.20, 1/0.80]` around the prediction. It must not be replaced by
`[0.80, 1.20]`, which would exclude some accepted errors.

Uncertainty uses outer repetitions of the **predicted full times**, not the
thousands of inner hardware iterations. The Student-t component on log times
describes the geometric mean under the declared stationarity/independence
model; a future individual execution needs a separate predictive-variation
term. Neither the model allowance nor a few holdouts proves distribution-free
95% coverage. The paired comparison uses log candidate/reference full-time
ratios, allocates `0.05 / (2 × case-family count)` error budget to each possible
attempt, and retains a
nonshrinking model allowance plus observed paired holdout errors. The paired
ratio allowance is a **separate conditional engineering assumption**, checked
on paired holdouts; two individual 20% error bounds do not imply a 20% ratio
bound. Both source
predictions must validate before that comparison can support a performance
pass. This familywise error statement concerns the gate decisions across the
predeclared families and at most two attempts; it is not simultaneous coverage
for every descriptive mean/prediction interval in the report. Absolute reported
differences are in full-workload seconds.

| Result | Meaning |
| --- | --- |
| Supported performance pass | The expanded slowdown interval's upper endpoint is at most 1.30. Required coverage and numerical checks also completed. |
| Performance regression | The expanded interval's lower endpoint exceeds 1.30. |
| Inconclusive warning | The interval crosses 1.30 after the bounded retry. It is distinct from a supported pass and does not fail CI solely because timing is noisy. |
| Hard failure | Numerical disagreement, invalid scaling without a valid fallback, missing required cases, setup failure, timeout or incomplete cleanup. |

A slowdown point estimate alone is insufficient to declare a regression. Keep
hardware-health warnings separate from implementation comparisons: current
slow hardware kernels must not be used to normalize a slow receiver back to
an apparent pass. A previously unmeasured hosted runner can support a paired
implementation comparison while lacking evidence for its historical hardware
health.

## Measured runtime evidence and its limits

The [retained study](validation-data/triad-v3/2026-11-03-performance/README.md)
includes source snapshots, reports, failed models and numerical checks.
All three hosts completed required coverage with no numerical failures. Each
retained inconclusive timing comparisons as warnings. These are comparisons
of unchanged receiver code with the fixed reference, not receiver speedups.

| Host/backend and measured policy | Total suite wall time | Supported / inconclusive case comparisons |
| --- | ---: | ---: |
| Ryzen 5 PRO 5650U CPU, final work-count guard | 410.58 s | 3 / 3 |
| EPYC 9554 CPU, earlier full-retry policy | 258.76 s | 5 / 1 |
| RTX PRO 6000 Blackwell Server CUDA, bounded retry before work-count guard | 298.99 s | 3 / 3 |

The ten-minute ceiling was met; the five-minute preference was not consistently
met on the laptop. Earlier laptop policies took 283.87–397.32 seconds, so the
work-count guard has not demonstrated a total wall-time improvement. Hardware,
software versions and policy differences preclude attributing all differences
to architecture. The final guard was not rerun on the GPU. Later source-identity
and telemetry metadata additions have focused test coverage, not another timing
campaign. Exact measured harness versions are archived.

The 58 focused tests passed. A real candidate scoring call with an injected
0.4-second delay retained identical numerical outputs and was correctly
rejected: its full-runtime ratio interval was [2.155, 2.644], above 1.30.
An initial scaling model with 31.18% held-out error was also rejected. These
controls test real failure handling; they do not establish universal interval
coverage or sensitivity. Native receiver throughput remains unmeasured.

## CI, release scope and architecture transfer

Normal [CI](../.github/workflows/ci.yml) invokes the dedicated performance
workflow unless `devfast` is explicitly selected. It is a separate job, not a
CTest added to every sanitizer/matrix leg. New direct-release builds depend on
the exact-source performance job before producing/uploading binaries. A noisy
warning is visible and may leave CI green; green alone is not a claim that all
runtime estimates are resolved. Existing ordinary-release certification remains
separate.

The hosted CPU job requests AVX2 fallback dispatch for ATen, OpenBLAS, MKL,
oneDNN and NumPy before loading the numerical libraries. It checks physical
AVX2/FMA support, ATen's reported selection and NumPy's selected optimized
functions, and retains `dispatch.json`. The local preflight passed. This tests
common fallback paths but does **not** emulate an older processor's caches or
clocks, constrain every vendor/library path, or certify binary portability.
Hosted GitHub Actions has not been run as part of this local study.

These controls have library-specific limits: [ATen's override](https://github.com/pytorch/pytorch/blob/v2.8.0/aten/src/ATen/native/DispatchStub.cpp)
does not itself establish physical ISA support; [OpenBLAS's selection](https://www.openmathlib.org/OpenBLAS/docs/runtime_variables/)
depends on a dynamic-architecture build; and [oneDNN's ceiling](https://uxlfoundation.github.io/oneDNN/v3.7/dev_guide_cpu_dispatcher_control.html)
depends on its build configuration. Actual
[NumPy dispatch inspection](https://numpy.org/doc/2.3/reference/generated/numpy.lib.introspect.opt_func_info.html)
is retained alongside requested controls rather than treating environment
variables alone as proof.

The `source_release` and direct APT repackaging routes reuse verified existing
application archives unchanged. They report **triad performance not evaluated**;
running today's harness against today's source would not qualify those older
archives. This exemption must be revisited if the experimental triad receiver
becomes a shipped runtime.

No current result predicts a Ryzen 5500U, i9-13900H or another unmeasured CPU
from GPU peak specifications with a confidence-qualified interval. The
statistical helper supports stage-rate transfer calculations, but the runner
abstains without matched stage shapes and independent target full-workload
validation. A measured target profile alone is insufficient. Validate transfer
on held-out workloads, preserve serial/setup/transfer stages, and report
out-of-domain shapes explicitly before adding it to a regression gate.

## Rationale and evidence discipline

The design uses a small number of representative full-geometry workloads
rather than multiplying every configuration axis. Add a case when it exercises
a different implemented cost regime, numerical hazard or scaling boundary.
Add a larger sustained holdout when changed cache/thermal behavior invalidates
the short model; do not turn every historical sensitivity study into CI.

[Kalibera and Jones](https://www.cs.kent.ac.uk/pubs/2012/3233/content.pdf)
explain why performance ratios need uncertainty that reflects the levels of
execution variability. [Google Benchmark's interleaving guidance](https://github.com/google/benchmark/blob/main/docs/random_interleaving.md)
motivates distributing comparisons across changing machine conditions. The
suite's alternating paired order is a bounded implementation choice, not the
same experimental design or a guarantee of independence.
[Measured Roofline analysis](https://spiral.ece.cmu.edu/pub-spiral/pubfile/ispass-2013_177.pdf)
supports separating compute and memory regimes; peak FLOP rates do not predict
complete acquisition time. [PyTorch's CUDA timing guidance](https://docs.pytorch.org/docs/stable/notes/cuda.html#asynchronous-execution)
requires synchronization or CUDA events for asynchronous device work.

Keep measured reports and failures source-bound. Unit tests establish the
harness's contracts; they do not establish prediction accuracy, sustained
utilization, GPU acceleration or a cross-architecture confidence interval.
Those claims require the actual held-out measurements and their uncertainty.
