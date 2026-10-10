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
acquisition now uses an explicitly estimated physical timing model. Eligible
short private FFT paths skip whole acquisition batches excluded by the UTC
arrival map. The newer coherent path additionally intersects every original
canonical phase with original start cells, retaining fractional/refinement
margins and the complete admitted clock envelope. Empty template jobs are
omitted. At most 32 original starts use one template pair followed by direct dot
products; supported larger sets use automatically chosen partitioned convolution
when its complete numerical allowance beats the original FFT by at least 10%.
Otherwise the original shared FFT remains. All-direct execution omits the input
FFT; partitioned execution transforms only the required tiles. Each path retains
both complex components and the existing energy/covariance calculation.

Guarded timing components can execute once their own retained starts have complete
observed symbols plus the four-bin scheduling margin. Split components preserve
the existing peak-exclusion and candidate-capacity rules; unsupported unions keep
full-union scheduling. Unobserved samples are zero-padded only where no retained
dot product can read them, and never supply absent symbols. The complete original
logical batch is charged once to the trial budget, including excluded starts.
Stream addresses, independent 0/1 patterns and frequency/rate lanes stay unchanged.
Ready tracking continues outside acquisition hops only before the earliest
unscored competing start, including refinement margin. Measured crossovers and
remaining full-bank qualification limits are recorded below.

The planner counts the complete correction-domain union, including every
original lane, and reports full arrival-window cost as a fallback before capture
metadata exists. Long compact banks report representative-anchor lattice counts.
Short coherent FFT counts use the shared runtime range and direct-method helpers,
with bounded per-epoch enumeration for initial and rolling cohorts. The GUI
separates representative retained jobs/positions from all-epoch CPU allowances,
and reports fine/inner/symbol timing, sample rate, timing grid, phase spacing,
combined admitted uncertainty, workspace, backend and fallback. The original
full-count preflight workspace remains reserved. Streamed idle epochs defer their
four FFT complex arrays and energy-prefix allocation until execution, then release
that scratch when no candidate tracks remain. Observation rings, partial bins,
oscillator state and active tracks persist. At transform262144 the five deferred
arrays total18,874,376 bytes per idle epoch. This is retained-buffer arithmetic,
not a peak-RSS or end-to-end performance measurement.

At 10 Hz / DSSS 1000× / target 40 dB-Hz / 7500 Hz stream carrier, the original
full-hop deadline is 26.2144 seconds and the logical start-grid hop is 13.4144
seconds. These are diagnostic reference coordinates, not mandatory new-backend
execution spans. Qualified component readiness follows its last retained start
plus the complete 12.8-second symbol and scheduling margin. The GUI reports
representative readiness and retained positions separately from the initial and
rolling epoch allowances. Known unsteered simulation uses its original bank;
hardware retains all possible peer steering lanes. A small GPS selection alone
earns no discount without a qualified map. See [timing conditions](clock-sync.md).

Automatic Live FFT estimates distinguish the initial epoch radius (default six
seconds, 13 epochs) from each receiver's ±7-second start scan and from the fresh
epochs admitted at each subsequent observed UTC second. Newly admitted cohorts
are charged constructor/template setup, mixing and full/retained FFT work over
a bounded unconfirmed lifetime. Retention must finish both the original start
scan and the physical prefix/symbol retirement condition; a small prior never
shortens these compatibility conditions. The initial cohort remains charged for
the full observation. See [simulation estimates](simulation-estimates.md) for the
cadence/lifetime assumptions, constant-time count envelope and reproduction.
This correction prevents a static per-epoch batch cap from hiding long-observation
acquisition cost. It does not model all synchronized/noise tracks, retries or
peak resident-bank memory, and compact rolling-admission cost remains unmodeled.
The CPU reference and initial workspace fit therefore do not certify total
receiver throughput or peak Live memory.

Outer DSSS changes actual fine-chip/sample geometry and template work. The
current matched-template fallback is included in the work model; there is no
constant-cost fast despreader or measured 1000x spreading speedup. Probability
estimates for the complete new receiver remain unqualified; separately labeled
conditional AWGN references may be shown. Interleave has separately versioned
maps/streams and a pre-limiter gain change. Eligible complex FFT geometries now
use an actual shaped/limited source reference; it is not full-bank sensitivity
qualification. Its finite template family uses measured construction/reuse costs
where eligible, with engineering allowances elsewhere. Compact CPU feasibility
and unsupported probability geometries remain unavailable. [Spread controls](spread-spectrum-controls.md)
describe the open structured-interference and qualification limits.

