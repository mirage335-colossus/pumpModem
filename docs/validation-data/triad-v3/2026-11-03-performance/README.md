# Bounded three-carrier receiver performance study

The suite completed its required isolated receiver coverage on a Ryzen laptop,
an EPYC CPU and an RTX PRO 6000 Blackwell Server Edition GPU within the
600-second backend budget. Numerical comparisons completed without a hard
failure. Some runtime comparisons remained inconclusive and were reported as
**warnings**, not supported performance passes. These are null comparisons:
the measured receiver source files matched the fixed reference commit
`242ea15fa032ee117b9113a7f3cd07cc6e9f0254`. The study qualifies the harness's
bounded behavior on these observations; it does not demonstrate a receiver
implementation speedup or radio sensitivity.

Use the [performance-suite guide](../../../triad-performance.md) for the current
commands, statistical contract and CI policy. The
[development workflow](../../../triad-development-workflow.md) maps the separate
functional and sensitivity checks. Ordinary Robust integration, native receiver
throughput and hosted GitHub Actions execution were not tested here.

## Measured completion and uncertainty

The table uses **top-level total wall time**, including backend startup, rather
than the shorter backend-only timer. Receiver workers used two software
threads. Setup/downloads and the native build were outside these totals.

| Host/backend | Total wall time | Required coverage | Supported case comparisons | Inconclusive comparisons | Overall result |
| --- | ---: | --- | ---: | ---: | --- |
| AMD Ryzen 5 PRO 5650U, CPU, **final cost guard** | **410.58 s** | Complete | 3/6 | 3/6 | Warning |
| AMD Ryzen 5 PRO 5650U, CPU, earlier bounded retry | 346.48 s | Complete | 3/6 | 3/6 | Warning |
| AMD EPYC 9554, CPU, earlier full-retry policy | 258.76 s | Complete | 5/6 | 1/6 | Warning |
| RTX PRO 6000 Blackwell Server Edition, CUDA, bounded retry | 298.99 s | Complete | 3/6 | 3/6 | Warning |

The latest [cost-guard laptop run](ryzen-cost-guard.json.gz) took 410.575688
seconds (6.84 minutes): it exceeded the five-minute preference but remained below
ten minutes. The EPYC and CUDA runs were below five minutes. Earlier laptop
runs took 283.87, 346.48 and 397.32 seconds under preceding harness policies;
they are retained rather than substituted for the slower latest result. The
cost guard reduced some planned calibration work, but this campaign does not
show an overall wall-time improvement from that change. The EPYC and GPU environment used NumPy
2.3.5, SciPy 1.16.3 and PyTorch 2.8.0+cu128. The laptop used NumPy 2.5.4,
SciPy 1.18.1 and PyTorch 2.8.0+cpu. These differences prevent treating all host
ratios as controlled architecture-only effects.

The final cost-guard laptop warnings were **broad HF timing, long VLF and the
Gaussian helper**. Its tight-HF, missed-FHSS and free-crystal comparisons were
supported passes. The earlier bounded-retry laptop warnings were broad HF timing,
free-crystal acquisition and the Gaussian helper. The EPYC warning was long VLF.
CUDA warnings were broad HF timing, free-crystal acquisition and long VLF. The retained
reports contain each full-runtime point estimate, confidence/prediction
interval, paired slowdown interval, precision decision and original/retry
measurements. A warning does not establish either a regression or equivalence.

The final **static CFO-work cost guard** measures the full bank directly when
declared training plus validation CFO work would consume at least 80% of direct
full-bank work. This decision uses the declared plan, not observed timing noise.
It preserves full sample/timing geometry and does not claim a universal wall-time
cost model.

| Case | Planned abbreviated CFO work over three repetitions | Direct full-bank CFO work | Choice |
| --- | ---: | ---: | --- |
| Tight HF | 117 | 99 | Direct |
| Broad HF | 103 | 99 | Direct |
| Free crystal, 992 CFO alternatives | 1,122 | 2,976 | Abbreviated: **37.7%** of full CFO work |

The earlier broad-HF plan required more CFO evaluations than direct measurement
before extra call overhead, so estimating its runtime offered no computation
saving. The latest laptop run exercises the guard. The EPYC and CUDA timings
precede it; **the final cost-guard suite was not rerun on the GPU**. Exact measured
harness snapshots and final post-campaign controls are distinguished below.

