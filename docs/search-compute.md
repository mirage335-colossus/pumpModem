# Batched iterative search

The iterative pattern receiver executes numerical batches on the CPU. This
supports large logical work grids without creating one operating-system thread,
template cache or transform buffer per hypothesis.

For a proposed independent chain that preserves this receiver and all transmitter
implementations, see [alternate Robust receiver opportunity and limitations](robust-alternate-receiver.md).
That study distinguishes further computational savings from sensitivity gains,
especially for symbols lasting days at path losses above 200 dB. Its numerical
targets are unmeasured and separate from the implemented changes below.

## Oscillator policy and bounded banks

Application configurations attach `Config::oscillator_search`, containing the
Baseband and Shift effective-link models, Shift (default 0 Hz), conservative
margin (default 3×) and clock-reference topology. Application settings fix USB;
the low-level policy retains an explicit sideband orientation.
`oscillator_pattern_search` produces one finite bank of explicit
`{frequency_offset_hz, clock_error_ppm}` pairs. Live reception, transfer reception,
simulation, link planning and compute estimates use that same policy. A
low-level caller omitting the optional policy retains its historical search.
Explicit receiver hypothesis lists remain an override.

A radio supplying real ADC/DAC samples retains the actual nonzero modem tone
in `Config::carrier_hz`. Choosing Shift Osc **Baseband clock** declares that
sampling and conversion share the selected Baseband reference, counted once.
At Shift zero, only the Baseband model contributes frequency, sample-clock or
phase error; every Shift contribution is excluded regardless of legacy topology.
Public Carrier is the absolute frequency and Shift is the known LO/translation
frequency. The real USB stream tone is Carrier minus Shift, which must be
positive; that tone, rather than the absolute frequency, drives DSP sample-rate
planning. `--rf-carrier` and `--rf-shift` are compatibility aliases for Carrier
and Shift. Equivalent alias entries normalize to the same policy and bank.
See [reference topology and bounds](oscillator-models.md).

An active shared Baseband clock requires one linked sample-rate hypothesis per frequency
candidate. Independent Baseband and active Shift conversion require independent
rate uncertainty; their paired bank omits combinations inconsistent with the
declared converter allowance. At Shift zero the bank uses only the Baseband
clock relationship. Both FFT and compact correlation consume the
explicit pairs. The FFT path does not infer sample-clock error by dividing an
conversion-derived frequency offset by the modem tone.

A sufficiently narrow independent region can use one timing lane per frequency
without assuming shared clocks. For stream tone `c`, Baseband uncertainty `A`
ppm and Shift uncertainty `B` Hz, the domain is `f = c * e * 1e-6 + r`, with
`|e| <= A` and `|r| <= B`. The nearest frequency lane, spaced by `df`, uses
`clamp(f / c * 1e6, -A, A)` for its timing. Its worst-case timing residual is
bounded by `(df/2 + B) / c * 1e6` ppm. This reduction is used only when that
fits the existing independent timing tolerance; the implementation also checks
the actual rounded lane coordinates. Wider regions retain a separate timing
grid. Reported timing cell width includes the complete represented radius,
while the requested physical bounds and coverage limits remain unchanged.

The frequency lattice uses spacing no larger than `0.25/T`, with `T` the sampled
symbol duration, refined to the declared endpoints. Independent rate cells allow
a quarter-chip accumulated mismatch over that symbol. The finite limits are
4,097 distinct frequencies, 65 independent rates, 8,194 frequency/rate lanes,
and an absolute clock-rate range of +/-10,000 ppm,
with an additional limit from the real passband, including clock-scaled waveform
support and tone alias boundaries. These limits reduce actual
coverage rather than silently coarsening the lattice. Workspace determines
backend support and affordability while preserving the declared pairs; it does
not silently narrow the policy bank. Results expose requested and covered
frequency/rate half-widths, paired lane count and limited joint coverage. Phase
diffusion affects coherence; it does not become a Doppler or phase-trajectory
search dimension.

