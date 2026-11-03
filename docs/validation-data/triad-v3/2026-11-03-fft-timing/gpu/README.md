# CPU/GPU FFT acquisition comparison on one retained capture

**CUDA did not accelerate this cold-start CLI workload.** On one RTX PRO
6000 Blackwell Workstation Edition, two CUDA FP32 runs took 5.356 and
5.058 seconds, compared with 4.605 and 4.471 seconds for CPU FP32 on the
same host. Full-bank likelihoods and decisions agree closely with CPU FP64.
This is a strong-signal arithmetic and latency diagnostic, not a weak-signal
precision qualification or a resident GPU throughput measurement.

The [independently reviewed summary](summary.json) retains commands, source
and input hashes, resource reports, all six job statuses and the full-table
comparison results. [compare.py](compare.py) is the byte-identical supplied
campaign driver. The six large raw score tables remain in ignored
`local-evidence/triad-fft-timing/gpu/remote/`; they are not duplicated here.

## Workload and independent checks

All six runs use the same sampled observation at **20 dB-Hz C/N₀**: Rate
360 Hz, Added Symbols ×10 with interleave and encryption, 30 MHz RF,
1.725 kHz conversion IF, GPSDO OCXO base/shift oscillators, margin 1,
HF-best 0.1 Hz/0.5 ms spread, Each65 selective modulation and envelope-only
FHSS. The spacing-scaled `dispersive-dft-v1` channel is a declared stress
model, not a field measurement. The eight-second active request produces
a 9.2-second waveform; the retained observation, including arrival allowance
and tails, contains 95,656 complex samples at 4,096 samples/s
(23.353515625 seconds). See the [parent FFT study](../README.md) for the
sampled acquisition implementation and broader limitations.

Each run evaluates all **57,632 integer timing lags × three public CFO
values = 172,896 groups**, with both candidate bits. Lags are 0 through
57,631 samples; CFO offsets are −0.0030001725001500084, 0 and
+0.0030001725001500084 Hz. Epoch, sample-clock hypothesis, fractional
arrival branch and drift are fixed. The score is a uniform likelihood
mixture over the entire bank, not the best group's score. A group tile of
512 changes execution storage, not the search family.

Independent review:

- Rehashed all six source files and all four input files (13,893,652 bytes),
  and matched every score's dependency/input binding to the campaign.
- Matched all six status files and score-file hashes. All jobs exited zero;
  their complete timing/CFO lists and ordering agree.
- Recomputed the two-bit mixture from each complete 172,896-row table.
  Maximum difference from the recorded mixture was **1.69×10⁻¹⁴**.
- Recomputed error maxima, both winners and top-ten rankings against FP64.
  Both bits preserve the **exact top-ten order**, not merely set overlap.
- Recomputed the strict/relaxed gates and full-group calibration allocation,
  including the documented outward rounding. All groups report union rank
  four. No known-noise override or public near-edge refinement was used.

No receiver, waveform or cloud job was rerun for this review.

## Numerical agreement and admission scope

| Bulk execution | Maximum absolute group log-score difference from CPU FP64 | Maximum mixture difference | Winning bit and top-ten order |
| --- | ---: | ---: | --- |
| CPU FP32 | 0.000157564 | 0.000148128 | Identical for both bits |
| CUDA FP32 | 0.000250006 | 0.000234941 | Identical for both bits |

Each setting's two replays produced identical full score tables. Relative
group-score differences reach about 0.00149 near small reference scores;
the absolute differences above are the more useful scale for admission.
No tolerance was adjusted and no hypothesis was removed to obtain agreement.

All runs admit the correct bit 1 at both gates. CPU FP64's accepted score
is **409.9511665**, CPU FP32's is **409.9510184**, and CUDA FP32's is
**409.9509316**. The strict gate is **22.72106339**, and the relaxed gate
is **17.42274602**. The minimum strict margin is therefore about
**387.23 nats**. These results do not resolve a sensitivity loss near an
admission boundary. Six replays of one signal are one reception realization;
no noise-only or wrong-key capture was included.

The declared Gaussian-noise bounds remain 10⁻⁴ per day (strict) and 1% per
12 hours (relaxed), allocating separate score and calibration budgets over
1,440 attempts/day, eight duration alternatives, 16 method slots and two
bits. Every group participates in residual calibration and the likelihood
mixture; independence between groups is not assumed. These conditional
model-based bounds are not empirically qualified by this signal-only
arithmetic comparison, and do not imply correct-bit or wrong-key guarantees.

All groups retained rank four, so this experiment also provides no new
qualification for near-singular rank decisions, unresolved masked edges,
subnormal inputs, long observation accumulation or selective refinement.

## Measured costs and actual arithmetic

| Execution | CLI wall, a/b (s) | Preparation, a/b (s) | Scoring after preparation, a/b (s) | Sampled peak host RSS, a/b (MiB) |
| --- | ---: | ---: | ---: | ---: |
| CPU FP64 | 4.674 / 4.624 | 0.192 / 0.192 | 2.982 / 2.973 | 643.2 / 638.3 |
| CPU FP32 | 4.605 / 4.471 | 0.098 / 0.103 | 3.075 / 3.012 | 631.1 / 630.8 |
| CUDA FP32 | 5.356 / 5.058 | 0.505 / 0.230 | 3.351 / 3.350 | 1582.1 / 1558.5 |

