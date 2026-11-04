# GPU precision choices and the numerical precision control

**Read this before changing GPU arithmetic or deciding what the precision
dropdown should force.** Treat bulk products, reductions, sensitive preparation
and exact protocol state separately. Cheap higher-precision work can protect a
fast pipeline; expensive work left in FP64 can also prevent the user's selected
lower/tensor arithmetic from providing any useful acceleration. Neither a
blanket downcast nor an unexplained higher-precision fallback is a satisfactory
implementation of an explicit experimental choice.

This is an experiment and control-design guide. It records current behavior and
measured evidence, then identifies decisions still requiring experiments. It
does not add GUI options, change receiver defaults, qualify an experimental
format, or integrate the isolated three-carrier study into ordinary Robust.
The [development contract](development.md), [current search arithmetic](search-arithmetic.md)
and [GPU comparison protocol](triad-gpu-optimization.md) remain applicable.

The [repository evidence map](validation-data/triad-v3/precision-controls/README.md)
links these recommendations to exact archived result/source/input paths,
directly browsable test logs, failed experiments, integrity checks and
reproduction commands. Consult it before treating a reported speedup or
precision exception as established for another workload.

## Current control versus proposed GPU controls

The current developer-only dropdown is **Search arithmetic**, exposed on the
CLI as `--search-arithmetic`. It is a receiver execution policy, not a global
cast of every floating-point operation:

| Existing choice | Contract and important limit |
| --- | --- |
| Default — automatic | Uses demonstrated native FP32 paths and eligible reuse, with wider paths elsewhere. Some experimental paths are Default-only; do not assume bit-identical dispatch to FP32 minimum. |
| FP32 minimum (`fp32-min`) | A floor on search operand precision, **not FP32 force or a maximum width**. FP64 fallbacks remain. TF32, FP16, BF16, FP8 and FP4 products must not be silently introduced under this label, even with FP32 accumulators or a multi-product approximation. |
| FP64 force (`fp64-force`) | Original templates and FP64 search, preserving existing wider sensitive calculations. It also bypasses approximate template interpolation. Exact caches remain permitted. A Default/FP64 comparison therefore does not isolate arithmetic width unless reuse is held fixed. |