## Compute boundaries

| Path | Input and output | Host responsibilities |
| --- | --- | --- |
| FFT acquisition, `src/pattern_fft_batch.hpp` | Indexed symbol/phase/frequency jobs, shared spectrum or original projections and bounded partition tile caches, energy rows, immutable pattern parameters or prepared templates; flat pairs of scores indexed by job and start position | Search enumeration, trial counts, thresholds, peak selection, tracking, admission and publication |
| Long-symbol correlation, `src/pattern_correlator_batch.hpp` | Indexed numerical lanes containing coordinates and fits, immutable pattern parameters, shared block projection rows; updated fits for each lane | Symbol completion, phase selection, peer ownership, reception state and publication |
| CPU execution, `src/search_parallel.hpp` | Contiguous logical ranges with a chosen grain size | A shared, bounded persistent worker pool, worker-private caches and exception collection |

Paired bounded acquisition currently executes synchronously on the CPU; it does
not use the persistent worker pool or implement GPU dispatch.

Logical indices do not encode a CPU worker number. Range dispatch allocates no
state proportional to the number of jobs; tests cover 100,003 jobs and sparse
ranges spanning `SIZE_MAX`. The generic CPU pool defaults to all but one available logical CPU, with at least
one. Automatic large coherent private acquisition selects the measured serial
native path when eligible and its scratch fits; see the dispatch follow-up below. Available work and workspace can lower
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
The current local candidate selects original per-phase timing cells, omits empty
private-template jobs and uses paired direct correlation for at most 32 retained
starts. Eligible larger sets use partitioned convolution in the existing two FFT
scratch buffers. Tile selection includes input transforms, both private template
transforms, products and output transforms; unsuitable geometry uses the original
full FFT. Paired construction shares pulse evaluation and rotation while retaining
independent private 0/1 coefficients and ascending per-candidate energy sums.
Input validation/projection still processes every physical sample. Neither input
rate nor the waveform changes in this receiver optimization.

Guarded timing components can be scored once their own full-symbol observations
are available. Components are joined under the existing half-symbol peak exclusion
radius and only split when every component can retain at most one peak and the
original candidate capacity covers the complete set. Unsupported unions keep the
full admitted-union schedule. Full original acquisition trials are prepaid once
per logical hop. At partial EOF, a cold hop with no established output can be
replayed from the retained ring using the original finite-final charge. Its small
checkpoint survives logical advancement until the original full hop is physically
observed, including a hop whose only selected component finished earlier. Final
passes cannot prepay excluded, unobserved starts again. This
repeats bounded work without allocating another PCM buffer or manufacturing
absence. Already established output is never rewound; exact finite-final threshold
equivalence for its remaining tentative tail still needs separate qualification. Ready continuation is bounded by the next unscored competing
start. This preserves next-poll publication without using source decoding or EOF
as physical absence.

The component metadata is bounded independently of epoch count. Partition input
statistics belong to one exact observed segment/projection geometry and selected
tile set; they are shared across independent candidate rows within that execution
only. Private generated values remain specific to key, canonical epoch/ordinal,
phase, waveform version/factor and clock/frequency hypothesis. Scratch reuse
invalidates tracking products before overwrite. Cancellation discards unfinished
scores. No cross-key or cross-bit private waveform reuse is introduced.

Rolling-bank inspection also found valid repeated canonical templates. In the
12.8-second/40 ksample/s geometry, the four initial indices address epoch offsets
{0}, {12,13}, {25,26}, {38,39}, with ordinal zero. Different bank/index labels can
therefore use the same private pattern. Their PCM ranges, timing/clock searches,
thresholds and reception states remain distinct. The 73 constructor epochs in the
60-second Live fixture are themselves unique. An enumeration that retains every
bank gives 222 fully observed group instances and 48 canonical epochs; this is a
potential cache-reuse count, not executed telemetry or an achieved speedup.

V2 now retains its existing two canonical map/coefficient states across phase
changes, while invalidating caches addressed only by absolute chip number. A
larger shared fine-chip cache was not added: two paired rows for this geometry
would require about 4.1 MB plus bounded synchronization/ownership state, and its
benefit still needs measurement. Frontend duplication also remains: coherent
outer-code guarding reconstructs local-phase projections from raw PCM even when
Live supplies shared projections. Eliminating that work requires an explicit
phase/Gram convention; arbitrary-phase input cannot be reused silently.