The paired CPU-FP32/CUDA-FP32 wall-time ratios are **0.860× and 0.884×**;
a ratio below one means CUDA is slower. Two repetitions do not establish a
latency distribution. The campaign took 31.160 seconds, running fresh CLI
processes serially in CPU64/CPU32/CUDA32/CUDA32/CPU32/CPU64 order. BLAS and
Torch used one CPU thread. Fresh processes include imports, CUDA startup,
preparation, input checks, computation and JSON output, but can still benefit
from filesystem or driver caching across the session.

The report field `total_receiver_seconds` starts **after** FFT workspace
preparation; it must not be presented as complete receiver wall time.
Observation FFT processing took 0.039–0.042 seconds on CUDA versus
0.042–0.044 on CPU FP32. The mixed residual/likelihood stage took about
3.31 seconds on CUDA versus 2.97–3.03 on CPU FP32. CUDA template preparation
took 0.116–0.329 seconds; the CPU FP64 Gram FFT remained 0.070–0.088 seconds
in the CUDA runs. These are instrumented host-stage times, not separately
profiled CUDA kernels; they do not isolate transfer, launch, FP64 or FP32 cost.

The `fp32` label covers observation FFT/products and projected likelihood
bulk. It is **not an entirely FP32 receiver**:

- Gram FFTs remain substantial CPU complex128 work, cached across CFO/drift
  alternatives. Public phase construction uses CPU long-double cycle
  reduction and float64 exponentials.
- Residual projection, rank decisions and covariance preparation use CPU
  FP64/complex128.
- `PreparedWindowedGaussian.score` also transfers the small correlations
  and whiteners as complex128 to the selected device, performs observation
  whitening there, then casts normalized projected operands to complex64.
  Evidence powers, determinants and accumulated scores use device float64.
  Final bit mixture and calibration are CPU float64.
- CUDA FP32 disallows TF32. The public-edge raw-dot FP64 exception exists,
  but no group invoked it in these six runs.

Thus CUDA FP32 is a partial GPU/mixed-precision implementation. The saved
arithmetic report's CPU preparation/finish phrase does not enumerate the
additional small device-FP64 work above. This review records that distinction
without changing the frozen runtime. No stage-specific experiment here proves
that every retained higher-precision operation is necessary.

The explicit array estimates are 185.20 MiB on CPU and 229.05 MiB on CUDA,
below the declared 1 GiB array budget. That budget excludes Python/Torch,
driver and external allocator overhead. Observed CUDA host RSS exceeds
1 GiB; the runner sampled `/proc/<pid>/status` every 25 ms, so its reported
peak is not a guaranteed maximum. GPU memory peak was not measured.

The evidence supports retaining CPU execution for this measured cold CLI
shape. It establishes neither a GPU advantage for larger/resident batches
nor a reason to change precision defaults. Resident throughput, weak-signal
agreement and hardware portability remain separate questions.

## Source binding and optional reproduction

The receiver entrypoint is SHA-256
`f91265b9d0a6915848be62e2b687982b2b07109d534a8487e137c73377d5b94b`;
the Gaussian helper is
`b22fe002864922e44bc7e10000672a163353818f610c4882bae5d62cfe1cf883`.
All six files in the campaign's dependency closure match before/after and
are individually bound by the score reports. The input manifest is
`b60ce336c2ba6af43bd506f1ac451d32ff3ff43b0ef18f02d255bad5b85da7be`.
Full hashes are in the summary.

The recorded environment was Python 3.12.3, NumPy 2.5.2, SciPy 1.18.1,
Torch 2.8.0+cu128 and CUDA build 12.8 on Linux. The campaign records one
visible RTX PRO 6000 Blackwell Workstation Edition; CPU model and driver
version are not recorded. Do not generalize these timings to other hardware.

The archived driver SHA-256 is
`08a037ca6dcffafa7780236bb0a8218c6934d2ef06c85580a174533231b01779`.
Its own hash was not included contemporaneously in `campaign.json`; this
review binds the supplied final driver and checks it against retained job
commands and report structure. Use the exact source closure and existing
sampled workspace when reproducing. The driver refuses a nonempty output
directory and limits each job to 120 seconds and the campaign to 13 minutes.

```sh
python docs/validation-data/triad-v3/2026-11-03-fft-timing/gpu/compare.py \
  --receiver tools/triad_fft_acquire.py \
  --input local-evidence/triad-fft-timing/wide-pilot/sampled \
  --out local-evidence/triad-fft-timing/gpu/reproduction \
  --cwd .
```

This command requires the recorded dependencies and a CUDA-capable
environment. It runs six receiver jobs; it is an optional experiment,
not a routine regression prerequisite. Ordinary Robust receiver integration
and its regression campaign remain outside scope.
