# Complete integer-timing acquisition and channel-continuity diagnostics

This continuation of the [configuration extension](../2026-11-03-extension/README.md)
addresses acquisition cost and consequential receiver failures in the isolated
three-carrier v3 experiment. It does not integrate the ordinary Robust receiver.
The complete requested configuration space has **not** been qualified for
reception. The [broad failure map](../2026-11-03-broad/README.md) distinguishes
configuration census, sampled diagnostics, unsupported geometry, and untested
combinations; its billions of coordinates are not billions of transmissions.

## What changed

`tools/triad_fft_acquire.py` uses one actual noisy `export-sampled` capture and
one public two-ray template group. Linear, zero-padded FFTs reuse that template
across every declared integer-sample arrival. Common frequency and drift
phasors are evaluated at physical sample times. All four cross-bit/template
columns enter the union residual used for noise calibration; both bit
likelihoods are uniformly mixed over the complete timing/frequency/drift bank.
Tiling does not prune hypotheses or change their prior weights. Simulation
truth is used only for evaluation after the decisions.

This is a **whole-observation static two-ray model**, with fixed public keyed
epoch and sample-clock hypothesis and one fractional arrival anchor. It is
not a general oscillator tracker, fractional/polyphase acquisition bank,
physical RF hopping implementation, or proof of continuous-grid optimality.
Additional selective modulation, actual oscillator/channel evolution, erasures
and tails remain in the sampled observation; the restricted receiver can
therefore suffer model mismatch.

The Gram FFT is substantial CPU complex-FP64 work. Observation correlations
and likelihood bulk work support FP32 or FP64, with an optional CUDA backend;
small preparation, calibration and final reductions remain higher precision.
These are explicitly partial arithmetic selections, not all-FP32 pipelines.
TF32 is disabled. Exact public support detection makes a wholly absent
template yield zero evidence. Weak masked support receives bounded direct
FP64 refinement when FFT resolution would threaten the unchanged observable
rank tolerance. The trigger is an engineering guard, not a rigorous FFT-error
certificate. Work caps fail explicitly rather than silently discarding groups.

The shared Gaussian helper also batches the small single-block SVDs without
changing the prior, whitening, observable rank or likelihood formula. The
same 172,896-group CPU pilot took **17.55 seconds before and 7.23 seconds after**
this organization change, with identical bit likelihoods and decisions. This
is one sequential end-to-end comparison, not a throughput confidence interval.
The initial unbound-source pilot is excluded from that comparison. See the
[source-bound comparison](performance/README.md) and [native parity](parity/README.md).

An actual RTX PRO 6000 WK comparison found **no cold-start GPU speedup** on
this capture: CPU FP32 took 4.47–4.61 seconds, CUDA FP32 5.06–5.36 seconds.
Both preserved the CPU FP64 winning groups and decisions; maximum per-group
log-likelihood errors were 0.000158 and 0.000250 respectively. This strong
capture is far from admission thresholds. The CUDA path also performs small
whitening and evidence reductions in device FP64; its name does not imply an
entirely FP32 GPU pipeline. Runtime/allocator overhead took measured host RSS
above the explicit-array budget. See the [precision and timing evidence](gpu/README.md).

A separate [near-threshold precision check](gpu/weak/README.md) replays three
postselected weak observations, including one only 0.0908 nats above the strict
gate. All six CPU-FP64/CUDA-FP32 replays preserve the original decisions;
the largest mixture-score change is 6.95 × 10⁻⁶. This diagnoses precision on
those observations, not a new independent reception cohort or a speed claim.

## Validation and practical interpretation

- Eleven FFT tests, thirteen Gaussian-window tests and eighteen existing
  grouped-receiver tests passed. Independent review repeated three changed
  edge/covariance checks and accepted the final implementation's stated scope.
- Native sampled parity includes a 40-second requested transmission whose
  within-bit processing waits extend the source calendar to **149.6 seconds**.
  The final-source replay matched nine native likelihood groups within
  **2.16 × 10⁻¹²**, with the same correct admissions. Its four wait interiors
  are actually silent at the transmitter; delayed state and receiver masks
  remain continuous. An earlier eight-second request did not activate those
  waits and is labeled accordingly.
- The [continuity comparison](continuity/README.md) reuses five retained
  observations across six predeclared intermediate models. They resolve the
  selected short-window wrong-bit and wrong-key examples, but provide no
  admission improvement over the existing whole-observation/CFO receiver.
  The weak random-25%-loss capture remains missed. These are paired diagnostics,
  not thirty independent channel trials or a new receiver default.

The prior missed-hop case retains only 42% of clean signal energy in its best
existing global basis (3.77 dB projection loss). The source audit found no
mask/calendar reset explaining that result: selected dispersive additional
modulation creates delays and sidebands absent from the restricted basis.
That is a plausible mismatch mechanism, not a measured causal decomposition
or proof that recovering every lost decibel is computationally worthwhile.

## Sampled configuration coverage