The final5 candidate repeats the D1000 endpoints on identical 60-second PCM.
Six-second width takes median 5.988 seconds CPU versus preceding 13.001 seconds
(2.171× paired median, observed range 1.665–2.221×); one millisecond takes 1.829
versus 9.866 seconds (5.395×, range 5.380–5.630×). Three alternating pairs per
endpoint include the slower run. These ranges are not precise confidence
intervals. Final bookkeeping adds at most 4,344 retained bytes under the same
64 MiB bound. One cold finite-EOF replay in the six-second case changes executed
start count to 2,275,570 and costs additional CPU; one millisecond remains 1,500
starts without replay. Published-tail guard/threshold equivalence is unqualified.
Single final-source controls at 1 s, 100 ms and 10 ms take 2.986, 2.365 and
2.168 seconds CPU respectively; each completes the same bit/absence assertions.
They are not repeated paired intervals. The coarse final-source profile still finds search dominant at 1 ms: fused paired
direct generation/contraction 0.906 s, separate partition-template generation
0.460 s and continuation 0.307 s, versus 0.020 s input/projection/ring residual.
Instrumentation increases that run's total by 12.25% relative to its plain control;
these component values are diagnostic, not primary throughput. Full covariance
and template-energy construction remain inside fused scopes. The historical
width table below precedes that final replay correction. Single final-source arithmetic controls
keep identical selected cells, scheduling, PCM and trial charges while forcing
full FFT: automatic CPU is 1.875 versus 3.929 seconds at 1 ms, and 5.956 versus
7.236 seconds at 6 s. Output/completion agrees. This supports the automatic mix,
not a measured full-FFT/forced-partition crossover for every retained broad job.

The local 9 October bounded-acquisition sweep compares frozen preceding source
`9ee268e` with `tight-candidate-paired-3`, using identical saved PCM at 40 ksample/s,
one key/epoch, all five oscillator/clock pairs and a qualified synthetic capture
map. Each case processes 60 seconds, including a complete bit and observed absence.
Three alternating pairs per row run on isolated CPU 11, one numerical worker.
Construction, input/search, polling/finish and destruction are timed; PCM creation
and loading are excluded. All measured runs, including outliers, are retained.

| DSSS1000 admitted width | Baseline CPU, median | New CPU, median | Paired speedup median [observed range] |
| ---: | ---: | ---: | ---: |
| 6 s | 12.582 s | 5.361 s | 2.356× [2.344, 2.398] |
| 1 s | 9.730 s | 2.828 s | 3.430× [3.340, 3.576] |
| 100 ms | 9.500 s | 2.244 s | 4.202× [4.187, 4.295] |
| 10 ms | 9.452 s | 2.159 s | 4.347× [4.247, 4.472] |
| 1 ms | 9.783 s | 1.867 s | 5.261× [5.063, 5.344] |

These ranges are descriptive, not precise 95% intervals. The retained start count
falls from 2,086,730 at 6 s to 1,500 at 1 ms. New total CPU falls by about 2.87×,
not by the approximately 1,391× position-count ratio: complete-symbol templates,
input processing and retained epoch alternatives still cost work. At 1 ms the
receiver executes 25 paired-direct and 15 partitioned jobs with no original broad
input/template FFT jobs. The peak accounted fixed-bank storage rises by 4,296 bytes
to 35,867,778; no new large tile buffer is allocated. First-bit media time changes
from 26.2144 to 13.7728 s; the physical symbol still lasts 12.8 s.

DSSS100 speedups are 1.332× at 6 s and 1.917× at 1 ms. The DSSS10 case uses its
application-selected 48 ksample/s, 9000 Hz stream carrier and 1200 Hz inner Rate
(106.667 ms symbols), unchanged within each pair. DSSS10 is essentially
unchanged (0.996× and 1.025× medians); its 6-second paired range includes 0.875×,
so a universal no-regression claim is unsupported. Full rolling Live pilots also
complete within the unchanged 2,216,646,656-byte workspace, while the preceding
baseline exhausts that budget before acquisition; no completed Live speedup ratio
is available. These stub runs do not qualify real audio-device deadlines.

Exact source/build hashes, all 54 timing runs, power/spectrum evidence, final-source
spot checks and remaining qualification are recorded in
`.agent-work/artifacts/receiver-opt-20261008/MANUAL-BOUNDED-ACQUISITION.md` and
`../receiver-tight-envelope-measure-20261009/paired3-width-matrix-summary.json`
(relative to that artifact directory). This is a local manual-test checkpoint.
Retained-score agreement does not alone qualify full acquisition sensitivity,
changed continuation order, interference or cumulative raw-reference loss.

