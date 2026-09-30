# Alternate Robust receiver opportunity and limitations

This design study evaluates an independent receiver for the existing Robust
waveform, especially where path loss exceeds 200 dB and symbols can last days.
The opportunity is to reduce repeated correlation and search work, bound memory,
and make useful carrier, clock and phase hypotheses affordable. It is not a
proposal to modify the existing Robust receiver or any transmitter implementation.

**Status: proposed research, not an implemented or qualified modem.** The source
assessment is dated 2026-09-30 against revision
`31027e2bd23faf7d14371e945fd7232cd17bccf0`. No alternate decoder was built or
profiled for this study. No CPU speedup, new sensitivity threshold, successful
200 dB reception or GUI option is established by this document.

The practical planning targets are **2–4 times the receiver work per CPU-second**
for equivalent coverage, with **5–10 times** an optimistic goal for particular
expensive long-symbol cases. Approximately **3 dB additional usable link margin**
is an initial experimental objective; **6 dB** is a stretch objective. These
numbers are engineering judgments, not measurements, forecasts with confidence
intervals, or consequences of choosing a particular filter. First establish
reception at the baseline conditions before testing an additional margin.

The [development contract](development.md) remains authoritative. Related
material covers [existing CPU batching](search-compute.md),
[weak-signal search limits](weak-signal.md), [statistical link planning](weak-link-planning.md)
and the [historical 1.2 kHz case study](1200hz-weak-link-planning.md).

## Evidence and interpretation

| Statement | Evidence and limit |
| --- | --- |
| Robust already uses FFT cross-correlation and CPU batching | Verified in current source; it is not a long direct FIR receiver awaiting an FFT conversion. |
| Search multiplication, repeated shaped references and template regeneration are optimization candidates | Inferred from source loops; their shares of actual CPU time have not been profiled for this study. |
| A 2–4 times efficiency improvement is worth investigating | An unmeasured development target for a separate implementation on the same hardware. The lower end is not a guaranteed minimum. |
| Shorter coherent segments can reduce required carrier-grid density | A conditional mathematical property of a different detector; not a measured whole-receiver speedup. |
| Faster equivalent processing of the same samples gives no inherent sensitivity gain | It evaluates the same evidence sooner. Better matching or additional search coverage may recover implementation losses, but that gain is unmeasured. |
| The ideal energy duration grows tenfold per additional 10 dB loss | Conditional on coherent integration and fixed required symbol energy. Phase uncertainty and detector penalties can require more time. |
| Three or six additional decibels are possible qualification objectives | They must be demonstrated at specified detection, false-accept, coverage and resource criteria. Neither is a current capability claim. |

The GUI and CLI processing estimates are operation-count models with fixed
reference-machine assumptions. They do not profile the current computer.
The [Robust CPU cost measurements](robust-cpu-costs.md) compare receive-processing
mitigation overhead on particular workloads; they do not attribute total DSP
time to FIR, FFT or template generation. Historical runtime estimates in the
1.2 kHz study predate current detector branches and explicitly include unsupported
workspace cases. Do not reuse them as current measured performance.

## Existing processing and likely costs

The implementation already avoids several obvious inefficiencies:

| Component | Current behavior | Candidate work for a separate receiver |
| --- | --- | --- |
| [FFT acquisition](../src/pattern_receiver.cpp) | Overlap-save correlation, one input spectrum reused across hypotheses, cached or streamed reference spectra | Tuned batched transforms, more reuse across mathematically identical observations, improved cache and memory layout |
| [Compact correlator](../src/pattern_correlator.cpp) | Shared carrier projections and bounded lanes; shaped references evaluated for both bits at sample coordinates | Factor reusable pulse calculations out of repeated reference evaluation, where equivalence can be shown |
| [FFT scoring batches](../src/pattern_fft_batch.cpp) | Coherent, drift and eligible local differential calculations, with direct/FFT crossovers | Reuse segment statistics and avoid repeated large transforms for local work |
| [Search geometry](../src/pattern_search.cpp) | Carrier spacing proportional to the inverse of complete symbol duration; finite hypothesis cap | A search organized around bounded coherent intervals and explicitly modeled trajectories |
| [Live receiver banks](../src/live.cpp) | Multiple key, epoch and receive-profile banks share a workspace allowance | Share input-only work without mixing keyed references, scores or evidence between hypotheses |