Shift metadata alone creates no signal samples or cipher work once
the stream tone is fixed. At a fixed public Carrier, changing Shift changes that
tone and can change the planned PCM sample rate. A wider ADC stream costs
more front-end processing, while repeated searches operate on reusable projected
observations. A higher physical RF frequency can require more hypotheses at the
same fractional accuracy; representing the same radio setting another way cannot.
The automatic sample clock rounds up to whole sampled half-chips when an exactly
represented decimal Rate permits it with at most 5% extra samples. For example,
Rate 100 Hz uses 6000 samples/s for both 1500 Hz and 1490 Hz stream tones, keeping
120-sample chips instead of creating partial chips and much larger transforms.
Even chip lengths also avoid one-sample projection bins at nearby tones such as
1480 Hz; its half-chip lattice otherwise collapses with an odd 119-sample chip.
This preserves the selected carrier and nominal symbol-duration formula. If an
aligned clock exceeds the 5% overhead or 120 MHz limit, the minimum valid clock
remains in use, including any inefficient geometry. Arbitrary long integration endpoints
can still have clock/RAM gaps; their actual geometry remains part of the estimate.
The main frequency-reference controls are Baseband Osc, Shift Osc and Margin.
The separate editable UTC clock fields do not change that reference topology.
Numerical assumptions and requested/covered bank information
appear under the Link planner's hideable **Model limits and references**.

The current local clock candidate has an internal compact-correlator affine
arrival map. It intersects each original half-chip coverage cell with the
admitted canonical-phase lattice, retaining original pulse parity and all
frequency/rate pairs. Raw/pulse regression comparisons cover clipped endpoints,
negative fractional origins and nonzero clock rates. Custom native compact
acquisition now uses an explicitly estimated physical timing model. FFT paths
and missing timing evidence retain the full window. The planner counts the
complete correction-domain union, including every original lane, and retains
the full arrival-window cost as a fallback before capture metadata exists. For
existing eligible long compact banks, the estimate also reports exact retained
origin/phase counts at a representative anchor using the actual captured timing
error and rate bounds. The original full-count preflight RAM remains reserved.
Short FFT banks receive no timing discount. Known unsteered simulation uses its
original bank; hardware retains every possible peer steering lane. Entering a
small GPS duration alone earns no CPU discount.
This is not a measured native acquisition speedup; see [timing conditions](clock-sync.md).

Outer DSSS changes actual fine-chip/sample geometry and template work. The
current matched-template fallback is included in the work model; there is no
constant-cost fast despreader or measured 1000x spreading speedup. Probability
estimates for the complete new receiver remain unqualified; separately labeled
conditional matched-template AWGN references may be shown. [Spread controls](spread-spectrum-controls.md)
describe the open structured-interference and qualification limits.

## Compute boundaries

| Path | Input and output | Host responsibilities |
| --- | --- | --- |
| FFT acquisition, `src/pattern_fft_batch.hpp` | Indexed symbol/phase/frequency jobs, shared spectrum and energy rows, immutable pattern parameters or prepared templates; flat pairs of scores indexed by job and start position | Search enumeration, trial counts, thresholds, peak selection, tracking, admission and publication |
| Long-symbol correlation, `src/pattern_correlator_batch.hpp` | Indexed numerical lanes containing coordinates and fits, immutable pattern parameters, shared block projection rows; updated fits for each lane | Symbol completion, phase selection, peer ownership, reception state and publication |
| CPU execution, `src/search_parallel.hpp` | Contiguous logical ranges with a chosen grain size | A shared, bounded persistent worker pool, worker-private caches and exception collection |

Logical indices do not encode a CPU worker number. Range dispatch allocates no
state proportional to the number of jobs; tests cover 100,003 jobs and sparse
ranges spanning `SIZE_MAX`. CPU concurrency still defaults to all but one
available logical CPU, with at least one. Available work and workspace can lower
the actual concurrency of the numerical batch paths.