The new [17-cell interaction screen](physical/README.md) completed 136 captures
in 97.68 seconds using four CPU workers. At 12 dB-Hz (20 dB-Hz for the 24-kHz
control), strict admission was **63/68 correct, 0 wrong, 5 missed**; relaxed
admission was 65/68, 0 wrong, 3 missed. All 34 null and 34 wrong-key captures
rejected. Those are counts across paired configurations, not 68 independent
trials from one operating distribution. Each cell has only four signals and
two of each control.

The failures concentrate in the 500-kHz / IC-7100 shift-oscillator cells.
Some paths lack sufficient energy even for the allocated oracle target;
two Each65 misses have sufficient oracle energy but still lose to model and
acquisition uncertainty. The tested microwave, high-spread, first/last/scattered
missed-hop, internal-wait and 24-kHz controls admitted all four signals each.
The new complete-lattice passes show that earlier nominal-timing-only misses
did not establish a 24-kHz sensitivity boundary; they do not replay those old
seeds or qualify the rate generally.

Two planned cells were not sampled: the explicit wide IC-7100 search needs
1,445,463 groups, above the declared one-million cap; HF-worst at margin3
does not fit the automatic capture bandwidth. The 1-MHz/30-MHz geometry
preflights still reject. These are identified implementation/front-end limits,
not fundamental impossibility results.

Long tests use rate .001 Hz, Added1, zero RF translation, half-rate conversion
carrier .0005 Hz, OCXO, corrected Each65 modulation, no FHSS and **−60 dB-Hz**.
The [fresh followup](physical/followup/README.md) gives:

| Actual active duration | Ray separation / delay | Strict correct | Relaxed correct |
|---|---|---:|---:|
| 853.334 days | .001 Hz / .005 ms | 30/32 | 31/32 |
| 426.667 days | .001 Hz / .005 ms | 11/16 | 12/16 |
| 853.334 days | .005 Hz / .05 ms | 16/16 | 16/16 |

All seven strict misses choose the correct raw bit. There are no wrong-bit or
control admissions in that followup. The primary 32-signal cohort uses fresh
roots; the other rows reuse its first sixteen roots, so they are paired
diagnostics. Exact two-sided 95% intervals for the primary cell are
**[79.2%, 99.2%] strict** and **[83.8%, 99.9%] relaxed**. Neither target is
qualified. An earlier independent four-signal screen passed 4/4 at each of
Each25 and Each65; it is preserved separately rather than pooled after selection.

The higher-separation row also changes sampling from 1/128 to 1/32 samples/s
and the sampled random trajectories. Its 16/16 cannot establish that larger
Doppler improves reception. It uses 2,307,126 actual samples and about 2.55 GB
of explicit arrays under a declared 4-GiB allowance. The complete 80-capture
followup took 215.75 seconds with four CPU workers. These are genuine physical
time coordinates with continuous sampled state, not time-compressed runs;
they remain a synthetic fixed-ray/spacing-scaled selective model with the
declared Wiener phase law. The 128-/32-second point-sampling intervals have
not been validated against high-rate integration followed by decimation.
They do not qualify arbitrary multi-year field
propagation, stochastic sampling-clock drift, colored oscillator noise, or
blind channel/epoch acquisition.

The resulting duration guidance is a **measured bracket**, not a replacement
planner constant: halving this duration materially reduces reception; 853 days
still does not demonstrate the requested 99% target. Neither a 38-day claim
at −60 dB-Hz nor an extrapolated longer duration is established here.

The [paired clean-projection diagnostic](projection/README.md) examines the
two strict misses in the primary 32-signal cohort. Their correct-bit public
base groups retain 56.81% and 56.42% of the clean signal energy: representation
losses of **2.46 and 2.49 dB**. Wrong-bit projections retain less than 0.004%.
This confirms useful pattern separation while quantifying energy outside the
restricted static basis. It does not identify an achievable recovery gain,
explain every likelihood penalty, or measure the whole six-hypothesis bank.
Recovering these few decibels alone does not justify an elaborate tracking
architecture without a cheaper proposal and independent evidence of benefit.

## Energy benchmarks and long-duration interpretation

For a *known complete deterministic waveform*, white complex Gaussian noise,
and a known noise variance, let `rho = observed noiseless energy / variance`.
The Neyman–Pearson presence benchmark is

\[
P_D=\Phi\{\sqrt{2\rho}-\Phi^{-1}(1-\alpha)\},\qquad
\rho_{\rm req}=\tfrac12[\Phi^{-1}(1-\alpha)+\Phi^{-1}(P_D)]^2.
\]

This oracle knows the alignment, phase and channel; presence detection is
easier than this receiver's correct-bit task. At unit useful received power
and −60 dB-Hz, a single-completion 99% / 10⁻⁴ test requires 18.273 energy
units, or **211.50 days**. The relaxed 90% / .01 reference requires 6.508,
or **75.33 days**. These are favorable references, not implemented durations.

Charging the chosen attempt/duration/method/two-bit family by Bonferroni gives
36.403 units (**421.33 days**) for the strict target and 21.737 units
(**251.58 days**) for the relaxed twelve-hour target. These are references
under that particular allocation, **not universal fundamental lower bounds**:
another design can exploit dependence or allocate its budget differently.
The actual scorer further divides its budget between likelihood admission and
noise calibration. The physical evidence reports the corresponding reference
using its recorded gate, rather than silently equating these allocations.

