# Developing the isolated three-carrier Robust modem

Use this workflow for isolated **three-adjacent-carrier Robust v3** development,
including its transmitter, simulator, planner, receiver, and integration
planning. The experiment remains separate from the ordinary Robust modem; its
specific contracts do not replace ordinary Robust compatibility requirements.
It also does not revive the historical v1/v2 experimental formats. V3 remains
a separate streaming study. Its waveform, key addressing, channel state, and
reception contracts are in the [v3 study](triad-v3.md); this page describes how
to make the next bounded development decision without replaying old campaigns
by default.

For isolated performance regressions, first follow the
[required efficient regression workflow](triad-performance.md#required-efficient-regression-workflow)
and [confidence and material-slowdown policy](triad-performance.md#confidence-and-material-slowdown).
The procedure covers computation paths while minimizing work, with validated
predictive chains where they save total work and direct full-target timing where
that is cheaper. Shared-code and ordinary-receiver changes retain their own
requirements.

For the new Helper transmitter/triad receiver composition, use
[Helper-assisted triad acquisition](triad-helper.md). It retains the complete
declared timing/epoch bank, measures shared-channel leakage, and calibrates the
whole adaptive frequency-selection procedure before independent held-out trials.
Its small focused checks are the routine entry point; the sensitivity campaign
is not an automatic regression prerequisite.

For ADC/DAC cable comparisons, use the [Live/Duplex hierarchy](triad-live-duplex.md).
It keeps public probe synchronization and measured audio noise separate from
operational receiver acquisition/admission. Do not feed physical captures to a
workspace that asserts a known ideal Gaussian simulation noise model.
For actual cable reception comparisons, its `triad_live_margins.py` path reuses
the maintained FFT receiver through an explicit physical adapter and a separate
empirical null gate. Keep that marker-conditioned experimental gate distinct
from the analytic operational false-admission guarantees.
The [actual cable reception evidence](validation-data/triad-v3/2026-11-04-live-reception/README.md)
includes successful comparisons and excluded floor captures with real playback
underruns. Read its short-test guidance before repeating hardware campaigns.
The [physical noise and margin follow-up](validation-data/triad-v3/2026-11-04-live-margin-bounds/README.md)
then isolates out-of-band audio-DC contamination, validates a cheap fixed narrow
frontend on fresh low-level captures, and records the limits of its conditional
SNR-loss proxy. Use its retained observations and focused Python checks before
spending hardware time. Its passband is qualified only for the stated flat
geometries; strong-spread sidebands need a new envelope and calibration.

**For a large timing bank, start with the
[sampled FFT acquisition study](validation-data/triad-v3/2026-11-03-fft-timing/README.md).**
[`tools/triad_fft_acquire.py`](../tools/triad_fft_acquire.py) consumes one
actual-noisy `export-sampled` capture with one public base synchronization group.
It reuses the shaped templates across every integer-sample arrival in the
declared lattice; CFO phase is applied at the original sample times. All declared
timing/CFO alternatives remain in the normalized likelihood mixture and residual
calibration. Clock and epoch are fixed, one fractional timing anchor is retained,
and the channel has two static fitted taps. This is a bounded alternative to
materializing a large grouped export, not a general phase tracker or continuous
timing search. The older hyphenated `triad-fft-acquire.py` is a different study.

Near-support-edge FFT Grams use a bounded FP64 public-template refinement when
their observable rank is unresolved; retain its operation and memory limits.
Batching the exact one-block-window SVD reduced one 172,896-group CPU pilot from
17.55 to 7.23 seconds with identical scores. This is a measured preparation
optimization, not a sensitivity improvement, reliability qualification or GPU
speedup. Read the source-bound study before extrapolating its cost.

That study also retains a 17-cell physical interaction screen and an independent
−60-dB-Hz duration comparison. Its primary Each65/OCXO/stable-ray case admitted
30/32 bits strictly at 853.334 active days, with about 2.5 dB of static-basis
representation loss in the two misses; half-duration admitted 11/16. These
point-sampled synthetic results do not qualify 99% reception or arbitrary
channel evolution. Reuse the saved observations and exact recipes before
repeating the long exports. Cold-start CUDA was slower on the tested timing
workload; the precision replays preserved the near-threshold decisions.

Make the physical arrival prior explicit. With the configured CLI policy,
`--clock-sync default` is inactive with FHSS off and gives `[0,0]` unless another
prior is supplied; with FHSS on its default interval is ±0.09 seconds. It does
not mean a several-second prior. Use `--arrival-prior-seconds -7.035 7.035` for
the studied wide prior, or the 10-ms/1-ms/0-ms policy with 70-ms audio error for
the tight ±0.0905-second prior. Check native metadata and declare the public
search bank separately. A nominal-bank miss is not a sensitivity boundary.

Also check that a requested burst wait actually occurs. At rate 1,200 with
Added ×1 and FHSS enabled, the eight-second request occupies one 8.8-second
tile, so three internal wait tiles have no effect. The 40-second request with those waits
instead has a 149.6-second source calendar (44 seconds without waits), including
four realized inter-tile gaps. Whole-bit `--symbol-burst` is a different calendar
control. Neither setting alone demonstrates receiver backlog clearance.

**Start new configuration work with the
[broad coverage and failure map](validation-data/triad-v3/2026-11-03-broad/README.md).**
Run `tools/triad-coverage.py` or the factorized census before exporting waveforms;
use `tools/triad_factor_native_inputs.py` to regenerate small source-matched
native census inputs. The 2.50-billion-coordinate count is an algebraic
configuration census, not a reception campaign. The subsequent
[configuration and acquisition extension](validation-data/triad-v3/2026-11-03-extension/README.md)
adds zero RF shift and a shared baseband/RF static-clock topology; automatic
1-MHz/30-MHz rate geometry exceeds its current bounds. Low-rate FHSS can lack
any complete pattern cells inside the protected dwell aperture.
Use the current source-matched census for these new topologies and preserve
the dated broad study's unsupported counts as historical. The explicit
`--selective-modulation-rate-hz` control permits modulation-only stress with
spread None; without it, the historical zero-Doppler bypass remains exact.

The broad study corrected numerical false admissions in both static and
Gaussian-window receivers by projecting onto observable Gram modes and using
small whitened SVD fits. FP32 bulk operations remain available; sensitive small
preparation and evidence reductions are explicitly FP64. Keep the rank-loss,
noise-inflation and arithmetic diagnostics when reusing these helpers. Do not
copy the earlier high-C/N₀ Cholesky/null-mode implementation or infer a physical
sensitivity limit from its failures. Noise-only calibration does not prevent
structured wrong-key admissions; those remain a separate reported limitation.
The [raw-sample diagnosis](validation-data/triad-v3/2026-11-03-broad/evidence/wrong-key-diagnosis.md)
shows how an oversampled narrowband wrong-key waveform can be almost fully
absorbed by very short nuisance fits. Its local residual is not a covariance
model for the remaining colored interference. Do not diagnose that result as
a cryptographic failure or fix it by weakening the Gaussian-noise threshold.

For stable-ray `B=0` models with sufficiently small residual phase evolution,
compare whole-observation complex accumulation with short-window fitting before
adding nuisance dimensions. `triad_group_acquire.py --reblock BLOCK_COUNT
--window-blocks 1 --doppler-width-hz 0` reuses every public timing group and its
complex statistics for the former comparison. Read `BLOCK_COUNT` from the public
workspace manifest. It is a constant-coefficient channel model, not a general
tracking solution for renewed fading or free-running oscillators. Do not prune
the timing bank using truth alignment. The sampled timing comparison recovered
ten selected signal captures and rejected five nulls with both complete-bank
methods; nominal-timing misses were not sensitivity boundaries.

The [extension's frequency bank](validation-data/triad-v3/2026-11-03-extension/README.md#reusing-complex-statistics-for-frequency-acquisition)
also reuses retained complex blocks for a public residual-CFO search before
reblocking. Its phase-arc guard bounds the within-block template approximation,
not the complete likelihood error. Do not silently enlarge that budget, omit
frequency alternatives from calibration, or replace physical timestamps with
deinterleaved symbol indices. Clock-rate and keyed-epoch banks remain separate
unimplemented acquisition work in these recipes.

Use the [independent short-window failure evidence](validation-data/triad-v3/2026-11-03-extension/physical/evidence/README.md)
when choosing channel model dimension. A 62.5-ms independent-gain window at
six fine symbols/s admitted one wrong bit and two wrong-key controls in the
12-dB-Hz screen; whole-observation fitting resolved those same examples.
Noise-only calibration does not certify the chosen bit or authenticate a key.
Physically constrained continuity should be tried before adding flexible
per-window nuisance gains.
The [intermediate continuity-window comparison](validation-data/triad-v3/2026-11-03-fft-timing/continuity/README.md)
reused five selected captures with six fixed window/width choices. Every choice
corrected the selected wrong-bit admission and rejected the two wrong-key
controls, as the existing whole-observation receiver already did. None recovered
the missed-hop admission. This screen supports trying existing continuity
settings; it supplies no weak-signal gain over that simpler global receiver and
no independent reliability sample.

For an hour-scale, maximal-spreading reference, reuse the
[rate 0.1 / Added ×1000 sensitivity bracket](validation-data/triad-v3/2026-11-03-extension/physical/evidence/frontier-README.md).
At fixed 3,842-second source duration, the complete public timing/CFO bank
accepted 5/16, 12/16 and 16/16 signals at −20, −18 and −16 dB-Hz under the
strict gate. The same physical paths were reused across levels; the two weaker
levels were selected followups. All raw CFO bit choices were correct, but many
did not pass admission. Keep the gate fixed when investigating that gap, and
compare actual retained signal energy with the favorable known-waveform bound.
These observations neither qualify 99% reliability nor prescribe a duration
multiplier. Recounting the saved score packages is cheap; do that before
regenerating the sampled captures.

Use the [clean-projection diagnostic](validation-data/triad-v3/2026-11-03-extension/evidence/projection-README.md)
after fixed noisy decisions to distinguish available energy from energy retained
by the receiver's channel basis. One missed-hop capture retained only 42% even
at its best existing public timing/CFO group. This diagnostic is evaluation
only: never feed clean means or truth into acquisition. More flexible short
fits retain more signal but can also erase pattern distinctions. Reproduction
of that case needs roughly 1 GiB RAM, not the unsuccessful 512-MiB estimate.

For long phase evolution, the [TCXO isolation study](validation-data/triad-v3/2026-11-03-extension/phase/README.md)
compares constant phase, coherence-length segmentation and existing finite-state
phase inference on actual sampled 850-day captures. At −60 dB-Hz phase inference
helped materially; at −57 dB-Hz the inexpensive coherence-length baseline
matched its 16/16 screening result. Start with that cheaper baseline where its
assumptions fit. Neither result establishes blind acquisition or qualified
99% reliability under the full propagation model.

The 850-day exports and the large conditional CPU/GPU batch are optional research
recipes, never default regression tests. Start with the small affected Python
checks and isolated 14-test triad group. Conditional redraws reuse a physical
path; they do not replace independent channel validation. Record source hashes,
completed conditions in failed jobs, actual elapsed/sample calendars, post-erasure
energy and the complete attempted denominator. Keep actual GPU throughput
separate from GPU availability or theoretical operation counts.

For performance work, follow the [required efficient regression workflow](triad-performance.md#required-efficient-regression-workflow)
and [confidence and material-slowdown policy](triad-performance.md#confidence-and-material-slowdown),
then consult the [maintained coverage matrix](triad-performance-matrix.md) to
choose affected checks. Cover distinct paths rather than Cartesian combinations.
Where forecasting saves total work, build a validated chain from full or sustained
references through abbreviated stages to a full-runtime predictive bound; use
direct full-target timing where it is cheaper. Reuse source-matched evidence and
existing builds; expand work only when uncertainty or a material slowdown calls
for it. The fast guard entrypoints are `triad_perf_guard.py --profile ci` and
`triad_perf_guard.py --profile gpu-dev --device cuda`; the exact timing decision
contract, including randomized balanced-block conditional-slot/no-carryover
assumptions, is in the [performance guide](triad-performance.md#confidence-and-material-slowdown).
A green guard means no confirmed regression, not full qualification. See the
[current completion record](validation-data/triad-v3/2026-11-04-performance-completion/README.md)
for its validated finite measurement routes and remaining limits.

**Selective-model version warning:** the detailed 2026-11-02 selective
campaign used `legacy-allpass-v1`. Its receiver results remain valid for that
simulated operator, but do not establish strong differential added-modulation
performance at 0.00075-Hz carrier spacing with 0.05-Hz Doppler. The isolated
v3 stream now defaults to `dispersive-dft-v1`; ordinary Robust channel paths
remain on the legacy implementation. Read the
[corrected model and receiver study](validation-data/triad-v3/2026-11-02-selective-correction/README.md)
before reusing depth-specific sensitivity or duration conclusions. The
earlier legacy 850-day selective captures omitted 1,478 rows (5,912 seconds at 0.25
samples/s) from the natural full-tail calendar; the missing-tail energy impact
remains unmeasured. Those earlier captures must not be described as fully tail-complete.
The old admissions are incomplete-support measurements, not proof that the
recorded detections were false. The correction record now includes separate
full-tail legacy/corrected 850-day replays; keep their identities distinct.
Use the common selective-tail floor for matched v3 exports and verify equal
row calendars and indexed noise before scoring. The record also contains accepted `B>0` renewed-Gaussian
information exclusions for both reception targets. These bounds do not cover
the prior stable-ray `B=0` 850-day case or every channel law; keep their
assumptions visible.

For timing-related misses or wrong-bit admissions, start with the
[finite-bank acquisition study](validation-data/triad-v3/2026-11-02-acquisition/README.md).
`triad_long --mode export-group-stats` and `tools/triad_group_acquire.py`
compare all declared timing groups against one actual sampled observation.
This avoids the quadratic cross-group Gram storage of `export-stats`;
it does not support independently resampling noise for each group. Keep a
fixed physical likelihood window when comparing block sizes. The small exact
Gaussian reference is an aligned classification benchmark, not an operational
admission test. Its fixed 99%-classification comparison demonstrated a gap
below 1 dB for one stipulated Gaussian model. Use the
[cheap reproduction recipes](validation-data/triad-v3/2026-11-02-acquisition/reproduction/README.md):
the exact quadratic-form simulation evaluated 4.2 million classification
trials in about eight seconds on one CPU thread. Large sampled-waveform
campaigns remain reproducible study evidence, not automatic regression
prerequisites. That dated campaign searched arrival only. The later frequency
and sampled FFT recipes above add declared CFO alternatives; clock-rate and
keyed-epoch uncertainty still need separate banks and evidence.

Grouped export now shares deterministic template synthesis between separate
clean/noisy streaming channel instances when the declared memory budget permits.
It retains a sequential fallback and reports the selected execution mode.
The optimized arrays were compared byte-for-byte with the prior exporter,
including nonlinear frontends, missed dwells and complete tails. This exporter
still uses one worker per capture: increasing `--threads` does not parallelize
its group loop. Batch independent captures within host CPU/memory limits;
do not multiply outer workers by a presumed inner speedup. Large timing banks
remain expensive, so inspect group count, sample count and disk estimates before
launching them. See the extension record for measured scope and remaining limits.
The current native candidate cap is 10,000. A two-ray group carries four
candidates (flat/two-ray × both bits), so its effective native grouped-export
limit is **2,500 synchronization groups**, even though separate native/Python
group checks mention 10,000/5,000. Those larger checks do not override the
candidate cap. The sampled FFT path generates its later timing/CFO bank from
one exported base group and has its own explicit limits.

The same study includes an optional **pattern-consistency postprocessor** for
structured wrong-key admissions. It compares one fixed bit across windows
with a window-by-window bit-switching proxy, using the already scored
observations. Keep its original admission gate and do not call the proxy an
authentication test. In fresh high30/Each65 validation, the preselected
`ln(100)` guard rejected all eight wrong-key cases at each of 12 and 20 dB-Hz,
but reduced legitimate 8-dB-Hz, 246.4-second reception from 16/16 to 15/16.
The diagnostic zero threshold retained 16/16; it was not promoted after seeing
those outcomes. Small-sample confidence intervals, the lost reception, and
the distinction from noise-only calibration are recorded in the study.
Doubling actual duration from 123.2 to 246.4 seconds also changed four paired
weak cases from 1/4 to 4/4 accepted, with measured post-channel energy ratios
1.990–2.017. These observations do not qualify a 99% duration planner, a
general blind receiver, or new multi-day/−60-dB-Hz performance.

For a small, exact preparation optimization, the current Gaussian-window
helper now reuses the immediately preceding prior root only when relative
window-center bytes match exactly. It preserves irregular calendars and tails,
uses bounded storage, and changes neither precision nor retained information.
A separate post-campaign comparison found identical old/new factors and
FP32/FP64 scores, with about 6% faster preparation on one retained four-group
case. The 33 affected window, grouped-acquisition and conditional-sweep checks
passed. Historical campaign receipts remain bound to the earlier helper;
the study records both identities and does not claim a full-receiver speedup.

Use the [Target SNR and simulation reference](triad-v3.md#target-snr-link-margin-and-simulation-reference)
when planning or reporting experiments. A lower design target than the nominal
physical link is intentional path-loss margin; test the target separately.
Preserve and report modeled energy changes rather than renormalizing them
away, and distinguish a target-level test from demonstrated target reliability.

Start with the [development contract](development.md), the
[shared channel-model contract](channel-models.md), and, for arithmetic or GPU
work, the [precision-control guide](gpu-precision-controls.md) and
[GPU comparison protocol](triad-gpu-optimization.md). They define the behavior
and evidence requirements; this page does not override them.

## Choose the smallest discriminating check

Reuse is the default. Reuse matching sampled observations and complex block
statistics for receiver comparisons. A new independently sampled physical path
is still needed when the claim concerns performance across paths, even if the
nominal configuration is unchanged. Generate a new waveform/channel capture
when the physical waveform, path, sampled noise, or absolute-time observation
must change.

| Question | First useful check | Escalate when |
| --- | --- | --- |
| Does a statistic, likelihood, width, window, or candidate-order change behave differently? | Re-score the same exported complex block statistics and public Grams. Use paired identical inputs and seeds. | A candidate wins only under a new physical/model condition, or the reduced statistic omits an effect implicated by the failure. |
| Is the difference only conditional on a fixed sampled path? | Run the conditional-noise/model screen on the same exported means, Grams, and geometry. Label each draw as conditional and keep path identity fixed. | The result motivates a receiver choice or a claim across physical paths. |
| Does the receiver handle sampled noise and wrong-key structure? | Score fresh **actual-noisy** sampled observations and separate correct-key, wrong-key, and noise-only controls. | A claim across physical paths needs independent paths; a rare admission rate or deployment operating point also needs the complete declared search family and suitable calibrated holdouts. |
| Does the behavior persist over a long observation? | Process a genuine coordinate-preserving long capture through the streaming path, including channel state and tails. | Only when the long-span condition is relevant to the proposed claim; a repeated short capture or an energy-scaled statistic is not equivalent. |
| Did waveform/channel implementation change? | First use small helper/API checks for the changed stage and its exact bypass, then one short sampled case with the shared channel path. | Re-run affected boundary and long-span cases only after the short check identifies a reason to do so. |

Do not generate new waveforms merely to retest code that consumes matching
exported statistics. `export-stats` saves complex block correlations, Grams,
validity calendars and provenance for its declared basis; it does not include
raw I/Q. Other export modes and retained evidence may have different contents,
and large long-span archives may intentionally retain representative raw arrays
only. Inspect the manifest and inventory before assuming samples are available.
The statistics retain joint carrier/tap evidence only for the projected basis.
A changed basis needs new projections: reprocess retained physical I/Q, or
generate a new sampled input if raw samples were not retained. The projection
statistics are not enough after nonlinear front-end processing, quantization,
clipping, AGC, or other observation-dependent processing absent from the
export; use actual processed samples for those comparisons.

Conditional-noise sweeps are cheap model triage, not independent path trials.
They may regenerate noise projections and total received energy from exported
means/Grams under the declared Gaussian model. Use them to reject poor widths,
windows, or score formulations before spending on captures. They do not test
wrong-key waveform overlap: the current conditional driver accepts signal
workspaces and generates only signal/null controls. Score fresh or retained
sampled wrong-key workspaces in `actual-noisy` mode instead. Do not count
repeated noise draws on a few paths as independent physical realizations.

An actual-noisy score uses the sampled noisy observation, its received energy,
public Grams, and validity calendar. It does not inject a replacement C/N0,
read the noiseless signal to decide, or turn simulator truth into receiver
input. Keep signal misses, wrong-bit accepts, wrong-key admits, and null admits
separate. A small control set is a diagnostic, not a rare-event qualification.
The current Gaussian-calibrated driver also rejects unsupported nonlinear
front ends in actual-noisy mode; having sampled observations does not restore
the Gaussian noise assumptions after clipping or quantization.

At weak fine-symbol SNR, start with joint complex/block likelihoods and preserve
complex accumulation before nonlinear combination. The separate v2 study found
that differential raw products reduced search CPU substantially but lost
weak-signal sensitivity, while amplitude-only reception had additional
limitations. Those are version-specific baselines, not assumed cheap
sensitivity-equivalent replacements for the v3 receiver; see the
[v2 measurements](validation-data/triad-v2/2026-10-31/README.md).

## What the measurements currently support

Use the evidence as candidate approaches tied to their measured conditions, not
as universal receiver thresholds or an approved architecture:

- Stable static FFT/frequency scoring remains a useful inexpensive baseline.
  The [spread study](validation-data/triad-v3/2026-11-02-spread/README.md)
  distinguishes stable ray-center separation from renewed stochastic Doppler
  width; success on stable rays does not establish performance under stochastic
  spread.
- Renewing Gaussian-window and matched OU models are useful comparisons when
  the channel evolves. A matched covariance is a hypothesis to test, not proof
  that the finite-window, block-constant approximation matches the physical
  FIR path. Window length and assumed width matter; the measured benefit of
  longer windows was modest, and width mismatch reduced reception. Preserve
  residual calibration degrees of freedom when changing block/window geometry.
- The [corrected selective study](validation-data/triad-v3/2026-11-02-selective-correction/README.md)
  includes 1,024 actual-noisy holdout exports. At rate 3.6 and −11 dB-Hz,
  the Gaussian-window receiver accepted 64/64 correct bits for Each25 and
  Each65 at both gates. The pointwise equal-bit 95% lower bound is 95.43%,
  establishing 90% for those cases, not 99%. Slower settings retained misses;
  zero null/wrong-key admissions in 16- or 32-path cells do not qualify an
  operational false-admission rate. Earlier legacy wrong-key failures remain
  documented in the [legacy selective study](validation-data/triad-v3/2026-11-02-selective/README.md).
- Full-tail corrected 850-day sampled captures at −60 dB-Hz produced 4/4
  correct static-frequency admissions per Each25/Each65 signal cell, in the
  local replay and separate remote set. This is finite seeded feasibility for
  fixed-gain rays with `B=0`, 0.05-Hz separation and 5-ms delay. It establishes
  neither 90/99% long-span reliability, real-world channel stability, rare
  false admissions nor a minimum sufficient duration. The correction record's
  [information-bound analysis](validation-data/triad-v3/2026-11-02-selective-correction/theory/README.md)
  excludes specified `B>0` zero-mean renewing-Gaussian −60 dB-Hz cases, but
  does not apply to the stable `B=0` path or establish a decoder duration.
- High-spread nominal banks can fail despite increasing signal power. In the
  corrected high30 Each65 conditional screen at 20 dB-Hz, Gaussian-window
  scoring accepted 165 correct and 91 wrong bits out of four paths ×64 noise
  draws. Separate exact-timing oracle comparisons recovered substantial lost evidence,
  but are diagnostic truth, not acquisition. Resolve timing, block compression
  and final-channel mismatch before calling this a Doppler sensitivity limit.
- The newest [paired arithmetic benchmark](validation-data/triad-v3/2026-11-02-selective-correction/reproduction/benchmark-review.json)
  measured CPU/CUDA FP64 ratios of 2.49–4.07× for static scoring, 1.04–1.06×
  for Gaussian-window method time, and 0.99–1.07× for bounded complete-process
  time on its two workloads. Sampled winners and admissions matched the CPU
  reference for both CUDA FP64 and FP32-min. These single-run measurements
  establish no universal GPU gain; preparation and simulation dominated the
  complete jobs. Historical [spread](validation-data/triad-v3/2026-11-02-spread/README.md)
  and [−60 dB-Hz](validation-data/triad-v3/2026-11-02-minus60/README.md)
  timings remain specific to their workloads and reuse state.
- If a long-span fixed-ray case suggests residual frequency mismatch, the
  joint ray-frequency search is a bounded candidate: it adds a constant
  relative-ray-frequency correction and its cross-Gram, without claiming to
  track OU evolution. Its small paired comparison improved a static mismatch
  example from 4/8 to 8/8 strict accepts; that is a diagnostic-sized result,
  not a receiver qualification. Phase-particle scoring is another explicit
  trajectory-prior option; check particle convergence and preparation/memory
  cost before enlarging it.
- Slower fine-rate/R:B choices and more elaborate channel models remain
  hypotheses to test. Zero wrong-key admissions in the new selected controls
  do not resolve every earlier overlap case. Keep structured wrong-key
  rejection separate from signal/noise discrimination.

These results are conditional on their sampled models and finite banks. A
pointwise confidence bound is not a bound on a whole receiver family. Keep
known/nominal arrival, blind acquisition, diagnostic truth, and actual receiver
inputs distinct. In particular, diagnostic truth may explain a score but must
not become hidden receiver input. Preserve every selected physical effect,
continuous state, exact bypass behavior, and complete channel/filter tails as
specified by the [channel contract](channel-models.md).

Detailed result tables, assumptions, failures, and audit limits live in the
[selective](validation-data/triad-v3/2026-11-02-selective/README.md),
[selective correction](validation-data/triad-v3/2026-11-02-selective-correction/README.md),
[spread](validation-data/triad-v3/2026-11-02-spread/README.md), and
[−60 dB-Hz](validation-data/triad-v3/2026-11-02-minus60/README.md) records.
The maintained [v3 study page](triad-v3.md) describes the implementation and
links dated evidence; the newest study is not necessarily the current or best
execution path.

## Bound the computation and reuse

1. State the decision, hypothesis, discriminator, unchanged controls, and
   stop condition before running. Reuse an existing export when its source,
   input bytes, model, seed, waveform, and geometry match the question.
2. Run the cheapest score/helper check first. Cache reusable factors, FFTs,
   Grams, bases, prepared covariance, and resident work only when their full
   identity matches. Invalidate on changed key, epoch, waveform, coordinates,
   search, model, variance, or dimensions; never reuse observation-dependent
   packed data or a prior posterior across changed inputs.
   Use the bounded batched retention audit for saved statistics: its measured
   48.7× scalar-analysis speedup preserved the complete report on one high10
   workspace. This improves evidence processing, not receiver sensitivity;
   reusing it avoids repeating an unchanged physical campaign.
3. Check the memory plan before increasing batch, duration, block count, or
   hypothesis count. Bound CPU jobs and GPU batches to available memory and
   avoid overlapping timing-sensitive work on a shared device. Record OOM,
   fallback, unsupported mode, and incomplete work as outcomes.
4. Keep CPU and GPU evidence tied to the actual path. Compare strict CPU FP64
   and strict FP32 references; test stage-specific arithmetic choices and
   precision exceptions against them. Do not assume every preparation or Gram
   operation must stay wide, and do not silently change a user's selection.
   Preserve the dropdown's floor/force semantics; `fp32-min` must not introduce
   TF32 or another reduced multiply mode. Report stage time, preparation/cache
   state, transfers, process time, peak memory, and decision differences, not
   kernel time alone. See the [precision guide](gpu-precision-controls.md).
5. Escalate to a new noisy holdout only when model triage supports the
   candidate. Escalate to long-span streaming only when the claim concerns
   long-span behavior. Do not rerun a historical full campaign by default.

The Python scoring tools require Python with NumPy, SciPy, and PyTorch; CPU
PyTorch is sufficient for the recipe. Use an isolated environment when the
system Python is externally managed. The focused suites
`tests/test_triad_fft_acquire.py`, `tests/test_triad_group_acquire.py`,
`tests/test_triad_gaussian_window.py`, `tests/test_triad_conditional_sweep.py`,
and `tests/test_triad_rbpf.py` are not all in the triad CTest group; select the
affected suite when changing those components. The
`triad-conditional-sweep.py --help` output currently omits wrapper-specific
options such as `--mode`, `--cn0-grid`, window settings, and methods. The
source parser and checked recipe below are authoritative for those options.

For reproducible isolated checks, start with this small nominal-branch recipe
when its HF/two-ray/Gaussian-width model fits the question. It builds the CLI
test profile (the ordinary app build does not include this excluded target),
preflights the geometry, exports one small actual-noisy sampled workspace, and
reuses it for conditional scoring:

Always preflight a changed rate/configuration: a short requested duration at
a very low rate can round to a private tile lasting hours or days. Default
export ceilings of 128 million rows and 8 GiB are guards, not a cheap-run
budget. The recipe deliberately lowers them. The conditional duration planner
is a separate model calculation; `--assume-model-bounds` asserts bounds rather
than measuring the selected channel's achievable reception.

```sh
triad_scratch="$(mktemp -d "${TMPDIR:-/tmp}/triad-v3-dev.XXXXXX")"
./build.sh test triad --cli --jobs 2 --build-jobs 2
triad=build/dev-cli/triad_long
"$triad" --mode configure --seconds 16 --rate 36 --added 1 \
  --sample-rate 256 --channel hf --phase-diffusion 0 \
  --ray-gaussian-width-hz 0.1 --basis two-ray --block-seconds 0.5 --cn0 8
"$triad" --mode export-stats --seconds 16 --rate 36 --added 1 \
  --sample-rate 256 --channel hf --phase-diffusion 0 \
  --ray-gaussian-width-hz 0.1 --basis two-ray --block-seconds 0.5 --cn0 8 \
  --stats-noisy --threads 1 --memory-mib 64 --export-max-samples 100000 \
  --export-max-mib 16 --seed 7101 --output "$triad_scratch/fixture"
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 \
python3 tools/triad-conditional-sweep.py "$triad_scratch/fixture" \
  --mode conditional --methods gaussian-window-ucb \
  --window-doppler-width 0.1 --window-blocks 8 --taps 2 \
  --frequency-bound 0 --coherence 4.50158158 --device cpu --precision fp64 \
  --threads 1 --memory-gib 0.5 --cn0-grid=4,8 --noise-trials 4 \
  --trial-batch 2 --controls signal null \
  --output "$triad_scratch/conditional.json"
```

The recipe uses a fresh temporary output directory. To score the sampled noisy
observation, use the same scoring command with `--mode actual-noisy`, omit
`--cn0-grid`, `--noise-trials`, `--trial-batch`, and `--controls`, and choose a
separate output filename. This mode scores the one actual observation per
exported physical path. Both modes use default family allocations; smoke-sized
counts are not operational qualification.
The 16-s export rounds to private tiles. Its HF preset is a 1.5-ms/0.3-Hz
two-ray channel with 5% slow/fast modulation; phase diffusion is disabled.
Timing/frequency are nominal only: this is a software/model smoke, not blind
acquisition or long-span evidence.

### Reusable tools

| Need | Maintained entry points | Scope and limit |
| --- | --- | --- |
| Large fixed timing/CFO banks from sampled I/Q | [`triad_fft_acquire.py`](../tools/triad_fft_acquire.py) and [sampled FFT study](validation-data/triad-v3/2026-11-03-fft-timing/README.md) | Reuses one actual-noisy sampled base group across integer lags with exact sample-time CFO phase; fixed clock/epoch, single fractional anchor and static two-tap model. Keep the complete bank and bounded public FP64 edge refinement. |
| Conditional noise or actual-noisy likelihood comparisons | [`triad-conditional-sweep.py`](../tools/triad-conditional-sweep.py), [`triad_gaussian_window.py`](../tools/triad_gaussian_window.py) | Reuses matching exported statistics; the conditional mode is model-conditional and does not add independent paths. |
| Static-frequency or phase/OU scoring | [`triad-static-frequency-study.py`](../tools/triad-static-frequency-study.py), [`triad-rbpf-study.py`](../tools/triad-rbpf-study.py) | Actual-noisy modes score saved noisy observations; candidate banks, controls and calibration remain explicit. |
| Validate and summarize selective correction evidence | [`analyze.py`](validation-data/triad-v3/2026-11-02-selective-correction/reproduction/analyze.py) | Uses bounded `retention_batch.py` by default. It preserves every exported block and row, verifies hashes/ranks/calendars, and separates noiseless projection from receiver admission. Source-bound development, holdout and long-span audits are recorded in the [reproduction guide](validation-data/triad-v3/2026-11-02-selective-correction/reproduction/README.md); analysis speed is not receiver sensitivity evidence. |
| Reusable design and resident scoring state | [`triad_state_workspace.py`](../tools/triad_state_workspace.py), [`triad_prepared_observation.py`](../tools/triad_prepared_observation.py), [`triad_gaussian_prepared.py`](../tools/triad_gaussian_prepared.py), [`triad_scan_workspace.py`](../tools/triad_scan_workspace.py), [`triad-scan-workspace.py` (CLI)](../tools/triad-scan-workspace.py) | Cache only public design or identical-input work covered by the cache identity. Reuse does not make inputs independent or validate a changed basis. |
| Performance screening | [`triad-profile.py`](../tools/triad-profile.py), [`triad-optimization-study.py`](../tools/triad-optimization-study.py), [`triad-resident-campaign.py`](../tools/triad-resident-campaign.py), [`triad-consumer-profile.py`](../tools/triad-consumer-profile.py) | Defaults can be large: profile uses CUDA/512 phase states, optimization can scan the whole export, resident campaign defaults to 8×8 batches and a 12-GiB limit, and consumer sustain repeats 4,000 times. Set explicit small dimensions: optimization uses `--max-blocks` (`0` means the whole export); profile uses `--blocks`. A diagnostic prefix cannot qualify whole-span behavior. |
| Bounded local process groups | [`triad_bounded_run.py`](../tools/triad_bounded_run.py) | Keeps a timeout and process-group watchdog for a single local job. |

The [low-rate campaign](../tools/triad-lowrate-campaign.py) also fixes Added
Symbols to 1, GPSDO-OCXO oscillators, margin 1 and tight clock/audio settings;
it is not a generic all-presets runner. Dated reproduction plans may contain old workspace
paths and broad campaign matrices; inspect and adapt their exact commands
instead of relaunching them unchanged. Historical scripts do not necessarily
exercise current prepared/resident paths.

Keep new bulky raw evidence in ignored `local-evidence/` as project guidance
requires, and retain compact provenance/results in the appropriate validation
record when the work is intended to be maintained.

Cloud is an execution option only when local resources or a demonstrated
workload bottleneck justify it; a CPU-bound or already-cheap job does not imply
that a GPU or cloud run will help. If cloud execution is useful and already
authorized, prepare and check the hash-bound job locally first. Keep the
sanitized source/input bundle small enough to upload in a few minutes; remote
builds are appropriate even for small source trees when dependencies or target
toolchains make local builds unsuitable. Use persistent build/data caches only
when measured upload/build reuse justifies their cost, and remove temporary
resources and stale cached material after use. Keep provider account, billing,
and other private operations outside the repository. Follow existing
authorization and service rules; this workflow adds no budget, approval, or
re-approval requirement. User authorization persists unless the user changes
it.

## Scope the checks to the change

Choose checks from the changed behavior rather than replaying all historical
campaigns:

| Change scope | Proportionate checks |
| --- | --- |
| Documentation only | Check links, commands, and consistency against maintained contracts. No new sensitivity campaign is needed. |
| Isolated v3 statistics, likelihood, or resource behavior | Run affected helper/module checks, matched statistics/scoring comparisons, and the isolated C++/Python checks for changed code. Add sampled or long-span evidence only when changed assumptions or the claim require it. See the [isolated-research build stages](building.md#isolated-three-carrier-research). |
| Shared runtime/channel source or ordinary Robust integration | Preserve ordinary modem contracts and run applicable shared-code, receiver, GUI, and integration regressions described by the [development contract](development.md) and [build/testing stages](building.md#testing-stages). Passing isolated v3 checks does not qualify ordinary Robust behavior. |
| Release candidate | Follow branch diagnosis, full CI/regression, and separate certification steps in the [release guide](releases.md#diagnose-a-branch-before-full-validation) and [build/testing stages](building.md#testing-stages). Diagnosis or packaging alone does not certify or promote a release. |

- A statistics/likelihood change: paired unit/helper checks, matched CPU/GPU
  scores when relevant, exact CPU FP64 reference, and boundary cases near the
  current decision gate. Preserve the original inputs and assertions.
- A channel or waveform change: read the channel contract; check selected
  effects, explicit None/Off bypass, deterministic seeds, chunk continuity,
  source versus physical calendar, FHSS guards where applicable, and full
  filter/channel tails. Use a short sampled integration case before a long one.
- A long-span or resource change: use the existing bounded streaming path and
  genuine absolute sample coordinates; record processed span, samples, wall
  time, streaming factor, peak host/GPU memory, and state continuity. A fast
  statistics replay is not a long-span streaming check.
- A precision/GPU change: retain strict CPU FP64 and strict FP32 references,
  measure preparation and full call costs, verify actual backend/multiply
  mode, and check score, rank, winner, and admission changes. Do not silently
  change defaults or reinterpret a fallback as the requested mode.
- A claim about acquisition or operational admission: exercise the complete
  predeclared timing/frequency/epoch/clock/channel/method/duration and repeated-
  attempt family. Include actual signal, wrong-key, and noise-only controls,
  calibration, and independent holdout as appropriate. Conditional screens or
  pointwise bounds alone are not this check.

These checks concern isolated v3 only. Ordinary Robust compatibility,
physical-device evidence, release qualification, and v1/v2 historical results
remain separate. Report passed, failed, unexecuted, and unsupported checks
explicitly; never turn diagnostic truth, a repeated conditional draw, or an
incomplete candidate bank into receiver qualification.

The checked smoke recipe above was exercised on 2026-11-02: the isolated C++
triad group passed 14/14 registered tests; configure and export completed, then
conditional and actual-noisy scoring completed on the exported 5,413-sample,
43-block fixture. These small CPU FP64 jobs check command wiring and scoring
paths only. They are not reception-probability, blind-acquisition, or
long-span qualification.

## Short acceptance checklist

- [ ] Isolated v3 scope and behavior are explicit; ordinary Robust and v1/v2 remain distinct.
- [ ] Matching complex statistics and physical inputs are reused; new path/basis work has a stated reason.
- [ ] Conditional, actual-noisy, wrong-key/noise-only, and long-span results are labeled separately.
- [ ] Hypothesis, controls, candidate family, stop condition, hashes, seeds, command, environment, and bounds are recorded.
- [ ] Selected channel effects, bypasses, state continuity, calendars, and complete tails are preserved.
- [ ] Precision stages, cache cost, actual device path, memory, fallback, and score/decision deltas are reported.
- [ ] Scoped checks, skipped/failed work, and remaining limits are explicit; no campaign is rerun without a reason.
