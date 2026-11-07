# High-SNR HF cost and duration estimate

Follow-up to the [46.4-second, target-8 cost measurement](../2026-11-06-hf-current-cost/README.md).
The new question omits that planning target and asks for a short preparation
and transmission allocation at the physical link's higher SNR. Runtime source
remains `7ac6bc22daf3f4a1fde9dab75cc8414b8d120723`; no receiver algorithm was
changed. The same local Ryzen 5 PRO 5650U and two numerical threads are used.

## Configuration and planning

Encrypted DATA 4 W, Helper 40 W, path loss 180 dB, default broad timing search,
free-crystal base oscillator, IC-7100 shift oscillator, HF-worst, Each 65%,
margin 3, Added Symbols Interleave ×10, FHSS/Burst off, Rate 36 Hz,
30 MHz shift, DATA IF 1725 Hz, automatic shaping. At the assumed receiver noise
density **−164 dBm/Hz**, nominal received C/N₀ is **20.0206 dB-Hz DATA** and
**30.0206 dB-Hz total Helper**, before random channel energy changes. Neither
number is an individual pilot's C/N₀ or a guaranteed instantaneous value.

The maintained stochastic sampled channel uses 90 Hz ray-center separation,
21 ms nominal excess delay, independent 0.5 Hz ray-renewal width, 0.0002 ms
delay excursion and 20-second delay knots. Random Each-65 modulation, tails and
real-audio image overlap are retained. These specific random-process laws are
study assumptions, not a unique interpretation of the GUI preset.

The current planner at 20.0206 dB-Hz selects **11.6 seconds DATA per bit** for
both one bit and 22-bit `fox`, including its default 99% requested per-message
planning allocation. The native emitted descriptor confirms **300000 samples
at 6000 Hz = 50.0 seconds** for one bit including the 38-second Helper lead.
For `fox`, one shared lead gives **22 × 11.6 + 38 + 0.4 = 293.6 seconds**.
The latter is a serialization estimate: arbitrary-text completion is not
implemented in this isolated receiver. Whole-bit fixture scheduling waits are
excluded from both numbers.

At a **fixed 11.6-second bit duration**, the duration surrogate's 90% per-message
allocation crosses at 11.61 dB-Hz for one bit and 13.15 dB-Hz for `fox`.
An explicitly assumed 50% retained useful energy raises those thresholds to
**14.62 and 16.16 dB-Hz**, respectively. Thus approximately 16–17 dB-Hz is a
*model-based* `fox` planning target with that assumed energy reserve, not an
experimentally qualified >90% end-to-end success threshold.
The assumed 3 dB allowance is not a measured bound on selected channel mismatch.
The model reserves half the error budget for acquisition/Helper/wrong-bit
errors without proving that allocation. Strong selective modulation and moving
delay mismatch remain incompletely represented. All corresponding model,
correct-probability and physical-null qualification flags remain false.

## Fresh full-window measurements

Independent channel seeds 202611065001/5002, bits 0/1, and arrivals +2.7/−6.9 s
use an independent public template fixture (seed 202611065000). Receiver scoring
does not open evaluator truth. Both decisions were correct and passed the
unchanged conditional admission gate; neither establishes physical admission.

| Measurement | Bit 0 | Bit 1 |
|---|---:|---:|
| Prepared RX CPU seconds | 26.399 | 24.007 |
| Prepared RX elapsed seconds | 20.474 | 18.092 |
| Workspace preparation CPU seconds | 1.399 | 1.294 |
| Scoring worker elapsed seconds | 18.252 | 15.955 |
| Peak process RSS, MiB | 571.1 | 597.7 |
| Coarse numeric peak estimate, MiB | 277.9 | 286.4 |

RX includes 48 kHz real PCM ingestion, continuous DDC to 6 kHz, Helper,
full broad coarse timing/epoch/clock acquisition, and sparse Gaussian-state
refinement including observation-dependent preparation. It excludes TX/channel
and PCM synthesis. It is unpaced software replay, not physical ADC/ASRC or
live backlog measurement. Full public coarse searching and sparse refinement
must not be described as exhaustive execution of the enormous fine-grid
admission prior. Both trials retained 15 Helper frequency proposals; duplicate
endpoint snapping gives 14 executed coarse frequency points in the first.

