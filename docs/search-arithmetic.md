# Search arithmetic policy

Robust uses qualified lower precision in the operating C++ receiver, including
live reception and simulation. The policy preserves complete public frequency
searches, the default `W=2*H` workers and conservative DSP admission. Arithmetic
selection must improve the useful processing ceiling while preserving public
search and physical completion; quantify any reception-sensitivity tradeoff.
See [current architecture](robust-current.md) and
[precision boundaries](gpu-precision-controls.md).

| Shared GUI / launch preference | Native CPU policy |
| --- | --- |
| Default / `--search-arithmetic default` | Automatic qualified Q15 spectral products or FP32 products, with FP64 exclusions. |
| FP32 minimum / `--search-arithmetic fp32-min` | FP32 spectral products, with higher precision where required. |
| FP64 force / `--search-arithmetic fp64-force` | Force the native CPU coarse FFT to FP64. |

The GUI reports the requested native policy. With **GPU** enabled, Default can
use the qualified Vulkan automatic policy or the calibrated native chirp-Z
route described below; FP32 minimum selects the FP32-storage/FP32-arithmetic
Vulkan FFT. Combining GPU with FP64 force is an explicit configuration error.
A precision preference is not a promise that every receiver stage uses that
type, nor does it select an INT8 FFT.

## Native CPU selection

Automatic reduced precision is confined to frequency search after native channel
isolation has removed strong out-of-band Helper tones. A qualifying broad native
single-ray, single-arrival, no-Helper public search can instead use the existing
FP64 chirp-Z frequency scan when its estimated transform-row work is at least
four times smaller; it still evaluates every public frequency. A single frequency
may instead use a bounded direct FP64 dot when the same modeled advantage holds.
Other qualified AVX2 routes use Q15 spectral products for native exact-projection,
single-ray geometry;
FFTs remain FP32 and scoring remains FP64. There is no separate Q15 FFT-size
cutoff; checked native transform and workspace bounds still apply. Other
qualified AVX2 searches use FP32 products and FFTs. The FP32 SIMD path processes
four complex values per vector rather than two FP64 values. Every template
workspace must pass the double-precision Gram check: the bank's minimum
eigenvalue ratio, smallest/largest, must be at least `1/16`. Selection uses
public geometry, CPU capability and template conditioning, never decoded bits
or a favorable observed score.

The following retain FP64 with an explicit reason: lag-group search, including
the eligible exact one-arrival chirp-Z route; projected
rate at most 10 Hz with a positive clock span at most 0.001 ppm; an
ill-conditioned template bank; an unqualified CPU kernel, including scalar;
or the explicit FP64 preference. Coordinates, demodulation phase, Gram matrices,
quadratic scores and fine Gaussian state remain in double precision.

Post-filter blocks whose maximum component exponent falls outside `[-16,16]`
receive exact power-of-two scaling before FP32 conversion. Correlations are
restored in double precision before the unchanged noise normalization and
scoring. Receipts expose scaling rather than treating tiny input as zero.
The FP32 and double FFT twiddle plans do not coexist after projection. Q15
packing uses per-spectrum power-of-two scaling; the admission bound charges
conversion overlap. A phase-aware one-ray bound can reduce the complete slot
lease when coarse work dominates. Full worker allocation is retained.

## GPU and measurement boundaries

On the qualified local 5650U Radeon, supported single-ray, single-arrival
searches use scaled packed FP16 direct products with FP32 accumulation and FP64
coordinates and scoring. The selector bounds unpadded projected template rows
to 131,072, and processes public frequencies and workspaces in bounded tiles.
Longer eligible single-arrival correlations and unrepresentable normalization
use FP32 direct products. Other cases retain qualified FP16-storage/FP32-arithmetic
FFT, FP32 FFT or explicit native fallback, depending on device and geometry. Precision and fallback
reasons appear in receipts. There is no accelerated INT8 dot path on this
device; exposed INT8 types do not create one. VkFFT's half-storage FFT still
computes in FP32. See the [GPU precision guide](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics)
for exact driver, scaling and qualification scope.
The ordinary GUI's Default policy may prefer native FP64 chirp-Z for the scoped
one-ray, one-arrival, no-Helper geometry when the measured Radeon/CPU crossover
and worker-dependent cost guards apply. Device initialization remains explicit,
and the full GPU reservation remains charged where either route can be needed.
The public frequency group may contain up to 65,536 points. The crossover uses
the measured 26-workspace, 1,750-row, 1,794-frequency anchor and scales the
required direct work upward for more native radix work or fewer admitted
workers; the separate fourfold native transform-work guard also remains. Larger
geometry decisions are model extrapolations, not measured GPU comparisons.
Explicit GPU precision choices and headless strict accelerator probes retain
their requested GPU execution semantics. Actual routing is reported, rather than
assuming that a checked GPU control means every score ran on the device.
The separately qualified CUDA FP16/INT8 matrix alternatives remain available on
their applicable devices and geometries. They do not establish local Radeon
capabilities or convert the CUDA FFT default into INT8.

Helper's current expensive FFTs precede isolation and retain double precision.
Fine Gaussian cancellation and conditioning also need precision even when the
filtered spectrum has modest dynamic range. Lower precision for those stages
requires its own numerical and complete-cost evidence.

The native probe accepts `--native-coarse-precision automatic|fp32|fp64|q15`;
explicit FP64 is a CPU reference control and explicit Q15 rejects unsupported
geometry instead of silently pretending to execute it. Requested policy, actual precision, selection
reason and scaling appear in receipts, and runtime identity separates policies
so regression baselines cannot silently compare different arithmetic. Use the
[bounded CPU precision comparison](triad-performance.md#native-cpu-arithmetic-comparison)
for matched measurements. Current outcomes and pending qualification belong in
[validation](validation.md); hardware capability or a pilot timing is not a
complete-receiver capacity result.
