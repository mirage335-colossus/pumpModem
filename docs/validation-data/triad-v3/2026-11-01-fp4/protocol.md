# Exhaustive FP4 correlation study protocol

This is an optional isolated-study backend. Ordinary Robust integration and its
unrelated regression campaign are outside scope. The waveform, encrypted
clock-addressed source, interleaving, coupled channel taps and sampled physical
path remain those of triad v3. No reduced-precision policy becomes a default.

## Frozen comparisons

The search is the complete declared candidate/epoch × integer lag × CFO bank.
Every retained physical block supplies complex correlations for every coupled
tap. The richer phase/frequency receiver consumes every hypothesis; acquisition
does not nominate a top-K list. The CFO grid represents fixed center hypotheses;
the existing residual phase/frequency prior is declared separately.

Only complete template windows are legal. A common exactly zero suffix may be
removed to accommodate positive lags, with the removed range recorded. Samples
and channel tails are never synthesized by wrapping a capture. Erasures must be
represented consistently in both correlations and Grams or explicitly rejected.

The independent numerical reference uses complex128 sampled templates and raw
sampled observations. Strict FP32 disables TF32. Native dense FP4 requires
executed block-scaled E2M1 tensor instructions, FP32 accumulation and inspection
of generated PTX/SASS. API success and FP32 output alone do not establish this.

Real GEMM rows span CFO, candidate, epoch, tap and quadrature. Columns span
captures and timing windows. CFO rotates fixed templates using FP64 coordinates;
a lag/origin phase factor restores absolute capture coordinates. Observation
packing is reused across the entire template bank. Packed public templates are
cached in a bounded persistent receiver workspace, with cold and warm costs.

## Numerical and physical gates

Use the same actual sampled observations for FP64 calculations of TᴴX,
Q(T)ᴴX, TᴴQ(X), and Q(T)ᴴQ(X). Compare native products with the last expression
to isolate arithmetic from representation loss. Quantization follows sampled
analog/frontend processing and precedes correlation; quantized input is not
replaced by Gaussian projected noise. Record scale groups, clipping, underflow,
input precision, padding, scale formats and all partial accumulation choices.

Report correlation and likelihood errors, full-bank ranking changes, margins,
misses, wrong admissions and noise admissions. Fixed-template approximation and
data-dependent observation quantization have different null distributions.
An experimental FP4 gate is uncalibrated until supported separately. The safe
comparison recomputes the complete bank strictly, protecting the reference
decision without assuming an approximate shortlist preserves sensitivity.

Template energy, Gram construction, conditioning/rank decisions, long sample
coordinates, phase generation and final evidence accumulation retain FP64.
The downstream finite phase/channel model is unchanged within each comparison.

Default target: 99% correct reception and at most 10^-4 probability of any
noise-only admission per 24 hours. Relaxed target: 90% and 1% per 12 hours.
Search-family accounting includes hypotheses, attempts, durations, channel
models and selected policies. A union-bound gate does not qualify a nonlinear
quantized-noise model. Zero events in a small holdout do not establish these
tails. Sensitivity budgets of 0.1, 0.25, 0.5 and 1 dB remain unresolved unless
paired boundary data and confidence intervals actually resolve them.

Development seeds and shapes select the implementation. Final validation uses
fresh waveform/key, channel, oscillator and noise seeds. Correlated SNR sweeps,
paired policies and repeated timing inputs are never pooled as independent
physical trials. Nonlinear frontend cases use the existing sampled simulator.

## Timing and acceptance

The initial objective remains 5× useful complete-job throughput against strict
processing of the same search and nuisance model. Report kernel, resident
correlation, full receiver and complete job separately. Charge packing,
quantization, gathering, output conversion, padding, transfers, strict
recomputation and downstream inference. Report first-use latency, warmed
captures/s, hypotheses/s and streaming real-time factor. Repeated captures
measure timing only. Cost per operationally validated result is unavailable
until both reception and admission requirements are qualified.

Separate original sampled simulation/RNG and template/crypto generation from
loading existing inputs, cold DSP preparation and persistent workspace reuse.
CPU AES capability-controlled runs compare identical outputs; OpenSSL hardware
dispatch must not be inferred merely from CPU flags. Cached ciphertext or
templates are valid only within the same key, epoch, format and source-address
identity. GPU encryption is pursued only if measured preparation cost warrants
it. Amortization never turns cold work into an omitted cost.

Use CUDA events and CPU wall/CPU timings. Preserve profiler traces and report
counter access limitations. Operation counts, arithmetic intensity estimates
and estimated bandwidth are not measured SM/tensor/cache utilization. Measure
packing and launch overhead before attributing low efficiency to arithmetic.

Start with a bounded SM120 native-support probe. Then screen GEMM dimensions,
tiling, scale formats and cache reuse; evaluate exhaustive sampled small banks
and representative complete receivers. Increase expensive independent validation
only after selecting a reproducible promising policy. Record failed approaches
and quantify Amdahl ceilings when inference, preparation or strict refinement
prevents a complete-job gain. Retain exact source/input hashes, commands, seeds,
environment versions and technical artifacts. Cloud control records stay outside
the repository.