Paired native exports with/without templates took 7.447/2.980 CPU seconds:
their **4.467 CPU-second difference** estimates raw keyed template generation.
This is one paired difference, not an isolated API timer. With workspace and
roughly two seconds of imports, startup is **7.6–7.9 CPU seconds**, or about
**7.3–7.6 elapsed seconds**. A practical initial allowance is roughly **10
seconds before transmission**. Existing full exporter setup also simulates an
unused observation; charging that overhead makes startup approximately
10.3–10.6 elapsed seconds. No unsupported preparation overlap with the Helper
lead is credited. Subsequent keys still require about 5.8 CPU seconds of
template/workspace preparation per bit.

Measured/estimated cost denominators:

| Quantity | CPU seconds / received second |
|---|---:|
| Prepared RX divided by active DATA duration | 2.07–2.28 |
| RX plus per-bit keyed preparation, continuous DATA cadence | **2.57–2.78** |
| One-bit average across 50 s, including cold startup | **0.63–0.69** |
| Repeated-bit `fox` forecast across 293.6 s, including preparation | **2.24–2.42** |

The `fox` forecast is 657–712 aggregate CPU seconds. It repeats measured
single-bit processing and per-bit keyed preparation, with startup once; shared
Helper reuse and message-adapter overhead are unmeasured. These are engineering
estimates, not confidence intervals or a worst-case CPU ceiling. The shorter
duration lowers absolute CPU time but increases CPU per DATA second because
the broad timing bank and setup do not shrink proportionally.

**The current single scoring worker is slower than the 11.6-second DATA cadence.**
The six-core host has more aggregate CPU capacity than the estimate consumes,
but that does not establish a working parallel pipeline or sustained real-time
contiguous-message reception.

The scheduler's numeric buffer/copy bound is 23,958,558 bytes and its ring holds
887354 narrowband samples, approximately 148 seconds at 6 kHz. Coarse numeric
workspace is about 0.3 GiB, with phase-dependent transient allocations; do not
sum coarse and refinement peak scratch or add already-counted templates again.
Whole-process RSS includes libraries, PCM preparation and receiver allocations.
**Allow approximately 1 GiB for this process**, rather than interpreting its
configured 2 GiB guard as a measured requirement. This is a practical allowance
for these measured cases, not a guarantee over all fallback paths.

## Lower-SNR diagnostic

One additional replay reuses the bit-0 channel realization and adds fresh
temporary-key AES-256 noise across the entire 48 kHz DAC band. Quiet-band
metrology estimates nominal DATA C/N₀ **16.226 dB-Hz**, compared with the
requested 16.2. This is a nominal unit-DATA reference, not post-fading realized
signal energy. No clipping occurred. The correct bit passed the unchanged
conditional gate (log-e 432.06 versus threshold 21.49), retaining 15 Helper
frequency proposals. RX took 25.398 CPU / 19.421 elapsed seconds, plus 1.433 CPU
seconds for workspace setup. This supports feasibility for that realization;
one such success does not measure the >90% threshold or qualify the assumed
3 dB energy reserve. Exact AES-noise replay requires the locally retained PCM;
the temporary noise key is not stored.

## Evidence and reproduction

[Compact evidence](evidence.tar.gz) includes exact native commands, measured
timers, public/received manifests and hashes, raw receiver decisions, numerical
work counters, planner calculations and the measurement wrapper. Large PCM/IQ
and templates remain in ignored `local-evidence/triad-hf-high-snr-cost/`.
`local-evidence/triad-hf-high-snr-plan/` holds the cheap planner calculations.
Use the same workflow and Python dependencies as the linked earlier report;
fresh generation scripts and exact invocations are archived. No live device or
RunPod resource was used, and no ordinary receiver regression was run for this
measurement-only task.
