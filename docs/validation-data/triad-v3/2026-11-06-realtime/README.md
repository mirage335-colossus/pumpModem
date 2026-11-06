# Practical local-CPU acquisition for the HF crystal / IC-7100 case

The new isolated Helper-onset → complex FFT proposals → sparse dynamic receiver
successfully acquired **16/16 independent native target-case transmissions**,
including both arrival-prior boundaries. It did not construct the earlier
208-million-entry Cartesian dynamic bank. On the local Ryzen 5 PRO 5650U,
three fresh-process acquisitions took **18.51–21.36 seconds** and
**21.74–24.56 process CPU seconds**, including imports, input loading and FFT
workspace construction, starting from stored keyed templates. Each input contains
84.678 seconds of sampled baseband; emission lasts 61.6 seconds.

This is a measured **one-bit, complete-capture CPU demonstration**, not completed
streaming integration, a general channel qualification, or an operational
false-admission guarantee. The [implementation guide](../../../triad-realtime.md)
describes the maintained path and inexpensive checks. The
[earlier CPU diagnosis](../2026-11-05-local-cpu/README.md) remains a record of the
reference-path bottleneck; its million-second arithmetic projection was never
an executed practical receiver benchmark.

## Preserved configuration and explicit scope

Native source revision `879fb8a2cea2a70a8947cadb09cd2dd864ba4b24`, exporter SHA-256
`c497ba10d1255f1e56a2976eb4c1be4854ba5f88b2891f0be15110a06ac22242`.
The frozen protocol binds the changed Python sources, original public manifest,
raw keyed templates, seeds and native binary. No transmitter, native channel,
ordinary Robust receiver, audio device, shaping, encryption or power allocation
was changed for this experiment.

The case uses DATA 4 W / Helper 40 W, loss 180 dB, crystal base clock, IC-7100
shift clock, 30 MHz RF, DATA IF 1725 Hz, Rate 36, Added Symbols ×10/interleave,
FHSS off, Symbol Burst off, and the existing 38-second Helper lead. At
−164 dBm/Hz noise density, physical DATA C/N₀ is **20.0206 dB-Hz**, Helper total
C/N₀ **30.0206 dB-Hz**. Planning target 8 dB-Hz does not override injected noise.
The default requested 16-second pattern becomes 23.2 seconds under the existing
shaping/calendar constraints; this study changes neither duration nor waveform.

HF-worst ×margin 3 gives two rays separated by 90 Hz and 21 ms. Additional
Each65 modulation uses the shared continuous dispersive filter with random
bounded Gaussian-erf coefficients, at 9/90-Hz slow/fast widths. It acts on the
complete DATA-plus-Helper waveform. Oscillator phase diffusion, sample-clock
offset, noise, overlapping passbands and channel/filter tails remain present.
The matched case sets **additional random ray width and delay excursion to
zero**, as in the preceding CPU study. Thus it retains specular ray components;
it is not a demonstration of arbitrary renewing propagation. Two 0.5-Hz
random-ray-width tests are reported separately below.

The receiver retains the original ±7.035-second DATA arrival prior, all 13
epoch alternatives and the original ±300.015-ppm public clock envelope. It uses
a three-point *coarse* clock bank with correlated local clock/timing refinement,
not an exhaustive continuous clock likelihood. Native holdouts drew actual clock
errors from −43.14 to +77.18 ppm. In particular, successful native draws do not
cover the whole enlarged margin envelope.

A separate cheap representation diagnostic used actual 23.2-second encrypted,
shaped templates at two epochs, both bits, nine true clocks from −300.015 to
+300.015 ppm, and interior/both boundary arrivals: 108 cases. It compares exact
clock two-ray subspaces with the best three-clock proposal representation on the
1-ms arrival lattice, preserving full FIR tails and the public arrival limits.
Worst minimum canonical energy retention was **82.25% (0.849 dB loss)** in the
interior and **68.96% (1.614 dB loss)** at a boundary. The boundary case used
true +75 ppm and coarse +300.015 ppm. Thus a universal half-grid clock-error
bound would be incorrect at the arrival edges. These are conditioned template
representation measurements, not a bound on stochastic acquisition loss or
unmeasured clocks. The diagnostic took 4.59 CPU seconds / 4.67 seconds elapsed.