Supported CPU acquisition products/FFTs can run FP32 while timing, guards,
compact statistics, Gram/covariance and other stages remain wider. Mapped split
Sched_Hop retains substantial FP64 work; that is **partial precision coverage**,
not evidence that every retained operation requires FP64. The maintained
[ordinary receiver inventory](search-compute.md#experimental-cpu-search-arithmetic-local-manual-candidate)
describes these distinctions. Selection must not change transmitted PCM,
framing, crypto addresses or waveform identity.

The current dropdown does **not** drive an ordinary GPU receiver or the isolated
Triad Python experiments. Those experiments expose separate `--precision`,
`--tensor-format`, `--tensor-engine`, `--tensor-packing`,
`--tensor-accumulation`, `--inference-precision` and
`--inference-implementation` options. Their existence is not GUI support.
`auto` selects an engine for the requested tensor format, not a different
format. The campaign's `fp64` correlation label still shares its separately
specified inference precision; it does not mean an entirely FP64 receiver.

Current implementation entry points are [the policy enum](../include/datapump/modem.hpp),
[GUI choices and propagation](../src/gui/controller.cpp),
[parsing and resolution](../src/tuning.cpp),
[low-level CPU resolution](../src/pattern_fft_batch.hpp), and
[the isolated experiment CLI](../tools/triad-fp4-campaign.py).
[Tuning tests](../tests/test_tuning.cpp) and
[GUI controller tests](../tests/test_gui_controller.cpp) preserve the public choices
and receiver-only scope. Recheck these sources when extending the control.

## Measured lessons that should influence experiments

These are observations on named workloads, not universal precision assignments.
The linked reports preserve hashes, commands, failures and qualification limits.

| Evidence | Decision it supports, and what it does not establish |
| --- | --- |
| Initial RTX 5090 B1 paired state inference: FP64 bulk took about **0.704 s**, strict FP32 **0.202 s**. Long log evidence/reductions can remain FP64 with FP32 bulk. [Stage record](validation-data/triad-v3/2026-11-01-optimization/precision-stages.md). | Making all bulk work FP64 can be costly (3.49× inference time here). A sensitive reduction is not a reason to force the whole receiver to FP64. These are inference costs, not complete acquisition or sensitivity equivalence. |
| Keeping both posterior-reduction levels FP64 while eliminating a full intermediate improved full-hour paired inference about **1.15×** in both execution orders; maximum score difference **4.3e−14**. Its standalone reduction kernel was slower than the comparator. [Optimization report](validation-data/triad-v3/2026-11-01-optimization/README.md). | Optimize representation, intermediate traffic and fusion before assuming lower reduction precision is necessary. The benefit is measured in the surrounding pipeline. In a later B12 trace FP64 partial reduction was **7.8% of kernel time**, so it is not always negligible. [Tensor report](validation-data/triad-v3/2026-11-01-tensor/README.md). |
| Cached CPU FP64 covariance/design preparation cost **0.158–0.174 s** on synthetic B32/929 blocks. Resident gains of 3.78×/4.06× fell to 1.64×/1.73× including preparation, transfers and verification; warm CPU single-use was about 0.95×. [Stage record](validation-data/triad-v3/2026-11-01-optimization/precision-stages.md). | Sensitive cached preparation can be economical with reuse, but "preparation" is not synonymous with cheap. Measure changing-channel/cache-miss and single-use costs. This Gaussian method does not validate nonlinear quantized noise. |
| RTX 4090 product scaling used an FP64 power multiplication followed by FP32 rounding to fix a measured subnormal defect. FP8 K32 tensor partials plus explicit FP32 additions reduced sampled arithmetic error **21.85×/26.62×** at similar kernel time; FP64 outer sums gave the same sampled answers at about **6.3×** the time. [Consumer report](validation-data/triad-v3/2026-11-01-consumer-gpu/README.md). | Tiny FP64 restoration and FP32 partial summation are different choices from FP64 bulk GEMM. The epilogue's standalone share was not measured. More precision everywhere is not automatically useful; repair arithmetic on represented values separately from input quantization. |
| RTX 4090 flat P512/F43/Q3, 85-block, 36-hypothesis receiver: identified correlation/reduction was **3.47%**, or FP16 product+packing **4.87%**, of instrumented kernel time. Removing only those kernels gives conditional kernel ceilings **1.036×/1.051×**. Phase stencil/summation/update families contribute at least **32%**. [Consumer report](validation-data/triad-v3/2026-11-01-consumer-gpu/README.md). | Prioritize the other state-update work for this shape; a fast correlation GEMM cannot alone deliver 5× receiver throughput. These are kernel-event shares, not complete-job shares or measured SM/tensor/DRAM counters. A broader search can have a different bottleneck. |
| Same RTX 4090 sustained workspace: strict FP32 **63.15**, FP16 **61.71** capture replays/s; FP16 batch eight **47.20/s**. Fused packing improved the synthetic pack+GEMM organization about 4.24×, without a receiver gain. Joint FP8 quantization added one/two relaxed-gate misses on 96 fresh signal captures. [Consumer report](validation-data/triad-v3/2026-11-01-consumer-gpu/README.md). | Tensor availability, larger batches and a lower dtype do not imply a useful speedup or safe sensitivity. Replay rates are not independent receptions. Better FP8 accumulation did not remove representation loss. |

## Stage allocation: what should follow the control?

The last column proposes experiments and future control scope. It is not a claim
that the present dropdown implements these choices. **No stage is declared
inexpensive merely because it has few lines of code, few output values, or is
called "verification" or "preparation".** Charge its packing, allocations,
transfers, synchronization and cache misses at the actual call frequency.
Retaining FP64 in a reference does not demonstrate that FP64 is necessary:
where a paired stage-specific precision sweep is absent, its sensitivity
benefit over lower arithmetic remains unmeasured.

| Stage | Numerical risk and current study practice | Cost/placement question | Guidance for forcing and required experiment |
| --- | --- | --- | --- |
| AES/clock-derived ciphertext, sample counters, indices, interleave and search-family accounting | Exact integer/cryptographic semantics; existing CPU generation and identity-bound caches. | AES-NI versus GPU encryption is unmeasured. Cache only matching key, epoch, coordinates, waveform and format identity; charge cold generation. | Outside a floating-point dropdown. Expose placement/cache experiments separately; never trade bit/address correctness for a floating-point speed setting. |
| Long timestamps, clock drift and oscillator/phase generation | Integer sample coordinates plus FP64 physical state/reference; large absolute coordinates and small differences can lose precision. | Separate infrequent origin calculation from per-sample bulk synthesis. An FP64 per-sample path can be expensive. | Retain precise origins/counts; test relative coordinates, range reduction and lower-precision local recurrence separately. Require continuous hour/day, hop/chunk boundaries and accumulated phase-error checks before making bulk synthesis follow a lower mode. |
| Template generation, FFT/correlation/GEMM, demodulation and energy arrays | Large products and long sums; strict FP32/FP64 and explicit tensor candidates exist in the isolated study. | Measure actual shapes, reuse, padding, packing, reconstruction and transfers. Correlation's small share above is workload-specific. | Principal candidates to obey an explicit **bulk** format request. Missing implementations should report unsupported or visible partial coverage, not silently run expensive FP64. Test FP32 partials, compensated/pairwise sums, scaling and selective refinement; hold the full search and source observations fixed. |
| Template and observation storage/quantization | Fixed template approximation differs from nonlinear quantized input noise; FP32 output cannot restore lost input bits. | Packing can exceed the product cost. Quantized template caches can amortize; each new observation still costs work. | Separate storage from products. Compare original, template-only, observation-only and joint quantization using sampled observations and represented-input FP64 oracles. Keep ADC/DAC full scale, ENOB, clipping and dither as separate physical controls. |
| Template energies, Gram/covariance, whitening and noise estimates | Small errors can change likelihood normalization or ill-conditioned channel fits. Existing paths retain FP64/complex128 preparation/Grams. | Cached small designs may be cheap; large searched or continually changing covariance may dominate. Include estimation and cache invalidation, not only matrix factor time. | Candidate higher-precision exceptions for named bulk modes, with measured cost. Also permit explicit diagnostic lower-precision preparation: test cancellation, PSD/Hermitian residuals, whitening error and scores. Do not protect all large Gram products from tensor experiments simply because their final reduction/factor needs precision. |
| QR/SVD, factorization and numerical rank | Nearly indistinguishable subspaces/deep fades can turn a small perturbation into a different model/rank. Rank policy must stay fixed in arithmetic comparisons. | Small CPU FP64 factorizations may be cheap after reuse; transfers and repeated fits may reverse that. General SVD precision is not qualified by these campaigns. | Retain independent FP64 reference/rank decisions. Experiment with fast bulk construction plus FP64 small factor/refinement, and separately with lower factors. Record singular/eigenvalue margins, rank changes, conditioning and reception; do not change rank tolerance to conceal numerical loss. |
| Bulk phase/frequency state prediction, emissions and channel-model batches | Recurrence can accumulate transition bias and lose tiny probability mass. Plain BF16/TF32/scaled FP16 failed a recorded likelihood-error gate; split BF16/TF32x3 were more accurate but remain experimental. | A measured major cost; arithmetic, layout, fusion and memory traffic must all be considered. Compare state count and shape unchanged. | Must be independently addressable in experiments: a correlation-only knob is inadequate if inference dominates. Test explicit lower/tensor modes, multi-component representations and strict references. Preserve states, transitions, nuisance priors and normalization semantics; test hour/day accumulated error and near-boundary margins. |
| Posterior normalization, long evidence sums, cancellation-sensitive margins and scale restoration | FP64 reductions/evidence remain the reference policy; their sensitivity benefit over lower reductions is not yet quantified. The FP64 scale-restoration fix corrected a demonstrated arithmetic defect. FP32 outer sums can be preferable to FP64 for other products. Predictor and posterior normalization are different stages. | Measure each reduction/epilogue in context; 7.8% partial-reduction kernel share is not zero. The 1.15× reduction experiment compares two FP64 organizations, not FP64 against FP32 summation. Compare fused/register reductions and CPU/GPU placement. | Default candidate for a narrow documented exception, not permission to convert the entire state array or bulk kernel to FP64. Expose stage overrides for diagnostics; compare native, K32 partial, pairwise/compensated, FP32 and FP64 paths on identical represented inputs and long recurrences. |
| Final bit mixtures, thresholds, tail probabilities and duration planning | Existing host FP64 finish; tiny operational tails and likelihood margins can be sensitive. Accurate arithmetic does not validate a wrong null/interference model. | Usually a small output stage, but standalone cost share is not fully isolated. Device-to-host synchronization or running it per hypothesis can dominate. | Keep FP64 as the initial exception candidate and measure full finishing cost. Test batching/residency before reducing precision. Any lower arithmetic requires threshold/margin and calibrated family-level admission checks; never silently weaken a gate to make it pass. |

## A useful future forcing contract

Keep **requested policy**, **resolved per-stage arithmetic** and **template
reuse/model choices** separate. A single dropdown may choose a documented
profile; it must not conceal what the expensive stages actually execute.

- A **precision floor** prohibits arithmetic below that floor within its
  declared scope, while allowing wider work. That is the existing FP32-minimum
  meaning; it cannot be used to force a bottleneck down from FP64.
- A **named bulk mode** specifies input/storage policy, multiplication format,
  accumulator and reduction algorithm for the targeted expensive stages.
  Explicitly list any higher-precision preparation, reductions or epilogues.
  This allows low/tensor bulk work without sacrificing a beneficial small
  exception. Do not call the result "FP16 everywhere" or "strict FP32" when
  those are not its arithmetic contracts.
- An **explicit diagnostic stage override** must execute the requested stage
  arithmetic or report unsupported. Do not silently restore higher precision
  and present its timing as the forced experiment. A genuinely all-floating-
  stage force, if ever offered, cannot retain undisclosed exceptions; exact
  crypto/integer state still is not a floating-point choice.

Do not make a potentially valuable low-precision experiment inaccessible
because a small stage benefits from FP64. Equally, do not infer permission to
lower a sensitive stage just because a bulk mode was selected. If expensive
stages ignore a request due to missing kernels, record their names, reasons,
time shares and potential speedup ceiling. This is an implementation gap to
investigate, not proof of a numerical necessity or a completed precision port.

Diagnostics should record actual storage, multiply, accumulator, reduction and
output formats; scaling/clipping policy; CPU/GPU placement; exact/approximate
cache reuse; fallback count/reason; and source/device/library identity. Verify
native dense instructions where claimed. Format support differs across
[Turing, Ampere, Ada and Blackwell](validation-data/triad-v3/2026-11-01-consumer-gpu/PROTOCOL.md#hardware-and-arithmetic-contracts);
an FP32 output is not evidence of FP32 multiplication, and FP4 emulated through
another format is not native FP4. Keep hardware/library unsupported separate
from accuracy failure and from a slower supported implementation.

## Experiment procedure for deciding dropdown scope

1. **Name the decision.** Specify the stage, current policy, proposed override,
   reference, hardware, workload/search family, caching/reuse assumptions and
   sensitivity budget before measuring. Keep arithmetic separate from model,
   rank tolerance, pruning, template interpolation and batch independence.
2. **Measure cost before forcing.** Report cold and amortized preparation,
   packing, transfers, simulation/RNG, acquisition, inference, finishing and
   complete wall time. For a serial non-overlapping fraction `f` improved by
   `s`, the conditional ceiling is `1 / ((1-f) + f/s)`. Use the matching timing
   denominator: kernel shares cannot establish a complete-job ceiling. Check
   memory/launch limits before expecting narrower multiplication to help.
3. **Cross bulk and sensitive precision deliberately.** Compare strict bulk
   with high-precision sensitive stages; lower/tensor bulk with those stages
   retained; then lower the sensitive stage alone. Include all-high and
   represented-input FP64 controls. This reveals whether a cheap exception is
   valuable and whether a costly hidden exception prevents the desired gain.
4. **Use paired physical observations.** Separate template, observation and
   arithmetic errors. Include subnormals, saturation, long sums, cancellation,
   near-singular channels, deep fades, interference/ADC headroom and genuine
   long-time state. Apply nonlinear processing to actual sampled observations;
   a projected Gaussian shortcut cannot validate quantized/clipped input noise.
5. **Judge receiver outcomes.** Record likelihood error/bias and margins,
   probability-mass/normalization error, candidate rankings, rank changes,
   misses, wrong admissions and noise-only admissions. A constructed sum,
   average score agreement, high-SNR success or unchanged few-trial decisions
   is not sufficient sensitivity or tail evidence. Include selective refinement
   cost and prove any screening/pruning does not discard recoverable signals.
6. **Freeze before fresh validation.** Use independent waveform/channel/
   oscillator/noise roots and separate calibration from holdout. Account for
   the full search and repeated-attempt family, including policy selection.
   Preserve 99% / `10^-4` any noise admission per day, and separately 90% / 1%
   per 12 hours. Resolve 0.1/0.25/0.5/1 dB loss budgets with intervals; the
   existing GPU studies do not resolve those operational budgets.
7. **Record a stage decision.** Classify it as supported bulk override,
   measured higher-precision exception, slow/unhelpful optimization, numerical
   failure, implementation unavailable, or unmeasured. Record the exception's
   complete-runtime contribution and sensitivity benefit (or unknown values).
   Only change an automatic/default policy after the applicable gates pass;
   explicit experimental modes must retain their unqualified status meanwhile.

Useful first experiments are the measured state-update/reduction bottlenecks
and reuse of preparation across genuinely independent captures. Keep AES-NI/
GPU encryption, CPU/GPU sensitive-operation placement and cache reuse as
separate experiments. The current replay and kernel results do not establish
independent-trial streaming efficiency, laptop/eGPU performance, operational
calibration, or a reason to force every DSP stage to one dtype.
