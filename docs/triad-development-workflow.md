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
