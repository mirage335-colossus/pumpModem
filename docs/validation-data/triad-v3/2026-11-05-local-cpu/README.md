# Local CPU cost: HF crystal / IC-7100, Helper, Each65

Later implementation: the [hierarchical CPU receiver study](../2026-11-06-realtime/README.md)
demonstrates practical complete-capture acquisition for this bounded physical-link
case. The measurements below remain the historical Cartesian-reference diagnosis;
they are not the cost of that newer operationally motivated acquisition path.

**The current dynamic joint receiver does not provide practical complete broad
acquisition for this case.** Small tiles are inexpensive to score after preparation,
but explicitly covering the broad timing, epoch, frequency and sample-clock banks
produces over 208 million synchronization hypotheses. The default candidate guard
rejects this bank. The arithmetic projections below describe the cost of the
current tile implementation, **not a measured full run, a fundamental receiver
limit, or the fastest possible algorithm**. This task changes no receiver code.

## Why this differs from the earlier near-real-time results

The [earlier frequency-assistance study](../2026-11-05-helper-frequency/README.md)
conditioned on known arrival, epoch and sample clock. Adding the explicit banks
here multiplies synchronization hypotheses by 3378×13×13 = **570,882**, before
considering the fallback from about 11 Helper-selected frequencies to 365
(another 33.2×). The combined difference is approximately 18.9 million candidate
combinations per former adaptive candidate bank. These are work-count ratios,
not measured runtime ratios or evidence of a comparable code performance regression.
The modulation widths and capture geometry also differ between studies.

Source inspection confirms why the current exhaustive path scales poorly:
`TiledAcquisition.score()` constructs a new receiver for every tile. Its constructor
rebuilds block/projection geometry, sampled bases, their Fourier projections,
Gram matrices and Gaussian channel designs. `correlations()` also retransforms
the same observations for each tile, despite the fixed full-bank projection.
The measured preparation fraction is 98.05%; eliminating all preparation would
still leave the excessive search count. The `DynamicJointReceiver` docstring
explicitly describes an initially small bounded acquisition bank. This is a
useful finite reference, not a demonstrated efficient blind live receiver.

Shared observation transforms, geometry and unmodulated source bases are clear
reuse opportunities. Projected Gram/covariance designs are not automatically
invariant to timing, CFO or clock changes; indiscriminate caching would change
the likelihood. Hierarchical or FFT acquisition and a qualified Helper onset/
frequency estimator require comparisons against the small exhaustive reference.
The static whole-observation FFT likelihood is a useful alternative to test,
not an already equivalent dynamic-channel replacement. No speedup or sensitivity
improvement is claimed from this read-only diagnosis.

## Configuration and measurement boundaries

Measured on an AMD Ryzen 5 PRO 5650U, six physical cores / twelve logical CPUs,
Linux 7.2.6, Python 3.13.5, NumPy 2.5.4, SciPy 1.18.1 and PyTorch 2.8 CPU.
The primary measurements limit numerical pools to two threads. CPU seconds use
`time.process_time()` (all process threads); elapsed seconds use `perf_counter()`.
CPU seconds are not elapsed seconds multiplied by a configured thread count.

Native source revision: `879fb8a2cea2a70a8947cadb09cd2dd864ba4b24`.
`ninja -C build/dev-cli -n triad_helper_export` reported no work.
Binary, Python sources, generated inputs and environment identities are retained in
[measurements.json](evidence/measurements.json). The binary SHA-256 is
`c497ba10d1255f1e56a2976eb4c1be4854ba5f88b2891f0be15110a06ac22242`.

The native capture uses encrypted DATA at 4 W, Helper at 40 W, 180 dB path loss,
30 MHz RF, 1725 Hz DATA IF, free crystal base clock and IC-7100 shift clock.
Rate 36, Added Symbols ×10 with interleave, FHSS off, Symbol Burst off and automatic
shaping give 60 fine coefficients/s/carrier, 75 Hz spacing and 23.2 seconds per
bit. The exporter requests 16 seconds by default; shaping/calendar constraints
round that to 23.2 seconds. The 8 dB-Hz target is planning metadata, not an
automatic duration selection or injected noise override. At the simulator's
−164 dBm/Hz noise density, the physical DATA C/N₀ is **20.0206 dB-Hz**.

