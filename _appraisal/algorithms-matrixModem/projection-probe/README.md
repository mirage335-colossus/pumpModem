# Matrix projection stage probe

> Relocation record: originally `_appraisal/algorithms_matrixModem/projection-probe`; now `_appraisal/algorithms-matrixModem/projection-probe`. Source and result data were moved without changing their bytes; this location update does not change the experiment's historical inputs or provenance.


This research harness compares two GPU implementations of the same complex
projections, using deterministic, zero-padded 160 ms windows. This README is the
research record and is retained under the
[research-material rule](../../../docs/development.md#research-material).

- the existing native `CudaNvfp4Projection` implementation, with block-scaled
  NVFP4 operands and FP32 accumulation;
- a batched complex FP32 cuFFT followed by a device gather of the requested
  bins.

It is a stage discriminator, not a Matrix receiver, a strict INT4
implementation, or a sensitivity result. The NVFP4 routine is reused without
changes from `src/triad_nvfp4_projection.cu`. Its wide accumulation is part of
that routine's numerical contract and must be stated when interpreting a run.
The harness does not infer that strict signed-INT4 software should accept this
arithmetic.

## Geometry

Each test row contains the first 512 complex samples at 3,200 complex samples/s
and 1,536 zero samples. A 2,048-point transform provides a 1.5625 Hz evaluation
grid across signed offsets −704 through +704, or −1,100 through +1,100 Hz. The
grid spans 2,200 Hz and its positive and negative FFT bins are represented by
their modulo-2,048 indices. Both methods evaluate the exact same signed-bin
list and return a unitary projection, divided by `sqrt(2048)`.

The fourfold zero extension refines the *frequency evaluation grid*. It does
not make the acquired observation four times longer or add information. The
corresponding NVFP4 projection also processes a 2,048-element row, so its
contraction performs extra zero-work compared with a 512-element direct
projection. That cost is included because this comparison asks which route
evaluates the same finer frequency grid more quickly.

Two grids are used:

- **sparse** has 47 approximately even-spaced bins across the 2,200 Hz span,
  including the exact 125 Hz bin used for the lane-zero tone check;
- **dense** has every bin from −704 through +704, or 1,409 frequency tests.

The harness tests batches of 16, 128, and 512 windows. Every row has
independent complex Gaussian noise with unit expected sample energy. Lane zero
also has a known 125 Hz complex tone. Other lanes supply noise-only projection
energy summaries. The input is fixed with an explicit PRNG seed.

## Build and run on a full RTX PRO 6000 Blackwell Server Edition

From a clean source checkout with CUDA Toolkit 12.8 or newer, cuBLASLt and
cuFFT installed, build the narrow target:

```sh
cmake -S _appraisal/algorithms-matrixModem/projection-probe \
  -B /tmp/matrix-projection-probe-build \
  -DREPOSITORY_ROOT="$PWD" -DCMAKE_CUDA_ARCHITECTURES=120 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/matrix-projection-probe-build --target matrix_projection_probe -j2
/tmp/matrix-projection-probe-build/matrix_projection_probe \
  | tee /tmp/matrix-projection-probe.csv
```

Use a full SM120 device with MIG disabled. The test aborts if CUDA/cuBLASLt
cannot execute the existing NVFP4 route. It does not silently time CPU work or
an alternate arithmetic path as NVFP4. The reported device name, SM, runtime,
driver, cuBLASLt version, mode count, memory allocations, upload, coefficient
preparation, GPU event timing, projection error and H0 mode energy identify the
case. CUDA events cover cuFFT plus gather or the NVFP4 input packing and
projection. They exclude plan/setup, host upload and host readback. The timed
loop uses four warm-up calls and twenty measured calls per shape.

For a GPU-instruction claim, additionally profile a representative sparse and
dense run with Nsight Compute on the same binary and device. Save a short
receipt identifying the kernel names and datatype-specific tensor-pipeline
activity. CUDA-event throughput or generic utilization does not by itself
establish Tensor Core use. Include launch, preparation, input packing, transfer,
and output/readback costs when judging whether a path benefits a receiver.

The harness computes selected direct double-precision scalar checks and prints
their differences from cuFFT and NVFP4 for the selected bin indices. These are
arithmetic checks on synthetic rows. They do not test fading, waveform
transitions, acquisition, coding, quiet-slot completion, sensitivity, or
complete-receiver throughput.

## Measured Blackwell comparison

24 November 2026. Three unprofiled invocations completed on one full NVIDIA
RTX PRO 6000 Blackwell Server Edition, SM 12.0, 97,887 MiB in `nvidia-smi`, MIG
disabled, driver 595.112.07. The device reported CUDA runtime 12.8 and cuBLASLt
12.8.4. Only the 9,777-byte source archive was transferred. Its SHA-256 was checked
after transfer. The build used CUDA 12.8.93, `-O3 -std=c++17 -arch=sm_120`,
and `-lcublasLt -lcufft` in `runpod/pytorch:1.0.2-cu1281-torch280-ubuntu2404`.
The local CMake attempt ended at compiler discovery because no local `nvcc`
was available. The successful remote compilation was direct:

```sh
nvcc -O3 -std=c++17 -arch=sm_120 -Isrc \
  _appraisal/algorithms-matrixModem/projection-probe/projection_probe.cu \
  src/triad_nvfp4_projection.cu -lcublasLt -lcufft -o /tmp/projection_probe
/tmp/projection_probe
```

The base tree was `84dd706d03d42bea606e6d01ef62bd98e431ea9c` with the standalone
probe added. SHA-256 of the compiled probe source was
`ab512a656dbc78535f52ea1607cb21407652eadd1b196deff558055e21b1dd76`.
The native NVFP4 source was unchanged. The resulting executable SHA-256 was
`bb74ba7397ee6a486c082d348d4ee871925a6d96ba609837e2591289d350ba59`.

Each invocation ran six shapes over the same deterministic input, with four
warmups and 20 CUDA-event-timed calls. Values below are the median of three
invocation means for each shape, excluding the profiling run. Each projection
route processed every requested bin in the corresponding row. Output relative
RMS compares complex amplitudes with FP32 cuFFT, including the planted-tone row.

| Bin set | Windows per batch | FP32 cuFFT + gather, µs | NVFP4 pack + project, µs | FFT speed ratio | NVFP4 relative RMS | Mean noise-only mode power, FFT / NVFP4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 47 | 16 | 5.8 | 26.4 | 4.55× | 7.78% | 0.2479 / 0.2566 |
| 1,409 | 16 | 5.6 | 27.3 | 4.88× | 12.87% | 0.2498 / 0.2593 |
| 47 | 128 | 5.9 | 29.9 | 5.07× | 11.67% | 0.2532 / 0.2620 |
| 1,409 | 128 | 6.3 | 31.5 | 5.00× | 13.25% | 0.2501 / 0.2598 |
| 47 | 512 | 9.0 | 40.4 | 4.49× | 12.76% | 0.2522 / 0.2609 |
| 1,409 | 512 | 10.8 | 44.4 | 4.11× | 13.28% | 0.2502 / 0.2598 |

At the reported precision, the selected scalar oracle discrepancies for
cuFFT were all below `0.00005` complex magnitude. NVFP4 differed by up to
`0.1280` on the displayed checks. `0.25` is the theoretical mean complex
noise-only power for the chosen unitary normalization with 512 of 2,048
nonzero samples. The approximately 3.4–3.9% NVFP4 increase in mean power
compared with the matched noise-only FP32 output motivates route-specific
calibration. These rows share one planted strong tone, and frequency-bin
scores within a row are correlated. The aggregate error depends on the
batch and cannot give a near-threshold BER or false-alarm result.

At batch 512 and 1,409 bins, both routes shared 8 MiB of padded input and had
5.5039 MiB of complex projections per route. NVFP4 allocated 6.2812 MiB of
coefficients and 10.6895 MiB of scratch. cuFFT used an 8 MiB transform output
buffer and requested 0.0039 MiB of plan workspace. Median upload was about
0.355 ms and combined readback of the two result arrays was 0.563 ms.
NVFP4 coefficient preparation was about 0.660 ms. cuFFT plan creation for
that case was about 0.326 ms after first-use setup. The timing loop reuses
resident buffers and the same input. These one-time/transfer measurements
cannot be added directly to a streaming service estimate.

The NVFP4 call completed with the declared packed data and FP32 accumulator.
Nsight Compute 2025.1.1 attached, then reported `ERR_NVGPUCTRPERM` when reading
performance counters. Its instrumented times were excluded. Dynamic Tensor
activity and achieved utilization remain unmeasured for this probe.

The first ephemeral pod reached RUNNING, and RunPod's command API returned
HTTP 403 before any upload or GPU check. The provider confirmed removal.
The second used the repository's verified direct SSH flow, ran the successful
comparison and counter attempt, and was terminated and verified removed. No
network volume was created. Account and infrastructure receipts stay outside
the checkout.

## How to read results

This stage favors cuFFT on the tested regular grid. The sparse case selects
fewer bins from that grid. A distinct GEMM taking only 512 active samples,
a true off-grid or irregular channel template set, a fused likelihood
epilogue, other arithmetic, or a different batch geometry can change the
crossover. A larger batch can expose GPU parallelism during simulation. A
live stream supplies work according to its hypothesis count and deadlines.

No crossover or dispatch rule is inferred from these measurements. A future
receiver comparison must also account for preparation, candidate generation,
tone scoring, tracking, missed or unsuccessful hypotheses, fallback, decoding,
and ordered completion on identical inputs.
