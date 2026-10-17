# Receiver strategy and CPU budget (local experimental candidate)

Receiver strategy is independent of Search arithmetic. The developer controls
select Automatic, Existing only, Complex only or Amplitude only, with an editable
processing budget (default 0.6 elapsed processing seconds per audio second).
The corresponding CLI options are `--receiver-strategy auto|existing|complex|amplitude`
and `--receiver-cpu-budget NUMBER`. Older settings use Automatic. These controls
change receiver computation only; they must not change the target, waveform,
source bits, communication keys or source addressing to make a decoder eligible.
Coverage metadata travels with the exact finite frequency/clock bank, including
UTC steering. Endpoint extrema alone do not certify missing joint alternatives.
The model retains local passband exclusions and expanded nominal/coupled rows;
amplitude bounds include both declared uncertainty and all supplied endpoints.
Probability availability is tracked separately for each selected backend in the
[receiver probability inventory](frequency-axis-probability.md). Transform
equivalence alone does not qualify acquisition sensitivity.
Unsupported forced requests remain visible as unsupported.

Automatic retains phase evidence when possible. An eligible frequency-axis
transform is an equivalent way to compute the existing complex statistic, and
may be selected below the budget. Amplitude-only is an experimental fallback
when complete complex coverage is unavailable or estimated complex work exceeds
the budget and supported amplitude work is cheaper. The budget is a selection
objective, not a hard runtime guarantee. Existing only disables both new paths;
Complex only prohibits amplitude decoding. For the preceding coherent diagnostic
reference, combine `--receiver-strategy existing --search-arithmetic fp64`;
FP64 force alone changes arithmetic and still permits Automatic decoder selection. Unsupported geometry retains the
existing complex path unless Amplitude only was explicitly requested.

## Exact complex frequency-axis acquisition and continuation

For each exact clock, key, epoch, bit position and canonical phase, construct the
original two independent shaped rows. At every admitted timing position, a
chirp-z transform computes the complex dots at the original uniform carrier
lattice. This changes the axis of the transform, not the hypotheses. I/Q,
coherent and four-quarter evidence, strongest-quarter rejection, original
energies, noise normalization and trial charges are retained. Rounded lattice
coordinates are bounded and decisions near a threshold or bit tie are refined
with the original FP64 calculation. This path currently uses FP64 bulk arithmetic
at every Search arithmetic setting; FP64-force retains original templates.

The same operator also evaluates the large carrier bank during continuation.
It excludes observations already scored for the preceding symbol without moving
source coordinates or quarter boundaries. Original local timing fits remain,
and strongest-carrier near ties are evaluated again with original direct
arithmetic. Cumulative carrier scores can retain small transform rounding; this
is statistic agreement, not a promise of bit-identical floating-point scores.

Qualified long-symbol timing windows retain their original coverage cells,
including fractional starts, clock bounds and pulse support. Disjoint selected
ranges have separate packed outputs; no shifted-PCM matrix is built. Scratch
and scores are bounded by the configured workspace. Cohorts outside the measured
crossover or workspace domain retain the original timing-axis calculation;
work counters report these broad fallbacks separately. They are remaining work,
not a claimed timing-position reduction.

Current eligibility requires shaped DSSS-Off patterns, four-section scoring,
no differential branch or real-sample Gram fit, at most three exact clock rows,
and at most 65,536 projected symbol bins. Frequency hypotheses still come from
the existing bounded policy. Thus this path accelerates the fully covered
256-second EME bank; it does not alone expand the capped very-long-symbol bank.
DSSS factorization is a plausible extension, not implemented coverage.

## Scheduled short-symbol complex reception

When Sched_Hop defers a short symbol into a complete full-gain slot, Automatic,
Existing only and Complex only can use the existing complex FFT statistic with
exact keyed calendar starts. This is distinct from long split-symbol mapped
complex or amplitude reception. Actual singleton phase starts, initial symbol alternatives and all
declared frequency/clock hypotheses remain. Continuation covers rate displacement
through physical null gaps from the last accepted anchor, with full trial charges.
Waiting and null time cannot create absent symbols or early physical completion.

Small/unsupported native FFT geometries retain FP64 bulk execution even under
Default and FP32 minimum. Eligible large native paths retain their existing FP32
dispatch and numerical fallbacks. FP64 force uses original templates, bypasses
approximate interpolation and permits exact caching. Actual operands/backend
are recorded by `--verbose`; the amplitude precision table below describes the
amplitude path only.

Packed scheduled estimates count exact representative calendar phases but retain
a full-input complete-cohort engineering allowance. They have no calibrated
whole-bank probability, GPU estimate or guaranteed real-time CPU ceiling. Raw
capture is retained because this path has no complete omission certificate.
Unsupported full coverage and workspace limits remain explicit. See
[scheduled hops](scheduled-hops.md) for split eligibility, diagnostics and the
separate manual-checkpoint limitations.

## Scheduled split-symbol complex reception

Long scheduled symbols can use streaming physical-I/Q direct contraction when
the complete bank and state fit. Automatic retains this complex evidence when
supported; Existing only and Complex only permit it too. Amplitude is a separate
fallback for unsupported complete complex geometry/state, and can still be
forced explicitly. A CPU-budget warning is not permission to omit alternatives.