HF-worst at margin 3 resolves to two rays separated by 90 Hz, with 21 ms excess
delay. Each65 uses random bounded selective filter coefficients, fast width 90 Hz
and slow width 9 Hz. Stochastic export requires an explicit random **ray** width,
which the requested preset does not specify: this measurement sets it to zero,
with zero additional delay excursion. Thus the rays have fixed center frequencies
and delays while additional selective modulation varies randomly. This is not a
qualification of every randomly changing propagation model.

The capture contains 508,066 complex samples at 6 kHz, or **84.6777 seconds**,
including broad timing uncertainty, padding and tails. Emission lasts 61.6 seconds,
of which 23.2 seconds is the DATA pattern calendar. Receiver processing starts with
this complex, zero-centered I/Q. Acquisition/driver work on a real device is
excluded; a separate real-audio conversion diagnostic is reported below.

## Measured preparation and scoring

The measured family is `TiledAcquisition` with core spectral projection, 0.25-second
blocks, two physical channel taps, coherence alternatives 0.5 seconds and infinity,
and the guarded affine scan. Bulk arithmetic is complex FP32; covariance/design
preparation is CPU complex FP64 and likelihood accumulation is FP64. A tile holds
eight arrival hypotheses at one epoch, one CFO and one sample-clock hypothesis.
It scores both keyed bit alternatives. Each measured size-eight tile uses 338 blocks,
49,790 retained DFT modes and about 28.77 MB of owned workspace (31.36 MB estimated
preparation peak). These are workspace estimates, not measured peak process RSS.

| Stage, one eight-arrival tile | CPU seconds | Elapsed seconds |
|---|---:|---:|
| Raw-template → design/workspace preparation | 1.907–2.055 | 1.716–1.835 |
| DATA score of an already prepared tile | 0.0349–0.0444 | 0.0298–0.0339 |

Across one training and four held-out size-eight tiles, this is **0.000412–0.000524 CPU
seconds per captured second for scoring**, or **0.0230–0.0248 including tile
preparation**. Those small ratios describe one tile, not the complete search;
they exclude the separately listed Helper attempt, bank planning, imports and
raw-template generation. Preparation starts with the 26 keyed raw templates
already generated; their construction cost was not isolated from simulation.
No scan fallback occurred; scores were finite. No correctness/admission threshold
or signal sensitivity was qualified by this timing experiment.

Additional measured costs:

- Native waveform/channel generation **and** template export: 9.798 CPU seconds,
  9.798 elapsed seconds. This includes simulation and disk output; it is not a
  receiver-preparation measurement and is excluded from the receiver ratios.
- Numerical-library import: 0.979 CPU seconds / 0.863 elapsed seconds. Memory-map
  setup: 0.00075 seconds; later first-touch input reads belong to measured preparation.
- Public bank planning: 0.0532 CPU seconds / 0.0365 elapsed seconds.
- Helper frequency attempt: 0.0245 CPU seconds / 0.0192 elapsed seconds. It retains
  **365/365 frequency candidates**, reporting `no_usable_pilot`: both pilot fit
  windows overlap the conservative possible DATA envelope under broad timing and
  AM90. This is an implementation guard, not proof that these pilots contain no
  useful frequency information. The independent timing guard disables coherent
  pilot-to-DATA phase transfer (`helper_phase_timing_unresolved`).

The broad prior spans 14.07 seconds. Without inferring Helper onset, the
intersection of all possible pure-Helper lead intervals is at most
38−14.07 = **23.93 seconds**, even before filter/support guards. A fixed
36-second window therefore cannot be guaranteed DATA-free across that entire
prior. Each actual transmission still has its 38-second lead. Coarse onset
acquisition followed by an onset-relative 36-second frequency window could avoid
this particular guard while preserving the waveform and lead. This is an
unimplemented candidate remedy requiring onset/coverage/admission validation;
the present fallback skips the pilot fit rather than demonstrating weak pilots.