CPU correlation chooses its range grain from both lane count and worker count,
targeting at least 16 ranges per worker when enough lanes exist, capped at 16
lanes per range. Small banks therefore retain enough independently schedulable
work, including when only some start-time hypotheses have become active. The
historical five-frequency, nominal-rate 1 Hz bank has 75 lanes; its previous
fixed grain of 16 exposed only five parallel jobs despite an 11-worker limit.

FFT job capacity now depends on the search bank and spare workspace, rather
than a small multiple of the CPU count. Each CPU worker retains only one
transform buffer and, when required, one mutable pattern cache. Jobs have
disjoint output slices. A backend can reorder or tile them while the host
collects scores in the original symbol/phase/frequency/start order. Prepared
template views stay valid until the synchronous backend call completes.

The raw long-symbol coordinator groups up to 64 original processing blocks and
tiles up to 65,536 numerical lanes at a time, reducing dispatch and host scans
between blocks. Both bounds can be reduced by spare workspace. Each lane
processes its blocks in the original order. The original oscillator restarts,
prefix sums and floating-point addition sequence remain intact; combining all
the samples into one new oscillator block would change scores.

Batching ends **before any hypothesis can complete a symbol**. The original
scalar coordinator handles that boundary in its original hypothesis order.
This also prevents batching across a caller's progress poll. No bit waits for
an arbitrary batch to fill, and partial silence cannot finish reception early.
One-worker and tight-workspace operation retain the unbatched coordinator.

All temporary arrays are bounded by spare DSP workspace. They are released
before they can displace payload growth and at the end of a push, preserving
idle receiver-bank capacity. Batch records contain no received messages, trial
counters or publication callbacks. Copied pattern seeds are cleared when the
batch view is destroyed. CPU pattern caches must be constructed from the same
configuration as the supplied immutable pattern parameters.

## Long-symbol pulse projection

Eligible shaped correlation separates shared pulse projections from private
template coefficients. Each chip cell retains 17 finite RRC pulse atoms, their
sample/template dots, a pulse-energy Gram matrix and a carrier-square Gram
matrix. Frequency/rate pairs share these observations across the two half-chip
start parities, with a separate lattice when the upper start endpoint is
clipped. Keyed bit/epoch coefficients are then contracted at chip-cell cadence,
rather than rebuilding two shaped private templates for every raw sample and
start hypothesis.

The fit retains the original **real observation count and received energy**.
Both Gram matrices are necessary: the carrier-square term preserves the image
covariance that can matter at low modem frequencies. Cells are a numerical
factorization of the raw two-real-basis fit, not newly independent observations
or a change to the noise evidence. Aggregating products changes floating-point
addition order; qualification compares numerical fits within stated tolerances,
not bit-identical scores.

The path requires shaped symbols lasting at least 16 sampled seconds and an
integral number of chips divisible by four. The former 4,096-sample chip limit
is removed for explicit paired banks. Those banks use their
individual half-chip start spacing `C/(2*r)`, where `C` is nominal chip length
and `r` the clock ratio, including noninteger received chip durations. The legacy
path requires even chip lengths, nominal clock rates and at most 4,096 samples
per chip, preserving its historical small-workspace footprint. Unsupported legacy
grids and unaffordable projection workspace retain the full raw bank. The
numerical pulse Gram cache reuses a quadratic expression within each pulse-table
region, for both small and large chips. The small-chip kernel updates changed
sample contributions across nearby regions; the large-chip kernel recomputes
bounded segment sums when the common valid interval or sample count changes.
Closed-support endpoints remain literal samples.