## What removed the excessive work

- Helper onset acquisition places a DATA-free 36-second pilot window relative
  to the acquired onset interval. This avoids the previous broad-prior overlap
  guard while preserving the original DATA search prior and fixed lead-in.
- The explicit specular-core pilot policy follows spectral cores, with zero
  added-AM allowance; the entire random-AM waveform is still input. This is a
  newly tested approximation, not a silent change to the conservative estimator.
  It supplies frequency assistance without pilot-to-DATA phase authority.
- FFT correlation shares observation transforms and template work across all
  14,071 timing positions, 13 epochs and both bit patterns. Two-ray complex
  likelihoods form proposals for **both bits**. Valid-only FFT padding avoids
  computing unused lags; all template/filter tails remain included.
- The final original dynamic likelihood scores a small correlated coordinate
  list, rather than expanding its coordinates back into a Cartesian product.
  Its original-prior core projection is fixed before observed selection. Both
  this projection and the coarse FIR are explicitly lossy; raw I/Q input does
  not imply a complete-sideband likelihood.

On the original retained capture, Helper narrows the old 365-point, approximately
0.5-Hz bank to three points, with a continuous CFO interval 4.0291–4.9153 Hz
covering actual 4.4678 Hz. Main holdouts retain three or four old-bank points.
The FFT screen then constructs a **different**, finer frequency grid, about
85 points at at most 0.01064-Hz spacing. Do not confuse these counts. A typical
screen covers 46,645,365 synchronization coordinates using 13,260 FFT products,
85 observation FFTs and 78 cached template workspaces. Dynamic refinement uses
at most 64 correlated coordinates; the original retained capture uses 30.

FP32 complex bulk processing remains explicit. Small Gram matrices, rank tests
and quadratic-score reductions retain FP64. A specialized well-conditioned
two-ray Hermitian expression avoids a generic contraction; ill-conditioned,
rank-deficient and one-ray cases retain the reference contraction. Complete
acquisition timings below include this optimization; no Cartesian-bank runtime
ratio is claimed from the earlier unexecuted arithmetic projection.

A complete strict-FP64 replay of the original native capture selected the same
bit, ordered coarse coordinates and sparse refinement coordinates as FP32. Final
scores were 2212.574180 (FP64) and 2212.574392 (FP32), a relative difference
9.59×10⁻⁸; margins were 2198.729713 and 2198.729928. This is paired numerical
evidence at the demonstrated physical link, not permission to transfer an
admission gate between arithmetic identities or a near-threshold guarantee.

## Frozen calibration and independent validation

The procedure was frozen before generating 61 distinct native captures. Nineteen
Helper-present/DATA-absent calibration captures precede independent validation.
The receiver uses the original public epoch/templates, not the actual shifted
transmit epoch. Every receiver output is saved and hashed before evaluator truth
is opened. The complete adaptive procedure has a new identity and a conservative
rank gate at **α=0.05 per capture**. Older gates were not borrowed.

| Independent group | Captures | Correct admissions | Wrong admissions | Not received | False admissions |
| --- | ---: | ---: | ---: | ---: | ---: |
| Physical target, balanced bits | 16 | 16 | 0 | 0 | — |
| Helper present, DATA absent | 16 | — | — | — | 0 |
| Pure noise, receiver still expecting Helper | 4 | — | — | — | 0 |
| DATA injected at 8 dB-Hz, Helper at 18 dB-Hz | 4 | 0 | 0 | 4 unresolved | Not calibrated |
| Physical target plus random ray width 0.5 Hz | 2 | 0 | 0 | 2 unresolved | Not calibrated |