One/two/six-thread diagnostic pairs all scored in approximately 0.0335 seconds
elapsed; mean score CPU rose from 0.0335 to 0.0401 to 0.0761 seconds. More numerical
threads did not speed up these small tiles. Independent-tile parallel execution
across six cores was not measured; dividing totals by six is not a validated
latency forecast.

## Explicit broad bank and conditional work projection

No maintained caller automatically supplies a fully resolving default joint
bank. This engineering scenario makes the choices explicit:

| Dimension | Bank |
|---|---|
| Arrival | 3378 points over ±7.035 seconds; 4.16642 ms spacing |
| Key epoch | 13 integer alternatives, −6 through +6 seconds |
| Common CFO | 365 points over ±90.5220 Hz; 0.497374 Hz spacing |
| Sample clock | 13 points over ±300.015 ppm; 50.0025 ppm spacing |

The clock half-step displaces the capture endpoint by 1.917 ms; arrival half-step
is 2.083 ms. These are explicit engineering mismatch budgets, not validated bounds
on sensitivity loss. Both bit alternatives and both coherence models are scored
inside each synchronization hypothesis. The count is **208,371,930 hypotheses /
26,092,755 tiles**, versus the maintained default 1,000,000-hypothesis guard.
The guard was raised solely to construct geometry and access selected tiles;
the full search was never executed.

The CFO bank is a segmented/dynamic engineering choice, not a whole-bit coherent
grid. A rectangular whole-bit 1/(4T) spacing benchmark requires approximately
16,803 frequency points here. It would be incorrect to call the 365-point study
an exact continuous or fully coherent exhaustive search.

Multiplying observed size-eight tile costs by the tile count gives:

| Quantity | Conditional arithmetic projection |
|---|---:|
| Preparation | 49.8–53.6 million CPU seconds (576–620 CPU-days) |
| Prepared scoring | 0.911–1.158 million CPU seconds |
| Prepared score CPU seconds /84.6777-second capture second | 10,754–13,677 |
| Preparation + score CPU seconds /capture second | 600,269–646,780 |
| Prepared score CPU seconds /61.6-second on-air second | 14,782–18,801 |
| Preparation + score CPU seconds /on-air second | 825,152–889,087 |

These ranges describe observed tile variation, **not statistical confidence
intervals or future runtime bounds**. Last partial tiles have two arrivals;
treating them as eight adds less than 0.3% to the arithmetic projection. It does
not resolve the much larger unvalidated outer-loop effects. The actual implementation
releases tile workspaces, but retains per-tile receipts outside the tile memory
budget; millions of receipts introduce an untested memory regime. Full-bank
execution is unsupported under the maintained cap. Prepared-only scoring assumes
reuse of identical keyed designs: the current implementation normally prepares
each tile for the call, and a different bit/epoch/key requires corresponding
templates. A campaign sharing designs is not a steady live-stream CPU ceiling.

The large cost identifies a serious implementation bottleneck: rebuilding
candidate-dependent designs across the arrival/frequency/clock/epoch product.
Existing FFT/static-channel receiver evidence describes another likelihood and
cannot substitute for this dynamic-channel workload without model validation.
These results do not rule out dramatically cheaper acquisition, workspace reuse,
or useful Helper timing/frequency inference.

## Prediction check and inexpensive reproduction

The model was frozen after tiles of 1, 4 and 8 arrivals and before four held-out
size-eight tiles at different arrival/CFO/epoch/clock coordinates. Frozen forecast for
their total preparation was 7.6297 CPU seconds; observed 7.9285 (−3.77% forecast
error). Frozen scoring forecast was 0.16243 CPU seconds; observed 0.15522 (+4.65%).
Elapsed errors were −3.43% and+5.70%, respectively. This checks local tile scaling;
it does not independently validate the 208-million-hypothesis projection.

The measurement script took 23.32 seconds elapsed with the generated capture
already present. Native capture generation took 9.80 seconds separately. The audio
diagnostic below took another approximately7 seconds of computation. Do not run
full acquisition to reproduce this cost diagnosis.