The 850-day request rounds to 853.334 active days, giving nominal `rho=73.728`
under the unit-power assumption. This is only 3.07 dB above the allocated
strict reference. Use measured energy instead: selective modulation can both
increase energy and cause receiver mismatch. Its energy changes must not be
mistaken for decoder gains. The sampled manifest's `channel_signal_energy_samples`
is **pre-erasure** energy; it is usable for these no-erasure long captures,
but not as post-erasure energy for missed-hop cases.

Stable ray separation is different from random channel renewal. For the
separate normalized renewed Gaussian channel law and its stated covariance
assumptions, the maintained information screen has
`KL <= min(c*T, c*c*T/(2*sqrt(pi)*B))` for large `B*T`. With `c=1e-6`,
853.334 days and `B=.005` or `.05 Hz`, the latter terms are approximately
0.00416 and 0.000416 nats, below even the single-test necessary divergences
9.062 (strict) and 3.821 (relaxed). Those particular diffuse-channel assumptions
exclude the target; they do **not** exclude the fixed two-ray captures merely
because their ray frequencies differ by the same number. The detailed
[covariance assumptions and bounds](../2026-11-03-broad/evidence/theory.md)
must accompany any use of this screen. The present long receiver is given the
ray delays and Dopplers; it does not solve blind estimation of those parameters
over hundreds of days.

Scientific captures and timing comparisons bind FFT source `f91265b9…` and
Gaussian helper `b22fe002…`. Current FFT source `d4a3c2cc…` adds only the explicit
selected-device FP64 arithmetic-report field; independent review verified that
removing that line reproduces the former source hash exactly. All eleven FFT
tests passed after this reporting-only change. Historical evidence is not
silently relabeled as a run on the later source.

## Acquisition and calibration limits

The old grouped exporter has a 10,000-**candidate** limit: four candidates per
two-ray timing group means an effective **2,500 groups**, not 10,000. The new
FFT receiver avoids exporting a template for every arrival but still has
explicit one-million effective-group and 4,096-frequency limits, memory
limits, and support/refinement work limits. These are computational limits.

The native `--clock-sync default` does not itself request a several-second
search: with FHSS off it resolves to inactive `[0,0]`; with FHSS on the current
default resolves to ±0.09 seconds. This study's wide prior is explicitly
±7.035 seconds. The requested 10-ms clock / 1-ms region / zero-offset /
70-ms audio setting resolves to ±0.0905 seconds. A 1-ms region is uncertainty
metadata, not a requirement to sample an extremely narrow waveform at 1 kHz.
Fractional timing loss still requires waveform-based evidence.

The strict and relaxed budgets remain separate: 99% correct reception and
at most 10⁻⁴ noise admission per day; 90% and .01 per twelve hours. The scorer
charges 1,440 attempts/day, eight duration alternatives and sixteen method
alternatives, plus the complete declared acquisition/calibration family.
This controls noise-only admission under its stationary proper-complex white
Gaussian assumptions; it does not guarantee bit correctness, authenticate a
key, or validate a mismatched signal model. Small null/control cohorts do not
empirically establish either rare-event target. Different residual-calibration
families cannot be combined after inspection without charging their union.

## Reproduction and next decisions

Use the [development workflow](../../../triad-development-workflow.md), the
focused Python checks and retained workspaces first. Long observations and
large cloud batches remain optional research recipes rather than default
regression prerequisites. No new −60-dB-Hz reliability or duration claim follows
from the timing optimization. The earlier sampled long-span and phase-boundary
evidence remains conditional on its explicitly documented channel and alignment.

With NumPy, SciPy and CPU PyTorch installed in the selected Python environment,
the affected lightweight checks run from the repository root without a native
rebuild or cloud allocation:

```sh
python3 -m unittest discover -s tests -p test_triad_fft_acquire.py
python3 -m unittest discover -s tests -p test_triad_gaussian_window.py
python3 -m unittest discover -s tests -p test_triad_group_acquire.py
```

Use the [frozen physical recipes](physical/reproduction/README.md) only when
new sampled evidence is needed. They require the recorded historical source;
new experiments should create their own source-bound plan rather than changing
old hashes to accept a later checkout.

The [preservation record](preservation/README.md) retains the exact source,
binary and all 232 cloud captures in a 9.14-GB local archive. Full streaming
verification checked all 2,413 payload files, including the 928 capture inputs,
against their hashes before cloud cleanup. The archive is ignored local
evidence; a fresh Git checkout contains the compact reports and recipes, not
those large arrays. Reuse selected observations instead of uploading or
regenerating the entire archive for a focused receiver change.

FFT correlation follows the same general timing-reuse principle described in
[GNSS-SDR's acquisition documentation](https://gnss-sdr.org/docs/sp-blocks/acquisition/)
and [SciPy's linear FFT convolution documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.fftconvolve.html).
Those references justify computational structure, not this waveform's measured
performance or the correctness of a channel prior.
