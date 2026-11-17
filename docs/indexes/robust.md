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

For an agreed single-bit signal, **Pattern Steps** exposes each received DATA
bit and its public ordinal before text decoding or physical completion. A
completed sequence with no missing bits can also appear as binary raw
observations in Signals when text decoding rejects it. Browser filtering can
hide that completed row; it does not alter Pattern Steps or repair a wrong bit.
Repeating the same raw bit with the same key advances the public ordinal and
therefore changes the keyed waveform. Keep the receiver's agreed calendar in
step; a retry is not an instruction to restart either peer at ordinal zero.

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
| Unrelated keyed signals may produce a DATA decision | The GUI retains unfiltered baseline content and lets Wrong Key Admission filter the browser retrospectively. Compare passing DATA-bit counts; choose Unfiltered to restore retained records. The displayed dB estimates are scoped HF references; stronger filters may also hide wanted signals, including strong long-window signals. | Completed rated rows require at least half their admitted DATA bits to pass; this is a visibility rule, not FEC re-decoding or a calibrated message error rate. Scalar counts add no DSP search or worker. Headless receiver policies still support direct DATA-to-erasure admission. See [evidence and limits](../triad-stochastic.md#wrong-key-admission) and [GUI options](../gui-contract.md#modems-and-current-availability). |
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

For decoding, the original fixed-affine lane remains operational. Changing-channel
settings also enable bounded short-coherence, common-frequency tracking,
medium coherent gain and whole-slot drift alternatives. Their properly weighted likelihoods participate in
the final bit decision; the older adaptive timing and combined lanes remain
diagnostics. A weak native sweep missed by the original model and a complete
random-HF message miss motivated these additions. Short blocks alone did not
fix either weakness: structured drift preserved long-coherent evidence, while
a small chronological frequency mixture recovered the random-HF example.
Independent diffuse fading exposed another weak-signal miss: coherent timing
proposals and causal gain history repaired the sampled case. Slower fading
also benefited from history even when its weak holdout remained an erasure.
These bounded findings do not establish all-channel sensitivity. Historical
evolving-ray and phase-particle likelihoods remain alternatives to revisit when
a measured failure identifies missing dynamics and the native implementation
preserves full search, false-admission accounting and end-of-message tests.
The current default also compares an admitted short-coherence likelihood when
the fixed-affine lane already says DATA. With Helper Off and no segmented
coarse timing result, frequency tracking can score up to eight quarter-clock
nominees derived from the strongest ordinary coarse proposal for each bit.
Up to eight nominal-epoch intermediate-clock/arrival-endpoint cells additionally
prevent that public epoch from disappearing from the raw timing shortlist;
the existing atom priors remain in force, with a charged raw timing cap of 40.
For supported raw or Helper-projected short tracking, a separate public
midpoint-lag family adds at most six coordinates with finer clock placement as
calendar age grows. Its
fixed one-eighth tracker share charges the full public lag/epoch domain,
including unselected points; the old family retains seven-eighths. The expensive
nominee count is bounded, but observation and FFT support still reflect clock
uncertainty. See the [repeated-bit findings](../validation.md#repeated-single-bit-channel-diagnosis--16-november-2026)
for scope, controls and unresolved modes.
Longer coherent-gain and drift models keep their fallback policy. This accepts
bounded added work for natural changing-channel resilience while preserving the
public search and full worker budget.

For a supported short-frequency plan with nonzero declared delay, an additional
nominal-epoch two-path model uses independent proper-complex per-block gains
(variance 0.5 per path), with zero relative-Doppler offset and the full declared
delay at the public arrival endpoints. Its fixed-common-frequency whole-aperture
component and changing-common-frequency Markov component are mixed equally
over the same grid of up to 65 states; the branch receives 1/8 short-family
mass while the old short family retains 7/8.
That mixture is applied at whole-slot evidence combination; the 7/8 retention
costs the old branch at most `-log(7/8) ≈ 0.134` nats, not a dB sensitivity,
pointwise-decision or CPU-capacity bound.
The frequency states cover the declared range (maximum 65), while no delay grid
or historical path search is introduced. A zero
`nominal_independent_gain_frequency_states` receipt with a nonzero
`nominal_independent_gain_candidates` means the projected tracker fell back to
central frequency only. The finite full-stream cohort passes both matched
seed-9202 polarities and both fresh seed-9601 polarities (5/5 each), while a
seed-9602 noise control remains 5/5 QUIET. This supports only those sampled
standard-Gaussian histories. At the prior checkpoint, scoped positive-delay
Helper checks passed, as did GUI (28/28), appraisal (5/5) and application
integration; these were prior-checkpoint results. Final-source triad (40/40),
GUI (28/28), appraisal (5/5) and the seed-9202 candidate-only replay now pass;
details are in the [final-source record](../validation.md#final-source-repeated-long-alarm-check--16-november-2026).
These do not qualify broader channel sensitivity. Arbitrary clock
ages, all delay values, plasma histories and weak Helper sensitivity remain
unqualified. See the
[HF Worst follow-through](../validation.md#hf-worst-30-hz--7-ms-follow-through--16-november-2026)
for hashes, runtime costs and source-bound limits.
The original late-clock baseline refused an ordinal-12 case at an absolute 30M
template-work cap; current production removes that cap while retaining the
mapped-route heuristic and other resource guards. A bounded signal-derived
clock proposer returns DATA on two matched long-bit captures at 0 and −100 ppm.
A 278.4-second-bit preparation covers its 156 columns under the 3-GiB lease but
does not score IQ. The Helper fixed-only replay preserves selected DATA/QUIET
results with slightly higher elapsed time. Evaluator-known clock remains
diagnostic. The frozen-source functional groups and candidate-only repeated-bit
replay pass; fixed-only consumer details are in the
[follow-on record](../validation.md#late-clock-and-interior-delay-probes--16-november-2026).
Complete capacity remains unqualified.
The prior delay fixture omitted its 0.25 s public preroll, so its +12/+20 pair
establishes no in-bound delay floor. The frozen source passes five corrected
15.75/5.25/10.5 ms bit/null cases, one combined old-age/clock/delay case, and a
selected common-frequency motion replay. Full triad 40/40, GUI 28/28, appraisal
5/5 and the candidate-only repeated-bit replay pass. These do not qualify
complete text reception or capacity. The Helper fallback still decodes selected
cases with added work on a stale frequency bound. These do not provide broad delay,
clock-age or availability guarantee; see the [follow-on record](../validation.md#late-clock-and-interior-delay-probes--16-november-2026).

A stable component and a changing component can coexist within one unresolved
spatial channel. Fast-cadence changing-channel reception now includes that
additive alternative using existing correlations and bounded state, without a
duration cutoff. It recovers the selected +4 dB-Hz two-hop message that the
original bank misses; the +3 miss remains. The original family retains 75% of
its affine prior mass, bounding that matched contribution's log-evidence loss
by 0.288 nats, not a guaranteed dB or bit-error bound. Existing two-spatial-basis
profiles and slow VLF alternatives remain separate. Wider-bound direct-template
support merits reconsideration when a weak case demonstrates an admission loss;
the strong zero/7-ms declared-spread check passes both settings. Ordinary
two-tap posteriors also use stable covariance factors to prevent a reproduced
high-information rank-one failure. See the [evidence and limits](../validation.md#weak-physical-channel-alarm-investigation--16-november-2026).

The GUI Wrong Key Admission option
remains a retrospective filter on retained baseline results; it is neither
receiver authentication nor a replacement for the native bit decision. See
the [current short-model and clock findings](../validation.md#bounded-short-model-competition--16-november-2026)
for outcomes and remaining limits.
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