From the repository root, with the compatible native target built through
`./build.sh`, copy the archived scripts to an ignored evidence directory (their
repository-root calculation expects that location):

```sh
mkdir -p local-evidence/triad-cpu-ceiling-replay
cp docs/validation-data/triad-v3/2026-11-05-local-cpu/evidence/measure.py local-evidence/triad-cpu-ceiling-replay/
cp docs/validation-data/triad-v3/2026-11-05-local-cpu/evidence/frontend.py local-evidence/triad-cpu-ceiling-replay/
/tmp/triad-weak-env/bin/python local-evidence/triad-cpu-ceiling-replay/measure.py
/tmp/triad-weak-env/bin/python local-evidence/triad-cpu-ceiling-replay/frontend.py
```

The environment path is local, not a portable dependency installer. The measurement
recipe uses the recorded SciPy/OpenBLAS symbols for explicit thread control;
another numerical build may require adapting that control and re-recording its
environment. Keep output/reference identities distinct when dependencies change.
Evidence includes the original raw measurements, normalized summary, frozen model,
generation command, scripts and source/input hashes. No cloud or live audio device
was used; the ordinary receiver and its regression suite remain outside scope.

## Analog frontend diagnostic

The maintained `triad_live_waveforms.from_audio()` was separately timed on 84.6777
seconds of synthetic float32 real PCM, 48 kHz → 6 kHz, 1725 Hz conversion center and
a 2800 Hz nominal retained band. Each call includes mixing, image-filter design,
373-tap image rejection, polyphase resampling, allocation and validity mask:
**2.252–2.342 CPU seconds**, or **0.0266–0.0277 CPU seconds per audio second**.
Elapsed cost was 2.251–2.342 seconds. See [frontend.json](evidence/frontend.json).
Input generation and Python imports are excluded. This is an actual maintained
conversion function, but it was not attached to the dynamic receiver in this test.

The test is a computational diagnostic, not a physical cable or complete live
receiver measurement. Its 2800 Hz nominal band preserves pilot centers; it does
not establish preservation of the complete AM90 sideband envelope, which can
cross the real-audio DC/image-separation limit at 1725 Hz. It assumes alignment,
has no cross-window streaming filter state, and excludes device/driver servicing.
Adding its small cost to the large search projection does not close these live
integration or spectral-validation gaps. A cable alone does not reproduce the
specified atmospheric impairments.

## Transmit duration

The current **configured** one-bit plan is 23.2 seconds of DATA plus the single
38-second Helper lead and final shaping/dwell rounding: **61.6 seconds on air**.
This was confirmed by the native exported source calendar. Capture padding and
receiver physical-end confirmation are not transmit time.

The maintained short-text encoder gives `fox` exactly 22 bits:
`1110001001111101111000` (f: 6, o: 3, x: 13 including its literal escape). Encryption
adds no source bits. Serial use of this same pattern calendar would be
22×23.2=510.4 seconds DATA, plus 38 seconds Helper once, final pulse halo and
400 ms dwell rounding: **548.8 seconds (9 minutes 8.8 seconds)**. The encoder was
called directly from the existing `compression_short.cpp.o`; this is a
codec/calendar estimate, **not a supported full-message waveform test**. The
isolated v3 source is one-bit and the Helper export's limited alternating-slot
evaluator does not implement the 22-bit `fox` message adapter.

The small [encoder probe](evidence/fox_probe.cpp) is reproducible against the
compatible existing native object:

```sh
g++ -std=c++20 -Iinclude docs/validation-data/triad-v3/2026-11-05-local-cpu/evidence/fox_probe.cpp build/dev-cli/CMakeFiles/datapump.dir/src/compression_short.cpp.o -o /tmp/triad_fox_probe
/tmp/triad_fox_probe
```

Neither duration is demonstrated to meet 99% correct reception / 10⁻⁴ false
admission per day for the complete requested stochastic broad-acquisition case.
The target 8 dB-Hz does not currently select a reliability-qualified duration.
The CPU measurements and arithmetic projections concern the one-bit observation
only. A longer blind message changes clock resolution and workspace requirements;
tracking between message bits would be a different, unmeasured execution path.