The current [local UTC benchmark](validation.md#utc-fft-launch-keys-and-audio-follow-up--manual-checkpoint-9-october-2026)
measures complete three-bit receptions against the frozen preceding optimized
receiver. The supplied1200x10 case reduces total CPU from1.389 to0.284s for13s PCM
(three pairs;48-byte retained-state increase). This is one key/epoch bank; it is
not a full Live acquisition or broader sensitivity qualification.


## Experimental CPU search arithmetic (local manual candidate)

`--search-arithmetic default|fp32-min|fp64-force` is a receiver-only policy.
The developer-only **Search arithmetic** control, immediately before the persistent
planner's **Model limits and references**, offers exactly:

* **Default — automatic:** native FP32 for supported coherent serial streamed
  direct/partitioned/whole FFT acquisition, with FP64 elsewhere. This selects the
  previously measured FP32 implementation instead of retaining INT8 solely for
  its narrower operands. No current geometry has sufficient complete-execution
  evidence to enable INT8 automatically.
* **FP32 minimum:** prohibit lower-precision search operands; currently use the
  same supported native FP32 paths and original-FP64 numerical/resource fallbacks.
  This is a minimum operand width, not a demand to narrow every calculation.
* **FP64 force:** original templates and FP64 search, with existing wider timing,
  phase, energy and cancellation-sensitive calculations preserved. Exact caching
  remains allowed; approximate interpolation is prohibited.

| Option | Actual execution | Expected CPU ceiling |
| --- | --- | --- |
| Default — automatic | Native FP32 and eligible template reuse where supported; higher-precision fallbacks elsewhere | Best demonstrated path in the measured geometries |
| FP32 minimum | Currently the same paths as Default; prohibits lower-precision operands | Essentially the same as Default today |
| FP64 force | FP64 operands and original templates; bypasses approximate template interpolation | Independent diagnostic reference; usually slower in the demanding tested cases |

The FP64 interpolation bypass is deliberate: it protects the independent original
template reference from interpolation error, in addition to forcing arithmetic
precision. A Default/FP64 comparison therefore measures both arithmetic and reuse
choices. It must not be labeled a pure floating-point-width speedup.

Fresh startup with no setting selects Default; a partial parameter-list import
with no arithmetic field preserves the current choice. `fp32`/`FP32` and
`fp64`/`FP64` remain aliases for `fp32-min` and `fp64-force`. Old `matrix8`,
`8-bit`, `int8` and `INT8` selections migrate to Default on CLI/import/load;
export/save writes the canonical policy. `matrix4`/`4-bit` remains rejected.
Internal integer enum values and kernels remain available to diagnostics, with
no public dropdown choice and no automatic integer dispatch. Requested policy,
resolved arithmetic and template reuse are distinct. GPU work remains deferred.
Transmitter samples, framing and cryptographic addresses do not depend on policy.

Approximate template reuse has a separate low-level permission, defaulting to
exact-only. Default and FP32 minimum may authorize the existing checked close-clock
interpolation in supported acquisition. Selecting FP32 alone in a low-level batch
does not authorize it. Identity, finite-pulse boundaries, displacement and workspace
checks remain mandatory. FP64 force and original-operand numerical fallbacks bypass
it. This does not disable any existing exact template or transform cache.

The retained internal integer diagnostic coverage is deliberately explicit: coherent bounded
paired direct acquisition with at most 32 admitted starts. Broader partitioned/FFT
searches, compact pulse statistics and tracking still execute FP64. This is
**partial INT8 coverage**, not a completed conversion of every CPU search path.
The selection never widens an admitted timing set to make a matrix. Experiments
with a 128-position direct crossover were slower than the preceding partitioned
algorithm on this host, so the manual candidate keeps the 32-position crossover.
FP32 direct and coherent FFT acquisition avoid integer quantization; eligible
private templates may use the bounded interpolation described below. Supported
streamed serial whole FFTs and bounded partitioned
FFTs now use native `complex<float>` storage, multiplication and accumulation,
with stage-contiguous public twiddle tables and optional runtime AVX2 butterflies.
The FP64 fallback can use the same tabulated kernel with double operands. Scalar
kernels preserve portable binaries; neither AVX2 nor GPU dependencies are global
requirements. Phase origins, absolute times, epochs, private coefficient generation,
template-energy sums, covariance and final scores remain wide. Sample-fit,
differential and drift reductions normalize to FP64 **before** input/template
preparation. Compact statistics and tracking retain FP64.

The requested mode is independent of execution ISA and storage. Native float
whole/partition arrays do not replace all receiver buffers: original observations
remain double for accurate energy and fallback. Optional plans/arrays are charged
against workspace before allocation and cannot lower retained-bit capacity.
Insufficient allowance retains the complete original search in higher precision;
serial whole-FFT scratch fallback regenerates original FP64 operands.
Cached/parallel whole FFTs and mixed-cohort generic fallbacks retain original FP64.
The old wide-buffer FP32 kernel remains an internal diagnostic, not an automatic
or minimum-policy fallback.
Huge/tiny finite input ranges switch to original FP64 operands. Native float
products are checked before scores are published; unsafe templates/products retry
from original operands. No quantized observation is used as a fallback reference.
FP64 remains unquantized. Its public tables cache the exact legacy stage
recurrence; scalar/AVX2 forward and independent inverse tests preserve numeric
equality on this host. This does not claim universal bitwise equality (signed
zero and platform libm behavior remain distinct). Earlier direct-polar table
measurements retain their separate source identity.

The separate bounded-interpolation policy admits a paired private-template cache for at least three close-clock
lanes. It evaluates the original shaped 0/1 pair once on the nominal projection
grid, including a genuine finite-pulse sample beyond each end. Two complex float
values per grid position are then interpolated before the original per-lane
frequency rotation. Eligibility requires coherent shaped acquisition, at least
two original samples per bin and no more than half a fine chip per bin, and
at most **0.001 original input sample** of clock displacement at the last bin.
Chip, symbol and bin durations must be even sample counts: bin centers then
remain at least half a sample from a finite-pulse cutoff. Other parity geometries
use original construction, including potentially aligned odd partial chips.
This is an explicit, independently gated approximation, not an INT8 certificate
or a change to private-pattern generation. Energies are computed from the actual
interpolated operands; I/Q covariance and final scoring remain wide.

The cache identity includes all pattern parameters and keys, outer construction,
epoch, symbol, canonical phase and grid. It is destroyed before advancing that
cohort; independent bit candidates and new bit positions never reuse another
pattern. Clock interpolation is allowed only inside its checked displacement
bound; frequency rotation is applied afterward. Unsupported geometry, fewer than
three lanes or insufficient spare workspace retain original construction. The
FP64 diagnostic, tracking, outer-presence guard and range-error retry use original
operands. Cancellation destroys and cleanses the private cache. Its nominal
primary allocation is about 2.05 MB, charged together with native FFT scratch.

Large serial private whole-FFT banks can stream regenerated rows to make room
for native arrays/plans, even without a qualified arrival prior. Small persistent
caches, parallel banks and the explicit FP64 control retain their existing choice.
No arrival cell, epoch, frequency or clock lane is removed; scheduling and full
observation requirements are unchanged. The measured primary crossover uses
N=262144. The implementation starts at N=65536; the intermediate-size crossover
is still an extrapolation, not separately qualified timing evidence.

INT8 is a conservative rejection screen. Each complex tile uses a common I/Q
scale, zero point 0 and symmetric operands [−127, 127]. A fixed second INT8 plane
represents the first plane's residual; three integer products approximate the
complex dot. This is fixed compensated INT8 arithmetic, not automatic precision
selection. Exact integer chunks of at most 16,384 complex values accumulate into
INT32, then checked INT64 totals. Even explicit −128 operands are supported:
each complex component contributes at most 32768, so a complete chunk is bounded
by 536,870,912. Scale reconstruction and evidence calculations remain FP64.

Outward residual-norm and floating-roundoff bounds enclose the original ascending
FP64 dot. A pair is discarded only when *both* score upper bounds are strictly
below the unchanged retention floor. Otherwise both original FP64 candidate
scores are recomputed, including the losing-bit score needed by admission.
Template energy, transformed observation energy/covariance, trial charges and
thresholds are unchanged. This avoids treating quantization error or shaped
samples as independent white noise. Unsupported floating environments (including
flush-to-zero) produce inconclusive certificates and exact verification. The
certificate relies on IEEE arithmetic without fast-math and a conservative
64-epsilon allowance for the platform evidence function; C++ does not itself
specify a universal libm accuracy bound. Cross-platform validation remains open.

The input planes are packed once per immutable bounded batch. Their lifetime
ends before the observation buffer is reused. Template planes are replaced for
every exact key/epoch/bit pair, symbol, canonical phase, clock and frequency job;
no private sequence is shared across a changed address. Shifted rows refer to
the same packed input rather than storing duplicated PCM matrices. Reductions
split at both input and template tile boundaries. Cancellation discards local
work before publication. The cache is admitted only after workspace preflight,
released before retained payload growth, and does not reduce the existing bit
retention limit. Insufficient workspace uses the existing smaller direct path.
The integer ISA dispatch is local to the kernel: AVX2, SSE2 or portable scalar,
without global VNNI, DOTPROD or I8MM requirements. ARM execution is untested here.

Near-threshold tests and whole-receiver measurements are reported in the local
`MANUAL-CPU-ARITHMETIC.md` artifact under
`.agent-work/artifacts/receiver-opt-20261008/`. This stage preserves the preceding
optimized runnable GUI and raw diagnostic reference. It is an experimental
manual-testing checkpoint, not full sensitivity/platform qualification. In
particular, exact conditional retained-statistic agreement does not supply a
full acquisition-bank or cumulative raw-reference 0.1 dB sensitivity bound.

### Precision audit and strategy disposition

The precision boundary is a numeric kernel interface (`search_fft.hpp` for typed
float/double arrays and public plans; `search_arithmetic.hpp` for integer operand
packing and exact wide reductions). It does not include waveform generation,
cryptographic addressing or admission state. Public IDs describe policy rather than a hardware format; legacy IDs migrate as
described above. Internal integer diagnostic enums remain stable.
FP8 is not INT8. No FP8 emulation, GPU kernel, VNNI, DOTPROD or I8MM requirement
was introduced.

| Stage under Default | Actual storage / multiplication / reduction |
| --- | --- |
| PCM input | float source; promoted for processing |
| Downconversion and disjoint projection bins | complex double; double sums, precise phase coordinates |
| Private templates and finite pulses | integer crypto/permutation; double coefficients/shaping/rotation |
| Compact pulse moments | double statistics; selected construction/coordinates long double |
| Supported bounded direct | FP32 products/reduction; FP64 energy and final evidence; no automatic INT8 |
| Supported native whole/partitioned FFT | native complex float; scalar/runtime AVX2 float butterflies/products |
| Unsupported/cached/parallel/generic FFT fallback | original complex double; FP64 butterflies/products |
| Energy, covariance/Gram, thresholds | double; no assumed independent shaped samples |
| Tracking, outer-presence guard, timing refinement | FP64 with precise coordinate/address state |

Default and FP32 minimum narrow supported acquisition planes, FFT products and
reductions, not the table above indiscriminately. Native transform counters
identify executed coverage. Both use FP64 for cached/parallel/generic FFT paths,
compact statistics and tracking. This remains a **partial receiver precision
implementation**. The earlier 2 dB working allowance and wide statistical bounds
were not observed losses: no substantial loss was measured, but full acquisition
bank sensitivity and universal negligible loss for interpolation remain unqualified.

The local audit tried the following substantial alternatives:

* **Implemented/measured:** compensated INT8 direct screening and SIMD dots;
  native float whole/partition FFTs; tabulated scalar/AVX2 float and double
  butterflies; close-clock FP32 private-template reuse and bounded native row
  streaming. Whole-receiver results and uncertainty are in the manual report.
* **Prototyped, not promoted:** INT8 time-domain input/template blocks followed by
  FP32 FFTs. It adds packing and nonlinear quantization while retaining FP32
  transforms; the primary forced-whole pilot was slower than native FP32. It has
  no conservative FFT certificate or qualified covariance/sensitivity model.
* **Prototyped/rejected:** block-scaled Q15 FFTs, also with INT8 input promotion,
  explicit exponents, INT32 products and bounded rescaling. Across 256–262144
  points the tested AVX2 pipeline took about 3.8–5.0 times native FP32, with larger
  errors. Packing, rescale scans, products, inverse and bounds are included.
  This rejects that implementation, not every possible integer transform.
* **Prototyped/rejected:** a second fixed-Q15 forward / FP32 product-and-inverse
  hybrid removes the repeated scans and rescale passes. Runtime AVX2 uses packed
  integer products; every forward stage scales by one half, with ties-to-even
  rounding. Across N256–262144 its paired median pipeline cost is 1.38–1.76 times
  native FP32 (all 50 fixture medians slower). At N262144 it erases impulse,
  partial, finite-tail and cancellation inputs. Q8 promotion does not help.
  These results reject this scaling/kernel, not all integer FFTs. Fully 16-bit
  multiply-high/averaging butterflies and other scaling schedules remain open.
* **Prototyped; integration outstanding:** native FP32 paired finite-pulse
  contraction. On DSSS1000 v2, complete template construction improved 1.029×
  [1.006,1.062] without a cache, or 1.102× [1.072,1.138] with a bounded 1 KiB
  converted-chip cache. Much of the latter gain is valid reuse, not precision.
  Given the preceding measured 38.1% template fraction, the conditional total
  ceilings at those measured rates are only 1.011× and 1.037×; these are
  projections, not receiver speedups. An exact FP64 reuse cache is unimplemented.
* **Bounded out on this host:** native FP8 arithmetic is unavailable. Converting
  FP8 operands to supported float operations cannot increase float FFT arithmetic
  throughput and adds packing; it could reduce operand traffic, but no such
  bandwidth benefit is established here. GPU/native FP8 work remains deferred.
* **Explicitly outstanding:** a useful certified integer FFT/spectral screen,
  low-precision compact moments/tracking/Gram fits, cached/parallel native float
  whole FFTs, and ARM execution. Gram rank tests reaching 1e-12 and cancellation
  in prefix/moment reductions preclude blindly replacing those values by FP32.

For the preceding primary profile, perfect acceleration of the 3.28% integer
reduction/certificate portion could improve total time at most 1.034×. Halving
its 29.4% partition-scoring portion gives at most 1.172×; halving the 38.1%
private-preparation portion gives at most 1.235×. These Amdahl bounds refer to
that recorded profile and are not additive. Later FFT improvements alter the
fractions. Three timing repetitions and conditional score agreement do not
establish full-bank or cumulative raw-reference loss below 0.1 dB.

## Hardware AES and whole-receiver cost

Private pattern and DSSS streams use OpenSSL AES-256-CTR. OpenSSL selects hardware
AES through CPU capability dispatch when available; this local Ryzen 5 PRO 5650U /
OpenSSL 3.5.7 build has AES-NI and VAES enabled. The application already receives
that acceleration. See [OpenSSL capability dispatch](https://docs.openssl.org/3.5/man3/OPENSSL_ia32cap/).

On 9 October, paired frozen-receiver runs selectively disabled only AES-NI/VAES,
using identical original PCM, synthetic keys and all the same search parameters.
Each case has three ABBA blocks, twelve fresh processes, CPU-0 affinity and a
30-second limit per process; no runs timed out. Other cooperative CPU work was
stopped, but desktop activity/governor were not disabled. These are one-key/epoch
qualified banks with five oscillator/clock pairs and the actual seven-second
start allowance, not the entire rolling Live bank.

| Case | Accelerated mean CPU | AES disabled mean CPU | Disabled / accelerated CPU, descriptive 95% interval |
| --- | ---: | ---: | ---: |
| Rate 10, DSSS 1000, 40 ksample/s, one bit + absence in 60 s PCM | 10.1864 s | 10.7762 s | 1.0574 [0.9935, 1.1255] |
| Rate 1200, DSSS 10, 48 ksample/s, 16 bits + absence in 12 s PCM | 0.30970 s | 0.31390 s | 1.0135 [0.9821, 1.0459] |

The intervals use three log block-mean ratios and Student-t with two degrees of
freedom. Both include one: these captures do not resolve a small total-receiver
benefit, and do not prove zero effect. The 512-byte `Crypto::stream` refill microbench
has a 1.259× ratio [1.216, 1.303]; at 64 KiB it is 9.414× [7.616, 11.636]. Each
includes per-call HKDF/EVP setup. Bulk AES speed is therefore not a receiver speedup
prediction. FFTs, template construction, pulse processing and searches still cost
CPU. Dispatch is supported by the controlled capability test and throughput, not
an instruction trace.

All paired runs preserve accepted-bit events, selected aggregate candidate scores,
work counters, exact PCM identity and physical-completion assertions. Total receiver
timing includes construction, push, polls/finish/drain and destruction; generation,
loading and initial configuration/OpenSSL startup are excluded. The latest case
retains 35,863,482 bytes inside its diagnostic 64 MiB workspace, with approximately
55 MB process RSS including PCM. Its nonempty event arrives 13.283 seconds of media
after that one bit's end; offline computation time is not hardware acquisition
latency. No new frontend, waveform, receiver optimization or detection-sensitivity
claim follows from this experiment.

The complete local report and raw paired results are retained in
`.agent-work/artifacts/receiver-aes-benchmark-20261009/report.md`, `summary7.json`
and evidence manifest SHA256
`5c04b72cd80f1b5de0ae7e0590aa157fd1218445cea306acaefc3bb5b13d4671`.
Frozen source is `1e016a0dffe033c36fc71afad5241d16dc8957e5`, library SHA256
`66b59da08a5a8444a54bada79a79358bb3b345bb30895702ca6eb1580685ab97`.
Earlier six-second diagnostic results remain separate. Full Live throughput,
weak-signal/detection curves and overall optimization qualification remain open.

The later user-approved experimental tradeoff permits less than a few dB of
measured sensitivity loss when it buys a substantial CPU ceiling increase. The
earlier investigation used about 2 dB as an experimental working target. That
allowance and the statistical uncertainty are not observed sensitivity loss: no
substantial loss was measured. The simplified policy introduces no new relaxed
accuracy budget, and preserves false-acceptance, hypothesis coverage and physical-end
requirements. Earlier 0.1 dB experiments and their uncertainty retain their
historical scope. Close-clock cross-lane template reuse is now implemented in
experimental FP32 and measured at this checkpoint; full-bank sensitivity and
raw-reference cumulative loss remain unqualified.


### Application arithmetic dispatch and estimate calibration follow-up

The application previously passed automatic worker count0, which bypassed the
serial-only native FP32 acquisition path used by the precision benchmarks. This
was a real dispatch limitation, not a reason to price that fallback as FP32.
Automatic Default/FP32-minimum now selects one worker for large private coherent
FFT geometry: N>=65536, more than256 symbol samples, multiple initial stream
alternatives, no exact raw-sample fit, no quarter/differential branch. Explicit
worker counts and FP64-force retain their original scheduling. The automatic scheduling change requires native scratch to fit the workspace;
runtime range, cache and mixed-cohort fallback checks still apply. Every hypothesis is retained.

Final validation used72 fresh-process executions, three alternating paired runs
per geometry, with identical original PCM, synthetic keys, oscillator banks and
input rates. The primary60s capture contains one accepted bit and observed
absence: Rate10/DSSS1000 Interleave, Fs40000,64MiB, one key/epoch, five qualified
frequency/clock pairs (three without a prior). Construction, setup, push, polls,
finish and destruction are included; common fixture preparation/loading is
excluded. CPUs2–5 were isolated from other cooperating tests/builds; desktop
activity and the governor were uncontrolled.

| Arrival prior | Frozen automatic CPU → new automatic CPU | Paired speedup [descriptive95%] |
| --- | --- | --- |
|1ms|2.0874 →1.5265s|1.367× [1.312,1.426]|
|6s|7.3807 →2.9433s|2.506× [2.250,2.792]|
|None qualified|8.3646 →4.6432s|1.803× [1.572,2.068]|

Intervals are Student-t on three paired log ratios. Broad6s wall time changes
4.538→2.945s; an all12-core-affinity check gives2.099× CPU [2.040,2.160] and
4.140→3.098s wall. New Default and FP32 minimum agree in recorded execution
fields and CPU ratio1.003 [0.995,1.010]. FP64 force remains the independent
original-template reference; broad6s CPU is7.258s versus Default2.943s, a
2.466× difference [2.400,2.534]. No hypotheses, trial charges, accepted bits,
physical completion or media publication identities changed.

DSSS10/100 legacy diagnostics and ordinary fast/wide/weak controls have no
statistically resolved regression in these runs. They do not establish
Interleave10/100 native arithmetic speedups. Peak RSS rises53.40→57.44MiB
in the tight primary and falls77.28→61.34MiB in the broad primary. Retained
workspace grows by2MiB in the broad case, within64MiB. Media first-bit publication
remains13.7728s tight and26.2144s broad: there is no added media delay. These are
single-bank measurements, not full rolling Live, multi-key or short-text
qualification. Native operator/cache sensitivity evidence is unchanged and
still conditional/inconclusive; no negligible-loss bound is newly established.

The arithmetic cost model separately prices production generic/native transforms,
plan setup and whole/paired original versus eligible cached template creation.
Rolling-epoch, outer-code guard and unknown-anchor allowances remain conservative
and uncalibrated; they can dominate and dilute the total predicted precision
ratio. A fixed-bank speedup must not be applied as a multiplier to that broader
workload. GPU times remain hypothetical host/offload projections.

Manual Doppler changes the effective physical carrier to `r*Carrier` before
unchanged Shift is subtracted. Frequency/clock uncertainty follows that adjusted
carrier and the resolved illustrative hopping plan. It does not reduce unknown
residual oscillator/Doppler uncertainty or the admitted timing set. See the
[calculator definition](modem.md#manual-doppler-calculator).