## Diagnostics that changed the design

The [initial full-bank falsifier](failed-initial-model.json.gz) rejected the
first broad-HF runtime model. Maximum held-out errors were 19.84% for the
reference and **31.18% for the candidate**, exceeding the declared 20%
actual-relative allowance. The invalid result was retained; it was not made to
pass by widening its allowance.

The [batch-size diagnostic](batch-size-diagnostic.json.gz) kept the same sampled
observation, lag lattice, five-CFO bank and numerical scores:

| Group tile | Measured score-call time |
| ---: | ---: |
| 128 | 3.218 s |
| 512 | 2.074 s |
| 2048 | 1.809 s |

The two bit likelihoods agreed to floating-point rounding. This approximately
1.78× pilot improvement supports using larger batches for this geometry; it is
not a universal batching or sensitivity gain. The retained memory estimates
increase with tile size and exclude external allocator/runtime internals.

A separate [deliberate slowdown control](deliberate-slowdown-control.json.gz)
added a 0.4-second delay at the beginning of the candidate's real
`score_workspace` call. Its numerical work remained unchanged. The quick
control exited **2** after 18.56 seconds and reported a performance regression:
the measured full-runtime ratio was 2.387, with a conditional 98.75% paired
interval of **[2.155, 2.644]**, entirely above 1.30. This tests the wrapped call
timer and gate together, including work outside the receiver's internal timer.
The quick profile intentionally does not complete normal CI coverage.

The 20% model allowance is an engineering assumption checked against withheld
work. It is not demonstrated 95% coverage. The reported Student-t intervals
assume stationary, independent outer log timings; small samples and shared-host
variation can produce wide intervals. Gate intervals account for declared case
families and at most two inferential attempts in the bounded policy. This does
not provide simultaneous coverage for every descriptive interval in a report.
The paired-ratio model allowance is separately assumed and checked; it is not
implied mathematically by two individual 20% error bounds.

## Arithmetic and utilization evidence

Five-second CUDA probes completed for strict FP32/FP64 GEMM,
complex64/complex128 FFT, copying, phase reduction, small SVD, and verified TF32,
FP16, BF16 and INT8 tensor paths. Selected-device UUID binding was used without
retaining the identifier. The optional telemetry sampler was reaped. Mean
reported GPU busy fractions ranged from **77.28% to 99.76%** across completed
probes. These are driver device-busy percentages, not SM occupancy, tensor
occupancy or proof of peak throughput.

Both requested FP8 probes were rejected: the generated product used FP16 tensor
instructions rather than the requested FP8 instructions. The original
[failed hardware profile](gpu-fp8-rejection.json.gz) and
[rejected product PTX](rejected-fp8-product.ptx.gz) are retained. The subsequent
suite records those paths as unsupported on this compiler/runtime combination,
with no substituted-format throughput and no accepted fallback. This is not a
claim that the GPU hardware lacks FP8 support. Wider tensor scale restoration
and sensitive receiver FP64 work remain explicit in arithmetic metadata.

The separate [12-thread laptop hardware run](ryzen-12-thread-hardware.json.gz)
completed in 50.36 seconds. FP32 and FP64 GEMM used approximately **10.68 and
11.04 CPU-seconds per wall second**. Other families used fewer cores: the copy
and some small-matrix/conversion probes were effectively single-threaded. The
profile therefore demonstrates high utilization for the dense arithmetic
families, not universal saturation. The isolated receiver measurements used
two threads and must not be presented as whole-host throughput qualification.

The [AVX2 dispatch check](avx2-dispatch.json.gz) reported ATen `AVX2` and NumPy
`X86_V3`/`baseline(X86_V2)` selections while requesting vendor-library ISA
ceilings. This verifies those reported dispatch selections. It is neither a
complete instruction-set quarantine nor an emulation of an older processor's
performance, and it does not qualify all released binaries on older hardware.

The receiver's measured precision choices were FP32 or FP64 under its existing
policy; tensor hardware availability did not enable tensor FFT reception.
Eligibility is scoped to the declared numerical corpus and measured-bank
comparisons. It does not establish universal low-precision admission safety.