Paired shaped symbols use an additional affine-span path when they do not
contain a multiple of four complete chips, or when the larger whole-chip
projection state cannot fit. They must last at least 16 seconds and have
at least 1,024 samples per chip. Affordable whole-chip projection remains the
first choice. This fallback shares complex sample moments, then
combines the 17 pulse atoms into each fresh private 0/1 template before fitting.
It needs no per-lane pulse Gram matrix or symbol-sized buffer. The last partial
chip retains its original center and amplitude, including its shifted table
knots. Every chip, quarter, complete local window, caller push and oscillator
block boundary clips the span. Four immutable carrier-moment entries per
frequency cover the full block and selected short lengths. Other lengths use
the same exact bounded geometric sum on the caller's stack, shared by both bit
fits. The palette and its tags have explicit workspace accounting; no mutable
cache crosses parallel private searches. Smaller chips and
tight budgets keep the raw path. The 1,024-sample gate avoids replacing cheap
short spans with more setup work.

The affine path fits observations at the same oscillator-block boundaries.
For chips of at least 8,192 samples whose shortest table interval exceeds that
block, it can retain the two private templates' complex values and slopes until
the natural pulse interval ends. Each lane and phase group owns an 80-byte
entry. Reuse translates the anchored value by the integer sample displacement;
it does not accumulate a chain of incremental translations. The interval ends
at a regular or shifted final-pulse knot, chip, quarter, local window or symbol
boundary. Literal support endpoints remain singleton spans. Completing a symbol
clears its entries before the next private stream address is selected.
When every active group's interval covers the entire next oscillator block,
the receiver also skips repeated phase, clock and boundary calculations. It
still performs the original per-block fit in the same order. Materializing
deferred quarter state invalidates the intervals so the next fit observes the
newly active boundaries.

This optional cache uses available workspace without lowering the preceding
path's bit-retention limit or detector reservations. Payload allocation and
workspace reduction can evict it and resume the preceding arithmetic. Eviction
cleanses the retained private coefficients. The cache is never shared between
keys, epochs, lanes or phase groups; only the existing public sample projections
are shared. Smaller intervals and insufficient workspace use the preceding
affine path automatically. There is no persisted setting.

Private coefficient construction then follows natural pulse intervals, while
I/Q projection, covariance fitting and ordered admission retain their original
sample/block cadence. No filtering, decimation, oscillator-bank reduction or
additional observation delay is introduced. Search is still not independent
of input rate in arbitrarily oversampled geometries. Reassociation introduces
floating-point differences, which require paired validation against both the
preceding optimized receiver and the raw reference; see the
[measurement record](pulse-moment-validation.md). No accepted bit waits for a
later block or symbol.

Cell storage is bounded by the processing block and bank size, independently of
symbol duration, including 32 KiB of preparation scratch in the workspace
allowance. Pending cells retain actual observations across caller pushes.
Symbol, quarter and local differential boundaries use the same canonical sampled
endpoints; input beyond a symbol is never needed to publish that completed
symbol. The host keeps admission, private phase selection, trials, pending-bit
publication and fully observed absence outside the projection layer.

For chips above 4,096 samples, the frontend exploits the actual finite pulse
table's piecewise-linear interpolation. Within one table segment each atom is
`a + b*n`. Shared complex zeroth and first sample moments therefore supply its
I/Q dot product without evaluating all 17 atoms at every sample. At most 260
segments, including literal closed-support endpoints, cover one chip cell.
Unmodulated polynomial sums and binary concatenation of geometric moments give
all 153 Gram entries without scanning the original chip. The latter remains
well conditioned at DC and carrier-square aliases; it does not divide by a
small `1-z`. Fractional clocks may rebuild the bounded segment sums, while
private key, epoch and bit coefficients are contracted only when a cell completes.

This is an algebraic factorization of every sampled finite pulse, with no
decimation or bandwidth cutoff. It includes spectral tails and amplitude-limited
input, introduces no filter state or delay, and keeps the original noise energy,
sample count and admission thresholds. Floating-point differences still require
paired sensitivity qualification. No samples or hypotheses are discarded.