All 16 target signals retain the true epoch; onset hulls contain actual arrival
and frequency intervals contain independently calculated physical CFO. Four
signals explicitly use arrivals −7.035, +7.035, −7.034 and +7.034 seconds.
Their final scores range 2007.54–2433.58, versus 2.45–7.92 in calibration.
Fifteen Helper-only holdouts execute the downstream receiver; one fails the
onset guard. All four pure-noise controls stop at onset, so they do not provide
downstream pure-noise scorer coverage.

The 16/16 success count has a binomial-reference two-sided 95% interval
**79.4–100%**, not a demonstrated 99% reception rate. Boundary-forced trials are
heterogeneous, so that interval is not a deployment-population guarantee.
Zero false admissions in 16 Helper-null trials has a one-sided 95% upper bound
**17.1%**; zero in four pure-noise trials gives **52.7%**. The matched rank test
has its exchangeable-null marginal statement, not a conditional, daily or
continuous-monitoring guarantee. No claim of 10⁻⁴/day follows from this study.

The stress failures identify acquisition limitations:

- All four injected 8-dB-Hz cases fail the fixed onset guard: working energy
  z statistics −0.061 to 2.314, below eight. No DATA acquisition was attempted.
  This is not a physical DATA sensitivity boundary. Their receipts contain two
  identities because cached per-worker construction took epoch-dependent power
  metadata from the first stress row; they are uncalibrated diagnostics, not one
  common calibrated stress procedure.
- With random ray width 0.5 Hz, one onset fails at z=6.59. The other narrows
  frequency to a 4.1165-Hz interval but exceeds the declared 32,768 FFT-product
  budget. That is an explicit computational guard, not a measured decode failure
  after executing the search. Raising the guard alone would not establish
  renewing-channel sensitivity or real-time margin.

The archived original summary has `misses=0` in these two stress groups because
its admission branch excludes uncalibrated regimes. Interpret them as the six
nonreceptions above, **not** zero misses or successful stress coverage. This
report preserves the original output and records the correction explicitly.

## Measured latency, preparation and throughput

This host has six physical cores/twelve logical CPUs. Uncontended cold runs use
two-thread numerical pools and two FFT workers. CPU time sums process threads;
elapsed time is measured separately. The values below are a three-capture
engineering range, not a timing confidence interval.

| Boundary | Process CPU seconds | Elapsed seconds |
| --- | ---: | ---: |
| Helper + full coarse acquisition + dynamic preparation/scoring | 16.13–18.85 | 13.04–15.60 |
| FFT constructor, starting with raw keyed templates | 3.18–3.30 | 3.17–3.26 |
| Fresh process total, including above, imports/loading/finalization | 21.74–24.56 | 18.51–21.36 |
| Extra raw keyed-template generation/export, paired estimate | 8.45 | 8.45 |
| Real-audio conversion, previous synthetic-PCM cost diagnostic | 2.25–2.34 | 2.25–2.34 |

The template estimate is an identical-seed native run with and without template
export. Observations are byte-identical. Subtracting 2.417 CPU seconds without
templates from 10.862 with templates gives 8.445 seconds for construction plus
export I/O. It is one paired cost estimate, not a pure in-memory synthesis timer
or a confidence bound. Native channel simulation itself is not receiver work.

The cold Python totals correspond to **0.257–0.290 CPU seconds per captured
second**, or **0.353–0.399 per on-air second**. Charging template generation and
the maximum separately measured audio-conversion cost gives approximately **0.384–0.417 CPU
seconds per captured second**, or **0.528–0.574 per on-air second**. Additive elapsed
cost is approximately **29.3–32.2 seconds**, giving about **2.6–2.9×** the captured
time and **1.9–2.1×** the emission-time headroom. These include one-time preparation
that can be done before receiving DATA; no overlapping work is credited here.