## Why whole-pipeline architecture transfer remains unqualified

The [summary](summary.json) includes a deliberately simple, rejected transfer
calculation. Measured GPU strict-FP32 GEMM throughput was about **286.8×** the
EPYC CPU FP32 GEMM probe. The probes themselves used different matrix sizes
(512-square on CPU and 1024-square on GPU), another reason not to transfer that
ratio indiscriminately. Yet the four matched FP32 receiver configurations had
CPU/GPU full-runtime point ratios of approximately **0.95–1.10×**. Scaling whole
receiver time by the GEMM rate therefore underestimated the GPU time by about
**262–300×**. Preparation, transfers, small fits and serial work do not disappear
when GEMM becomes faster.

The long VLF comparison used different selected arithmetic policies and is
reported separately in the JSON; it is not a clean backend-only speedup test.
No confidence-qualified Ryzen 5500U, i9-13900H or other unmeasured-target runtime
estimate is established. A measured target profile alone remains insufficient
without stage-shape mapping and independent full-workload validation. The
suite's explicit abstention is preferable to treating a microkernel ratio as
an end-to-end prediction.

## Reproduction and retained evidence

From the repository root, build/reuse the narrow native exporter and run the
current protocol with fresh output paths:

```sh
./build.sh configure --cli --build-dir build/dev-cli -- \
  -DBUILD_TESTING=ON -DDATAPUMP_COMPILER_CACHE=OFF
cmake --build build/dev-cli --config Release --target triad_long --parallel 2
python tools/triad-performance.py --binary build/dev-cli/triad_long \
  --device cpu --threads 2 --stress-seconds 5 --budget-seconds 600 \
  --output local-evidence/triad-performance/reproduction-cpu.json
python tools/triad-performance.py --binary build/dev-cli/triad_long \
  --device cuda --threads 2 --stress-seconds 5 --budget-seconds 600 \
  --output local-evidence/triad-performance/reproduction-cuda.json
```

Use `--hardware-baseline FILE` only for a comparable saved profile. Hardware
health is a separate diagnostic; current slow kernels are not used to normalize
away a slow receiver. A hosted CI runner without a comparable historical
profile cannot establish historical CPU/OS/driver health from one run.

[Evidence manifest](evidence-manifest.json) binds original report digests and
retained compressed digests. Repository/reference paths were normalized and
absolute monotonic deadlines removed; numerical evidence and source hashes
remain. Each report records the actual harness, candidate, fixed reference,
exporter and fixture identities.

Two seven-file source archives preserve the measured variants:

- [Measured bounded harness](measured-bounded-harness.tar.gz): every harness
  digest matches both the earlier Ryzen bounded and CUDA reports.
- [Measured cost-guard harness](measured-cost-guard-harness.tar.gz): every harness
  digest matches the latest Ryzen cost-guard report.

For either measured protocol, use a clean checkout of
`242ea15fa032ee117b9113a7f3cd07cc6e9f0254`, overlay its archive at the checkout
root, then build and run with the relevant numerical stack and commands above.
The commit supplies the unchanged receiver/runtime, frozen input plans and
tensor backend. The older EPYC full-retry policy is separately identified by
its report and is not asserted to match either archive.

The native exporter used for these measurements had SHA256
`9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47`.
Rebuilding with a different toolchain or standard library need not produce
identical binary bytes; retain the new binary and generated-input hashes.
Neither replay route promises identical wall times on a new host.

[Source and checks](source-and-checks.json) separately records final harness
hashes and metadata/source-guard additions made after the timing campaigns,
including the unchanged tensor support source. Those controls passed **58
focused tests in 9.697 seconds**. Focused checks are not another full timing
campaign; the source archives above, rather than the latest checkout, identify
what each campaign measured. Hosted CI and ordinary-receiver regressions remain
unexecuted in this study.

The required physical cases retain full sample arrays and timing lattices,
including HF/FHSS losses and the low-rate −60-dB-Hz observation spanning about
850 source days. That source calendar is part of a defined point-sampled model,
not validation of arbitrary long-term propagation or correct reception at every
configuration. Reduced CFO banks are performance-only; their decisions are
not operational full-search admissions.