Live banks can also share the original carrier prefix and moment arrays across
keys and epochs for one immutable PCM push. Cache keys include the exact input
subspan, receiver sample coordinate, sample rate, carrier and oscillator block
convention. Private patterns never enter this cache. Its 256 KiB ceiling,
128-row bound, reserved detector storage and finite-push recording/drain
allocation bounds are checked before use. The bounds include vector growth
and transient copies; they replace the earlier blanket 2 MiB reservation per
receiver. Admission sums them once and consumes them as each receiver runs,
without repeatedly scanning every peer. Exhaustion falls back to the receiver's own frontend;
the cache is released before presentation/content growth. Different sample
origins and mixed FFT banks can reduce or disable sharing. Sequential whole-file
transfer banks currently use individual frontends.

Receiver-major pushes can exhaust this small cache quickly: 101 carriers and
2,048 samples require 6,464 compact rows, while the arena holds about 118.
A successful short-block sharing test is therefore not evidence of sustained
frontend sharing for that larger push. The cache ceiling has not been raised.

The planner charges unique carrier projections, bounded segment/Gram work and
chip-cadence private fits separately. It conservatively assumes cross-receiver
cache misses and up to three timing lattices per frequency/rate pair; actual
clipped timing ranges can require fewer. Expected kernel preparation follows
initial setup and changed-knot/count density. A separate full-rebuild allowance
is exposed for fractional clocks and excluded from the central total. This
avoids presenting a cache-miss bound as ordinary CPU demand. The density and
operation coefficients remain engineering estimates, not measured execution
times. Partial affine spans are counted separately from complete chip cells. Input conversion still follows sample
rate, and wider oscillator uncertainty can require more carrier banks. See
[pulse-moment measurements and limits](pulse-moment-validation.md) for paired
execution and sensitivity evidence, and the earlier
[oscillator-search qualification](oscillator-search-validation.md).

## CPU validation and reproduction

The CPU reference tests exercise 16,387 FFT jobs and 10,019 correlator lanes,
worker-count and tile-size equivalence, reordered FFT jobs, malformed spans,
cancellation and reuse. Receiver-level tests cover next-poll prefixes, physical
absence, tight workspace and idle footprints. Pulse regressions compare the
factored real fit with raw fits, including low/high modem carriers, signed rate
error, quarter/differential scores and 137-sample pushes. Numerical score
tolerances and completed-bit/endpoint identity are separate checks. Current
candidate qualification and measured workloads are recorded in
[validation](validation.md).

For a reproducible large-bank CPU workload, build and run:

```sh
cmake --build build --target benchmark_correlator
./build/benchmark_correlator 1200 -10 1 2048 .25 1
./build/benchmark_correlator 1200 -10 0 2048 .25 1
```

This selects 11,265 hypotheses with approximately 631-second symbols, feeds
2,048 deterministic noise samples, and reports wall time, process CPU time and
idle memory. Arguments select bandwidth, target C/N0, workers, sample count,
clock uncertainty and compact mode. C/N0 selects the receiver geometry; this
partial-symbol workload measures computation, not decoding or detection
probability. Run paired comparisons without other CPU-heavy work.

The separate post-reception hard-bit recovery engine and its resumable search
are outside these DSP interfaces. Wire formats, source decoding and GUI message
presentation are also unchanged; see the [development contract](development.md).

The [9 October DSSS follow-up measurements](dsss-followup-validation.md)
compare actual execution with the preceding optimized local receiver. They
record coherent-guard CPU regressions as well as bounded RF improvements;
the new work allowances are conservative accounting, not recalibrated throughput.
The next local candidate adds conservative whole-batch pruning for qualified
short private FFT arrival windows. Its engineering model retains ingestion,
continuation and the original trial charges, and separately reports full-window
fallback. Timing telemetry and measurements remain distinct from this estimate.

The current [local UTC benchmark](validation.md#utc-fft-launch-keys-and-audio-follow-up--manual-checkpoint-9-october-2026)
measures complete three-bit receptions against the frozen preceding optimized
receiver. The supplied1200x10 case reduces total CPU from1.389 to0.284s for13s PCM
(three pairs;48-byte retained-state increase). This is one key/epoch bank; it is
not a full Live acquisition or broader sensitivity qualification.