The source map pauses coefficient/pulse position during nulls, while physical
carrier phase continues. Required frequency spacing follows the full physical
aperture, not compressed active time. The shared builder retains original exact
frequency/clock pairs before adding physical-aperture refinement. It verifies
joint oscillator/UTC provenance, passband, finite bank and workspace limits;
explicit incomplete diagnostic banks cannot advertise complete coverage.

The mapped scorer keeps raw white-noise energy and sample degrees together.
A first prototype copied a four-dimensions-per-chip count cap without projecting
its raw-noise denominator; that was invalid and is retained only as diagnostic
evidence. The implemented coherent path uses the ordinary two-real-basis score.
Only the existing nested chip-subspace excess test can lower it to projected-chip
evidence. This conditional correction never increases white-noise evidence.
It is a scheduled detector guard, not a claim of exact FFT-score identity.

Four-quarter evidence uses the corresponding raw/projected calculation only when
all quarters have rank-two Grams and chip partitions respect quarter boundaries.
The full quarter projection supplies the lack-of-fit test; the strongest quarter
is still removed from admission evidence. Singular or unaligned quarter geometry
retains the original conservative raw-reference quarter fallback, counted in
work diagnostics. Differential whitening and its separate guard remain intact.
The excess test's analytical activation bound assumes the documented correct
signal/mismatch and white-noise model; it is not a universal wrong-key bound.
Full acquisition sensitivity, interference and phase-diffusion behavior still
need paired qualification.

Mapped samples retain limited I/Q template construction, real-noise Gram covariance,
usable energy and source-quarter/local-window guards. Nulls are excluded from
both template energy and noise degrees. Independent adjacent-symbol carry state
preserves samples that arrive before the previous cell's completion fence; more
than one additional symbol explicitly exhausts supported coverage. Timing budgets
do not restart at bursts, symbols or rolling admissions, and observed absence
still requires fully scored source symbols totaling six seconds.