The conversion term is computational evidence only: synthetic real 48-kHz PCM,
1725-Hz direct conversion and 6-kHz output with a nominal 2800-Hz frontend. It
does not validate full stochastic sideband/image preservation, driver service,
continuous cross-window state or cable reception. The current implementation
still scores after a complete capture, so processing headroom is not zero
post-observation backlog. Peak measured process RSS was about **496–507 MiB**.
The proposed initial 4× *fully cold* elapsed-headroom goal was not met after
charging raw template synthesis; the measured cold path is nevertheless below
real time with the margins above. Prepared scoring is faster, but excludes that
work and cannot stand in for cold cost.

The complete 61-capture campaign took **417.17 seconds**, including native
generation, hashes, loading, four worker initializations/caches, scoring and
reporting. Four concurrent workers use one BLAS/Torch thread each and two FFT
workers. Most physical-link captures exercise full acquisition; pure-noise and
stress failures exit early. This mixed campaign is throughput evidence, not a
single-capture latency claim or a homogeneous 61-receiver throughput benchmark.
No GPU/cloud instance was needed for this seven-minute local campaign. GPU
acceleration is not required for the reported local-CPU margin and was not
measured for this new path.

## Reproduction and remaining work

Use the focused checks in the implementation guide for ordinary development.
They cover actual arithmetic/coordinate/fallback behavior in well under a second
after numerical-library imports; they do not rerun this research campaign.
Only changes to acquisition geometry, channel assumptions, candidate retention,
precision or admission policy warrant focused sampled requalification. Changes
to the underlying dynamic scorer require its existing joint/acquisition/scan
checks. Do not put all 61 native captures into routine CI.

The compact evidence includes the frozen protocol, original receiver/evaluation
outputs, gate, native commands and artifact identities, latency/preparation
recipes, numerical environment and source snapshots. Raw I/Q and templates remain
in ignored `local-evidence/`; regenerating them is occasional qualification.
The archived recipes expect copying back to `local-evidence/triad-realtime/` in a
compatible checkout. First generate the base capture using the recorded command
from the preceding CPU study, then run `campaign.py --prepare` once and
`campaign.py --run`; do not replace an existing frozen protocol. Historical hashes
are deliberately source-bound, so changed implementations require a new output
location/protocol rather than relabeling old results.

Evidence entry points:

- [Frozen protocol](evidence/protocol.json), [original summary](evidence/summary.json)
  and [rank gate](evidence/gate.json).
- [Cold latency](evidence/latency-summary.json), [template preparation](evidence/template-cost.json)
  and [derived inclusive costs](evidence/cost-derived.json).
- [Precision comparison](evidence/precision-comparison.json),
  [focused tests](evidence/focused-tests.json) and [environment](evidence/environment.json).
- [Original result archive](evidence/original-results.tar.gz) and
  [per-file identities](evidence/original-results-index.json), including the
  108-case clock-projection measurement. The 13.4-MB archive retains original
  result bytes; the approximately 500-MB sampled captures are reproducible
  separately and are not included.
- [Source overlay](evidence/source-overlay.tar.gz),
  [source identities](evidence/source-overlay-index.json),
  [campaign recipe](evidence/recipes/campaign.py),
  [latency recipe](evidence/recipes/latency.py) and [SHA-256 inventory](SHA256SUMS).

The final focused test run completed **82 passes and two explicit skips** in
8.33 seconds (84 discovered cases): one CUDA check had no local hardware, and
one optional older native replay was not requested. This run includes the new
Helper/FFT/sparse/admission tests and existing dynamic joint, acquisition, scan,
projection, frequency and campaign checks. `git diff --check` also passed. No
ordinary-receiver regression or native rebuild was needed for these isolated
Python-only runtime changes. Unit results do not replace the sampled evidence.

Remaining work is explicit: lower-SNR Helper onset, broadened/renewing-ray
acquisition, arbitrary erasures/missed hops, rates outside this geometry,
no-Helper acquisition, operational false admissions, continuous device/streaming
support and a multi-bit message adapter. A single-bit receiver result does not
establish the CPU cost or completion behavior of the 22-bit `fox` message.
