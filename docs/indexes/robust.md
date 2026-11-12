# Robust operation and experimental lessons

This is the practical entry point for the three-carrier Robust modem. It links
current operating rules to the experiments that explain when a technique helps,
where it failed, and what remains unqualified. Read the
[current architecture](../robust-current.md) for implementation and limits,
the [protocol](../protocol.md) for exact transmission formats, and the
[GUI contract](../gui-contract.md) for controls. Research outcomes below are
conditional findings; they do not promise sensitivity or real-time service in
an untested configuration.

## Start with a configuration both peers can receive

Robust is one compiled C++ DATA, Helper, receiver, channel-simulation and audio
stack linked into the GUI. In-process owners share immutable buffers and a
bounded executor; Python only orchestrates native testing and analysis.

For live reception, first match the peers' public calendar epoch, initial keyed
ordinal, waveform profile, encryption/key, FEC, compression and burst schedule.
Configure the receiver's local input noise model and use the actual audio-device
timestamps where available. The receiver continuously scores DATA or null at
the configured addresses. It does not currently acquire an arbitrary start
time, waveform epoch or ordinal.

Choose Off when transmitting without encryption, a matching keyfile entry when
using private keys, or AMATEUR when both peers use the public amateur keyset.
AMATEUR is public key material; it does not provide confidentiality or
authenticate a sender. Noise transmission intentionally uses
fresh DATA key material and the public Helper key. See the
[GUI map](../gui-contract.md#modems-and-current-availability),
[key policy](../robust-current.md#transmit-key-policy) and exact
[protocol profiles](../protocol.md).

The short text format covers 1–16 bytes. Longer text is emitted in fixed marker
and codeword intervals; Off, RS20 and RS60 are agreed settings, not autodetected
headers. Exact raw-bit drafts bypass text coding and FEC. Two fully scored quiet
slots after DATA establish physical completion. EOF drains accepted input but
cannot stand in for radio silence; received lengths never set DSP geometry or
message completion. These rules also explain why a missing final uncompressed
interval may leave a valid shorter message. The
[protocol reference](../protocol.md) has the bit-level layout and limits.

Use duration planning to estimate airtime and set reasonable expectations; it
is a model, not a sensitivity, reliability or real-time guarantee. Public
calendar waits add wall-clock time but do not lower the peak workspace of one
search. Random-extra waits are reproducible only with matching key/calendar
settings and do not create quiet-slot completion evidence. See
[duration assumptions](../triad-duration-planning.md) and
[burst scheduling](../symbol-burst.md).

For first-time operating choices, configure waveform, Helper, channel, clock,
audio and FEC through the shared GUI. Use simulation's Off settings as an
explicit clean baseline before enabling Doppler, delay spread, selective
perturbation or noise. Those controls are synthetic stress assumptions rather
than field calibration. See the [channel model](../channel-models.md) and
[GUI controls](../gui-contract.md).

Leave Search arithmetic on **Default** for automatic native routing. The
receiver defaults to the full `W=2*H` software-worker budget shared across
owners, where `H` is the available logical hardware-thread capacity. Explicit
worker, owner and memory limits remain exact. Turn on
the GUI GPU option when a compiled CUDA or Vulkan device is available, then
check diagnostics for the device, executed stages and fallback reason. This
preserves the same total CPU budget and does not promise speedup on every
geometry. See the [arithmetic controls](../search-arithmetic.md) and
[GPU ownership](../robust-current.md#native-interfaces-and-ownership).

Keep the GUI Target SNR (C/N0, default 32 dB-Hz) separate from the simulated
link's transmit power, path loss and noise density. Live input noise is a local
dBFS/Hz setting; it is not the same quantity as link noise in dBm/Hz. The
Transmit noise action creates an outgoing noise waveform; it does not measure
or configure the receiver noise floor. See [GUI meanings](../gui-contract.md#modems-and-current-availability)
and [duration-planning limits](../triad-duration-planning.md).

## Choose techniques by the situation

| Situation | Supported starting choice | What the evidence says and when to revisit |
| --- | --- | --- |
| Peer start time, epoch or ordinal is not known | Establish a shared public calendar and keyed ordinal first. | Unknown-address acquisition is outside the current receiver scope. A retired fresh-onset prototype admitted DATA under a wrong ordinal. Broader acquisition needs competing-address and incorrect-address checks plus measured work; see [current limits](../robust-current.md#configured-calendar-reception). |
| Frequency uncertainty is large, but a Helper can be received | Enable the configured Helper and keep its public frequency, timing and power bounds honest. | A Helper can narrow frequency proposals without proving phase transfer or removing arrival-prior cost. Historical sampled dynamic-channel cohorts found help from stronger Helper pilots in selected weak cases; this is not universal receiver sensitivity. Revisit for fading, pilot loss or known oscillator conditions using the [Helper findings](../triad-stochastic.md) and [native validation](../validation.md#helper-acquisition-overlap-follow-through). |
| Helper phase is unstable, but its frequency is useful | Use the configured frequency proposal while retaining a DATA-only fallback within the public hypothesis set. | Frequency information can remain useful when coherent phase transfer does not. A retired blind coarse timing/epoch pilot search failed badly under relative-phase quantization; that coarse phase fit is not a universal gate for DATA. See [stochastic-channel results](../triad-stochastic.md#retained-evidence). |
| Channel is moving or dispersive | Use a continuous channel state across chunks and include the delayed tail in simulation. Test static and time-varying paths separately. | Selected dynamic-likelihood trials beat a static two-ray model, but had known synchronization, finite timing/epoch banks and seeded model assumptions. They do not establish field sensitivity, daily false-admission rate or blind multi-day acquisition. See [channel limits](../channel-models.md#interpretation-limits) and the dated [stochastic record](../validation-data/triad-v3/2026-11-05-stochastic/README.md). |
| Very short DATA windows or AGC-like power variation matter | Keep selected rate, Added Symbols, interleave, shaping and carrier spacing fixed when comparing. Treat the power-envelope screen as a diagnostic. | Correlated waveform windows found that modestly widening 75-Hz spacing to 90 Hz did not reliably pass the 100-ms screen, and one case worsened. Rate and added symbols changed the shortest passing window substantially. This is not a radio AGC, sensitivity or spectrum-compliance result. Revisit only against a declared receiver/front-end limit; see [pattern](../triad-patterns.md) and [shaping findings](../triad-shaping.md). |
| Long messages or scheduled gaps are needed | Keep the public ordinal continuous; use matching burst settings and the two-quiet completion rule. | Extra wait separates transmissions in calendar time; it does not cut one search's peak workspace or prove backlog recovery. Long-coordinate arithmetic checks are not multi-day physical reception. See [burst rules](../symbol-burst.md) and [clock/tracking scope](../triad-clock-follow.md). |
| A manual configuration is computationally expensive | Compare rate and spreading together with the native duration, minimum cadence and full workspace; use a burst wait when added latency is acceptable. | On the 5650U, row-6 HF-FHSS at 72 Hz/10x reduced sampled active/quiet slot demand per scheduled second by 57–61% versus 36 Hz/10x; EME at unchanged 360 Hz with 2x Interleave reduced it by 21–31%. Other rate/spread changes were modest, cancelled by faster cadence, inadmissible, or missed DATA. Short message checks passed for the selected candidates, but conservative complete capacity remains unqualified. See the [all-eight parameter trends](../validation.md#manual-rate-spreading-and-burst-trends--12-november-2026). |
| Several onset apertures or public addresses are plausible | Preserve every applicable public hypothesis and account for all proposal and refinement work. | A favorable proposal is not an exhaustive-search or message-admission proof. The current configured-calendar receiver has multiple-aperture tests; unknown-start acquisition does not. Keep those scopes distinct. See the [current coarse search](../robust-current.md#configured-calendar-reception) and [validation limits](../validation.md#current-receiver-overlap-qualification). |
| Broad multi-arrival search dominates | Prefer the qualified automatic full-coverage FFT path; keep the full public grid and resource bound. | Current CPU/GPU routes apply explicit geometry, condition, workspace and feature guards. Automatic mode prioritizes difficult-case service and backlog ceiling; easy-case speed is not a reason to drop hypotheses. See [arithmetic selection](../search-arithmetic.md) and [precision controls](../gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling). |
| No Helper and a single exact arrival are available | Compare the guarded direct or chirp-Z route with the FFT for that arrival geometry, including the full frequency coverage. | On a measured native CPU shape, automatic chirp-Z beat the FFT; it is selected only when estimated work has the required margin. This exact-arrival alternative can cover a broad frequency sweep. GPU matrix contractions may suit suitably batched, reusable shapes; packing and launch costs can dominate tiny jobs. Neither isolated result ranks all devices or multi-arrival work. See [chirp-Z evidence](../validation.md#exact-one-arrival-chirp-z-follow-through) and [matrix dispatch](../validation.md#current-cuda-precision-and-tensor-dispatch). |

The historical power-envelope rows are correlated samples of the emitted
waveform under named parameter sets, not independent captures. At the stated
quarter-step envelope threshold, rate 36 with Added Symbols 10 and interleave
passed from 150 ms; without interleave it required 200 ms.
Added Symbols 1 did not pass up to 800 ms. FHSS rate 360 with Added Symbols 1
and a burst with three wait tiles passed from 400 ms, while rate 24,000 with
Added Symbols 1 and rate 3.6 with Added Symbols 1,000 passed from 50 ms.
These correlated waveform-screen rows show how strongly the threshold depends
on settings; they are not recommended values or a radio AGC/sensitivity test.
Full setup, hashes and failure rows are in the [pattern evidence](../validation-data/triad-v3/2026-11-06-independent-patterns/README.md)
and [spacing evidence](../validation-data/triad-v3/2026-11-06-spacing/README.md).

For decoding, the current receiver computes fixed-affine, adaptive and combined
evidence, but only the fixed-affine lane controls operational admission and the
final bit. Treat the others as diagnostics. Historical evolving-ray and
phase-particle likelihoods are possible model choices, not GUI modes or proven
improvements over the current decision rule. Revisit them only when a measured
channel failure identifies the missing dynamics and the native alternative
preserves the full search, false-admission accounting and end-of-message tests.
See [receiver details](../robust-current.md#configured-calendar-reception) and
[historical likelihood research](../triad-v3.md#likelihood-techniques-worth-retaining).

## Compute choices and alternatives

Current defaults and measured hardware are distinct from techniques worth
revisiting. Select by complete affected work, admitted workspace, executed
arithmetic and fallback reason. Do not infer whole-receiver speed from bit
width, theoretical peaks, storage size or a component kernel.

| Work shape | Current path or useful alternative | Finding and boundary |
| --- | --- | --- |
| CPU frequency search after DATA channel isolation | Qualified AVX2 Q15 spectral products and FP32 FFTs; use FP64 for sensitive coordinates/state and guarded lag/tight-clock or ill-conditioned routes. | The local Ryzen 5 PRO 5650U supports AVX2 and F16C conversion, not native packed FP16 arithmetic. Current Q15/FP32 choices have their own geometry/conditioning bounds. See [search arithmetic](../search-arithmetic.md#native-cpu-selection). |
| Single-ray, one-arrival or a narrow exact frequency interval | Direct sum or FP64 chirp-Z can beat a broad FFT when total transform-row work is small enough. | This is a geometry-specific CPU crossover, not permission to omit hypotheses within the declared interval. Current automatic guards include a modeled work-margin requirement. The algorithm must still cover the full configured frequency interval; see [native CPU results](../triad-performance.md#native-cpu-arithmetic-comparison). |
| Radeon Vega GPU, channel already isolated | Qualified packed-FP16 direct vector arithmetic with FP32 accumulation and FP64 scoring on the supported envelope. | On the local Vulkan device, the matched five-slot `8b842916`-to-candidate comparison processed 279,864 packed-FP16 score rows and completed in 0.978 versus 1.017 s. A separate comparison against the historical FP64 CPU path improved the broad coarse phase 1.269× while a narrow case was 39.6% slower. This is geometry-specific; shared RAM and workspace matter. See [Radeon evidence](../validation.md#local-radeon-vulkan-backend) and [overlap comparison](../validation.md#current-receiver-overlap-qualification). |
| VkFFT half-storage mode | Use it when lower buffer/cache footprint helps an admitted job. | VkFFT half storage may still compute in FP32. Buffer type alone does not prove packed FP16 arithmetic or a compute-speed benefit. The current Radeon packed direct route is a separate implementation. See [precision stage boundaries](../gpu-precision-controls.md#vulkan-on-vega-integrated-graphics). |
| CUDA broad, multi-arrival FFT on supported Ada/Blackwell hardware | Current automatic dispatch can use actual half-cuFFT under public-size, arrival-count, conditioning, workspace and fallback guards. | In recent matched overlap comparisons on RTX 4090 and RTX PRO 6000 Blackwell, the large workload improved about 10–22% while the arithmetic route stayed half-cuFFT. This measures scheduling/dataflow changes, not FP16 versus FP32. Receipts showed zero HMMA/IMMA. Small ready-backlog pairs varied; no universal capacity or sensitivity claim follows. Half-cuFFT is not a Tensor Core FFT. See [current CUDA route](../gpu-precision-controls.md#gui-automatic-gpu-selection) and [paired results](../validation.md#current-receiver-overlap-qualification). |
| Suitably batched, reusable correlation or phase-state contractions | FP16/INT8 or mixed-precision matrix work may fit Tensor Core shapes; historical NVFP4, split-BF16 and TF32x3 techniques show what to investigate. | Git history contains real Tensor Core wins, including packing-inclusive large correlation on PRO 6000 and phase-state work on RTX 5090. Other shapes, including complete RTX 5090 receiver and PRO 6000 receiver stages, favored FP32 or showed acquisition slowdown. These Python/Triton or retired-waveform results are research candidates, not native receiver qualification. See [full Tensor/GPU history](../triad-gpu-optimization.md#verified-git-history-inventory). |
| Long repeated work with identical geometry | Consider cached plans, resident coefficients/buffers, fusion or CUDA graphs when setup amortizes across enough batches. | Historical fixed-shape graph and cache gains were large in some studies; a warm CPU single-use cache was slower, and one lag transform sped up while acquisition became slower. Data generation, packing, transfers, resets and end-to-end stages decide the result. See [GPU optimization lessons](../triad-gpu-optimization.md#what-to-measure-separately). |
| CPU utilization or GPU batch preparation limits service | Overlap independent preparation/lookahead with an owner; retain ordered commits and actual W=2*H work limits. | Current native CPU and GPU pipelines use bounded owner/workspace shapes. Recent tests confirmed preserved work and outcomes; backlog results were mixed and remain non-capacity evidence. More workers, owners or streams are not automatically faster. See [overlap contract](../development.md#overlap-within-a-slot) and [current qualification](../validation.md#current-receiver-overlap-qualification). |

The [CPU history inventory](../triad-performance.md#earlier-cpu-implementations-worth-reconsidering)
also records native DC sums, shared spectra, candidate-parallel updates,
mapping reuse and ready-window overlap. Some techniques survived into the
native stack; others remain worth reconsidering only when source geometry and
complete-work measurements justify them.

High-SNR functional checks do not establish low-SNR sensitivity. The exact
native precision controls distinguish packed arithmetic from storage,
FFT arithmetic from matrix contractions, and bulk score precision from
conditioning-sensitive state. Higher precision remains useful where it protects
rank, likelihood or coordinate decisions; quiet spectrum alone does not bound
FFT intermediate range. Compare proportionate sensitivity cost against complete
work and workspace rather than demanding universal FP64 agreement. See
[precision requirements and gates](../gpu-precision-controls.md#qualifying-a-faster-path).

If an applicable failure points to unknown but common stationary white-noise
power, history contains a common-scale angular likelihood and an independent
residual-based variance bound. These rely on a fixed scored bank, appropriate
rank and noise assumptions; residuals chosen after selecting a winner do not
retain that independence. Colored, block-varying or adaptive search needs a
different model. They are derivations to reassess against the actual filtered
input and current native likelihood, not selectable GUI options. See
[noise-model limits](../triad-noise-calibration.md#model-and-admission-boundaries).

## How to read the evidence

Use the evidence that matches the question:

* [Protocol](../protocol.md) and [GUI contract](../gui-contract.md) define
  current controls and interoperability rules.
* [Current architecture](../robust-current.md) and
  [precision guidance](../gpu-precision-controls.md) define current routes,
  guards, ownership and fallbacks.
* [Validation](../validation.md) records current-source checks and measured
  CPU/GPU service. Recent paired CUDA improvements qualify only the recorded
  device, shape and matched work; the mixed backlog and full-coarse sanitizer
  timeout remain visible.
* [Pattern](../triad-patterns.md), [stochastic-channel](../triad-stochastic.md),
  [shaping](../triad-shaping.md), [clock](../triad-clock-follow.md), and
  [GPU history](../triad-gpu-optimization.md) explain useful alternatives,
  failures, provenance and conditions for reconsidering them.
* The dated [validation data index](evidence.md) points to source-bound
  records. Those records are historical research and should not be silently
  promoted into current sensitivity, capacity or physical-radio claims.

The current CPU and receiver have not passed an absolute all-configuration
capacity claim. Actual-device Radeon and NVIDIA checks are narrower than a
hardware-family guarantee. Unknown-start acquisition, broad physical channel
qualification and general sensitivity remain open. For new work, consult the
[development contract](../development.md), [bounded test method](../triad-performance.md)
and [manual capacity workflow](../robust-manual-capacity.md). Preserve the
question, full matched work, input/source/device, arithmetic actually executed,
workspace, unfavorable outcomes and stop rule. Retain compact findings and
remove raw experiments when they no longer serve a declared validation need,
following [material lifecycle](../development.md#material-lifecycle).