Input projection mixes PCM using an oscillator recurrence and averages disjoint
bins. It is not a conventional long per-sample receive FIR chain. The compact
shaped-reference path does perform repeated pulse-weighted sums, but their
presence does not establish that they dominate the whole application.
There is also an actual [FIR resampler](../src/resampler.cpp) when audio rates
need conversion. It serves capture and playback; changing that shared component
would cross the transmitter-preservation boundary.

Memory is part of the CPU tradeoff. Retaining every reference spectrum may be
prohibitive; streaming references saves RAM by regenerating work. Existing
batching and parallelism are already described in [search computation](search-compute.md).
A proposal must compare against those implementations, including the existing
coherent, four-section and local differential branches. Introducing segmentation
for the first time is not the opportunity.

## Filtering choices

FIR and IIR describe impulse-response structures. A biquad is a second-order
IIR section. FFT processing is an implementation technique, commonly used to
evaluate FIR convolution or correlation efficiently; it is not a third
transfer-function category.

For a known waveform in additive white Gaussian noise, a matched filter
maximizes output SNR among linear filters. A cheaper bandpass IIR is not an
equivalent replacement for correlation against an arbitrary keyed waveform.
This optimum does not make the current receiver optimal for every unknown,
drifting or interfering channel. See [MathWorks matched filtering](https://www.mathworks.com/help/phased/ug/matched-filtering.html).

Useful alternatives to investigate include optimized FFT batches, polyphase
or other multirate processing, bounded template caches, and factoring the
pulse response from chip-pattern correlation. IIR notches or whitening may help
specific interference or colored-noise cases. They require analysis of signal
loss, phase response, group delay, startup transients and the transformed noise
covariance. FIR can offer exactly linear phase and finite transients; IIR can
meet some magnitude specifications with lower order. Neither is universally
cheaper for this workload. See [the FIR and IIR comparison](https://www.mathworks.com/help/signal/ug/fir-filter-design.html).

The current scorer deliberately avoids counting correlated filtered outputs
as independent noise observations. A new filter chain must preserve a valid
likelihood/noise model or establish a new one. Reusing the old threshold with
more apparent samples can create false confidence instead of sensitivity.
Any decimation must also retain the occupied waveform and the full intended
carrier uncertainty without aliasing them away.

## Two different iterative searches

Signal acquisition searches timing, carrier, sample clock, phase behavior,
pattern position and locally configured key/profile alternatives. This is the
layer addressed by the proposed processing chain.

The existing [post-reception recovery engine](../src/recovery.cpp) instead
enumerates retained hard-bit assignments and alignment hypotheses, then uses
the existing correction and authentication routines. It runs only after
physical completion. It must remain outside the symbol scorer and retain the
hard-bit and missing-slot interface in the development contract. This proposal
does not feed audio, soft confidence or source-text plausibility into that layer.

In a simple exhaustive search over `m` independent unknown bits, work is
proportional to `2^m`. A candidate-throughput speedup `S` therefore buys only
about `log2(S)` additional unknown bits at the same budget: one bit for 2 times,
two bits for 4 times, and about 3.3 bits for 10 times. Actual RS and alignment
constraints alter the enumeration. There is no universal conversion from that
gain to channel SNR, and faster filtering does not directly accelerate it.

## CPU efficiency and sensitivity

A 2–4 times efficiency improvement means 50–75 percent fewer CPU-seconds for
the same specified receiver task. It need not mean the same reduction in total
machine utilization or elapsed time. UI work, acquisition latency, serial
tracking, memory traffic and concurrent recovery can remain.

As an illustration, accelerating a kernel fourfold when it occupies 80 percent
of total CPU work gives a whole-chain speedup of only
`1 / (0.20 + 0.80 / 4) = 2.5`. The 80 percent fraction is an example, not a
measurement of Robust. This is why a large FFT microbenchmark gain cannot be
reported as an application gain.

There are three distinct sensitivity comparisons:

1. **Same samples and equivalent detector:** zero inherent dB gain. Results can
   arrive sooner, backlog can shrink, or CPU use can fall.
2. **Same samples with better matching or more complete search:** losses from
   timing, carrier, phase or omitted hypotheses may be recovered. The amount
   requires paired detection and false-accept measurements.
3. **Longer symbols already present in received transmissions:** more signal
   energy can be accumulated if the receiver can process their full duration.
   This is conditional capability for existing longer waveform profiles,
   not increased energy in an unchanged recording.

Changing only an RX Expected SNR value cannot extend a received symbol, add
repetitions, or integrate unrelated payload symbols as the same unknown bit.
No transmitter code, settings, wire format or airtime changes are part of this
proposal. Comparisons across existing profiles must disclose their different
transmitted durations rather than describing them as a gain on identical PCM.

For a conditional scaling illustration, let complete-symbol work be
`W(T) = a * T^p`, where `T` is symbol duration. A speedup `S` permits:

- At fixed total computation per symbol: duration ratio `S^(1/p)` and ideal
  coherent C/N0 improvement `(10/p) * log10(S)` dB.
- At fixed average CPU rate while following the signal in real time: duration
  ratio `S^(1/(p-1))` for `p > 1`, and ideal improvement
  `(10/(p-1)) * log10(S)` dB. The longer symbol also allows more wall time.

For the illustrative case `p = 2`:

| CPU speedup | Ideal gain at fixed computation per symbol | Ideal gain at fixed real-time CPU rate |
| ---: | ---: | ---: |
| 2 times | 1.5 dB | 3.0 dB |
| 4 times | 3.0 dB | 6.0 dB |
| 10 times | 5.0 dB | 10.0 dB |

This table is not a receiver prediction. Longer observation and finer frequency
search can produce approximately quadratic work, but additional clock grids,
FFT factors, detector windows, memory limits and capped coverage change the
exponent. Search penalties and loss of coherence also change the required
energy. For linear work, a fixed real-time CPU-rate constraint alone does not
set a maximum duration; other limits still do. Do not use these equations to
claim arbitrarily weak reception.

## Link budgets above 200 dB loss

Path loss alone does not specify signal strength relative to receiver noise.
Using positive path loss `A`, transmit power `P` in dBm and effective receiver
noise density `N0` in dBm/Hz:

```text
received power in dBm = P - A
C/N0 in dB-Hz         = P - A - N0
SNR in bandwidth B   = C/N0 - 10 log10(B in Hz)
ideal Es/N0 in dB     = C/N0 + 10 log10(T in seconds)
```

Robust target planning uses C/N0 in **dB-Hz**. It must not be confused with
in-band SNR or path loss in dB. At fixed bandwidth, power and noise density,
one additional dB of tolerable loss corresponds to one dB lower C/N0.

The following arithmetic uses the project's +3 dBm and -164 dBm/Hz defaults
and the planner's nominal 18 dB symbol-energy target:

| Path loss | Actual C/N0 | Ideal days per bit | Three bits and one full absent symbol in days |
| ---: | ---: | ---: | ---: |
| 200 dB | -33 dB-Hz | 1.457 | 5.828 |
| 203 dB | -36 dB-Hz | 2.907 | 11.629 |
| 206 dB | -39 dB-Hz | 5.801 | 23.203 |
| 210 dB | -43 dB-Hz | 14.571 | 58.284 |
| 220 dB | -53 dB-Hz | 145.709 | 582.836 |
| 230 dB | -63 dB-Hz | 1457.090 | 5828.358 |

Here `T = 10^((18 - C/N0)/10)` seconds and a day is 86,400 seconds. The exact
short message `a = 011` provides the three-bit example. These are energy-model
durations, not optimized rates, Shannon limits or successful receiver trials.
They omit waveform tails and imperfect coherence. The 18 dB reference is not a
calibrated sensitivity threshold. A physical receiver's external noise and
interference may differ greatly from the assumed density.

For these long symbols, completion also requires one entirely observed absent
symbol. In general the fully scored failed durations must be consecutive and
cover at least six seconds. Six seconds of silence inside a day-long symbol
cannot finish the message. This cost remains even if computation becomes
instantaneous.

Thus the proposed 3 dB objective can be expressed as testing 200 versus 203 dB
at those fixed power/noise assumptions; 6 dB would mean 206 dB. These are
qualification steps, not established tolerable path losses. Test the same
waveform first to measure actual detector improvement. A separate longer-profile
test can assess computational reach, with its extra airtime explicitly recorded.

## Carrier coverage and memory

At the inspected revision, default long-pattern carrier spacing is
`delta_f = 0.25/T`. For a half-span `F` in Hz, an uncapped grid of that spacing
requires `Nf = 1 + 2 * ceil(F / delta_f)` distinct frequencies. At 1500 Hz,
plus or minus 200 ppm gives `F = 0.3 Hz`, assuming sufficient passband headroom.

| Path loss and ideal duration from the previous table | Frequencies for full span | Half-span retained by a 4097-frequency grid |
| --- | ---: | ---: |
| 200 dB | 302,145 | 0.004067 Hz |
| 210 dB | 3,021,423 | 0.000407 Hz |
| 220 dB | 30,214,211 | 0.000041 Hz |

These counts exclude timing, bit, key, epoch, profile and clock alternatives.
The current grid is capped at 4,097 distinct frequencies; its coupled bank
also considers nominal and carrier-coupled sample-clock hypotheses. At 200 dB
the uncapped carrier count alone is about 74 times the cap. A modeled 100 ppm
shift at 1500 Hz is 0.15 Hz, outside the approximately 0.004 Hz covered span.

Workspace limits are independent of this cap. Streaming reference rows avoids
retaining them all, but the FFT core must still fit. The existing local fallback
preserves bounded reception with narrower coverage; it does not qualify the
requested expanded search. See [implemented search limits](weak-signal.md#implemented-search-and-its-limits).

A new receiver's first savings may therefore pay for previously missing
coverage or make a case executable. Comparing its complete search against an
old capped search without reporting the difference would misstate both speedup
and sensitivity. No modest CPU factor establishes complete 200 dB coverage.

## Segment based acquisition

One architectural candidate is to correlate bounded coherent segments, retain
their sufficient statistics, and accumulate evidence across the full physical
symbol. If segment phases are deliberately not coherently combined, carrier
spacing can be chosen according to segment length `L`, rather than whole-symbol
duration `T`. This trades phase information for a less dense carrier grid.

Using the same illustrative quarter-cycle spacing, a 20,000-second segment
gives `delta_f = 0.0000125 Hz`. Full plus or minus 0.3 Hz coverage still requires
**48,001 frequencies**, more than the current cap. Relative to whole-symbol
spacing, the approximate grid reductions are:

| Path loss and ideal whole-symbol duration | Approximate carrier-grid reduction using 20,000 seconds |
| --- | ---: |
| 200 dB | 6.3 times |
| 210 dB | 63 times |
| 220 dB | 630 times |

These ratios describe **one search dimension**, not overall speedups or dB
gains. Every segment still has to be processed. An implementation must cover
the entire symbol: either handle the final shorter segment correctly or use
`K = ceil(T/L)` equal segments of actual length `T/K`. With equal segments the
exact spacing and counts differ from the fixed-L illustration above.

Timing and sample-clock uncertainty remain. Trajectory alternatives can add
substantial work. Attempting coherent recombination across the whole symbol
can restore the need for fine residual-frequency or phase-trajectory resolution;
it cannot automatically retain both full coherence and the coarser grid.
Very weak segments need no individual hard decision, but their combined score
must have a justified noise distribution and account for all tested alternatives.

The sensitivity cost becomes severe when individual coherent segments are weak.
For a simple noncoherent energy sum with `K = T/L` independent normalized noise
observations, the noise standard deviation grows as `sqrt(K)`. With received
power-to-noise-density ratio `r` in linear units and segment coherence factor
`eta(L)`, the signal excess relative to that spread scales approximately as
`r * eta(L) * sqrt(T * L)`. Holding L, noise statistics, search volume and
detection criteria fixed then gives the low-segment-energy scaling
`T proportional to 1/r^2`: 10 dB weaker can require about 100 times the duration.

That is an asymptotic result for this energy-combining detector, not a law for
all segmented, differential or phase-tracking receivers. Before phase loss,
20,000-second segments have approximately 10, 0 and -10 dB segment energy at
the 200, 210 and 220 dB examples respectively. The regimes differ. The existing
[reference experiments](weak-link-planning.md#computed-reference-cases) illustrate
the loss but assume timing/clock acquisition and do not run the full receiver.
[ESA's baseband processing discussion](https://gssc.esa.int/navipedia/index.php/Baseband_Processing)
explains the coherent/noncoherent integration and clock/Doppler tradeoffs.

## Proposed independent processing chain

The intended boundary is:

```text
Captured PCM with original sample coordinates
    -> new capture-only conditioning and multirate processing
    -> shared segment projections and bounded reference/statistic caches
    -> timing, carrier and clock search with optional phase trajectories
    -> complete-symbol evidence and competing-hypothesis resolution
    -> existing PatternBurst representation
    -> unchanged transfer::StreamReceiver and post-end recovery
```

Develop the new DSP in a separate namespace and new files. Existing immutable
waveform definitions and public APIs may be consumed unchanged. Do not alter
Robust receiver sources, transmitter sources, shared pulse generation, crypto,
FEC, source codecs or the shared capture/playback resampler. Any new conditioning
belongs only to the alternate receive path. Avoid global compiler/math flags or
shared backend substitutions that would also change existing RX or TX behavior.
The [PatternBurst fields](../include/datapump/pattern_receiver.hpp) and
[content receiver interface](../include/datapump/transfer.hpp) define the
sample coordinates, positioned missing data and completion boundary to preserve.

Candidate work should proceed in increasing order of statistical change:

1. Measure equivalent calculations with optimized FFT batches, bounded caches
   and shared input-only projections. Tune plans, strides and alignment for
   the intended platforms; [FFTW's FAQ](https://www.fftw.org/faq/section3.html)
   describes relevant implementation choices, not a promised DataPump gain.
2. Factor repeated pulse/reference work and test multirate observations against
   the original waveform, including sample-clock mismatch and symbol edges.
3. Evaluate segment-based acquisition and conservative coarse-to-fine search.
   A permissive first stage, defensible pruning bound or measured miss-rate
   budget is necessary: exact verification cannot recover a discarded candidate.
4. Investigate phase/clock trajectory models only with explicit state, coverage,
   memory and false-accept budgets. A precise oscillator can reduce uncertainty
   only when the actual system justifies that assumption.

Bound live storage independently of total symbol airtime. Retain required
statistics and limited working windows instead of requiring days of PCM in RAM.
If a chosen refinement needs discarded samples, it must retain a budgeted window
or explicitly declare that refinement unavailable; it cannot silently rescore
data it no longer has. Shared work must keep alternative schedules and key/epoch
identities separate and never count reused observations as independent evidence.

Cancellation, exhausted computation and unscored hypotheses mean unfinished
coverage. They must never become physical absence. Competing duration/rate
hypotheses need fair, deterministic resolution before accepting an early
candidate. Every accepted bit must be available at the next progress poll;
segment processing does not create smaller payload symbols or permit premature
bit decisions.

## GUI integration and transmitter isolation

An eventual selectable label such as **Robust experimental RX** would describe
receiver compatibility, not a new wire protocol. It is a proposed label only.
The current GUI has no such mode or general receiver plug-in interface.

[Live Session](../src/live.cpp) owns concrete `modem::StreamingReceiver` objects,
and [StreamingReceiver](../src/streaming_modem.cpp) embeds `PatternReceiver`.
Transmit and receive implementations also share source files. Introducing a
factory by refactoring those files would violate this proposal's strict
preservation scope. A sibling receiver/controller and additions to shared GUI
routing and build composition are the prospective integration path.

Preserving transmitter source bytes is necessary but insufficient. The existing
GUI controller owns a persistent `live::Session`; that session's in-memory
per-key epoch history enforces the current transmit-use lock. All alternate-mode
send requests must reach that **same existing session**. Do not instantiate a
second sending session, destroy the original on selection, clear its history,
force transmission, or reinterpret an override to make the new mode work.
This preserves the existing session-lifetime protection; it does not claim that
the history survives application restart.

Audio ownership requires deliberate orchestration. `try_suspend_capture()`
refuses a handoff with pending receive/transmit work and returns success only
after idle capture is released. A suspended session rejects transmission.
Therefore a new controller cannot suspend Robust and continue sending through
it unchanged. A prospective application-level coordinator must acknowledge an
idle release by the alternate receiver, resume the existing session before
sending, and return capture safely afterward. Pending reception cannot be
silently transferred, reset into completion, or discarded to simplify selection.
This remains an integration design to prove, not an existing injectable API.
Resuming the existing session also resumes its ordinary capture/receiver path.
These APIs therefore do not establish transparent hot switching or uninterrupted
alternate reception during transmission. The first live design may need to
defer a switch or send until idle. If a viable design requires changing Robust
session internals, it falls outside the preservation scope instead of silently
authorizing that refactor.

The alternative must preserve these behavior boundaries:

- Exact short dictionary and explicit binary bits, including leading zeros and
  partial bytes; no added preamble, pilot, marker, padding, length, FEC or MAC.
- Existing fixed 192-bit marker and 128-coded-byte interval geometry for longer
  messages and attachments; no received length controls physical framing.
- Consecutive fully scored failed-symbol durations covering six seconds for physical end;
  EOF, successful correction, codec ends and cancellation are not completion.
- Next-poll accepted-bit progress, stable pending-row identity, positioned
  unknown slots, and the existing 4,096-bit diagnostic prefix bound.
- Existing bounded content interpretation, restricted received-text presentation,
  and recovery only after physical completion.

No receiver optimization establishes a new LPI, interference or cryptographic
security guarantee. It must leave the current transmission policy and key-use
tradeoffs intact. Narrowing a search to a known key, timestamp or oscillator
condition must be reported as a changed assumption, not a free general speedup.

## Evaluation and qualification

Start with an offline alternate receiver fed identical immutable PCM or
deterministically reproduced sample streams from the unchanged transmitter.
This isolates DSP value before introducing GUI/audio lifetime changes. Retain
independent wire vectors as well as generated captures: matching new code on
both sides would not establish compatibility.

| Stage | Required evidence | What it does not establish |
| --- | --- | --- |
| Baseline profiling | Time in projection, reference generation, FFT, scoring, tracking and separate recovery; key/profile/epoch counts and coverage | A source loop or modeled operation count is not a measured hotspot. |
| Numerical kernels | Reference comparisons across geometry, boundaries, precision, cancellation, tight memory and worker counts | Partial-symbol microbenchmarks do not demonstrate detection. |
| Full sampled reception | Accepted bits, erasures, identities, full-symbol absence and complete outcomes over signal/noise fixtures | An ideal correlated-statistics experiment does not replace PCM acquisition. |
| Resource comparison | CPU-seconds per audio-second and per bit, wall latency/backlog, peak memory, startup and cancellation latency | More threads can reduce wall time while increasing total CPU work. |
| Sensitivity comparison | Detection/miss and false-accept rates at fixed waveform, observation time and stated coverage, with uncertainty | A changed threshold, smaller search or longer transmission is not an identical-signal gain. |
| GUI and live integration | Audio handoff, unchanged TX route/history, pending rows, routing and cancellation on actual supported hosts | Successful offline reception does not qualify device lifecycle behavior. |

Use two distinct performance experiments: equal complete search coverage to
measure CPU savings, and equal resource budgets to measure useful capability.
Keep the waveform, key/epoch assumptions, carrier/clock range and acceptance
criteria explicit. Record omitted or incomplete work; do not compare a complete
candidate with an incompletely searched baseline as though their tasks match.

The signal matrix should include raw one-bit and partial-byte messages, short
dictionary texts, fixed-interval messages and attachments; AWGN and colored
noise; carrier offsets across the whole claimed span; independent sample-clock
error; phase diffusion and changing drift; timing edges; interference; wrong
keys; noise-only and isolated-fragment controls; supported sample-rate conversion;
multiple key/profile/epoch banks; and small/large workspace limits. Measure
complete-message outcomes as well as per-symbol results.

Use paired fixed seeds for comparisons and independent held-out captures for
qualification. Specify detection and false-accept criteria before interpreting
the results. Include confidence intervals and enough noise-only exposure for
the claimed rate; zero failures in a small trial count cannot establish an
extremely low false-alarm probability. Recalibrate changed statistics and account
for adaptive detector selection, pruning and all tested hypotheses.

Long-coordinate fixtures, accelerated generation, and bounded statistical
experiments serve different purposes. Each fully sampled long-symbol test must
process the samples and completion assertions it claims. Coordinate coverage
or a model-only trial is not physical oscillator stability over days, real-time
audio continuity, or demonstrated performance at that duration.

Before GUI delivery, retain the [existing contract suites](development.md#regression-coverage-and-checks),
including `pattern_receiver`, `pattern_correlator`, drift/differential/batch
tests, weak-signal and probability calibration, stream/recovery behavior,
live profile/resource tests and shared GUI progress. Add mode-switch tests for
the behaviors already covered by [transmit-lock regressions](../tests/test_live_transmit_lock.cpp):
future waveform exposure, cancellation, profile changes, key reload, stop/start,
clock changes and the existing one-transmission override. A mode change must not
reset or bypass any of them.

Use `./build.sh` and `./build.sh test GROUP` with the appropriate claimed build
tree. Focused cases precede the full applicable regression, native GUI, platform,
SDK and packaging checks with normal coverage and `devfast=false` for final CI.
Preserve every required seed and
statistical gate; follow [testing stages](building.md#testing-stages) and
[release qualification](releases.md) if publishing binaries. The present change
is documentation only; it adds no new decoder test result or release assurance.

## Decision points

Proceed from profiling to an independent prototype if the expensive work is
substantial and reusable under the same statistical and compatibility contract.
Report an efficiency result only after measuring equal work. Report sensitivity
only after demonstrating a detection/false-accept improvement under declared
conditions. Establish the selected 200 dB baseline before treating 203 or 206 dB
as meaningful additional qualification steps.

A useful outcome could be full carrier coverage, bounded RAM or dependable
progress on a case the old receiver cannot execute, even without a lower
fixed-waveform threshold. Conversely, a fast kernel is insufficient if phase
uncertainty, missed search cells or accumulation loss dominates. There is no
present basis for promising another 10–30 dB of path-loss tolerance from CPU
optimization alone.

Revisit these estimates after measured profiles, a specified processor and
workspace, actual power/noise/oscillator inputs, and complete sampled receiver
results are available. Keep that evidence separate from this proposal and from
the older reference-detector experiments.