Dense mapped and original-refinement physical projections, I/Q products, real
Gram terms, covariance and scoring retain FP64 under all public arithmetic
choices. The separate bounded-acquisition path uses
[native timing FFTs](scheduled-hops.md#october-14-bounded-acquisition-manual-checkpoint)
where eligible. Default and FP32 minimum may use
bounded 512-sample carrier moments and an eight-row source-chip curve cache for
eligible long fine chips. Both candidate rows remain independent. Curve identity
uses resolved physical epoch/ordinal/local chip and matched-FHSS phase, under the
immutable parent key/geometry/calendar. Leases prevent in-use eviction. Physical
gates and limiting remain after lookup; support-change knots and partial-chip
boundaries use direct fallbacks. Reordered finite-table interpolation is an
approximation; it is not an original-template reference. FP64 force bypasses it,
while retaining exact worker/chip/calendar reuse.

Native FP32 curve storage/interpolation was implemented and inspected, but its
complete local pair used 5.1% more CPU for 1.78 MB less DSP memory. It remains an
internal override; no narrower automatic path is selected solely for its type.
Fine-frequency fusion for this dense mapped representation remains a kernel
prototype, not an integrated gain; bounded acquisition has separate family
execution and retained original fallbacks.
The later shared candidate adds exact-seed public moment reuse, leased L2 private
source curves and complete DSSS permutation snapshots; these do not change
arithmetic policy. Bounded common-family sharing is now enabled with an eight-phase
cap and at most sixteen total lanes per job. Source/address/ownership proofs and
same-frequency pure Fit reuse preserve each lane's independent detector state;
unsupported pieces retain original execution. Conditional one-bank ABBA results
and one preliminary complete Live observation are documented in the
[shared-execution diagnosis](scheduled-hops.md#shared-execution-measured-diagnosis-2026-10-13).
Latest combined paired performance remains pending; full-bank sensitivity and
native FP32/INT8 production selection do not follow from those observations. Exact
block-local gated-template reuse remains allowed at every precision when its
full source/rate/phase/index identity repeats. No enormous shifted-PCM matrix or
whole-symbol buffer is introduced.

Key-dependent mixing may be skipped only after a whole-null proof for every
retained timing/phase/clock lane. Raw capture remains retained without a complete
mapped-complex omission certificate. Cached physical END/first-full coordinates
and null deadlines preserve ordinary scoring at completion fences. Silence never
adds observed absence. Dirty publication state preserves next-poll bit delivery.
Live retains optional scratch across ordinary refreshes, but reclaims it before
required admissions or deferred detector reservations could be excluded.

The bounded FIFO permits processing across multiple active/null cycles. Strategy
selection uses sustainable mean work; the stricter next-hop cost remains visible
as a diagnostic. A full 348-physical-second accelerated predecessor fixture certified 42 later
active-hop starts with prior work queued; the rebuilt-source confirmation certified
21 and peaked at 7.424 seconds of backlog. Both drained all 87 groups within the
declared 16-second queue bound. An earlier conservative ledger indicated 11
crossings; it did not provide the same optimistic-frontier certificate.
See [scheduled hops](scheduled-hops.md#split-symbol-efficiency-follow-up-2026-10-13)
for exact performance, memory, approximation and sensitivity limits.

Cost and workspace allowances are provisional engineering estimates. Full-bank
sensitivity, high multipliers, broad oscillator banks and
hardware execution remain unqualified; scheduled probability/GPU estimates must
stay unavailable. A bounded complete Live 16-source-second split-symbol run is
recorded in [scheduled hops](scheduled-hops.md); it does not qualify arbitrary
rolling cohorts or hardware timing. See that document for the restriction
matrix and diagnostic events. No transmitted waveform or private address changes
are required by this receiver path.

Supported split-complex scheduling currently retains complex evidence even above
the advisory CPU budget. Switching to a different statistic needs a separately
qualified complete-execution/sensitivity crossover; scheduling alone does not
authorize amplitude substitution. Explicit amplitude-only selection remains
available where its own geometry is supported.

## Amplitude-only acquisition

The [amplitude probability work](amplitude-probability.md) provides an explicitly
unqualified matched-envelope planning reference and records a repaired continuous
Live public-arrival gap. Public amplitude windows now advance with capture time;
continuous FFT retains its singleton. The planner prices amplitude's longer
rolling cohort separately. Overlapping amplitude passband centers share a
presentation row, while resolved coherent-frequency rules remain intact.
Standalone high-power capture qualification does not establish full continuous
Live success probability.

This is a receiver of the existing varying envelope, not a new OOK transmitter.
It supports independently keyed private rows as well as the public alphabet.
Nonoverlapping real-input FFT blocks provide positive-frequency complex-bin
energies. All declared carrier uncertainty is covered, including uncertainty
outside the capped complex bank. Overlapping coarse frequency bands may reduce
unnecessary noise integration; their grid is set by the short FFT geometry,
not inverse full-symbol duration. The same transformed block is shared across
bands and, where the live workspace allows, across keyed epoch banks.

Deterministic quadrature estimates of the original shaped, radially limited
candidate envelope (at least eight nodes per chip) weight those observations.
These are not exact integrals of retained FFT-bin signal energy; the sensitivity
approximation remains separate from the noise bound for deterministic weights.
An unknown-noise-scale Chernoff bound supplies evidence; overlapping channels are
charged through a union bound. Fits omit incomplete observations and protect
physical quarters. The full white-Gaussian-noise certificate is retained. A
separate centered envelope-correlation guard caps its nominal evidence count at
one profile observation per original chip; independence of shaped profile
observations is not proved. Its minimum with the noise certificate
cannot increase the latter's false-admission bound. This guard is empirical
protection against unrelated structured signals, not a proved wrong-key bound.
Earlier blanket score tempering was rejected after it cost roughly 8–12 dB
in bounded actual-decoder experiments and prevented short supported patterns
from being admitted even at high signal power.

Shared input statistics are keyed by absolute sample coordinate, FFT geometry
and admitted bin interval. They contain no secret template. Template/moment
state remains separate for each key, epoch, symbol, bit, start, phase and clock.
Caches have fixed workspace bounds and recompute on exhaustion. Spectral rows
are immutable within a push and include the arithmetic policy in their identity.
The identical PCM determines any finite-range precision fallback; even an
automatic row that fell back to double cannot satisfy FP64 force. Range checks
run on cache misses only, rather than rescanning the same PCM in each bank.
On a cache miss, each bank retains its public FFT plan and native scratch across
pushes, within the preceding complete scratch reservation. A partial block does
not construct a plan. Switching precision releases the other arena first.
Canonical phase groups are cached by symbol index and both phase bounds;
completion or narrowed bounds invalidate that entry. No secret patterns or
already-consumed addresses are reused between bit positions.

Default and FP32 minimum use native FP32 spectral FFTs and packed FP32 envelope
contractions. FP64 force uses native FP64 FFTs and original FP64 coefficients
and contractions. Tiny/large finite PCM ranges fall back to the original FP64
**transform**; this does not change the bounded envelope arithmetic. I/Q powers
are computed in FP64, their prefixes and statistical moments in long double.
The original pulse coordinates/table and independent coefficient generation
remain FP64. Default/minimum pack each local coefficient neighborhood once into
four float lanes (I/Q for each independent bit), multiply and accumulate pulse
taps in FP32, then use FP64 for limiting, node energy and quadrature summation.
SSE2 uses explicit packed multiply/add without FMA on this machine; the portable
fallback has the same four-lane arithmetic. FP64 force bypasses this conversion.
Exact reuse is allowed at every setting; no approximate envelope interpolation
is used. All settings retain the same finite-quadrature approximation and guards.

Observed energy moments are shared between the two bit alternatives; template
moments are shared between frequency channels. Weighted cross-products remain
separate. Whole and four-quarter accumulators retain their original boundaries.
This factorization requires constant Fourier degrees and independent-observation
allowance within each clock/channel fit, as guaranteed by the current geometry.
Original per-bit/per-channel moments remain in the test reference. The reduction
changes floating-point association slightly, not the mathematical statistic.

Smaller moment storage must not silently add coarse frequency bands. Production
uses the preceding unfactored planner allowance to choose its band count and
passes that cap into the correlator; actual workspace accounting uses the smaller
factored arrays. Band widths, Fourier degrees and trial charges are therefore
unchanged in the paired production fixtures, including a constrained-workspace
regression. Direct low-level construction retains the old moment-capacity rule
with current fixed metadata; exact historical byte-edge eligibility is not an
ABI guarantee. An actual allocation failure is explicit, never an excuse to
omit frequency coverage or lower the statistical guard.

The amplitude publication queue is revisited when a fully scored symbol changes
its evidence, timing remains unsettled, or queued/committed output still needs
draining. Polls with only partial FFT input retain the empty allocation and skip
the unchanged hypothesis scan. Every accepted bit is still published on the next
poll; a full queue keeps subsequent drains active without requiring more input.
This exact bookkeeping optimization is independent of arithmetic precision.
For a clean input push too short to complete the next spectral FFT, allocation
preflight also reuses the proven zero-growth bound: only the reserved PCM array
changes. Equality at an FFT boundary, pending output and unsettled timing retain
the original full per-hypothesis forecast. No workspace bound is relaxed.

| Stage | Default / FP32 minimum | FP64 force |
| --- | --- | --- |
| Original PCM | FP32 input, unchanged | Same original PCM |
| Spectral FFT storage, products and sums | Native FP32; FP64 finite-range fallback | Native FP64 |
| I/Q bin energy | FP64 products; long-double prefix sums | Same |
| Private coefficients, pulse table/coordinates | Original FP64 construction | Original FP64 construction |
| Local shaped-envelope dots | Packed FP32 operands/products/sums | Original FP64 operands/products/sums |
| Radial limiting, node power, quadrature | FP64 | FP64 |
| Whole/quarter moments, final evidence | Long double with original guards | Same |
| Absolute time, epochs, sample coordinates | Original integer/wide precision | Same |

The FFT range fallback and exact caches do not refine a detection decision or
prune alternatives. There is no INT8 screen, residual plane or hidden FP64
correlation refinement in this amplitude path. The inexpensive policy/control
calculations need not use the same precision as bulk correlation.

Ordinary unscheduled amplitude-only currently requires shaped DSSS-Off, no FHSS masking, at least
64 chips and 16-second symbols, and a positive-frequency admitted band with
FFT transition margins. A finite arrival interval and a complete clock grid of
at most 65 lanes within +/-10,000 ppm are also required. It does not discard
time/clock/epoch alternatives. Scheduled split symbols have separate flat-window
geometry and support Interleave DSSS and applicable FHSS masks, as documented in
[scheduled hops](scheduled-hops.md); the retained-observation guard remains.
Long private messages still pay for independently keyed rolling epochs and
for their template construction. Cohort estimates include prefix/pulse-support
retirement time and reserve a per-bank share of the total workspace. Shared
frontend work can reduce cost, but it does not remove independently keyed state. Longer integration can help square-law
evidence, but does not guarantee practical encrypted EME at an arbitrary link
budget. Its RX probability remains unavailable until the relevant complete
bank has been calibrated; the existing complex probability is not reused.

## Compatibility and limits

All decoder paths use the existing pending/accepted-bit interface. Completion still
requires fully scored absent symbols covering at least six seconds. EOF,
flushing and cancellation provide no absent-symbol evidence. No amplitude-only
source interpretation or successful decryption can end a physical stream.

Encrypted production PCM comparisons against the preceding frozen manual
candidate cover DSSS Off/10/100/1000, ordinary cover, fractional source phase and
varying output block sizes. All four complete generated captures are byte
identical. This is transmitter compatibility evidence, not receiver sensitivity
qualification.

The preceding [EME investigation](eme-amplitude-receiver.md) records prototype
and baseline results. Its statements that the new statistics were not yet
production paths describe that earlier snapshot. The current candidate adds
the experimental paths above; neither prototype sensitivity nor kernel timing
can stand in for full-bank receiver qualification. No GPU implementation is
provided. Alternative GPU estimates are unavailable rather than extrapolated
from CPU arithmetic.

This is a local manual-test checkpoint. Full acquisition-bank sensitivity,
long encrypted epoch cohorts, structured interference/rare false admissions,
ARM execution, hardware timing and broader platform/SDK qualification remain
separate requirements before overall qualification.

## Bounded complete-receiver measurements

The local paired driver uses the preceding optimized library, not an original
raw receiver. Both get identical original real PCM, synthetic private keys,
noise seeds, input sample rates and complete carrier hypotheses. One worker is
pinned to the same CPU, with no concurrent agent measurement/build. Transmit
construction is outside receive timing. CPU/wall include construction, input
processing, acquisition, continuation, allocation and next-poll publication.

A 512-sample/s fixture has 64 private chips, 32-second symbols, 1,025 frequencies,
two payload bits, and a complete absent symbol. Three serial paired repeats
for each total arrival-window width gave:

| Arrival width | Preceding CPU, median | New CPU, median | Paired speedup range | New CPU through first bit | Scored acquisition cells |
| --- | ---: | ---: | ---: | ---: | ---: |
| 6 s | 2.133 s | .0978 s | 20.9–25.1x | .0935 s | 103,525 |
| 1 s | 2.191 s | .0286 s | 69.6–87.6x | .0235 s | 21,525 |
| 100 ms | 2.109 s | .0126 s | 146–171x | .00784 s | 7,175 |
| 1 ms | 2.318 s | .0120 s | 176–224x | .00692 s | 5,125 |

These ranges describe execution variability, not population confidence limits.
Both use Default arithmetic. A final-build confirmation after the policy/coverage
and Config-layout repairs measured 2.035249/.100924 CPU seconds at 6 s (20.17x)
and 2.064615/.010547 seconds at 1 ms (195.75x). Bits, trial charges, scored cells,
publication coordinates and physical completion matched the earlier evidence;
retained state was 669,986 bytes. These are additional single pairs, not confidence
intervals. The preceding receiver scored 345,425 acquisition cells in every case. Both retain 1,134,683 total threshold trials, both bits and
one physical completion. The new receiver executed acquisition and continuation
CZT without broad numerical fallbacks. Input spans 101 seconds; therefore these
single-bank timings are not the cost of all live epoch alternatives.

At the 317-sample poll cadence, first-bit publication moved from 49.53 seconds
of input to 39.63/37.15/36.53/36.53 seconds as the window narrowed. The payload
starts at four seconds and its first symbol ends at 36 seconds. Second-bit
publication was unchanged. The complete six-second arrival interval is inside
the captured PCM; no negative-start portion was silently excluded.
Retained state after a push was approximately .670 MB in both versions. The new
additional transient CZT score/scratch peak was 2.056/.744/.515/.482 MB for
those widths, included in the workspace bound. Process peak RSS was about
16–17 MB, including the fixture. The same pinned worker and isolated paired
conditions were used; wall time closely matched process CPU.

A separate actual 5.8-GHz EME fixture uses encrypted patterns, 6,000 samples/s,
128 inner chips, 256-second symbols and all 3,565 carrier/clock pairs. One bit
plus a fully observed absent symbol used .0901 CPU/.0901 wall seconds in the
new receiver, including .0533 seconds through first publication. It retained
7,447,289 threshold trials and scored 17,825 acquisition cells. This is one run,
not a repeated estimate or full rolling-bank measurement. The frozen receiver
failed the strict physical-end assertion for this four-second fractional-grid
arrival: it selected an earlier start and completed before the fixture's full
absent symbol. That failure is retained; it is not counted as a successful
paired EME speedup. The earlier one-second-arrival, pre-continuation diagnostic
remains in local artifacts and is superseded for current performance claims.

A complete mocked-audio Live probe additionally exercised independently keyed
rolling admissions and retirements. Its encrypted 64-second symbol used 512
samples/s, one-second inner chips, qualified GPS/audio timing, and 200 seconds
of input including ordinary cover and two absent symbols. The single-key
baseband run used 1.547 CPU seconds; the 5.8-GHz uncertainty/two-key run used
2.925 seconds (Automatic: 3.228 seconds). Both retained the single correct
pending row and completed only after absence. The two-key run admitted 412
banks, retired 254, peaked at 167 banks and 23.86 MB accounted workspace within
128 MiB; process RSS was 39.2 MB including the fixture. The first bit published
at 72.018 seconds of input, completion at 136 seconds. Wall time was 9.32 seconds
(Automatic 9.67), including intentional backpressure waits: this is a functional
and CPU-cost measurement, not a real-time hardware ceiling or weak-signal curve.
All received rows were checked, including the wrong-key bank.
The final-build Automatic confirmation used identical PCM and completed with
2.830 CPU/9.029 wall seconds, 23.837 MB peak accounted workspace and 40,016 KiB
process RSS. Admissions, retirements, pending/complete input positions and all
row assertions were unchanged. This remains a backpressured functional fixture. A distinct
128-second-symbol forced-complex Live test failed coverage after its wider UTC
clock bank selected the original fallback; its approximately 50.8-second partial
execution is recorded as a failure, not complete-reception performance. Automatic
amplitude fallback passing a different 64-second fixture does not qualify that
failed forced-complex geometry.

The complex work model uses a measured aggregate CZT-kernel fit and a rounded
continuation reserve using the same measured kernel fit, with original local
timing and winner-refinement work retained. Selection and
alternative finite/sustained estimates include that reserve; acquisition alone
is not compared with the total CPU budget. Other model terms and extrapolated
rolling cohorts remain engineering estimates, not a CPU reservation or a measured
hardware ceiling. The amplitude model charges per-channel state/moments and
conservatively charges frontend work per bank, even where Live shares it. GPU
cost for either alternative is unmodeled.

## Complex threshold-region check

An additional 80 paired captures use the same encrypted 64-chip/32-second,
1,025-frequency fixture at C/N0 2/3/4/5/6 dB-Hz, 16 synthetic keys/noise seeds
per level. Complete-two-bit successes were 7/9/12/16/16 for both frozen and new
receivers; every accepted bit sequence matched, including incomplete receptions.
The interpolated 90% point is 4.6 dB-Hz for both (observed difference 0 dB).
Resampling these identical paired outcomes alone yields a degenerate zero
interval; that does not establish zero population loss. There are only 16
independent seed trajectories, so unseen discordance and interpolation/systematic
uncertainty remain substantial. The 99% point and a cumulative sub-0.1 dB claim
are unqualified. This is one clock, one private epoch and a qualified 100-ms
arrival prior with a full carrier bank, not all live rolling cohorts.
Independent kernel tests cover frequency/clock and partial-observation boundaries;
prior 3,565-frequency kernel comparisons had maximum score difference 1.24e-11.

## Amplitude sensitivity and execution tradeoff

The measured scope is a bounded diagnostic, not a live acquisition bank:
512 private chips, 256-second symbols, 512 samples/s, nominal 4-Hz bandwidth,
one known arrival/clock/carrier/key/epoch, fractional .375-sample start, arbitrary
phase, two payload bits and fully observed absence. Each of 128 paired keys/noise
seeds supplies identical original PCM to the preceding optimized complex and
amplitude decoders. Power is measured from real payload PCM; one-sided
noise density defines C/N0.

The final geometry uses two FFT-bin margins for patterns of at least 256 chips,
with eight-bin margins retained for all supported 64–255-chip patterns. The
64/128-chip fixtures showed that the conservative statistic can otherwise fail
even at high signal power. In sampled finite-pulse
fixtures the two-bin guard lost at most .174 dB retained energy (.117 dB for
exact integer-start PCM); these sampled tails are not a worst-case filter bound.
The net detector result improved because less unrelated noise was integrated.

The interpolated 90% complete-two-bit points are -8.095 and +7.743 dB-Hz:
**15.838 dB additional required C/N0**, paired bootstrap 95% interval
**15.498–15.988 dB**. This is paired sampling uncertainty under fixed linear
interpolation of the sampled grid; it does not quantify interpolation-model,
full-bank or systematic uncertainty. There were no accepted wrong or extra bits in
that finite fixture. The 99% point remains unqualified: 128/128 successes still
has only about a 97.7% one-sided 95% lower confidence bound. This measured penalty includes conservative gates and block geometry as well as
discarded phase evidence; it is not an intrinsic lower bound for every possible
amplitude decoder. This is not a few-decibel path, and neither lower planner coefficients nor a longer nominal
symbol establish that an extreme link will work.

A preceding eight-bin-margin 4096-chip, 8192-second-symbol single-bank pilot used four keys and
complete two-bit-plus-absence captures. Complex succeeded at -20 dB-Hz; amplitude
failed at -5 and succeeded at 0 dB-Hz in all four cases. At the two levels where
both succeeded (eight pairs), mean receiver CPU was 1.472 versus .130 seconds;
geometric speedup 11.31x, bootstrap 95% interval 10.71–12.05x. This includes
setup, FFTs, template preparation, allocation and publication. Frontend/search
time was not separately instrumented. Reported receiver workspace was about
177 versus 90 kB; whole-fixture process peak RSS was 338 MB and includes the
finite PCM arrays. First publication was approximately 7.99 seconds later in
the amplitude path. Accepted bits still publish at the next poll.

These measurements used the actual production correlator in a bounded artifact
prototype with matching promoted geometry/statistic; integration regressions
check the built implementation. The raw-reference diagnostic was retained but
not run for these curves. Full oscillator/timing banks, rolling private cohorts,
phase diffusion, multipath/colored interference, cumulative raw-reference
sensitivity and rare structured false admissions remain unqualified. Sixteen
strong unrelated-key fixtures plus CW/multitone/noise regressions are finite
tests, not a rare-event rate.

For an 8192-second private symbol the complete live lifetime can retain more
than 8200 epoch alternatives per key/profile. Single-bank speedups do not prove
that cohort is practical. The conservative model charges frontend work per
bank unless shared execution has been established; workspace and serial
template/moment costs can still exceed the selected budget.


## Remaining computation opportunities

The frequency-axis chirp plans currently rebuild for each execution even when
public frequency/clock geometry repeats across different keys and epochs.
A bounded, workspace-accounted shared plan cache is not implemented. The
single-bank speedups above do not bound that omission in a live rolling cohort.
Mixed Live cohorts containing any ordinary FFT bank currently disable the shared
amplitude frontend cache for all banks. This conservative reservation restriction
and shared chirp plans are explicit remaining optimization opportunities.
Neither new path currently factors outer DSSS or FHSS gaps. Those geometries
retain the original decoder, or explicitly reject a forced amplitude request.
An automatic CPU budget is not permission to drop required hypotheses, and it
cannot guarantee real-time operation when every supported path exceeds it.


## Affordable fallback policy

Amplitude-only is a last resort for computation, not a requirement to spend
substantially more CPU for every marginal sensitivity gain. The configured
budget includes independently keyed rolling work. It must not silently enable a
large private-template spectral bank merely to improve a single-bank curve.
Known inexpensive opportunities (a tighter certified noise bound or a small fixed
scale-free variance-likelihood mixture) are unimplemented, unmeasured follow-ups;
first determine whether the current noise, profile or quarter guard limits the
actual decisions. These scoring changes cannot be assumed to remove the measured
15.8 dB penalty. More elaborate short independently phased matched-block decoding
is a distinct optional algorithm, not mandatory amplitude-only computation.
Neither the budget nor a lower target guarantees a feasible extreme link; the
remaining epoch count, workspace and sensitivity limits stay visible.


## Preceding receiver-strategy checkpoint validation

The final affected regular run passed 57/58 tests. The retained ALSA fractional
DSSS reconstruction assertion also fails on the frozen baseline: peak 1.00944,
63 clipped samples, distortion energy fraction 2.47619e-8 against 1e-8. No waveform
or assertion was changed to conceal it. GUI coverage passed all 38 tests, using
37 matching passes from the full group plus the complete planner test after its
legacy-probability fixtures explicitly selected Existing only. Automatic
amplitude coverage has separate assertions and does not borrow that probability.
Encrypted production TX digests for DSSS Off/10/100/1000 match the frozen baseline.
The multi-hour streaming memory assertions pass unchanged (Config 400 bytes,
PatternCode 5,424 bytes, transmitter 16,363 bytes on this build).

The application was rebuilt with the normal build.sh entry. Calibration, native
display/platform, sanitizer, SDK and packaging qualification were deliberately
deferred for the requested manual checkpoint. They are not passes. Exact source,
build identity, results and launcher are in the local receiver-strategy-20261012
artifact; this stage does not declare the overall work qualified.

## Amplitude efficiency follow-up (October 12, local)

The frozen preceding amplitude implementation and the new receiver were compared
on identical PCM at 512 samples/s, with native Release binaries pinned to one
CPU and cooperating builds stopped. A complete 200-second Live fixture includes
one private bit, all received streams, rolling key/epoch admission and a fully
observed absent symbol. There are two keys and 64-second symbols. Each execution
includes receiver setup, preprocessing, allocation, scoring and publication.

| Live timing policy | Preceding CPU median | New CPU median | Paired CPU speedup range | Admissions / peak banks |
| --- | ---: | ---: | ---: | ---: |
| Default six-second arrival interval | 5.804 s | 3.800 s | 1.480–1.637x | 408 / 264 |
| Qualified 105.039 ms interval | 2.415 s | 2.219 s | 0.998–1.176x | 412 / 167 |

Each row has three alternating-order pairs. These are execution ranges, not
population confidence intervals. The unusually small 17-sample capture callbacks
exercise per-block overhead. Median wall times are 12.312→9.684 seconds broad
and 8.667→8.604 seconds qualified; they include the fixture's intentional
backpressure waits and do not demonstrate a hardware real-time ceiling.
First accepted-bit publication is at input position 72.017578125 seconds and
physical completion at 136 seconds in both versions/policies. No extra bits or
stream completions were accepted. Peak aggregate workspace in the broad fixture
falls from 20,454,883 to 18,722,811 bytes within the same 128-MiB limit.

The numerical FFT/envelope improvements alone did **not** improve complete Live:
an intermediate six-second run regressed 6.373→9.743 CPU seconds despite equal
hypotheses, observations and FFT-sharing counts. The enclosing numerical work
was faster; the precise cause of that intermediate outside-kernel regression
was not isolated. The final exact partial-input allocation and publication
shortcuts remove redundant work and produce the paired total gains above. It
would be incorrect to infer these totals from single-bank kernel speedups.

The model explicitly identifies uncalibrated callback-cadence and epoch-lifecycle
cost. It must not promise that a predicted 0.6 ratio is a measured real-time
ceiling. The maintained probability limitation also remains: no full-bank
amplitude detection curve is substituted from coherent or conditional evidence.
Detailed bounded precision/sensitivity experiments and source/binary identities
are retained with the local manual-checkpoint report under
`.agent-work/artifacts/amplitude-opt-20261012/`.


The final paired 512-chip/256-second standalone fixture reduces candidate work
with arrival width: 6 s retains 127 hypotheses/194,052 observations, 1 s retains
27/41,256, and 100 ms or 1 ms retains 10/15,265. New complete CPU is respectively
0.38355, 0.08554, 0.03369 and 0.03353 seconds, versus 0.49690, 0.10429, 0.03806
and 0.03984 seconds in the preceding implementation. Endpoints use three pairs;
intermediate widths use one final pair. This is a single bank, not the complete
Live cohort. The remaining floor reflects coverage cells and clock alternatives.
No temporal FFT batch is introduced to enlarge tight banks.

Bounded precision validation compares identical PCM at 128 synthetic seeds and
five C/N0 levels. All 640 paired Default conditions agree in decisions, progress
and completion, with no wrong/extra bits. First-bit detection is 107/128 at
7 dB-Hz, 122/128 at 7.5 dB-Hz and 128/128 at 8 dB-Hz. Wilson 95% intervals are
76.2–89.0%, 90.2–97.8% and 97.1–100%; the last does not establish 99% population
detection. Sixteen additional seed sets at FP32 minimum and FP64 force agree.
Sixteen paired per-seed transition bisections yield identical brackets, bounding
the observed conditional required-C/N0 shift to ±0.00586 dB at that search
resolution. This is not a population confidence interval or a full-bank loss
bound. Zero disagreements in 128 seed-grid events gives a one-sided 95% upper
bound of 2.31% on that event, not a rare false-accept rate. Original analytical
trial/noise guards remain; this optimization does not change the detector score.

No substantial additional loss was measured in those conditional fixtures.
Full-bank 90%/99%, cumulative raw-reference sensitivity, ARM and actual hardware
remain unqualified. Existing failures are retained: the broad 128-chip/16-second
private fixture receives only the first bit of `01` in both versions; a clock-edge
impairment also misses the second bit in both; strong interference rejects both.
None is a successful full-reception claim. Neither the old amplitude penalty nor
the new conditional evidence is a universal performance guarantee.


The amplitude-efficiency candidate was rebuilt with `./build.sh` and completed
all 38 affected GUI tests. The bounded regular group again passed 57/58:
`alsa_audio_contract` retains the same preceding fractional DSSS clipping failure
(peak 1.00944, 63 clipped samples, energy fraction 2.47619e-8 versus 1e-8).
Its assertion is unchanged; the separate ALSA interleave test passes. Fresh
production TX digests for DSSS Off/10/100/1000 are byte-identical to the frozen
preceding candidate. The regular calibration case, sanitizer/native-display,
ARM, platform, SDK and packaging qualification remain deferred. This is a
local manual-testing checkpoint with a documented pre-existing failure, not
an overall qualified release.


For the shared scheduled complex follow-up, exact common-source and same-frequency
contributions improve complete identical-PCM CPU by 1.2608× in the measured
M10/G1, DSSS-Off fixture. Bounded work crosses later active hops and drains;
scheduling alone still does not force amplitude reception. The demanding DSSS100
split fixture has not completed its bounded drain, so practical headroom for that
case remains unsupported. Native FP32/SIMD diagnostics are not selected merely
because a kernel is narrower or faster. See the [measured scope and gap](scheduled-hops.md#final-shared-hotcold-measurements-and-remaining-gap).


The latest [shared-execution and fine-chip checkpoint](scheduled-hops.md#shared-execution-and-fine-chip-follow-up-13-october-2026) records complete-prefix CPU measurements, actual arithmetic, cache identities, numerical scope and unresolved full-bank work. Native FP32, fine source tables and borrowing experiments receive no automatic planner/simulation speed discount from operand width or kernel timing. Default/FP32 minimum mapped arithmetic remains partial FP64 coverage; FP64 force retains original templates and bypasses approximate interpolation.


The [calendar acquisition continuation](scheduled-hops.md#calendar-acquisition-continuation-13-october-2026) identifies the rate36/DSSS10 broad-search state cliff, actual combined timing uncertainty, default-off shared frequency/private experiments and remaining tiled-acquisition work. Prefix gains do not establish a rescued continuous link. Scheduled CPU remains uncalibrated where complete execution is unsupported; neither native tile kernels nor narrower operand labels receive a blanket model discount.

The October14 bounded scheduled representation is an availability fallback, not
a measured fastest-backend selection. Dense representation remains selected
where its necessary state can fit the immutable per-bank share. CPU and RX
probability for the new complete rolling bank remain unavailable; see the
[partial checkpoint](scheduled-hops.md#october-14-bounded-acquisition-manual-checkpoint).


## Moderate scheduled support and remaining cliffs

Sched_Hop exposes 0/3/5 and developer dilution0/3, with exact compatibility
handling in [scheduled hops](scheduled-hops.md#controls-and-timing). Three active
hops in a three-hop frame use continuous acquisition and preparation; FHSS ramps
remain separate. Scheduling does not force amplitude-only decoding. Explicit
receiver overrides and arithmetic policy remain independent.

The current implementation retains implicit catalogues, bounded native-FP32
acquisition tiles, frequency-family execution, exact source/projection reuse,
shared dated capture and multi-cycle queues where eligible. Original refinements
still use wider real-I/Q statistics and guards. The moderate follow-up removes
a fixed 16-second sharing cutoff, repairs steering-resolution order and separates
joint coverage from runtime clock cells. It does not qualify full-bank
sensitivity or establish sustained rescue.

Lazy owner payload is now proportional to materialized tracks, not all logical
alternatives; published owner addresses remain stable. Metadata admission uses
the same remaining-budget layout in planning and runtime, with local history,
pure shared projections and actual shared owners kept distinct. These changes
do not select another detector or remove hypotheses. Actual native FP32 tiles
were observed in a complete-input 5/3 attempt, but expensive original guard
refinement and source-workspace fallbacks prevented full drain. The automatic
availability policy therefore remains uncalibrated as an algorithm crossover;
forcing that path is a diagnostic, not a promised rescue.

Consequential outstanding cliffs include gain-aware FHSS tile eligibility,
calendar-translation family sharing, odd/fractional origin certificates and
cost-based dense/implicit crossovers. GCD changes must be compared against
continuous reception: Fs6120/N87040 gives continuous680 versus scheduled136;
changing only Fs to6121 makes both1. Conversely N=14×Fs at Fs6121 has
continuous6121 versus scheduled1, a scheduling-specific expansion. These are
analytical geometry examples, not measured receiver performance.

The [practical-example follow-up](scheduled-hops.md#practical-examples-and-shared-preparation-14-october)
retains complex selection for the exact target8/rate36/DSSS10 command and removes
repeated exact-rate calendar preparation. Full-GUI transmission succeeded, but
reception did not meet either bounded deadline. The Spread example exposes a
separate short-symbol implicit-lifecycle gap and an FHSS gain-aware cold-screen
gap. They remain explicit limitations; scheduling does not mathematically require
amplitude-only reception. Internal input-moment sharing remains opt-in without a
demonstrated complete-execution gain. FP64-force still uses original templates
and bypasses approximate clock interpolation; exact calendar reuse is permitted
at every precision. The original diagnostic FIFO, guards, admission thresholds
and physical-completion requirements remain intact.
