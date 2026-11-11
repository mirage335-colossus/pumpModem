# Search arithmetic policy

Robust uses qualified lower precision in the operating C++ receiver, including
live reception and simulation. The policy preserves complete public frequency
searches, the default `W=2*H` workers and conservative DSP admission. Arithmetic
selection must improve the useful processing ceiling without changing the search
or receiver decisions. See [current architecture](robust-current.md) and
[precision boundaries](gpu-precision-controls.md).

| Shared GUI / launch preference | Native CPU policy |
| --- | --- |
| Default / `--search-arithmetic default` | Automatic qualified FP32 coarse FFT, with explicit FP64 exclusions. |
| FP32 minimum / `--search-arithmetic fp32-min` | Same automatic policy; permits higher precision where required. |
| FP64 force / `--search-arithmetic fp64-force` | Force the native CPU coarse FFT to FP64. |

The GUI reports native automatic precision or forced FP64. Combining **GPU**
with FP64 force is an explicit configuration error: the current Vulkan FFT uses
FP32. A precision preference is not a promise that every receiver stage uses
that type, nor does it select an INT8 FFT.

## Native CPU selection

Automatic FP32 is confined to the frequency-search FFT after native channel
isolation has removed strong out-of-band Helper tones. The qualified AVX2 path
processes four complex FP32 values per vector rather than two complex FP64 values.
Every template workspace must pass the double-precision Gram check: the bank's
minimum eigenvalue ratio, smallest/largest, must be at least `1/16`. Selection
uses public geometry, CPU capability and template conditioning, never decoded
bits or a favorable observed score.

The following retain FP64 with an explicit reason: lag-group search; projected
rate at most 10 Hz with a positive clock span at most 0.001 ppm; an
ill-conditioned template bank; an unqualified CPU kernel, including scalar;
or the explicit FP64 preference. Coordinates, demodulation phase, Gram matrices,
quadratic scores and fine Gaussian state remain in double precision.

Post-filter blocks whose maximum component exponent falls outside `[-16,16]`
receive exact power-of-two scaling before FP32 conversion. Correlations are
restored in double precision before the unchanged noise normalization and
scoring. Receipts expose scaling rather than treating tiny input as zero.
The float and double FFT twiddle plans do not coexist after projection; the
existing conservative memory bound and full worker allocation are retained.

## GPU and measurement boundaries

The Vulkan route uses FP32 FFTs and products with FP64 coordinates and quadratic
scoring. On the local 5650U Radeon there is no accelerated INT8 dot path; exposed
INT8 types do not create one. VkFFT's half-storage option still computes in FP32.
The separately qualified CUDA FP16/INT8 matrix alternatives remain available on
their applicable devices and geometries. They do not establish local Radeon
capabilities or convert the full FFT default into INT8.

Helper's current expensive FFTs precede isolation and retain double precision.
Fine Gaussian cancellation and conditioning also need precision even when the
filtered spectrum has modest dynamic range. Lower precision for those stages
requires its own numerical and complete-cost evidence.

The native probe accepts `--native-coarse-precision automatic|fp64`; explicit
FP64 is a CPU reference control. Requested policy, actual precision, selection
reason and scaling appear in receipts, and runtime identity separates policies
so regression baselines cannot silently compare different arithmetic. Use the
[bounded CPU precision comparison](triad-performance.md#native-cpu-arithmetic-comparison)
for matched measurements. Current outcomes and pending qualification belong in
[validation](validation.md); hardware capability or a pilot timing is not a
complete-receiver capacity result.
