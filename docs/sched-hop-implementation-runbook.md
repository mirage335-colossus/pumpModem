# Sched Hop implementation runbook for practical processing headroom

Planning baseline: **15 October 2026**, `codex/pulse-moments`,
`7a9640764e2a0249e5dbd64f039cd8df987350e7`. Audience: the subsequent GPT-6.1 Sol
implementation pass. This is a proposed implementation and decision procedure,
not an implemented receiver, passing measurement, or qualification report.

The selected architecture is **bounded Original numerical cohorts with shared
physical projections and exact source reuse, followed by ordered commits**.
Add qualified temporal/FFT and public-pulse contractions behind that same complete
numerical interface. Keep the existing point FFT as a separately identified
Default approximation until its entire guard path and sensitivity are qualified.
Do not begin by enabling its flags in Auto or rewriting the calendar.

The first vertical change must remove repeated calendar/template/guard work from
the actual lazy Original replay path. It must produce the complete detector state
that `complete()` consumes. Parallel execution alone is a wall-time experiment;
it is not a claim of reduced CPU. The final acceptance remains a matched sustained
reception rescue, including all guards, rolling banks, diagnostics, absence and
drain. A useful manual checkpoint with an unmet rescue must say **unfinished**.

| Requested output | Runbook entry point |
| --- | --- |
| I1 Diagnosis and evidence | [Baseline](#baseline-and-decisive-evidence) |
| I2 Dispatch, predicates and obsolete findings | [Current call graph](#current-call-and-dispatch-graph) |
| I3 Complete numerical specification | [Mathematics](#mathematical-specification) and [execution interface](#three-stage-execution-interface) |
| I4 Ordered executable work | [Cards 0–12](#ordered-implementation-cards) |
| I5 Decisive experiments and branches | [Fixture recipes](#fixture-recipes-and-independent-producer) and [critical path](#critical-path-and-bounded-decisions) |
| I6 Every serious strategy | [Disposition matrix](#strategy-disposition-matrix) |
| I7 Stop and escalation rules | [Sol rules](#stop-and-escalation-rules-for-sol) |
| I8 Final evidence and delivery | [Report template](#required-implementation-report-and-machine-readable-outputs), [tests](#build-and-test-matrix) and [GUI](#gui-and-manual-testing-checkpoint) |

## Scope and execution contract

Follow [AGENTS](../AGENTS.md), [coordination](agent-coordination.md),
[development](development.md) and [building](building.md). Read the relevant
source again before every card; symbol names below are authoritative over line
numbers. Later source changes require reconciliation, not forced patches.

This planning pass changed no runtime code and ran no receiver benchmarks or
application tests. Its checks were source inspection, retained artifact hashes,
manifest reconciliation and documentation review. The implementation pass is
authorized to work locally, build, run the affected local tests, and deliver a
rebuilt GUI with normal, verbose and bounded debug launchers. It is not authorized
to push, publish, dispatch hosted CI, introduce GPU execution, change mixer/driver
routing, or begin hours-long platform/SDK qualification.

Preserve the optimized continuous receiver, preceding scheduled binaries,
Original scalar diagnostic path, failing fixtures, raw measurements, assertions,
other sessions' files and all existing wire/progress behavior. In particular:

- Text of 1–16 source bytes keeps its exact short dictionary codes and endpoint;
  raw binary keeps every entered bit. Add no wire metadata or padding.
- Longer text and attachments keep the local 192-bit marker plus 128 coded-byte
  interval. Received lengths do not control physical framing or allocation.
- Complete reception only after consecutive **fully scored absent symbols** cover
  six seconds. Null time, EOF, cancellation, queue retirement or valid source
  decoding are not absence.
- Publish each newly accepted bit at the next progress poll into the same pending
  row. Preserve all-stream inspection, exact retained prefixes and pending versus
  completed identity.
- Keep target SNR, source waveform, hypotheses, input rate, workers and workspace
  fixed within a comparison. Do not select amplitude, lower planner coefficients,
  narrow required timing/frequency coverage, or enlarge memory to manufacture gain.

## Baseline and decisive evidence

### What was verified

`a0143115..7a964076` changes only `FURTHER-agents.txt`. The worktree/index were
clean at planning startup. At that point all 2,278 entries in the amended
calendar30 validation manifest matched the covered files. The planning diff adds
this runbook and its topic navigation link; runtime inputs remain unchanged.
The newer commit therefore does not
invalidate the calendar30 runtime evidence, although a commit alone remains
insufficient build identity.

Let `A` denote the local ignored evidence directory
`.agent-work/artifacts/sched-fft-rescue-20261014/`. Preserve it read-only.

| Item | Verified identity or scope |
| --- | --- |
| `A/datapump-gui-integrated30` and root `datapump-gui` | SHA256 `d52cd0847e1ab6e287613328c92ab85844ca644ea0ccd597ad793ce59981d009` |
| `A/libdatapump-integrated30.a` | SHA256 `28336ea8491d53e2eb92f115c35d00a5284977ef5e082abcbf9049b86aaebccd` |
| `A/source30-validation-manifest.json` | Current amended SHA256 `b0edace253e883e3186da3a9890312af9513543c7f84b4debdfd449b72890974` |
| `A/source30-manifest.json` | Original parent SHA256 `6c307e4c59b1708321642d7913eb9df1a160d5e49a423a9585e782ce128fb62b` |
| Amendment | `A/source30-validation-amendment/IDENTITY.json`; reconcile its parent and changed files |
| Build/candidate provenance | `A/source30-delta/IDENTITY.json`, `calendar30-root-promotion.json`, `calendar30-delivery-checks.json` |
| Earlier baseline | `A/baseline/identity.json` describes earlier GUI `253c0dd7…` / core `87488a2b…`, not calendar30 |
| Planning read dependencies | `.agent-work/artifacts/sched-astra-runbook-20261015/baseline-read-dependencies.json` |

The earlier baseline used GCC 14.2, Linux Release/FLTK and
`-O3 -ffp-contract=off -fno-fast-math`. Re-extract each retained candidate's own
cache and compile commands; do not attach those flags to a different binary by
assumption. Preserve runtime dependency hashes and loader paths as well as the
GUI, static core, source manifest and fixture sources.

### Measurements that may be reused with their original scope

These are retained measurements, not new executions by this planning pass.
Primary local report: `A/CALENDAR-CHECKPOINT.md`, especially the identity,
full-Live, calendar approximation and final-validation sections.

| Capture | Established result | What it does not establish |
| --- | --- | --- |
| Earlier M10/G1 complete 54-bank fixture | CPU 923.544652 → 457.734789 s; wall about 118.884 → 65.709 s; about 348 physical s | Current M3/M5 efficiency against continuous FFT |
| Latest Spread continuous, Fs14400 | `001`, observed absence and drain; CPU 5.474223 s / 52.313958 physical s = 0.104642 CPU/physical s; paced wall 52.568066 s, n=1 | A near-real-time F3 load, hardware success or statistical sensitivity |
| Spread M3 production | Original 24 s backlog bound failed; about 27.1095 CPU / 24.4543 wall s | Complete CPU/source or sustainable service rate |
| Spread M5 production | Same bound failed; about 28.9583 CPU / 24.6051 wall s | Complete comparison or rescue |
| User36 M3/M5 production | Same original backlog bound failed; about 268.095 / 339.328 CPU s | Whole-message throughput ratios |
| Calendar26 single bank with frozen25 core | 226,863 → 1,035 lanes, nine phases/five pairs; `001`, absence, drain; CPU 144.482248 / physical 212.533 s | Production Auto or full rolling Live success |
| Single-bank profile | Guards 110.073 CPU s; frontend 21.714; residual 12.047 | Fractions transferable to Live |
| Spread M3 coarse profile | Residual 17.388322 / 25.541081 exclusive CPU s = 68.1%; setup 2.959761; rolling 2.091771; templates .924223; tagged guards .747178; search .796221 | One arithmetic kernel consuming 68.1% |
| Candidate30 validation | 39 GUI passes; regular 203 passes, three failures, one incomplete, 207 selected | An overall green qualification |

The regular retained failures are `scheduled_point_source_nodes`,
`live_exact_source` and `alsa_audio_contract`. Parallel preparation exceeded an
external 360 s diagnostic bound and is incomplete. Preserve the original 30 s
Live drain bound and fractional clipping assertion: peak about 1.00944, 63 clipped
samples, fraction `2.47619e-8` against `1e-8`. These are not tunable performance
targets. Reconcile `calendar30-regular-final-summary.json` and amendment membership
before quoting counts for a rebuilt candidate.

The M10 evidence is in
`.agent-work/artifacts/sched-practical-20261013/EFFICIENCY-FOLLOWUP.md`.
The 5.1%-slower native curve variant, 9.8%-slower packed-Gram variant and
21.4%-slower combined signal/Gram variant reject those implementations in their
measured scopes. They do not reject every FP32, temporal or family transform.

### Diagnosis and what remains unknown

**Established source fact:** split complex scheduling returns from
`PatternReceiver::Impl` before ordinary timing FFT setup. If dense mandatory
state does not fit, `PatternCorrelator::Impl` initializes lazy acquisition and
returns before mapped family/source-curve/worker preparation. Lazy pushes also
return before those normal resources are bound. Cold candidates that do not
screen out materialize owners and replay Original rows; much of that replay and
completion traversal is serial.

**Established execution:** latest Spread uses C8 fine chips and reports zero
ordinary curve hits, pulse spans, temporal attempts and common mapped pairs.
The ordinary pulse shortcut requires C≥1024. Nine preparation workers were
already used. The path performed tens of millions of Original replay coordinates,
thousands of owners and many refinements. A stale one-worker cap is not the
demonstrated cause.

**Inference:** repeated mapping, source construction, guard updates and serial
replay across overlapping events are the strongest coherent target. The 68.1%
residual bucket supports investigating that seam; it does not identify its
internal fractions. Card 2 measures those fractions before selecting expensive
specializations.

**Unknown:** exact complete-cost crossover for the proposed cohort representation;
how many real production lanes share each useful numerical identity; whether
guard-rich C8/C34 groups benefit more from direct SIMD, shared pulse outputs or
small FFTs; current F3 geometry; full-bank sensitivity of Default chord methods;
physical-audio outcome. The runbook resolves these through bounded gates below,
without claiming that the architecture guarantees rescue.

Startup, sustained numerical work, ordered commit, diagnostic-history replay,
input waiting and audio dating must be measured separately. A queue-age failure
with valid dated synthetic PCM is not evidence of an audio-clock fault.

## Schedule and rescue budget

Public controls remain Sched_Hop `0/3/5`, dilution `0/3`. Public dilution0 means
internal `G=1`. Programmatic `SchedHopPlan::validate` permits `1≤G≤M`; that does
not create a public G4 option. Use the existing schedule's exact rounding and key
addressing, including the `(M,G)` identity.

For `J=floor((M-G)/2)`, there are `J+1` keyed placement choices. Adjacent quiet
gaps are between `M-G-J` and `M-G+J` hops. A hop is 0.4 s before sample rounding.

| Plan | Nominal G/M | Contiguous active time | Quiet gap | Placement choices |
| --- | ---: | ---: | ---: | ---: |
| M3/G1 | 1/3 | .4 s | .4–1.2 s | 2 |
| M5/G1 | 1/5 | .4 s | .8–2.4 s | 3 |
| M5/G3 | 3/5 | 1.2 s | .4–1.2 s | 2 |
| M5/G4 diagnostic | 4/5 | 1.6 s | exactly .4 s | **1** |
| M3/G3 | 1 | continuous | none | 1 |

M5/G4 has no unpredictable placement. Its 1.25× nominal extra physical time is
a stringent overhead diagnostic, not satisfaction of the secret-calendar goal.
Promoting it requires a separate authorized CLI/UI/serialization and potentially
versioned schedule design. Do not casually change the keyed addressing.

Let `C0` be continuous CPU/source second, `k` scheduled/continuous CPU per matched
source work, `d=G/M`, and `I` additional CPU/physical second. The diagnostic budget
is `Cs≈C0*k*d+I`. With `C0=1.1,I=0`, M5/G3 needs `k<1.51515`, M5/G4 needs
`k<1.13636` merely to average below one CPU/physical second. Require margin;
these illustrative numbers are not fitted planner coefficients.

Measure elapsed service independently: CPU aggregated over workers is not a
single-core real-time deadline. Record time in serial commit, useful worker CPU,
idle joins and producer waits. A serial bottleneck can violate the queue bound
despite apparently acceptable aggregate utilization.

For a measured improvable fraction `p` and local speedup `s`, total gain is
`1/((1-p)+p/s)`. At p=.681, infinite speedup is at most 3.135× and 4× improvement
is about 2.044×, **only if the entire measured bucket is improved**. Recompute
from complete runs including cold setup, rolling admits and hostile refinements.
Do not apply the single-bank 76% guard fraction to Live.

The final denominator uses actual source work and physical schedule, including
ramps, short-bit deferral, matched active power and listening/tail costs. Report
`sum(gain²)` and waveform energy. Source group length is
`G*floor(2*Fs/5)`; source advances even inside its guard/ramp positions and pauses
in intergroup nulls. Packed utilization can reduce payload throughput below G/M.
Do not pad the receive tail to improve CPU/physical ratios.

## Current call and dispatch graph

```text
GUI import/example/controller or CLI -> effective Config + timing/provider map
  -> Live rolling key/profile/epoch admission and receiver_workspace loan
  -> resolve_receiver_strategy_impl
      amplitude chosen -> PatternCorrelator(amplitude_only) [reject for rescue]
      complex
        -> PatternReceiver::Impl
           scheduled_bursts && !packed_scheduled_symbols
             -> scheduled_complex_search -> PatternCorrelator
                dense fits workspace -> ordinary mapped/worker preparation
                implicit selected + lazy eligibility -> initialize_lazy; return
                  push_lazy -> flush_lazy -> prepare screens/families
                  -> ordered lazy_for_each_due
                     point/chord result or certified bound if enabled
                     rejected safely -> preserve trials/journal semantics
                     nonrejected -> lazy_ensure_slot -> lazy_replay_to
                       -> accumulate_scheduled -> complete -> timing -> publish
           otherwise -> continuous/packed FFT setup
             -> score_frequency_axis
             -> score_bounded
             -> score_native_whole
             -> ordinary shared/parallel/generic scoring
             -> ordered trials/tracking/publication
  -> Live take_pattern_bursts/process_pending/retirement -> pending GUI row
```

The graph is source-derived. The path census must record the executed branch and
reason for every acceptance fixture; an eligible function is not an observed call.

| Fixture | Current expected dispatch | Required census distinction |
| --- | --- | --- |
| F1 Spread M0 | Continuous complex timing path | DSSS10 excludes frequency-axis CZT; native whole needs its own ≥65536 transform/streamed/one-worker predicates, so do not presume FP32 |
| F1 M3/M5 G1 | Recorded implicit split, original replay after screens | C8; count prepared families, positive accelerator executions, replay and owners |
| F2 User36 M0 | Continuous complex, possible compact/workspace fallback | Extract exact N/C and actual direct/partitioned/whole choice at Fs6120 |
| F2 M3/M5 G1 | Recorded implicit split, full Live backlog failures | Production Auto separately from one-bank point diagnostics |
| F3 | Not yet selected | Establish current useful continuous decoding and independently paced deficit first |
| M3/G3 | `continuous()==true`, `scheduled_bursts()==false`, packed=false | Split machinery must be bypassed; applicable FHSS guards remain |
| Packed short symbols | Geometry-selected path | Do not infer split/packed from M alone |

### Execution and ownership table

Record observed counts in Card 1's census alongside the static entries below.
Do not fill unknown counts with zero.

| Source and symbol | Controlling condition and output | Arithmetic, lifetime and owner |
| --- | --- | --- |
| `src/receiver_strategy.cpp::resolve_receiver_strategy_impl` | Split complex considered before amplitude; implicit unsupported/empty/model-unavailable states distinct | Coordinator; estimates are not execution; retain unavailable/infinite uncalibrated costs |
| `receiver_strategy_bank_count`, `scheduled_complex_search` | Physical aperture/refinement yields actual paired coverage | Exact grid inputs, overflow/caps preserved; coordinator |
| `src/pattern_receiver.cpp::Impl` | Split fork near line195 precedes FFT setup | Receiver lifetime; route choice before native buffers |
| `score_frequency_axis` | Noncontinuous schedules excluded; also DSSS/factor>1/FHSS excluded; four quarters, no differential, ≤3 clocks, ≥128 hypotheses, projected N≤65536, workspace/crossover | Separate algorithm from ordinary timing FFT; F1/F2 fail independent predicates |
| `score_native_whole_impl`, `private_acquisition_template` | Streamed private rows; exact repeated address reuse; native/range/memory gates | Float/double plans; shared input transform, independent bit rows; batch lifetime |
| `scheduled_acquisition_policy::select` | Auto selects lazy when dense mandatory bytes do not fit | Availability policy, not measured dense/implicit crossover |
| `PatternCorrelator::Impl`, `initialize_lazy` | Lazy additionally requires current eligibility, including one section/no differential | Returns before ordinary mapped preparation; stable lazy owners allocated on demand |
| `prepare_scheduled_families/common_shape/frequency_piece/temporal_templates`, `prepare_fine_curves/workers` | Normal dense/mapped preparation; lazy early return bypasses binding | Existing scratch/source/loan contracts cannot be transplanted wholesale |
| `lazy_prepare_screens` | Linear grid/fractional/origin-frequency strategies and overrides | Existing parallel preparation; count actual workers and complete cost |
| `lazy_prepare_detached_replay` | Colored-demand mode can return before replay batching | `REPLAY_BATCH=4/16` alone may do nothing; retain precedence diagnostic |
| `lazy_default_point_fft` | Default arithmetic plus Default linear/FFT prerequisites and supported geometry | Native FFT operands, FP64 compensated spectra; whole chord fits only |
| Guard archive/prepass | Qualified point family, observed due lanes, optional archive | One serialized current-family Slot, bounded archive, optional summaries; still calls lane guard work |
| `accumulate_scheduled` | Original active sample mapping, pair shaping, gain/limiter and full guards | FP64/wider; current/carry distinct; Original processing-fence prefix convention |
| `complete/timing_decision/settle_timing/publish` | Complete observation then current trial/threshold/chain rules | Ordered coordinator only; never worker calls |
| `SharedScheduledCapture` and processing journal | Dated immutable PCM, original fences, subscriber/pin/generation identity | Mandatory retention until all original/future consumers permit release |
| `Live::Bank/Receiver` | Keys, automatic profiles, epochs, rolling admits, exclusive loans | Source/permutation/temporal/projection pools have differing lifetimes; hits must be demonstrated across deferred replay |

### Findings that must not be repeated as current limitations

| Old statement | Current correction |
| --- | --- |
| Default authorizes independent multi-second hop-calendar motion | Shared GPS joint prior now intersects the original phase/origin lanes |
| Default Audio is50ms | Default causal Audio is70ms; explicit50ms remains50ms |
| Profile load/target persistence/launch quoting are necessarily still broken | Latest candidate contains fixes; verify actual round trips before changing |
| Only one preparation worker runs | Latest Spread observed nine; much later replay remains serial |
| Lazy catalog needs one fully allocated decoder per lane | Implicit catalog/stable lazy owner machinery exists |
| Split support cannot span later active cycles | Shared capture/journal/queued multi-cycle processing exists |
| Point FFT only accelerates dots or only one dwell | Eight/16 planes include full real Gram and already append disjoint multi-dwell fragments |
| Zero shared-input hits disprove immutable tile sharing | They disprove benefit of that cache/consumer alignment only |
| FP32 is universally slower | Specific fixed-bank/curve prototypes were slower; other complete routes remain open |
| Sparse symbol-long FFT rejects small dense tiles | Different density, outputs and full-detector cost must be measured |
| 43× frequency kernel proves receiver gain | It excludes guards, preparation, fallback and rolling state |
| One-bank calendar success is practical scheduling rescue | Production full-Live failures still stand |

## Mathematical specification

### Coordinate and identity dependencies

Use three clocks: physical capture time through nulls; source position that
pauses in nulls; processing/publication time that may lag. Processing time never
appears in the carrier phase or cryptographic address.

Let `Fs` be input samples/s, `E` integer UTC epoch seconds, `φ` canonical nominal
sample phase, `o` fractional capture-sample arrival origin and `r=1+ppm*1e-6`
from an original frequency/rate pair. With immutable source map `S`:

```text
a = E*Fs + φ                         nominal physical sample anchor
u0 = S(a)                           absolute source anchor
tick(n) = a + r*(n-o)                physical calendar coordinate
v(n) = S(tick(n)) - u0               source position relative to anchor
i = floor(v(n)/N)                   source-symbol ordinal, with original edges
```

Use `SchedHopCoordinate` and the actual `Piece` anchor association. In a Piece
anchored at physical cursor `b`, preserve
`within=source0+(n-b)*r`; do not replace it with a large absolute subtraction.
Original validation precedes the gain test: negative source skips; source≥2N
fails; source≥N belongs to carry after subtracting N and checking address overflow.
Zero gain must not hide invalid source/carry ownership.

The joint prior is `o ∈ O+α[φ,φ+w]±W`, intersected with original clipped origin
cells, including phase-cell padding. Hence
`a-r*o=(E*Fs-r*O)+(1-r*α)*φ-r*ε`; calendar displacement is derived, not an
extra independent Cartesian axis.

```text
key/profile + waveform/schedule revision -> keyed map S and source generator
E, canonical cell, rate pair, qualified arrival map -> allowed original origins
E, phase, origin, rate, S -> physical/source Pieces and boundary ownership
source symbol + S inverse -> actual private UTC/sample address for bit0 and bit1
capture generation + Original processing prefix recipe + raw provenance -> projections
Pieces + private address + waveform + gain/limiter -> paired template rows
rows + projections + guard partitions + seed state -> numerical event result
original event order + current trials/threshold + numerical result -> commit
```

| Field | Units and allowed set | Identity/reuse rule |
| --- | --- | --- |
| Key/profile namespace | Opaque immutable ID, selected keys/profiles only | Never log keys; separate namespaces unless content-equivalent under a qualified identity |
| Stream epoch | Integer UTC seconds from original rolling domain | Not processing time or assumed message length |
| Canonical phase cell | Nominal samples, representative and retained width | Preserve original cells, holes and phase refinement |
| Arrival origin | Fractional capture samples from original grid/intersection | Derived joint domain; not extra calendar translation |
| Source index | Integer ordinal plus exact within-symbol position | Use actual inverse physical start for crypto address |
| Frequency/clock pair | Hz and ppm, original paired/refined list | Do not create or prune a Cartesian product; duplicate logical trials survive numerical sharing |
| Fractional residue | Precise local coordinate representation | Exact residue class required for translation reuse; one-ULP rates separate |
| Capture/provider revision | Generation IDs, physical timestamp/rate uncertainty/correction | Invalidated by discontinuity/correction change, not by processing delay |
| Processing fence | Original call cursor/limit, bank-relative restart/prefix-zero origin and fence ID | Capture blocks alone do not define recurrence/prefix-subtraction identity |
| Algorithm/arithmetic | Separate policy IDs plus implementation revision | Same float label does not imply same template/rounding recipe |
| Numerical seed | Current/carry cursor, state revision and owner generation | Prevent stale result adoption after timing/owner updates |

Catalog logical equivalence and numerical sharing are different. Keep each
logical event/trial even when its numerical input is identical. Prove catalog
reduction using the independent enumerator; low survivor counts do not prove it.
This runbook does not require a new catalog reduction to obtain the first gain.

### Arrival uncertainty

Use `clock_sync::calendar_arrival_map`, `qualify_start_window` and Live's current
joint calculation. Default hopping has ±10ms at each station. For
`τ=seconds_per_frame`, the nominal map is
`O=captureSample+(E+offset+A/2-captureUTC)/τ`, `α=1/(Fs*τ)`.
Before rate/inverse-slope corners, the bound includes
`2*accuracy+region/2+max(providerRadius,A/2)+A/2+.01/Fs+roundoff`.
The implementation evaluates corners; reuse it instead of recoding this formula.

Audio0 with exact dating gives about20ms half-width; Default70 with provider
radius≤35ms gives about90ms and a +35ms TX midpoint. Apply the causal midpoint once.
Known queue delay is already compensated by physical dating. Preserve full
provider radius under Audio0; a declared GPSDO is not measured discipline. A valid
empty intersection is an empty epoch. Missing qualification follows the existing
broad fallback and must be reported as such.

### Original additive numerical state

For a physical projection `p=(xc,xs,cc,ss,cs,E)` and paired private template
`q=a+i*b` after production gain/limiting, preserve these operations per bit:

```text
dot_re += a*p.xc - b*p.xs
dot_im += b*p.xc + a*p.xs
Gcc    += a*a*p.cc + b*b*p.ss - 2*a*b*p.cs
Gss    += b*b*p.cc + a*a*p.ss + 2*a*b*p.cs
Gcs    += a*b*(p.cc-p.ss) + (a*a-b*b)*p.cs
energy += p.energy
count  += 1
```

Gain weights the template, not raw noise energy. An actually admitted
observation with private q=0 still contributes E and N. Nulls/artificial FFT
zeros contribute neither. Count bit0/bit1 independently in identity but require
the same admitted observation coverage where Original requires it.

For `Δ=Gcc*Gss-Gcs²`, `CorrelationFit` uses its existing rank gate, explained
energy `(Gss*dot_re²+Gcc*dot_im²-2*Gcs*dot_re*dot_im)/Δ`, and
`-.5*(N-2)*log1p(-clamp(explained/E,0,1-1e-15))`. Keep the exact existing
conditions, clamps and operation order; this formula is not authorization to
substitute a new detector or tolerate a negative Gram.

| Consumer | Additive state and partitions | Nonlinear/final rule and sharing constraint |
| --- | --- | --- |
| Whole fit | Paired seven-field Fits | Full real 2×2 Gram, including cross term; use original rank/score |
| Chip guard | Per-chip completed Fit or projected-energy/rank totals plus unfinished final chip Fit/index/nested | Project only after the whole chip is accumulated; never add projections of fragments of the same chip |
| Quarter/drift | Active section Fit, index, explained sum, largest section, quarter-rank mask | Source-coordinate boundaries; missing quarters do not become observed absence |
| Differential | Current window Fit/index/expected count; original DifferentialAccumulator complex sums, previous state, count/pending | Preserve all original disjoint windows, incomplete-window rules and strongest-quarter treatment |
| Public outer pulse | 17-slot unfinished pulse ring; per-bit dot, variance, nonzero count | Assemble full pulse across pieces/dwells before squaring; public pulse is not privately limited |
| Carry | Separate copy of all applicable numerical state | Adjacent symbol has independent crypto/bit rows and ownership; transfer once |
| Timing/weak chains | Original timing neighbors, thresholds, retention/reliability rules | Ordered state, not worker-owned sufficient statistics |
| Pending/absence | Original accepted prefix and fully observed failure durations | Coordinator only, next-poll publication, no EOF/cancellation completion |

Use `CorrelationFit`, `CorrelationChipEvidence`, `CorrelationOuterEvidence`,
drift/differential types in `pattern_correlator_batch.hpp`. `CorrelationLane` and
`scheduled_default_linear_events::EventStats` are existing numerical containers.
Factor their common Original state; do not create a third score-only ABI.
`EventStats::valid_geometry` currently rejects partial final chips: lifting that
restriction requires explicit tail ownership and tests, not deleting a predicate.

The outer pulse ring uses completed pulse `z_j`, private coefficient `c_bj`,
`D_b=Σc_bj*z_j`, `V_b=Σ|c_bj|²|z_j|²`, and contributing nonzero count. Its
evidence is `max(0,|D_b|²/V_b-log_constant)`. Keep the original minimum-term and
colored-excess activation conditions at the **current** threshold. Partial
support, final-chip amplitude and current/carry pulse rings remain separate.

### Carrier and exact shared projections

Seed the oscillator at each **Original processing fence** using the same
bank-relative start, remainder, frequency and integer/long-double association as
`lazy_prepare_block`; advance by the same complex recurrence. Reconstruct its
complete cumulative projection prefix from that fence's original zero origin.
Original rows are `prefix[i+1]-prefix[i]`, not freshly evaluated raw products.
Even with identical oscillators, restarting the prefix sum at a tile boundary
changes those differences. Cache the original prefix slice or the exact adjacent
differences derived from it, including the original accumulation/subtraction recipe.
Raw capture fences remain separate byte provenance: the Default point operator
uses a raw-fence/direct-product recipe, and `lazy_carrier_restart` bounds one
part of that difference. Never conflate these two operators.

Numerical tile cuts cannot introduce oscillator or prefix resets. Physical gaps
retain phase. Source compaction needs a segment table with
original physical offsets and exact within-segment mapping; merely concatenating
active PCM is not an exact transform.

Initially share only `CorrelationProjection` rows with exactly matching capture
generation, absolute interval, Original processing-fence partition, bank-relative
restart/prefix-zero origin, input rate, carrier frequency, recurrence and prefix
accumulation/subtraction rule, correction generation and arithmetic recipe.
Key-independent unweighted projections can cross keys. Gain/mask-weighted
statistics require that identity too. Equal absolute PCM with different executed
bank-relative recurrence/prefix recipes is a cache miss for bitwise Original
sharing. Cross-bank consumers may share only after proving equal recipes; an
alternate phase-normalized recipe needs an explicit bound/refinement policy.
Equal PCM bytes also do not erase a differing continuity generation.

### FFT identities and bounded selection

The existing point engine implements linear correlation through convolution.
For fragment capacity F, output count B, output step s and H=(B−1)s:

```text
T = bit_ceil(F+H)
actual fragment length f <= F
input length = f+H
reverse template at F-1-i, not f-1-i
extract inverse output F-1+j*s
result[j] = sum_i template[i] * input[i+j*s]
```

Use the local `search_fft` normalization/convention and its tests. There is no
conjugation in these products: the template convention already matches
`CorrelationFit::add`. Add impulse, complex sign and short-last-fragment controls
before using production arrays. Do not import an FFTW normalization assumption
into the local engine.

For a **proved integer physical translation** family of exact Original sampled
templates, define `Y=p.xc+i*p.xs`, `K=(p.cc-p.ss)+2i*p.cs`, `U=p.cc+p.ss`.
Per bit correlate Y with q, K with q² and U with |q|². If outputs are D,H,V:
`xc=Re(D)`, `xs=Im(D)`, `cc=(Re(V)+Re(H))/2`,
`ss=(Re(V)-Re(H))/2`, `cs=Im(H)/2`.
Masks/count/energy remain independent. q includes exact gain and limiter.
Noninteger translations, different crypto addresses, clock slopes, masks or
limiter state do not qualify by proximity. Use exact cohort fallback.

The current chord point engine uses eight input planes
`aY,bY,a²K,2abK,b²K,a²U,2abU,b²U`, with `a=1-t,b=t`, and eight products per
bit `L,R,L²,LR,R²,|L|²,Re(L*conj(R)),|R|²`. It already spans disjoint physical
dwells. Its output is the selected endpoint-chord operator, not an Original
certificate. Guard integration must consume the same declared operator or
retain a correctly bounded Original refinement.

Its reservation is approximately `16KiB+sizeof(engine)+T*(10*sizeof(native
complex)+32*sizeof(complex<double>))+B*256`. That is 592T bytes FP32 and672T
FP64 before other pools; T131072 costs about74MiB for that FP32 term alone.
Native float does not halve complete storage. Stream a subset of output planes
only if repeated input/template work and synchronization still win; keep every
required Gram plane. Never allocate a mostly-null symbol-long transform.

Start candidate tile capacities at512/2048/8192 physical rows, output cohorts
4/8/16, and choose among only candidates fitting the current loan and latency
bound. These are benchmark candidates, not replacement processing fences.
Use measured full preparation+contraction+guard+commit time to set crossovers;
retain direct execution for small or ineligible families. Do not use a
theoretical FLOP count as Auto calibration.

### Public outer pulse sharing

This is the first temporal specialization to attempt if Card2 confirms material
outer-pulse cost and reusable family sizes. For a family with the **same** source
map `s(n)`, public gain `g(n)`, frequency, Original processing-prefix recipe and source origins
`a_h=a0+h*C/2`, define
`Q_p=Σ_n g(n)*Y(n)*pulse((s(n)-(a0+p*C/2))/C)`.
For full interior chip j of lane h, its complete pulse dot is `Q_(h+2j+1)`.
Contract those shared values with the independent private coefficients using the
unchanged outer formulas. Complete symbol-clipped edge corrections before norm².
Enumerate actual atom centers whose closed support crosses either symbol edge
and deduplicate the sets: ordinary full chips have eight per edge, but a shifted
partial-final-chip atom can add a case and short symbols can overlap both sets.
Partial final chips need their shifted center and `sqrt(tail/C)` amplitude.

Make `FamilyProof` executable: for every partition and lane h, verify that
`s_h(n)+a_h` equals the common normalized source coordinate, actual gain and
observation/pulse-add membership match, and the Original projection recipe
matches. Same key/map object and rate alone are insufficient because origin/phase
change `tick(n)`. Endpoint checks suffice only where a proved affine/monotone
piece covers every included integer. Partition at the union of lane mask,
support, carry and source breaks; different executed rounding association needs
a bound or Original fallback. Track touched pulse atoms even if their final dot
cancels to zero; do not infer pulse-add membership from a nonzero result.

A practical first checker can prove **executed row reuse** without proving a
universal floating-point affine identity. Start with identical executed double
source-coordinate bits plus symbol/chip/producer/environment identity. For
translated candidates, evaluate each row through its frozen Original Piece;
preserve long-double `within`, its double cast and current/carry ownership.
Enumerate atoms with the actual `pattern_pulse_each` begin/end, chip count,
partial-final duration and center expressions. For each atom compute exactly
`argument=(sample-center)/length`, `position=abs(argument)*256` and
`scale=sqrt(duration/length)`. Compare descriptors containing producer/table
revision, floating-point environment, checked translated atom ID, executed
`position`/`scale` bits and enumeration/membership boundaries. A source-residue
signature is only a lookup hint; neither `fmod(source,C)` nor a table-cell index
proves equal interpolation. Equal complete descriptors permit one immutable
`{relative_atom, coefficient_bits}` list. On mismatch execute the row normally.
Keep gain outside this unweighted basis and retain Original's association
`gain*pulse*Complex{projection.xc,projection.xs}` and `coefficient!=0` rule.
Sharing gained products additionally requires equal executed gain bits.

**Whole-dot reuse needs a stronger proof:** structurally identical pinned ordered
physical-row spans, equal processing-prefix projections, gained pulse values and
multiply association, identical pulse-add sequence, initial accumulator bits,
completed domain and current/carry identity. Otherwise compare the actual ordered
records; a hash is never the final equality proof. Use a cached finished dot by
replacement of the same zero-seed whole pulse or the same nonzero seeded state.
Where this proof fails, share basis/per-row products and replay products into each
seed in Original order. A zero-seed delta followed by `seed += delta` changes
floating-point addition association; aggregated Fit deltas have the same issue.
Such deltas require a declared reassociation bound and exact refinement, not an
exact-reuse label. Private outer D/V/count contraction remains in Original atom
order, with each receiver's own coefficients.

This first checker still performs at least O(lanes×samples) coordinate/membership
work; translated descriptors may calculate roughly17 pulse arguments per row.
It saves repeated interpolation/basis preparation, not all guard work. Measure
descriptor construction, comparison, misses, shared evaluations and whole-dot
hits separately. No complete-fixture gain means this specialization is still
unproven; retain its exact fallback and take Card8's bounded alternative.

A physical affine piece with an integer output stride permits the ordinary
small convolution above. Otherwise use exact physical rows and a bounded direct
public pulse bank with vectorized output lanes. Do not resample PCM. Distinct
fractional residues are separate groups; rate uncertainty still accrues over the
full physical aperture. Retain direct irregular edges in both paths.

A future source-knot convolution is **contingent**, not required for the first
route: the pulse table is linear at1/256-chip knots, but its closed ±8 support
endpoints are nonzero. Exact-knot rows need the closed kernel[-2048,2048];
fractional rows need two kernels on[-2048,2047]. Naive interpolation creates a
false ramp outside support. No promotion without outward rounding/edge bounds
and complete-cost evidence. If pulse-dot error bounds are e_j, use
`eD=Σ|c_j|e_j` and `eV=Σ|c_j|²*(2|Qhat_j|e_j+e_j²)` as real-arithmetic
perturbation components. Add outward-rounded product, norm, reduction, division
and Original-reference rounding errors. Refine if the variance interval includes
zero unless the exact-zero branch is proved. Bound Original's executed positive
weight count with `[Nlo,Nhi]`: `Nlo>threshold+log_constant` proves term eligibility,
`Nhi<=threshold+log_constant` proves inactivity, otherwise refine. Preserve its
actual `norm(c)*norm(z)>0` test, not an epsilon or support count. Colored-excess
activation is an independent current-threshold condition. Ambiguous full-detector
decisions use exact cohorts; a raw-dot bound cannot certify the detector.

### Numerical policy

| Stage | First exact cohort implementation | Later eligible native path |
| --- | --- | --- |
| PCM | Original FP32 capture | Same captured bytes |
| Coordinates/addresses | Existing integers/long double | Never narrowed |
| Mapping/gain/private shape | Original FP64 and original association | Approximation only under separately named Default operator |
| Carrier/projection | Original FP64 recurrence | Shared exact rows; native alternative needs its own bounds |
| FFT buffers/products | FP64 first | Native FP32 when range/error and complete cost qualify |
| Spectral reductions | FP64/compensated existing semantics | Retain wider accumulation where needed |
| Fit/Gram/energy/chip/quarters | FP64; existing sensitive rank/subtraction | Wider guards/refinement allowed under FP32-min |
| Outer completed sums | Existing double/long-double types | Same sensitive state |
| Scoring/trials/publication | Unchanged ordered code | Never approximate threshold/trial semantics |

FP64-force requires unquantized Original templates and FP64 operands, with no
approximate template interpolation. Exact reuse and faster exact algorithms are
allowed. FP32-min forbids narrower operands but permits FP64 state. Default may
select a demonstrated approximation only after declared sensitivity checks.

An FFT identity may be algebraically exact while its reduction is not bitwise
Original. Small seven-field/statistic differences alone never authorize changing
rank, colored-excess, retention or admission branches. Such fast results need
applicable complete-statistic/branch intervals and exact cohort refinement on
overlap, or separate declared Default approximation qualification.

For the first exact extraction preserve per-lane sample order and expression
association; vectorize across lanes, not across the sample reduction. Keep
`-ffp-contract=off -fno-fast-math`, portable scalar fallback and runtime AVX2
dispatch. Verify emitted instructions for the executed hot path; no global AVX2
requirement. Do not repeat the already-slower packed-Gram implementation unchanged.

Nonfinite/range overflow, missing halo, unsupported environment, ambiguous rank,
unstable Gram, identity mismatch, unsupported family or insufficient scratch
returns **fallback**, never a rejection/zero score. For transformed equivalence,
compare normalized seven-field error against an Original oracle and preserve
existing stricter assertions. A new diagnostic tolerance may start at
`1e-10*max(1,abs(reference))` for FP64 fields and `1e-7` final statistic agreement,
but is an investigation trigger, not permission to weaken any current test.
FP32/default tolerance must come from the actual error envelope plus paired
decisions; a clean decode does not qualify it.

## Three stage execution interface

Proposed names below are new. Reuse the maintained state/fence/capture types;
these signatures specify ownership, not a demand to expose private implementation
types as public API.

```cpp
enum class ScheduledAlgorithm { dense_mapped, implicit_direct,
                                family_original, family_point };
enum class NumericalStatus { ready, exact_fallback, cancelled, invalid };
enum class FallbackReason {
  none, unsupported_geometry, unsupported_environment, family_mismatch,
  missing_capture, missing_halo, changed_generation, stale_seed,
  workspace, source_cache, plan_cache, clipping_or_limiter,
  fractional_edge, pulse_edge, ownership_edge, rank_uncertain,
  numeric_range, certificate_unavailable, threshold_overlap,
  colored_requirement, quarter_requirement, differential_requirement,
  outer_requirement, diagnostic_original_override, detected_signal
};
struct OriginalNumericalIdentity; // fields specified in identity table above
struct OriginalState;             // factored CorrelationLane/EventStats state
struct OriginalEventJob {
  OriginalNumericalIdentity identity;
  CatalogLaneDescription lane;
  OriginalState seed_current, seed_carry;
  PinnedCaptureView capture;
  PinnedProcessingFences fences;
  std::span<const OriginalPiece> pieces;
  std::uint64_t first, end, due, seed_revision;
};
enum class ResultDomain { seeded_replacement, full_replacement, bounded_delta };
struct OriginalEventResult {
  OriginalNumericalIdentity identity;
  OriginalState current, carry;
  std::uint64_t first, end, due, seed_revision;
  ResultDomain current_domain, carry_domain; // explicit domains for both states
  NumericalStatus status;
  FallbackReason reason;
  NumericalWork work;             // counters, no secret samples/keys
};
PreparedBatch prepare_scheduled_batch(const BatchRequest&, WorkspaceLoan&,
                                     std::stop_token);
void execute_scheduled_batch(const PreparedBatch&, WorkerContext&,
                            std::span<OriginalEventResult>, std::stop_token);
void commit_scheduled_results(const PreparedBatch&,
                             std::span<OriginalEventResult>, std::stop_token);
ScheduledDecision choose_scheduled_execution(const Geometry&,
    const ChargedResources&, const Overrides&);
```

The coordinator enumerates original due order without advancing it, reserves all
simultaneous bytes, pins capture/source/plan inputs, snapshots seed state and
publishes immutable jobs. Workers have private `PatternCode`, cursor and scratch;
shared rows/plans are read-only with qualified thread-safe execution. Workers
cannot mutate LRUs, create owners, call `room_for`, alter loans, charge trials,
publish pending rows or call `complete`.

Workers write disjoint result slots. Join before coordinator reads them. A ready
flag is installed only after full numerical work and cancellation checks. Never
publish a partially populated result as a valid low score. Reserve output storage
before launch so commit has no surprise allocation failure.

The coordinator walks original ProcessingJournal blocks, then the exact
`lazy_for_each_due` comparator **(global lane ID, source-symbol index)** within
each block. It does not sort by physical due time. Verify identity/seed revision,
refine exact fallbacks, adopt state and invoke the original completion path.
Preserve the cold `TrialReservation` RAII behavior precisely: temporarily increment
trials before screening; a committed rejection retains that one charge; a cold
survivor leaves the reservation's scope uncommitted, restores the old value, then
`complete()` adds its original `group_count`. A resident completion has no cold
precharge. Cancellation before a screen decision restores the reservation.
`threshold()` remains `-log(Pfa)+log(trials)+log(trials+1)+log(2)`. No worker or
new adoption path adds another charge. Keep retention/reliable/final thresholds
distinct, and test this transient as well as committed counter sequence.

`complete()` retains its own original publications. Only after **all** events in
the original processing block commit may the coordinator drop detached replay,
call block-final `settle_timing()`, advance the journal and set processed/next_due.
A numerical cohort/tile is not a new settlement or journal boundary. Preserve
the original no-stop interval between final consumer and block-frontier commit.

Choose conservative speculation for screening: prepare threshold-independent
raw/guard sufficient summaries first. Before expensive Original jobs, a lane may
be excluded only by a complete upper bound valid over every possible serial
threshold in the cohort, below the minimum possible rejection floor. Derive a
conservative trial interval from current trials and the sum of per-event maximum
charges (rejection1 or completion group_count, with overflow checks); include the
temporary cold+1 in the threshold interval. For coherent-only raw bounds this is
simple; threshold-dependent colored/outer bounds must hold uniformly over that
interval. If no such certificate exists, the lane is ambiguous and gets a full
numerical job. At commit always repeat the original screen decision at its actual
temporarily charged threshold, with original diagnostics/TrialReservation rules.
Discard a speculative full result if the original screen rejects. Count wasted
speculation and certificate cost; if material, reduce cohort size or use the
original serial screen for that scope, not a weakened bound. Never use a later
larger threshold to discard an earlier weak-chain candidate.

Numerical results contain no stored threshold/winner/rejection. Whole-fit-only
screens cannot reject four-quarter or differential cases: those branches can
increase evidence through `max(existing,branch)-log(2)`. Either bound every
enabled branch or compute the full result.

At cancellation, retain successful earlier commits; discard or keep explicitly
uncommitted immutable results according to generation identity. Journal advancement
is only through committed fences. On resume, start from the saved committed
cursor and seed revision. Do not double score observations/trials. Do not prepare
dependent source symbols of the same lane concurrently without explicit seed
chaining. Complete()/timing changes may invalidate a later speculative job;
discard and reprepare it instead of applying stale state.

The first exact implementation returns `seeded_replacement`: start a private
state from the exact seed bits, execute `[first,end)` updates in Original order,
then replace the verified matching state. `full_replacement` instead recomputes
the entire explicitly proved observation domain from its original initial state.
Never add a full-symbol Fit/pulse dot to a seed containing its prefix. Reserve
`bounded_delta` for an explicitly qualified reassociated aggregate with its error
certificate and exact fallback; adding a zero-seed sum to a nonzero seed is not
bitwise Original. Merge any qualified unfinished pulse/chip contributions before
nonlinear projection/norm². Current/carry have separate domains and seed cursors;
verify both before adoption.

Use immutable global tile IDs based on absolute capture coordinates for compatible
rolling consumers. Default numerical tile512 does not imply a new512-sample
carrier/prefix fence. A tile may intersect several original processing fences
and stores their cuts plus raw-capture provenance. Flush computation when an
accepted-bit/completion event is due, age/byte
pressure approaches its bound, or cancellation arrives. Never wait for a whole
long symbol or another message just to fill a batch.

Memory preflight includes mandatory catalog/owner/carry/chip state, retained PCM,
producer queue, processing metadata, seed/result copies, all concurrent scratch,
old+new growth overlap, transform planes, source rows, guard archives and deferred
diagnostics. Use checked size arithmetic. Optional caches are evicted before
denying mandatory coverage. A parallel cohort reserves the **sum** of worker
loans, not the entire remainder independently for each worker. Stable required
owners and pinned capture cannot be evicted to make a benchmark fit.

Cached sensitive rows/spectra inherit existing cleansing and lifetime policies.
Use monotonic opaque namespace/generation IDs plus exact structural keys; a hash
may accelerate lookup but collision resolution compares the structural identity.
Pointer identity alone cannot survive owner recycling, eviction or cross-bank
reuse. Do not log keys, plaintext PCM or private template values.

## Ordered implementation cards

Each card uses the exact frozen source/build/configuration manifest from Card0
plus its prerequisite cards' reviewed hashes. Re-read those inputs at the start;
changed dependencies invalidate affected evidence. The integration owner owns
`invariant:project:receiver-search` through coupled numerical/commit changes and
their validation. Other agents may analyze/review in parallel. Do not assign
different halves of the state ABI to independent shared-checkout writers.

For every card, first claim its exact files, new tests, output tree and artifact
namespace through the checked coordination helper. Builds require stable inputs
or an isolated snapshot. Timed runs require `workload:host:timing-sensitive` and
must run without concurrent compilation or other benchmarks. Audio/display claims
are needed only for actual device/native work. Keep a working reference executable
and an explicit scalar/previous-backend override until final validation.

### Card 0 Freeze the actual candidate and prepare isolated outputs

**Purpose/prerequisites:** no prerequisites; establish runnable references, not
just Git labels. Read current HEAD/status/diffs, session claims/inbox, relevant
closed handoffs, the source30 amendment and the build instructions.

**Files/resources/API:** own artifact/TMPDIR/build namespaces; no runtime edits.
Read GUI/core files, `build-info.txt`, CMake cache/compile commands, dynamic loader
dependencies and fixture sources. Use tested `agent-session.py` operations.

**Algorithm:** capture HEAD, index/worktree diff hashes, compiler version/flags,
CPU model, available cpuset/affinity, worker count, memory limit, load, all
`DATAPUMP_*` names/values that are not secrets, dependency path/hashes and fixture
input hashes. Copy binaries/dependencies/source inputs into a **new** claimed
reference directory; do not overwrite root launchers or earlier references.
Record synthetic key generation, seed, rates, geometry and original PCM hash.
Do not bundle a private developer key. Verify the copied executable actually
loads its intended dependencies. A source archive must include intended dirty
inputs if there are any.

**Ownership/cache/precision:** coordinator only; no workers or active jobs during
copy finalization. Independent copies, not hard-linked mutable files. No new
numeric policy. Freeze reference manifests only after all writers stop.

**Tests/counters/gate:** hash match the three references: optimized continuous,
preceding scheduled, candidate. Verify build identities separately from source
identities. Reuse matching old captures; no timing rerun solely to restate known
failure. Any mismatch means re-establish the corresponding reference, never reset
a newer checkout. Preserve all existing failing captures.

**Review/rollback:** reviewer checks copied runtime collection, manifest parent
versus amended identity and no accidental inherited overrides. Rollback is launch
of the frozen preceding GUI/core; no source rollback required for this card.

### Card 1 Census actual production paths and fixture settings

**Purpose/prerequisites:** Card0. Produce one machine-readable census per F1/F2
M0/M3/M5 and M3/G3 before new timing claims.

**Read/edit/claims:** read `src/gui/controller.cpp`, `launch_command.cpp`,
`application.cpp`, `src/main.cpp`, `receiver_strategy.cpp`, `simulation_estimate.cpp`,
`tuning.cpp`, `pattern_search.cpp`, `scheduled_complex_search.cpp`,
`pattern_receiver.cpp`, `pattern_correlator.cpp`, `live.cpp`. Edit only bounded
census/trace fields, relevant test cases and new copied fixture sources; claim
each edited file and search invariant. Preserve GUI example settings unless a
round-trip defect is actually reproduced.

**API/algorithm:** use existing strategy resolution and work counters. At
configuration resolution and first bank admission emit: requested/effective
target, source rate, Fs/N/C, oscillator pair list/hash and count, phase cell
width/refinement, original origin grid/intersection, combined arrival bounds,
epoch/profile/key counts, mandatory/optional bytes, selected dense/lazy/packed
state, algorithm/arithmetic/reuse IDs, actual worker count and fallback reason.
After a bounded event emit positive execution counts for each relevant backend.
Use opaque key IDs. A `not_called` value is different from a counter omitted by
an old binary. Count refinements and repeated coordinates by bank/family.

**Thread/identity/cache/precision:** read-only snapshots on coordinator; worker
counters merge after join. No per-sample logging, allocation or mutation of
detector state. Counter identity includes bank/capture generation. Use integers
for counts and existing precise geometry values, not rounded GUI strings.

**Tests/gate:** F1/F2 load→export→load retains exact effective fields, including
argument-order permutations and atomic failed imports. Run GUI launch/controller/
planner tests; baseline numerical tests are unchanged. Require a census row for
every required key/profile/epoch. A missing or amplitude route fails the fixture.
Do not repair a path by shrinking its coverage.

**Measurement/failure/review:** first inspect current counters on the preserved
failed prefix and one complete bounded control. At most one added unprofiled
control establishes census overhead; >3% CPU difference or materially changed
queue tail means reduce census frequency, not relax acceptance. Reviewer checks
the call table against positive counters. Keep instrumentation optional and the
prior binary as control; no speed claim from this card.

### Card 2 Attribute the serial residual and select the vertical specialization

**Purpose/prerequisites:** Cards0–1. Split the actual residual bucket before
writing another kernel.

**Read/edit/claims:** `scheduled_work_profile.hpp`,
`scheduled_template_cpu_attribution.hpp`, lazy replay/materialize/complete and
diagnostic-history paths in `pattern_correlator.cpp`, admission/loans in
`live.cpp`. Claim only necessary instrumentation files plus measurement resource.

**API/algorithm:** add mutually exclusive coarse categories for calendar/Piece
construction; source crypto/permutation/shaping; carrier/projection; whole
Fit/Gram; chip/quarter/differential/outer updates; owner allocation/growth;
traversal/adoption; diagnostic replay. Classify each fallback by one **primary**
reason with optional secondary tags. Keep sample counts and clock CPU distinct.
Record per-family unique input/source identities and requested-versus-unique rows.
Account unattributed CPU as total minus nonoverlapping scopes. Never sum nested
timers or concurrent wall durations as CPU fractions.

**Thread/cache/precision:** thread CPU timers, per-worker counters joined once;
no cache behavior changes. First use coarse scopes and the existing 10ms sampler;
fine per-row timing remains a separately labeled intrusive diagnostic. Preserve
all numerical operations and endpoint identities.

**Bounded experiment:** one F1 M3 prefix to the unchanged24s backlog failure,
one F2 first-bank complete run if existing identical evidence cannot answer the
new categories, and one hostile wrong-key/tone case. A separately named drain
may extend **diagnostic workload** after a preserved failure, but cannot turn
that failure into an acceptance pass. Compare an unprofiled control. Stop after
two informative profiles; do not spend the whole pass refining timers.

**Decision gate:** implement Cards3–6 regardless of optional FFT eligibility,
because they establish the complete numerical/commit boundary. Then choose:
outer/public pulse cost material with reuse≥2 → Card7 first; whole-fit/source
construction dominates with a proved shift family → Card8 first; neither → keep
exact shared direct cohorts and measure complete cost before more transforms.
If Card6 merely moves unchanged CPU into workers, at least one applicable
Card7/8/9 shared representation becomes mandatory before calling the optimization
implemented; a scaffold/parallel-only checkpoint must remain explicitly unfinished.
Use a conservative Amdahl bound: an optional stage whose maximum possible gain
cannot close even half the measured remaining deficit is secondary, not the
critical path. This does not reject that strategy universally.

**Review/rollback:** reviewer reconciles exclusive totals within observer overhead
and checks rejected-lane costs. If a category cannot be separated cheaply, report
its interval/bound and use counts plus exact cohort measurement; do not assign
the entire residual to a convenient kernel. Preserve old profiling scope and logs.

### Card 3 Extract the complete Original numerical state boundary

**Purpose/prerequisites:** Cards1–2. Refactor with no performance or detector
change; this is the correctness substrate for every later fast path.

**Read/edit/claims:** `src/pattern_correlator_batch.hpp/.cpp`,
`pattern_correlator.cpp::accumulate_scheduled/complete`,
`scheduled_original_boundary_partition.hpp`, `scheduled_default_linear_events.hpp`,
`scheduled_resume_state.hpp`; new `scheduled_original_event.hpp` and
`test_scheduled_original_event.cpp`, corresponding CMake registration.

**API/algorithm:** factor Original current/carry numerical members into a value
type backed by the existing Fit/chip/drift/differential/outer types. Add
`accumulate_original_event(job, state, scratch, stop)` as a private/internal API.
Its first implementation moves the Original loop literally: same Piece anchor,
source validation, gain and limiter order, independent paired shaping, projection
operations and guard boundary transitions. No q reconstruction approximation.
Leave timing/chain/owner/publication state in `Hypothesis`.

Implement adapters `snapshot_original_state(h)` and
`adopt_original_state(h,result)` with explicit current/carry fields. At the old
call site run the extracted scalar function synchronously and adopt immediately.
Do not serialize opaque pointers or copy a mutable `PatternCode` cache into jobs.

**Ownership/cancellation/commit:** one owner initially; stop checks at original
boundaries. A cancelled work buffer is discarded without changing the live
state. Accepted earlier events remain. Source state that affects future template
generation must be a seed/recipe or a worker-private reconstructed object, not
an untracked mutation shared with the original hypothesis.

**Identity/memory/precision:** include seed cursor/revision, current/carry source
indices, exact fence list and generator identity. Charge duplicated seed/result
state and temporary Pieces before copying; cleanse private arrays. Keep FP64 and
original operation order; no new cache in this card.

**Tests/gate:** exact field-by-field/parity comparison after every actual fence
for C8/C34, partial C/N, q0/q1, clipping, smooth FHSS, source end/carry, quarters
and differential. Compare trial counts/events/prefixes and diagnostic history.
Use an actual supported Original comparator, not exit77. No numerical difference
is acceptable for the literal scalar extraction. Existing full `complete()`
tests must remain unchanged.

**Failure/review/rollback:** if a hidden mutable dependency prevents value-state
extraction, identify the member and its first writer/read, add it to the seed or
coordinator boundary, and repeat the narrow test. Do not omit a guard. Keep an
explicit old-scalar reference call for A/B until Card12; reviewers inspect carry
transfer and exactly-once chip/pulse finalization.

### Card 4 Prepare immutable jobs and commit in original order

**Purpose/prerequisites:** Card3. Change execution scheduling while using the
same scalar numerical function first.

**Read/edit/claims:** `scheduled_parallel_preparation.hpp`,
`scheduled_processing_journal.hpp`, `scheduled_resume_state.hpp`,
`scheduled_acquisition_catalog.hpp`, `pattern_correlator.cpp::lazy_plan`,
`lazy_for_each_due`, `lazy_replay_to`, `flush_lazy`, `lazy_prepare_detached`,
`lazy_adopt_detached`; new job/result tests. Own the entire search invariant.

**Algorithm:** enumerate at most eight independent next events initially, in
original ProcessingJournal block order and lexicographic (global lane ID, source
symbol index) within that block. Prepare screen summaries and apply only the
uniform conservative speculative exclusion rule above before full jobs. Snapshot
immutable descriptors and seed
revisions without advancing frontiers/trials. Reserve seed/result/scratch/pins
for the entire cohort. Compute into disjoint result slots. After join, visit slots
in original order and verify still-current seed/generation. For a cold event,
run the original screen with the temporary trial charge **before** creating an
owner or adopting numerical state. Rejection commits only the original
screen/diagnostic outcome and its single charge. A survivor rolls back that
temporary charge, then materializes, verifies and adopts the full numerical state
and calls unchanged `complete()` including its own charge and publication.
Resident events adopt and complete without the cold precharge. Only
after all events in the original block, drop detached replay, settle timing and
advance its journal/frontier; do not do this at each numerical cohort boundary.
If a prior commit invalidates a later seed, discard that result and reprepare.

Start one numerical event per lane per batch. Do not parallelize successive
symbols sharing carry. A null Piece still participates in coverage/journal
accounting even when it performs no Fit update. Ready-result ordering cannot
reorder trial charges or weak-chain retention.

**Ownership/cache/precision:** no worker LRU, `room_for`, owner allocation or loan
mutation. Coordinator owns all leases and result vector; workers own private
execution contexts. Scalar Original arithmetic remains unchanged. Result identity
contains source/fence/seed generation; no cache yet beyond existing pins.

**Cancellation tests:** inject before reservation, after pin, mid-Piece, after
compute/before ready, after join and between serial commits. Resume exactly once;
verify prefix, counters, carry, pins and journal. Reverse worker finish order
deterministically and compare all-stream output to scalar. Force a stale-seed case.

**Gate/measurement/review:** scalar-cohort one-worker output must match Card3.
Multiworker output must match event order; measure aggregate CPU and serial
fraction separately. No rescue claim yet. On reservation failure choose a smaller
cohort or original direct path with the same coverage; on stale state reprepare.
Reviewer checks publication and journal only advance over committed work. Rollback
uses synchronous scalar job execution through the same ABI.

### Card 5 Share immutable physical tiles across lazy and rolling consumers

**Purpose/prerequisites:** Card4. Remove duplicated key-independent mixing and
projection without changing capture or calendar semantics.

**Read/edit/claims:** `shared_scheduled_capture.hpp`,
`scheduled_original_projection_cache.hpp`, `scheduled_carrier_restart.hpp`,
`scheduled_shared_input.hpp`, `scheduled_frequency_recurrence.hpp`,
`live.cpp` source/loan/pool lifecycle, new tile-cache tests.

**API/algorithm:** introduce a bounded coordinator-owned projection cache keyed
by the full physical recipe described above. `borrow_projection_tile(key,range)`
returns an immutable lease or miss; `prepare_projection_tile` runs once before
workers. Intersect globally aligned512/2048/8192 storage tiles with Original
processing-fence cuts; retain raw capture cuts separately. Store absolute range,
bank-relative recurrence start, original prefix-zero origin and exact prefix
slice/differences. Consumers select subspans without resetting oscillator or
cumulative summation. Retain existing physical PCM until every required
consumer/future-proof permits eviction.

Align compatible rolling work requests to these tiles when already due. Never
delay a due publication for alignment. Key-independent projections can be shared
across epochs/keys; candidate gain masks and source rows cannot inherit that scope.
Use the current future-consumer proof before omitting any raw capture. All-key,
profile, epoch, timing/clock null proof is required; candidate-local nulls suffice
only to skip that candidate's arithmetic.

**Ownership/cancellation/memory:** coordinator reserves/inserts/evicts; workers
hold const leases. Cache entries are optional and pinned during execution.
Include coexistence of old/new generations, halos, metadata and every active
lease in peak accounting. Cancellation releases leases after workers join.
Generation mismatch misses rather than reinterpreting cached bytes.

**Precision/tests:** first use exact FP64 recurrence/projections. Test same absolute
tile with different bank-relative origins (assert miss unless executed recipes
are proved equal), different correction generations, overlapping subranges, odd
fence sizes, gaps and noninteger carrier cycles. Require positive cross-bank hits
in an intentionally aligned rolling test with genuinely identical processing
prefix recipes and assert sample-for-sample equality. Report real production
miss rates separately. Existing shared capture/restart/loan tests remain.

**Gate/review/rollback:** timed complete-cost comparison includes construction,
eviction and misses. If hit rate is low, report consumer alignment and identity
miss reasons; retain direct tile construction and do not call a cache shared
execution. Do not indefinitely retain projections with no future reuse. Reviewer
checks broadest safe key independence and narrowest correct phase identity.

### Card 6 Fuse full Original numerical cohorts and exact private reuse

**Purpose/prerequisites:** Cards3–5. This is the mandatory first complete vertical
implementation. It must change the measured hot replay, not just add scaffolding.

**Read/edit/claims:** `pattern_correlator_batch.hpp/.cpp`,
`scheduled_private_batch.hpp`, `scheduled_mapped_piece_groups.hpp`,
`scheduled_ordered_pulse_candidates.hpp`, `scheduled_pulse_moments.hpp`,
exact source sample/permutation pool helpers and lazy job executor in
`pattern_correlator.cpp`. New `scheduled_original_cohort` tests/benchmark hook.

**Detailed algorithm:** group jobs by exactly equal physical projection recipe,
then by exact source/template identity where available. Keep original logical
indices separately. Factor one Piece traversal per structurally proved map class;
do not repeat identical `locate`/boundary scans for every member. Validate class
members against the executed normalized coordinate/membership recipe, including
origin/phase intercept and rounding. Pre-export Pieces with `scheduled_original_partition::make_piece`
using each worker's private map cursor. Split row runs at actual source-chip,
section/window/carry, gain/limiter and support transitions. Cache only exact repeated
paired rows/address sequences; bit0 and bit1 are independent values.

For each bounded tile, use a structure-of-arrays scratch of at most eight lanes
initially. Loop physical sample n ascending; load the shared projection once;
for each active lane compute or borrow its original paired q; update whole and
active chip/quarter/differential state in the **same pass** with the exact formulas.
Update public outer pulse contributions through the existing 17-slot ring.
The old Original loop already updates these statistics in one pass: that fusion
alone is not a new saving. The required reductions are fewer map traversals,
fewer source-row reconstructions per true identity, shared public pulse evaluation
where proved, and zero second numerical replay after adoption. Count each against
the preceding implementation; do not count moved arithmetic as eliminated work.
At a lane boundary finalize only its completed chip/window/section/pulse state.
Preserve the unfinished boundary state in the result. Vectorize arithmetic across
lanes with identical update shape; handle divergent edges through the same scalar
reference function. Never skip q=0 observation energy/count.

Source generation policy: share the original private pair only if key namespace,
physical crypto address, symbol/chip permutation/rotation, source coordinate,
waveform revision and limiter/gain recipe match. Exact whole-symbol DSSS
permutations can share independently of private samples. A cache keyed only by
source ordinal or one candidate bit is invalid. Generate rows outside inner
guard updates, in bounded tiles, rather than reconstructing them separately for
each consumer/diagnostic query.

**Thread/cancel/identity/memory:** no shared mutable `PatternCode`; allocate one
qualified context per worker. Coordinator loans capture, exact-source and plan
storage before launch. Charge SoA+source+boundary buffers, current/carry results
and alignment padding. Cleanse private rows on eviction. Cancel between bounded
tiles and use Card4's result/commit rules. Strong-signal fallback uses this same
bounded cohort engine, not a new unbounded full-symbol allocation.

**Precision/fallback:** FP64 Original first, no sample reassociation. Portable
scalar/SSE/AVX path must agree under its stated policy. Reasons include unsupported
shape, missing exact source loan, row-cache miss, edge divergence and memory;
each fallback retains the complete detector. Reuse existing long-chip moments
only at their qualified geometry; C8/C34 use the actual new cohort route.

**Tests/positive counters:** require `original_event_batches`,
`original_cohort_rows`, `projection_reuses`, `exact_pair_reuses`,
`source_rows_generated`, `guard_updates`, `scalar_edge_rows`,
`adopted_without_replay`, and `stale_results` counters with explicit units.
Compare all numerical state and public events for F1/F2, multi-cycle/fractional
and hostile cases. Force cache misses/eviction and confirm identical results.
One nonzero batch counter is not evidence that most work used it; report fractions.

**Acceptance/failure:** F1/F2 full scalar-versus-cohort control must retain complete
decoding/absence/drain or preserve original deadline failure at identical work.
Require useful total CPU reduction outside repeat noise, with no new queue/memory
regression. If only elapsed improves while CPU does not, label parallel service
gain and continue to the measured dominant shared specialization in Card7/8 or
the fully qualified Card9 route. If exact source/map reuse is scarce for C8/C34,
that is a required branch, not permission to finish with unchanged
`O(lanes*samples*pulse_support)` CPU. Do not tune
planner coefficients. Reviewer checks loop nesting eliminates repeated map/source/
guard traversal rather than simply moving it. Rollback selects original scalar
through the ABI; frozen binaries remain independent references.

### Card 7 Share complete public pulse and guard work

**Purpose/prerequisites:** Card6, Card2's guard-cost evidence. First optional
accelerator when outer/public pulse work is material; it targets guards directly.

**Read/edit/claims:** `CorrelationOuterEvidence`,
`scheduled_outer_public_replay.hpp`, `scheduled_pulse_moments.hpp`,
`scheduled_temporal_templates.hpp`, `scheduled_temporal_normalization.hpp`,
`scheduled_original_boundary_partition.hpp`, new Original cohort executor/tests.
Do not rewrite `complete` or the pulse/waveform definition.

**API/algorithm:** `prepare_public_pulse_field(FamilyProof, pieces, projections)`
returns complete pulse-dot tiles with explicit source-center IDs, support masks,
physical observation intervals and error/exactness policy. Use the public sharing
formula above. A family proof compares map/rate/gain/Original processing-prefix
identity and source center lattice; it is not mere proximity or an `eligible`
boolean. Implement the per-partition FamilyProof checker specified above.

Start with exact direct batched pulse dots, reusing projections and pulse values
across compatible lanes/private rows. Add the small FFT specialization only when
centers are integer physical translations on each qualified affine piece. Accumulate
piece dots across all dwells before outer norm². Enumerate and deduplicate actual
boundary atoms, including shifted partial final chips, and correct them with
Original rows. In seeded-replacement mode execute `[first,end)` pulse updates
from exact seed bits in Original order and replace that verified seeded state;
in full-replacement mode replace the entire proved observation domain. Aggregate
deltas need the separate reassociation certificate described above. Never add a
full-symbol dot to a seeded prefix.
Use the same ring/finalizer, preserving touched membership, coefficient order
and executed contributing-term count.

For per-chip/quarter/differential Fits, use shared physical moments only when
the complete template and membership polynomial is proved valid on the run.
Otherwise keep Card6's fused original updates; do not call whole-Fit acceleration
integrated guard work. A source-knot method with the endpoint/rounding certificate
is a separate contingent subcard if direct/physical convolution is insufficient.

**Ownership/memory/precision:** immutable field with coordinator pin; per-worker
outer state remains private. Store only bounded pulse windows and needed future
consumers, not an entire hours-long symbol. Include old/new field and edge buffers
in loan. FP64 first; errors from FFT reassociation use a declared bound or exact
refinement before threshold-sensitive decisions. Public pulse basis must not
inherit private limiter clipping.

**Tests/counters:** full pulse crossing two unequal null gaps, noninteger carrier,
both bits, strong wrong-key, partial chip, zero pulse, clipping and all edge starts.
Compare `pulse_dot`, outer D/V/count and final evidence to Original; assert edge
correction happens before squaring. Count reused pulse outputs, direct edges,
FFT transforms, complete guard adoption and fallback cost. Extend actual Original
guard tests with a supported C8 and C34 positive case.

**Gate/review/rollback:** compare 1/2/4/8 consumers including preparation, gather,
cache and guard finalization. Reject a specialization if it fails numerical
coverage or total cost; retain Card6 exact execution. If family equivalence is
false for current catalog lanes, report the differing intercept/rate/gain/address
and its frequency, then attempt only the proven subclasses. Do not relabel a
new independent calendar approximation as exact. Reviewer independently checks
support endpoint and outer versus private-limiter distinction.

### Card 8 Add bounded temporal whole Fit and frequency family execution

**Purpose/prerequisites:** Card6 and its complete result API; may precede Card7
only under Card2's documented cost choice. Reuse valid engines rather than build
conversion-heavy FFT machinery.

**Read/edit/claims:** `pattern_fft_batch.cpp`, `pattern_correlator_batch.cpp`,
`search_fft.hpp/.cpp`, `scheduled_acquisition_tile.hpp`,
`scheduled_default_point_fft.hpp`, `scheduled_spectral_products.hpp`,
`scheduled_frequency_recurrence/fusion/postinverse.hpp`, source/template caches,
new executor dispatch and actual-class tests.

**Algorithm:** enumerate structurally proved exact translation groups. For each
group, select a bounded F/B/step fitting the loan; generate paired Original q
once per actual private row identity; reuse Y/K/U input transforms; correlate
q/q²/|q|² with the fixed reversal/extraction convention above. Restore masked
energy/count and direct boundary/limiter corrections. Supply the whole Fits to
the complete event result, with Card6/7 guard state. Never replay the whole event
again merely to reconstruct guards already computed.

Frequency siblings may share source templates/metadata across exact equal rates.
Use maintained coarse/fine recurrence/fusion only with the original physical
phase origin and restart convention. Real Gram uses doubled carrier phase;
outer/chip/quarter/differential products have their own frequency dependence.
Preserve every original refined frequency required by physical aperture.
For an approximation of residual `δf`, the phase span is `2πδf*Tphysical`, not
tile or compacted-source duration. Enforce the existing bound over accumulated
pieces; otherwise use exact per-frequency projections. Do not invent a lower
frequency count to fit memory.

**Cost decision:** compare complete direct cohorts, small timing FFT and current
frequency machinery on actual F1/F2 family sizes, including guards and setup.
A 2D timing/frequency route is pursued only if reuse counts and a full-plane
operation/traffic estimate beat the measured direct lower bound by at least20%
before prototyping. If inconclusive, one bounded family prototype resolves it;
no repeat of the43× dot-only demo. Keep all logical trials and separate true
extra-aperture work from avoidable per-hypothesis work.

**Ownership/identity/cache:** plans prepared/borrowed before worker launch;
per-worker execution buffers; cache key includes plan environment, exact template
identity, F/B/step, plane definition, precision and revision. Cache spectra only
under existing sensitive-lifetime rules. Charge streamed plane passes and all
simultaneous transforms, never give each worker the full bank loan. Stop checks
between transforms/products; partial results are uncommitted.

**Precision/tests/gate:** algebraically exact FP64 specialization first, with
complete-statistic/rank/guard/threshold intervals and Original refinement on
overlap (FFT reassociation is not bitwise equality). Native FP32 only with
range/rank/error fallbacks and complete paired decision evidence. Assert impulse
index/sign, final short fragment, many dwells, clock endpoints, non-grid starts,
partial chip and mask-specific E/N. Positive native transform/product counts and
actual source reuse required. Stamp whole Fits as seeded/full replacement or
explicitly bounded delta and validate observation domain/seed cursor before
adoption. Existing Original skip77
does not pass the gate.
If group counts are too small or complete CPU is worse, use exact direct cohorts
and retain the measured crossover; do not reject all temporal/frequency work.
Reviewer checks physical phase, full Gram, guards and inverse normalization.

### Card 9 Integrate the Default point operator only under explicit qualification

**Purpose/prerequisites:** Cards6–8 complete baseline; this is contingent, not an
excuse to bypass exact fallback. Existing code is retained and reused where useful.

**Read/edit/claims:** `scheduled_default_point_family.hpp`,
`scheduled_default_linear_events.hpp`, point FFT/bulk/archive/summary/compact
headers and lazy include files, guard prepass/reference recipe and policy selector.

**Algorithm:** name the operator in results, e.g. `default_chord_q4`, distinct
from `original_sampled`. Reuse its eight/16-plane whole Fits and prepared event
resources for the complete chip/outer/quarter/window result; guard computation
must match the declared operator, membership and edge corrections. Remove
duplicate family reconstruction/guard replay by passing immutable prepared events
through the common API. Threshold remains outside prepared results.

Do not enable the current coherent-only raw screen for quarter/differential
families. Either implement their full result and qualifying error envelope or
select Original for those geometries. FP32-min may select the same algorithm
only when it meets that policy's permitted source approximation/operand rules;
changing a `default_mode` predicate alone is not enough. FP64-force remains
Original with exact reuse.

**Ownership/cache/memory:** guard archive belongs to the prepared family, not a
mutable singleton shared across worker calls. Retain bounded immutable summaries
for actual completed observations; cache no winner or threshold. Existing archive
cap is32MiB and whole cache128 entries; these are optional maxima, not per-bank
mandatory reservations. Charge archive rebuilds, retained diagnostics and all
pins. Cancellation follows the same batch rules.

**Numerical/measurement gate:** preserve existing strict comparisons and run the
predeclared threshold-region/hostile campaign below. Specify which intermediate
statistics differ, their bounds, rank/limiter fallback and full-bank scope. No
lost intended acceptance or new false admission in required paired regressions;
statistical uncertainty is still reported. A loose interval that refines nearly
everything fails efficiency, not correctness. If certification costs more than
direct shared computation, select direct. Reviewer checks the result never claims
an Original certificate from chord arithmetic. Rollback is exact family route.

### Card 10 Complete Live sharing, fairness and diagnostic retention

**Purpose/prerequisites:** Card6 plus whichever qualified specialization won.
Full rolling execution is required; one-bank success cannot close this card.

**Read/edit/claims:** `src/live.cpp` Bank/Receiver admission/loans,
`shared_scheduled_capture.hpp`, exact source/permutation/curve/temporal pools,
`scheduled_processing_journal.hpp`, `scheduled_diagnostic_journal.hpp`,
`scheduled_default_point_diagnostics.hpp` and lazy history queries.

**Algorithm:** coordinator gathers already-due compatible consumers from all
required banks into bounded cohorts. A deterministic rotating tie-break among
equally ready banks may govern **numerical dispatch only**. Coordinator consumption,
burst/pending/deduplication order stays the existing Live receiver traversal,
with original per-bank block/lane ordering. Do not introduce a new cross-bank
commit order without an independent proof and observable-event tests. First reserve mandatory
coverage for all required banks; then distribute optional projection/template
cache and scratch. Pressure triggers bounded progress and eviction of unpinned
optional entries, not omission of a key/profile/epoch.

Persist numerical evidence needed by history queries with its exact operator,
capture/source/observation endpoint and journal identity. A query consumes an
immutable result; it must not create events, alter trials or replay the same
Original symbol repeatedly. If an explicit Original diagnostic is requested for
an approximate result, run and account it once under a separate charged recipe,
cache within the retention bound, and keep its cost out of invented decoder
fractions but inside total complete service/memory cost.

**Ownership/cancel/cache:** Live owns cohort loans and cache mutation. No worker
can release capture behind another consumer or change a bank's loan. Wait/join
before retiring banks or leases. Future-consumer null proofs are preserved.
Cancellation preserves committed pending prefixes and invalidates only matching
uncommitted generation. Keep reliable unadmitted chain owners.

**Tests/gate:** multi-key/multi-epoch/automatic-profile overlap, small and generous
valid workspace, owner growth old+new peaks, cache thrash, consumer retirement,
cancel/resume, repeated history polling, all-stream inspection. Positive cross-bank
reuse and bounded queue slope required in a complete run; no cache-hit claim from
the one-bank fixture. Compare all counters/events with scalar under deliberately
different worker completion order. Review mandatory-byte agreement between planner
and runtime. Rollback uses per-bank exact cohorts with optional caches disabled.

### Card 11 Select production Auto from demonstrated complete cost

**Purpose/prerequisites:** Cards6–10 and matching measurements. Consolidate
accidental precedence without deleting reproducible diagnostic routes.

**Read/edit/claims:** `scheduled_acquisition_policy.hpp`, new small
`choose_scheduled_execution` helper, `receiver_strategy.cpp`,
`scheduled_complex_search.cpp`, planner/tuning owners only where the verified
model actually resides, GUI backend/status display tests.

**Algorithm:** separate representation availability (dense/implicit) from numeric
algorithm (direct/family/point), arithmetic and template reuse. Resolve explicit
diagnostic overrides once and emit both requested and effective policy/reason.
Use actual geometry, family size, masks/guards, loan, worker availability and
measured total-cost crossover. Production Auto selects the demonstrated route
for F1/F2 without opt-in switches. Keep fallback truthful and preferred complex
strategy intact. Unsupported CPU estimates stay unavailable until calibrated
against complete matching runs; no coefficient haircut.

**Ownership/cache/precision:** coordinator decision immutable for a prepared
batch; a loan/config change invalidates the pending plan and causes a bounded
replan, not reuse under a new key. No change to public serialization or algorithms
under FP64-force. Inspect actual execution stage types in trace.

**Tests/gate:** override precedence including colored-demand versus detached
batching; Auto/FP32-min/FP64-force positive paths; dense/implicit memory crossover;
empty/unsupported/too-slow distinctions; F1/F2 GUI round trips. Require positive
production path counters and fallback fractions. A flag-enabled success with
Auto still bypassing it fails. Reviewer checks full cost includes cold/rolling,
guards and diagnostics. Rollback retains the preceding selector and exact cohort
override; no historical controls removed until their evidence is reconciled.

### Card 12 Demonstrate rescue, finish local regression and deliver GUI

**Purpose/prerequisites:** Cards0–11, with unsupported contingent branches
explicitly recorded. No new architecture at this stage without returning to its
focused gate.

**Claims/files:** stable candidate source/invariant, claimed Release test/build
tree, fresh artifacts/launchers; serialized measurement resource, native display
and audio device only when exercised. Edit maintained result documentation after
actual outcomes, not beforehand.

**Algorithm:** run matched complete F1/F2 production comparisons, predeclared
moderate-dilution diagnostics, establish F3 and run independently paced multi-cycle
rescue with repeats. Preserve24s backlog and physical deadlines. Run boundary/
sensitivity checks and complete affected local groups. Rebuild GUI at exact final
source/configuration, freeze binary/runtime/source hashes and create launchers.

**Memory/precision/cancellation:** use final configured limits and actual policies,
with caches/counters/history behavior enabled as delivered. Confirm queue, pins,
owners and temporary buffers return to bounded steady state. Test all cancellation
stages with final code. Record actual stage precision and exact-refinement counts.

**Gate:** useful complex continuous F3 is slightly too slow at fixed settings;
scheduled candidate drains intended data and fully scored absence with sustained
margin, no dropped input, bounded queue/memory and next-poll prefixes. Require
complete affected local checks, with inherited failures separately shown and
new failures investigated. Inherited failures prevent an all-green claim; no
deadline/threshold waiver. GUI mock and real sound-card outcomes are separate.

**Failure/review/rollback:** if F3 is not rescued, deliver a manual checkpoint
with rebuilt GUI and exact unresolved limiting work/strategies; label objective
unfinished. Do not claim impossibility or end with only kernel benchmarks.
Independent reviewer checks matched settings, producer independence, every stream,
tail/diagnostic cost and final binary identity. Keep frozen prior launchers and
failed PCM/config/seed. Do not push, publish or dispatch CI.

## Fixture recipes and independent producer

### Fixed acceptance fixtures

| ID | Configuration held fixed | Purpose and explicit limits |
| --- | --- | --- |
| F1 | Built-in Spread: target40dB-Hz, rate360, private auto-keystream, DSSS10/interleave, crystal baseband/GPSDO-XO RF, independent, Carrier30001500/Shift30000000, margin3, budget.6, Default clock/arithmetic, Audio0, fake-FCC, 4W/path60/noise−164; Fs14400, N5120/C8 in retained run | M0/M3/M5 G1 production regression; complete001/absence/drain and overhead, not near-real-time rescue |
| F2 | Weak/User36: target8dB-Hz, rate36, private, DSSS10/interleave, independent GPSDO-XO/GPSDO-XO, same carrier/shift/margin/budget, Default/Auto, Audio0, FHSSoff, workspace50%, duplex; first hold Fs6120, N87040/C34 | Fix the ordinary production failure without lowering target; application/hardware-selected Fs are separate later cases |
| F3 | Current useful continuous configuration established by the bounded selection below | Only rescue fixture: continuous slightly too slow under independent input, then M3/M5 and moderate G at identical settings |
| F4 | F1/F2 relevant geometry M3/G3, FHSSoff then applicable FHSSon | Continuous-limit bypass with original guards and addressing |
| F5 | F1/F2/F3 M5/G3 and internal M5/G4, plus M3/G1/M5/G1 | Duty/contiguity diagnostic, never conceal G1 failure or claim G4 randomness |
| F6 | Built-in EME: auto-pattern, tx-key none, target−3.0720996964786846, rate1, Carrier5800001500/Shift5800000000, bothgpsdo-ocxo, independent, path220, power36.020599913279625dBm, Audio0, DSSSoff, fake-FCC, Default | Separate public/private and propagation study; does not replace F1/F2/F3 |

Common examples set noise−164, margin3, independent reference, zero Doppler,
workspace50%, Default clock/arithmetic and duplex. They preserve selected
key/device/draft and do not explicitly overwrite every receiver/scheduling
control. Pin the effective loaded values, including budget.6 and
frequency-search1. EME's built-in common patch has no explicit propagation offset;
test encryption and offset correction as separate changes.

Use synthetic key bytes `0x67` repeated32 times, payload`001`, epoch1800000000
and MT19937 seed8071 where the retained fixtures specify them. Retain their common
whole-second schedule anchor calculation; do not independently pick a favorable
start for each M. Clean noiseσ=.003 is a functional regression, not target-SNR
sensitivity qualification. F1 uses its `noise example` link-budget setting.

### Current and historical harness distinction

The older `.agent-work/artifacts/sched-moderate-20261014/fixture/user36-multicycle.cpp`
accepts `--sched-hop off` and internal dilution1, disables automatic receive
profiles and hardcodes full-receive Fs6120/rate36. Its moderate scope has120s
backlog/900s workload limits. It is a useful historical diagnostic, not the current
24s production acceptance harness.

Use these preserved current fixture sources as the starting point, copied into
the implementation's newly claimed namespace before any modification:

```text
A/calendar29-live/spread-calendar.cpp
A/calendar29-live/audio-capability-mock.cpp
A/calendar29-live/provider-paced.hpp
A/calendar28-live/user36-calendar.cpp
A/calendar28-live/audio-capability-mock.cpp
A/calendar28-live/provider-paced.hpp
A/calendar28-live/bounded-multicycle.hpp
A/firstbank-calendar26.cpp
A/firstbank-full-profile.cpp
```

Also copy `.agent-work/artifacts/spread-live-fixture-20261014/spread-tail.hpp`.
The Spread source currently includes that header by absolute historical path;
in the copied fixture only, change that include to `"spread-tail.hpp"` and
record the path-only diff. Place these headers beside the copied `.cpp` files
under `$O/fixtures`. The two current provider headers are byte-identical
(SHA256`17ecdd5b1753155b545ab98921f6799d37f2aa55303218308d83008517afa3b4`),
as are both audio mocks
(`63a3beb8e3b45efb51cded45331f42b4ddfc49e30e9af97bce73a31a8af2a04d`);
verify before using one shared copy. Reinspect quoted includes for any new
dependencies. Copying only the `.cpp` and mock would fail to compile.

The current User36 production harness requires `--application-rate yes` even
though it asserts Fs6120. To satisfy the requested fixed-Fs-first comparison,
make one reviewed fixture-only extension permitting `no` in that scope while
retaining the exact final Fs6120 assertion, production automatic profiles,
selected key deduplication, full duplex, Auto/Default and all deadlines. Add a
metadata field `rate_source=explicit|application`. Both variants must resolve the
same Config before comparing. Do not quietly switch to the older driver.

Public dilution parsing rejects4. Add `--internal-group-hops 4` only to a
separately named diagnostic fixture, mutually exclusive with public dilution,
constructing `SchedHopPlan` directly and asserting M5/G4/J0. Keep the ordinary
production parser/GUI/persistence unchanged. Extend that fixture's validation
explicitly; passing a nonexistent public option is not an experiment.

Audio70/default and other rates similarly require separately named fixture
variants where the current strict Audio0/rate assertions intentionally reject
them. Preserve the original F1/F2 assertions and recipes.

Do **not** run historical Python runners in place. Some import old coordination
code, mutate the old session, compile, or overwrite fixed `results.json`, trace,
`.run.json` and temporary paths. Extract inspected argv/environment into a fresh
runner. `run-bounded.py` inherits arbitrary DATAPUMP overrides and has a45s
external kill; it cannot establish production completion.

### Build and compile commands

The following are future execution recipes. Bind the shell's actual cwd to the
checkout. Replace SESSION with the registered implementation ID and claim B/O/
TMPDIR before creating or writing them. Do not reuse an old build owner's tree.

```sh
R=/home/user/___quick/p/_cur/dataPump/pumpModem
SESSION=YOUR_REGISTERED_IMPLEMENTATION_ID
B="$R/build/agents/$SESSION/release"
O="$R/.agent-work/artifacts/$SESSION"
mkdir -p "$O/tmp"
export TMPDIR="$O/tmp"
./build.sh build --build-dir "$B" --build-jobs 2 --jobs 1
```

The build wrapper prepares the application targets and test configuration.
Complete compilation before acquiring/running timing measurements. Use the same
compiler/flags and dependency recipe as the frozen reference. This illustrative
probe command is adapted from the inspected `spread-compile.json`:

```sh
c++ -O3 -std=c++20 -ffp-contract=off -fno-fast-math \
  -DDATAPUMP_FIXTURE_PHYSICAL_FRONTIER=1 \
  -I "$R/include" -I "$R/src" \
  "$O/fixtures/spread-calendar.cpp" \
  "$O/fixtures/audio-capability-mock.cpp" \
  "$B/libdatapump.a" -lcrypto "$B/third_party/xz/liblzma.a" \
  -ldl -pthread -o "$O/spread-current"
```

Use the corresponding copied User36 source/mock for `user36-current`. Record
source/header/core/mock hashes together. Do not combine current headers with an
old frozen core and call it the old executable. Preserve exact historical
executables where available; otherwise rebuild a complete frozen source snapshot.

### Production argv

For each M in0,3,5, sequentially, use fresh trace/output names and a sanitized
environment. The following are exact current fixture spellings, not public GUI
arguments. Replace M/LIMIT/FRESH paths before launching.

```sh
"$O/spread-current" \
  --sched-hop M --operation receive --dilution 0 \
  --rate 360 --dsss 10 --input-rate 14400 --application-rate yes \
  --rf-shift 3e7 --pace 1 --wall-limit 180 --noise example \
  --workspace-bytes 1337640960 --workers auto --strategy auto \
  --timing default --audio-error 0 --block 512 --trace FRESH.jsonl

"$O/user36-current" \
  --operation receive --sched-hop M --dilution 0 \
  --rate 36 --dsss 10 --input-rate 6120 --application-rate yes \
  --rf-shift 3e7 --pace 1 --wall-limit LIMIT --noise clean \
  --workspace-bytes 1337640960 --workers auto --strategy auto \
  --timing default --audio-error 0 --block 512 \
  --capture-scope production-auto-full --processing-scope production-auto \
  --backlog-cap 24 --trace FRESH.jsonl
```

User36 LIMIT is300 forM0/M3 and420 forM5; external supervisor limit is LIMIT+15.
Spread external supervisor limit195. These retain the original24s backlog bound;
the outer supervisor does not extend it. The archived workspace1,337,640,960
bytes is a fixed recipe, not universally equal to50% on another host. First
reproduce it if valid on the current host, then separately pin the actual GUI50%
budget. Never give scheduled reception more than its matched continuous budget.

The archived environment removed all inherited `DATAPUMP_*`, then set
`DATAPUMP_RX_CPU_PROFILE=coarse` and
`DATAPUMP_SCHED_CERTIFICATE_DIAGNOSTICS=1`. Run an unprofiled control with only
the CPU-profile variable removed. The production harness rejects experimental
receiver overrides; keep that protection. Record explicit worker affinity:
`--workers N` in these fixtures constrains available CPUs toN+1. Keep cpuset and
effective worker count fixed across variants rather than relying on `auto` while
host limits change.

A fresh runner must first create a fresh per-run directory under the claimed
`$O/runs/<run-id>` and use `subprocess.Popen(argv, cwd=run_dir,
env=sanitized_env, start_new_session=True)` with absolute executable/input paths
and exclusive new output files. These fixtures require `--trace` to be a local
filename; pass a fresh basename such as `trace.jsonl`. Do not launch them with
the repository as cwd, which would write unclaimed traces into the checkout.
Record monotonic start,
actual exit/status and complete stdout/stderr. On outer timeout terminate only
that owned process group, join it and retain first causal stderr/trace failure.
Never pipe through `tail` or treat a successful logger as a successful receiver.
Sanitize only task overrides; retain required loader/toolchain environment and
record it. Do not dump unrelated secrets from the full environment.

### Existing single bank experimental control

The complete calendar26 result used frozen25 core and this exact diagnostic
configuration. It remains a useful numerical/guard control, never production
acceptance:

```text
--operation receive --sched-hop 3 --dilution 1 --rf-shift 3e7
--pace 0 --wall-limit 180 --noise clean
--workspace-bytes 1337640960 --workers auto --strategy auto
--timing default --audio-error 0 --block 512 --trace FRESH.jsonl
```

```text
DATAPUMP_SCHED_LAZY_ACQUISITION=1
DATAPUMP_FIRST_BANK_ALL_EVIDENCE=0
DATAPUMP_SCHED_CERTIFICATE_DIAGNOSTICS=1
DATAPUMP_RX_CPU_PROFILE=coarse
DATAPUMP_SCHED_COLORED_GENERAL_MEMBERSHIP=1
DATAPUMP_SCHED_LINEAR_GRID=4
DATAPUMP_SCHED_DEFAULT_LINEAR=1
DATAPUMP_SCHED_DEFAULT_LINEAR_PRIMARY=1
DATAPUMP_SCHED_DEFAULT_POINT_FFT=1
DATAPUMP_SCHED_DEFAULT_POINT_BULK=1
DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE=1
DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES=0
DATAPUMP_SCHED_POINT_TEMPLATE_FFT_CACHE=0
DATAPUMP_SCHED_DEFAULT_POINT_COMPACT=1
DATAPUMP_SCHED_POINT_GUARD_PREPASS=1
DATAPUMP_SCHED_DEFAULT_POINT_DIAGNOSTICS=chosen
```

Provenance: `firstbank-calendar26-complete.run.json`,
`compile-firstbank-calendar26.json`, `firstbank-calendar26.cpp`. The exact dated
prior retained broad message-start coverage; whole-point/bulk/compact/archive
were opt-in. A production comparison on the same PCM must separately capture
Auto without these switches and report which operators actually ran.

### F3 selection and sustained rescue gate

The historical4.8GHz lead is documented in
[the earlier checkpoint](scheduled-hops.md#october-14-bounded-acquisition-manual-checkpoint)
and `.agent-work/artifacts/sched-finish-20261014/MANUAL-SCHED-FINISH.md`.
Its unpaced continuous result used856.7409 CPU/133.5796 processing wall s over
98.933333 physical s, while independently paced input hit backlog24s at about
70.88s. That is a lead for current revalidation, not a selected current F3.

The exact historical V7 identities are HEAD`be39636876b3abd87ffd6959e55f3eb4ac43e490`
plus dirty runtime manifest`778169c9964d04cf0a7fb138168b005d6f5cac294af6ce44eaacda7755b685df`,
core SHA256`f9f79bb7e0a44fe8089ce1e22a6552636f565733f2eb84d765ae53ad9eb93435`,
complete executable`704095a4eeec3ad17e09990485316e331bdcb47887c3a8438247b56ec1f4d662`
and paced executable`2f7971e1b878536d29f4de5cc06d39c456be6325757b4024614a4a320856163d`.
Recipes/results are `frozen-continuous-rf4p8G-{job,result}.json` and
`frozen-sustained-rf4p8G-{job,result}.json` in the sched-finish artifact directory.
Historical commands, shown only to define the lead (run copies in new outputs):

```sh
./user36-frozen-v7 --operation receive --sched-hop off \
  --rf-shift 4.8e9 --pace 0 --wall-limit 180 --noise clean \
  --workspace-bytes 1337640960 --workers auto \
  --trace FRESH-CONTINUOUS.jsonl --strategy complex
./user36-sustained-frozen-v7 --operation receive --sched-hop off \
  --rf-shift 4.8e9 --pace 1 --wall-limit 200 \
  --capture-scope sustained-full --noise clean \
  --workspace-bytes 1337640960 --workers auto \
  --trace FRESH-SUSTAINED.jsonl --strategy auto
```

These old binaries accept `off`; that spelling must not be transplanted blindly
to a current public0 fixture. The historical complete source is
`.agent-work/artifacts/sched-calendar-20261013/user36-001.cpp`, SHA256
`4e4b4c16642f220e76acb037b47c857d8ce3f3789d84df113ef3913c7ec54962`;
paced source is `sched-finish-20261014/sustained-source/user36-sustained.cpp`,
SHA256`75d15c59996d68a399e02ed0358d9153c33bbe54e3c65957a6a36f837c3f6b7a`.
Defaults were target8/rate36/DSSS10/Fs6120, carrier1500 plus RF shift4.8e9,
independent GPSDO-XO references, margin3/budget.6, Default/Audio0/FHSSoff,
key32×0x67/epoch1800000000/payload001/seed8071/cleanσ.003 and an M10/G1-derived
common anchor. It used producer512 samples and maximum callback306 samples.
Both original runs report PCM hash7654209635458539950 and165 covered pairs;
the trace selects existing complex. Exact historical workers/affinity/inherited
environment are not fully established, so recapture them for current comparisons.
The historical elapsed/input ratio1.350198 is outside the preferred slightly-slow
band below; a current result in that range may still be reported explicitly as
a harder rescue case, never described as1.1 without measurement.

Before trying a grid of new configurations, inspect that retained run's argv,
loaded Config, source/build manifest and PCM recipe. Reproduce its **current**
continuous geometry with unchanged target and preferred complex strategy. If
unsupported now, report the exact predicate and choose the nearest previously
supported continuous geometry from the retained optimized references; do not
extend unsupported EME or use amplitude as a substitute.

Use this finite selection procedure:

1. One complete unpaced service run of the4.8GHz lead with all expected bits and
   absence. One independently paced run preserves the original24s backlog bound.
   Count service time excluding producer wait and TX synthesis separately.
2. If continuous is already sustainable, examine at most three **predeclared**
   wider carrier/oscillator configurations from actual supported continuous
   references. Keep target/rate/DSSS/payload/workspace/workers fixed during this
   selection. Record the grid before any candidate scheduled results. If no useful
   current boundary is found, F3 remains unselected; do not invent a rescue.
3. Select a continuously correct setting with measured unpaced service ratio
   roughly1.05–1.25 and independently paced positive backlog slope. This is a
   target selection band, not permission to lower coverage or tune thresholds.
   A slightly faster/slower point may be used with an explicit reason and actual
   measured deficit, never hidden.
4. Freeze that continuous Config, worker/cpuset/memory/input rate, source/key/noise
   recipe and full required hypothesis domain. Scheduled M3/M5 may legitimately
   add aperture-required pairs; report their count and cost. Hold everything else
   fixed. Use M5/G3 and internalG4 as diagnostics after ordinaryG1 results.
5. Require at least three message/absence cycles using the new fixture contract
   below. A hop cycle and a late processing-frontier witness are not a completed
   reception. Use frozen source payloads/timestamps and the original six-second
   observed-absence rule. The producer runs1× independent of decoder returns;
   a separately labeled accelerated run uses the same per-M PCM to measure
   service, not hardware capacity.
6. Run paired/order-controlled repeats. First two complete scheduled repeats and
   two continuous controls (ABBA where runtime permits); if dispersion can change
   the conclusion, add one predeclared pair rather than choose the favorable run.
   Report n/range. Existing matching baseline evidence may replace redundant runs.

### New F3 multi-message fixture contract

Implement a separately named `f3-cycles.cpp` from the current provider-paced
User36 driver. Its proposed `--cycles 3` means three distinct `001` transmissions
and three physically completed receptions in **one uninterrupted Live session**.
The existing `bounded-multicycle.hpp` checks late frontier windows within one
capture; it does not supply this feature. Preserve the original one-message
fixtures. This new fixture needs timeline planning, a read-only production
boundary-audit adapter and an expectation table keyed by cycle/reception identity.

Use the existing deterministically selected common first anchor A0 from epoch
1800000000. For each M/G independently, precompute the following recurrence:

1. For message i, set ordinary TX timestamp to `Ai/Fs` and
   `stream_phase_samples=Ai%Fs`. Use unchanged `transfer::seeded_config` and
   `transfer::binary_transmitter({0,0,1},options)`. Record every private
   `symbol_stream_address`, masked bit, actual `physical_output_origin()` Oi and
   actual TX end `Ti=Oi+total_samples()`; preserve the entire emitted waveform.
2. Let N be the actual source-symbol sample count and `K=ceil(6Fs/N)`, as in the
   existing fixture. For split scheduling, map the anchor source coordinate plus
   `3N` and `(3+K)N` with `source_to_physical(...,true)`. For continuous/packed
   cases reuse the existing symbol/calendar endpoint helpers. Never add source N
   directly to a split symbol's physical start. Keep all three bit endpoints.
3. Compute Di, the latest required complete-observation fence over the admitted
   timing/phase/clock cells for this payload and its K absent symbols. Use the
   production boundary construction, including canonical-cell upper endpoint,
   timing radius, rate corners and actual completion/lookahead dependencies.
   For a split lane whose corresponding first message symbol is index jl, the
   symbol fence is `ceil(catalog.boundary(lane,(jl+3+K)*N,true,cursor))`, converted
   from its bank-relative capture coordinates to the absolute timeline. Derive
   jl from the actual lane/source anchor; do not assume all banks start at zero.
   The adapter must audit equivalent complete-observation rules for other
   production paths and report each contribution. Do not guess a40-second tail
   or derive Di from a candidate's successful reception. An END fence never
   substitutes for actually scored absence.
4. Let `Fi=max(Ti,Di)`. Select Ai+1 as the earliest deterministic whole-second
   candidate whose ordinary TX origin is at least Fi and whose first payload
   placement is valid for that variant. Bound the forward search to600 seconds;
   fail the plan if none exists. Record the exact rounding/calendar placement gap.

Share A0 and this recurrence across M0/M3/M5, **not a later anchor grid derived
from the slowest schedule**. That would add scheduled-duration idle gaps to the
continuous control and could hide its deficit. Freeze one timeline/PCM manifest
per M/G. At a fixed M/G, preceding and candidate receivers replay identical
bytes/timestamps; cross-M comparisons match source workload/settings while
reporting their necessarily different physical timelines and private addresses.
Reconfirm the continuous processing deficit on the final three-cycle fixture.

Precompute the finite capture before timed reception. Reject overlapping complete
TX waveforms. Add ordinary channel noise at **every capture sample**, including
all transmitted samples, with one continuous PRNG stream from seed8071 through
the final endpoint `F_last`; never reset noise per message. Add no
`max(capture_end,150Fs)` padding or extra sustainability tail.
Date all input as `capture_start+sample_index/Fs` with the same declared quality.
The independent producer never waits for a decoder, completed reception or queue
space. After the final sample allow computation drain without adding samples or
counting drain wall time as physical exposure.

Replace single-stream `ProbeRows` with a fixed three-cycle expectation table.
Each cycle must expose exact successive `001` prefixes under its stable
reception identity, then complete no earlier than its required absence endpoint.
Keep every unexpected stream/admission and every first failure; later correct
cycles cannot erase earlier wrong output. Do not restart or clear Live between
cycles. Continue unchanged queue, producer, pending-poll and cancellation checks.

Proposed new interface, not options accepted by the old fixture:

```sh
"$O/f3-cycles-current" \
  --operation plan --cycles 3 --cycle-plan "$O/plans/f3-m3.json" \
  --sched-hop 3 --dilution 0 --rate 36 --dsss 10 \
  --input-rate 6120 --application-rate no --rf-shift 4.8e9 \
  --workspace-bytes 1337640960 --workers N --strategy auto \
  --timing default --audio-error 0 --noise clean --block 512

"$O/f3-cycles-current" \
  --operation receive --cycle-plan "$O/plans/f3-m3.json" \
  --pace 1 --backlog-cap 24 --trace trace.jsonl
```

Use the selected F3 shift and fixed worker count, rather than these lead
placeholders. Create claimed fresh plan/run directories first and launch with
the per-run cwd rule above. The plan freezes loaded Config, all addresses,
anchors/origins/endpoints, seeds, PCM/source hashes, worker/cpuset/workspace and
the expected producer/observation deadlines. Record reference/candidate binary
hash separately; receive rejects incompatible input/config overrides. Create
equivalent M0/M5/G3 plans; G4 needs its separately named internal diagnostic
option. The new fixture permits fixed Fs6120 explicitly while retaining the
same final-geometry assertion and production receiver/profile behavior.

Bound this fixture to900 physical seconds. Its workload deadline is
`ceil(capture_samples/Fs)+30` seconds, with owned-process external watchdog15
seconds later. The unchanged24-second backlog bound and individual physical
assertions still apply. If planning exceeds scope, report the actual endpoint
and blocker; do not shorten absence, lower cycle count or silently extend it.

### Practical margin and producer audit

Proposed practical margin gate: candidate's upper observed complete service ratio
across accepted repeats≤.85, no monotonically growing queue after startup, no
producer slip/drop/saturation, and peak backlog≤the unchanged24s bound. Report
the actual remaining margin if .85 is not met; a marginal sustainable result is
useful evidence but not the intended comfortable rescue. Assess queue slope on
each post-startup cycle and over the concatenated run, with endpoint confidence
or range and maximum transient age. Do not fit away a positive trend.

Producer audit must record intended/actual enqueue timestamps, deadline misses,
queue capacity, saturation, dropped input, peak age and wakeup/blocking time.
The producer never waits for free decoder capacity; saturation is an explicit
failure. Separate synthesis from measured receive work or precompute identical
PCM into a bounded queue/backing fixture, with no secret production capture.
Validate that queue mutex contention cannot backpressure its schedule. Record
the decoder's physical capture frontier separately from its processing frontier.

## Critical path and bounded decisions

| Step | Initial bounded work | Result that changes the plan | If inconclusive |
| --- | --- | --- | --- |
| Freeze/census | Existing artifacts + one F1/F2 prefix and M3/G3 control | Different runtime identity/route invalidates transferred claims | Reconcile only changed inputs, then one rerun |
| Attribution | One original-bound F1 prefix, one guard control, hostile case | Dominant cost chooses Card7 versus8 after exact cohort | Keep unresolved bucket explicit; measure Card6 total, no endless profiling |
| Exact extraction | One C8 and C34 original event with carry/fence edges | Any field/event difference blocks parallelization | Isolate first divergent row/owner, fix extraction |
| Cohort sharing | 1/2/4/8 jobs on same immutable tiles, then complete F1/F2 | No real sharing or no aggregate gain rejects current grouping | Inspect unique identities/miss costs once; retain exact direct fallback |
| Guard/FFT specialization | One representative complete family including every guard | Preparation/edge/fallback dominates, or equivalence false | One alternate bounded tile/direct strategy, then quantified scope limit |
| Production gate | F2 complete drain; F1 complete useful overhead | Auto no-op, hidden amplitude, nondrain or changed fixture fails | Preserve failure and dominant cost; return to responsible card |
| Dilution/F3 | Brief G3/G4 family probes, then selected F3 multi-cycle | OnlyG4 works orG1 fails remains partial; no secret-calendar claim | Report remaining gap and measured overhead budget |
| Boundaries/delivery | High-risk pairwise matrix, complete affected local groups, GUI | Regression returns to focused test before revalidation | No overall pass until actual statuses known |

Do not spend the whole implementation pass on counters, caches or primitive
benchmarks while leaving Original replay untouched. Conversely, do not discard
the existing FFT/guard machinery before a census proves what it actually executes.
If the required cohort cannot fit the minimum valid workspace, compute the exact
mandatory+simultaneous byte lower bound and show the first failing allocation.
Try cohort1/streamed planes/direct fallback; do not silently omit a bank. A
resource blocker is specific, not a proof that scheduling is impossible.

## Boundary and failure handling

### Canonical cells and GCD

For split geometry the unrefined phase step is
`gcd(N,Fs,floor(2Fs/5),ceil(2Fs/5))`; when Fs%5≠0 the adjacent hop lengths force1.
Continuous geometry may use `gcd(N,Fs)`. Thus Fs6120/N87040 has continuous680
versus split136; Fs6121 makes both1. With N=14*Fs at Fs6121, continuous is6121
and split1. These are geometry facts, not measured cost claims.

Do not round N/Fs to a favorable GCD. Test neighboring N, Fs6120/6121, fractional
final chips and original coverage. Packed phases are exact slot starts through
`for_each_packed_scheduled_phase`, not a full Fs sample scan. Complex phase
refinement may choose the largest divisor no greater than flat/8; retain its
coverage and boundary behavior.

The recorded Spread phase step320 has319 samples of cell padding, around224
origin cells and19 paired alternatives in one M3 family; some epochs have18
phases, and M5 has37 pairs. The representative also chooses a central hop mask.
The prior correction is not proof of exact within-cell mask equivalence. Test
starts at representative, +1, midpoint and upper edge, paired with opposite
timing/rate bounds and both sides of ramp/null transitions. Compare full Original
and new statistics, accepted prefixes and physical completion. If the old central
mask itself loses valid coverage, retain the failing paired capture and request a
specific representation correction; do not erase padding to make counts smaller.

### Default70 short symbol cliff

`tests/test_scheduled_point_source_nodes.cpp` uses Fs8000/C32/N2048: symbol256ms
plus32ms support at each side needs320ms. `packed_scheduled_symbols` selects
packed when short-symbol guaranteed flat fraction is below.5; the packed calendar
then cannot fit full support in roughly270ms at Default70.

Binary64 policy inputs followed by the source's ceilings predict left161,
right721 and81-sample ramps, flat2156 samples at Audio70; Audio50 predicts
right561/flat2316. These exact numbers are a read-only arithmetic prediction,
not a compiled result: assert actual `SchedHopSourceMap` endpoints. The respective
worst flat fractions are .490234375 and .568359375. Do not recompute ideal-real
guards and expect equality at the boundary.

Receiver numerical changes cannot repair this pre-receiver constructor rejection.
Preserve the original failing regression and mark the limitation. A separately
named valid Audio50 cache test may add coverage, but cannot replace or turn the
Default70 failure green. Add an explicit cause/endpoint diagnostic if useful.
Do not lower.5 or truncate pulse shoulders.

If pursuing a stronger usable-energy proof, make it a separate bounded proposal:
enumerate all admitted starts and exact shaped waveform/gate energy for the
existing geometry, include independent private bit rows and worst support edges,
derive a conservative lower bound and compare both detection and outer/colored
guards. A sampled favorable energy sweep is not a universal proof. Any change to
TX placement/waveform needs explicit separate justification/authorization; this
receiver optimization plan does not authorize it. A genuine unsupported geometry
must remain an honest error.

### Geometry coverage matrix

Fill actual route and coverage for each applicable row; unsupported continuous
geometry is not silently included in the rescue claim.

| Dimension | First cases | Later high-risk interaction |
| --- | --- | --- |
| DSSS | Off,10 with actual C8/C34 |100/1000 where continuous is supported; fine chips with outer guard |
| Source | Private bit0/bit1 and public | Two independent keys, same calendar/different private layers |
| Shape | Full chips, partial final chip | Pulse endpoint, limiter transition and source/carry boundary together |
| Guard | One-section coherent+colored+outer | Four quarters, differential windows, missing section rank |
| Schedule | M3/G1, M5/G1, M3/G3 | M5/G3/internalG4, unequal null gaps, FHSS product ramps |
| Time | Audio0/default70/explicit50 | Provider radius aboveA/2, rate uncertainty, non-grid phase cells, offset/region |
| Arithmetic | Default, FP32-min, FP64-force | Range underflow/overflow, zero/near-singular Gram, portable versus AVX2 |
| Memory | Generous fixed and tight valid budget | Many keys/rolling admits/owner growth+cache eviction+parallel scratch |
| Input | Clean/near-threshold intended | Tone, wrong key, clipping, colored bursts and phase diffusion |
| Lifecycle | Several dwells and complete absent symbols | Cancellation at each stage, repeated history queries, discontinuity generation |

Use pairwise/high-risk combinations, not an unbounded Cartesian campaign. List
unexercised dimensions and reason. A one-section point test does not qualify
quarter/differential geometries; zero/near-singular cells need original rank-one
handling rather than an artificial zero observation.

### Empty epochs and pressure

Distinguish: proved-empty admitted start set; unsupported coverage; insufficient
mandatory bytes; optional-cache pressure; sustainable-compute failure. Empty
qualified epochs allocate no private rows/transforms. Nonempty required epochs
remain covered even when optional acceleration is evicted. Planner and runtime
must agree on mandatory/carry/chip storage and growth overlap. A bank fitting
alone does not prove the whole rolling cohort fits.

### Hostile inputs and diagnostics

Predeclare strong unrelated private keys, tone/narrowband disturbance, clipping,
near-threshold intended signal and quarter-localized energy. Keep the correct
Data calendar in at least one wrong-private-key test to force realistic survivors.
Measure tail CPU, refinements, peak queue and memory. Family-wise exact fallback
must stay bounded even when most lanes survive. A fast noise-only screen is
insufficient.

Repeated scope/history polling must not recreate old receiver events or trigger
unbounded repeat replay. Preserve diagnostic result identity and first causal
terminal failure. Trace drops/overflow/finalization failures invalidate timing
evidence, not necessarily the decoded bits; report both separately.

## Numerical and sensitivity validation

First compare the literal Original scalar extraction field-by-field. Then compare
new shared arithmetic per piece/bit/guard to the supported Original oracle,
followed by the complete event stream. Preserve all existing tolerances and
assertions. Skip77 or a missing actual Original path is omitted evidence.

The current Original reference gate in `pattern_correlator.cpp` requires glibc,
GNU compiler≥14, x86_64, no FAST_MATH and the coordinate-environment check.
Several other helpers require IEEE binary64 and a sufficiently wide long double.
Check the **executed** environment predicate. A platform name is insufficient.

Useful existing bounded diagnostics after their prerequisite build:

```sh
DATAPUMP_SCHED_GUARD_ONLY=quarter "$B/test_sched_hop_correlator"
DATAPUMP_SCHED_GUARD_ONLY=common-simd "$B/test_sched_hop_correlator"
DATAPUMP_SCHED_GUARD_ONLY=affine-reuse "$B/test_sched_hop_correlator"
"$B/test_scheduled_default_linear_original" --paired-seeds
"$B/test_scheduled_default_linear_original" --long-case 0 --q both --slow-row
"$B/test_scheduled_default_linear_original" --long-case 18 --q both --bulk
```

Each guard selector runs only that diagnostic; run the ordinary test afterward.
Long cases0–19 use Fs6120/C34/N87040/M3/DSSS10, seven hypotheses and one source
symbol. Cases16/17/18/19 cover noise/clipping/tone/strong wrong key. `--q both`
compares Q4/Q8 on identical PCM. `--measure-only` removes some no-loss/wrong/noise
assertions and is excluded from acceptance recipes. Seven lanes or two Q choices
are not independent captures. This is not full-bank/END/BER qualification.

Run this finite campaign for a proposed numerical approximation or reordered
arithmetic, keeping original full calibration gates separately:

1. First long cases0,7,12,15,16,17,18,19 and a supported actual Original C8 case.
   Inspect every component and first divergent row; run a complete receive case.
2. Use a disjoint small pilot to choose three energy points around baseline
   low/middle/high detection probability. Freeze levels and seeds before examining
   candidate results; never retune away a loss.
3. Run64 independent paired PCM captures per level. Extend the middle point to256
   after the candidate is stable. Record harmful and beneficial discordances,
   exact prefixes, wrong admissions and completion, not just longest correct stream.
4. Run64 independent captures per hostile class: noise; wrong private key;
   off-frequency tone; colored/narrowband disturbance; burst/clipping. Use the
   same thresholds and all enabled guards.
5. Run a separately predeclared complete production-bank cohort of at least16
   captures for each F1/F2 if it fits the local bounded campaign. If infeasible,
   report exact executed counts and full-bank sensitivity as unqualified; do not
   substitute seven selected lanes. This limit does not excuse incomplete required
   correctness or sustained-rescue runs.

The capture/config/seed is the statistical unit. Correlated bits and hypotheses
do not increase independent n. Report paired discordant counts and an appropriate
paired interval; separately report BER with its dependence caveat. With zero
harmful paired discordances the one-sided95% exact binomial upper bound is
`1-.05^(1/n)`: n16≈17.1%, n64≈4.57%, n256≈1.16%, n1024≈.292%.
These are bounds on harmful-discordance probability, not dB loss. A dB claim
requires a response curve and uncertainty. See the primary
[NIST exact binomial interval reference](https://www.itl.nist.gov/div898/software/dataplot/refman2/auxillar/exacbici.htm).

This small campaign can reject a bad approximation; it cannot automatically
establish negligible loss. Keep measured additional loss, inconclusive results
and unqualified full-bank scope separate. Existing amplitude penalties do not
bound complex approximation loss.

## Build and test matrix

Valid wrapper groups are `contract regular fast legacy gui frontends web native
packaging build all`. There is no `scheduled` group or wrapper regex option.
Use `./build.sh` for initial configure/application build, then the documented
direct CMake prerequisite build plus CTest for a narrow regex. CTest never rebuilds.

```sh
cmake --build "$B" --config Release --parallel 2 --target \
  test_sched_hop test_scheduled_symbol_calendar test_sched_hop_transmit \
  test_scheduled_complex_search test_scheduled_acquisition_catalog \
  test_scheduled_clock_reference test_clock_sync test_pattern_start_geometry
ctest --test-dir "$B" -C Release --output-on-failure --no-tests=error \
  --parallel 1 \
  -R '^(sched_hop|scheduled_symbol_calendar|sched_hop_transmit|scheduled_complex_search|scheduled_acquisition_catalog|scheduled_clock_reference|clock_sync|pattern_start_geometry)$'
```

For each subsequent row, build the actual registered prerequisite targets before
running its exact CTest names. `cmake/TestGroups.cmake` and the newly configured
CTest inventory are authoritative; the old development-doc phrase “up to39”
contract tests is stale. Do not infer total coverage from a historical count.

| Change | Focused existing tests to preserve and extend |
| --- | --- |
| Schedule/timing/catalog | `sched_hop`, `scheduled_symbol_calendar`, `sched_hop_transmit`, `scheduled_complex_search`, `scheduled_acquisition_catalog`, `scheduled_clock_reference`, `clock_sync`, `pattern_start_geometry`, `sched_capture` |
| Split full detector | `sched_hop_complex`, `sched_hop_correlator`, `scheduled_pulse_moments`, `scheduled_mapped_piece_groups`, `scheduled_frequency_fusion`, `scheduled_frequency_piece`, `scheduled_acquisition_quarters` |
| Gram/fractional/original | `scheduled_origin_gram`, `scheduled_origin_gram_family`, `scheduled_origin_gram_bulk`, `scheduled_origin_gram_original`, `scheduled_fractional_basis`, `scheduled_fractional_basis_runtime`, `scheduled_fractional_basis_original`, `scheduled_linear_grid_original`, `scheduled_default_linear_original` |
| FHSS/point/full guards | `scheduled_default_linear_fhss`, `scheduled_default_linear_fhss_original`, `scheduled_default_point_fhss`, `scheduled_default_point_fft`, `scheduled_default_point_fft_runtime`, `scheduled_default_point_bulk`, `scheduled_default_point_bulk_original` |
| Guard archive/edges/reference | `scheduled_default_point_guard_edges`, `scheduled_default_point_guard_edges_runtime`, `scheduled_default_point_guard_archive`, `scheduled_default_point_guard_archive_runtime`, `scheduled_default_point_reference_recipe`, `scheduled_point_guard_prepass`, `scheduled_point_guard_prepass_runtime`, `scheduled_point_source_nodes` |
| Original owned/window work | `scheduled_owned_fft_window_filter`, `scheduled_owned_fft_window_hook`, `scheduled_owned_moment_runtime`, `scheduled_owned_moments`, `scheduled_sparse_owned_spans`, `scheduled_owned_moment_correction`, `scheduled_owned_field_errors`, `scheduled_sparse_carrier` |
| Capture/source/cache/parallel | `shared_scheduled_capture`, `scheduled_exact_source_samples`, `scheduled_private_batch`, `scheduled_parallel_preparation`, `scheduled_capture_layout`, `scheduled_carrier_restart`, `scheduled_original_projection_cache` |
| Journal/cancellation/diagnostics | `scheduled_processing_journal`, `scheduled_cancellation_resume`, `scheduled_detached_replay`, `scheduled_detached_resume`, `scheduled_diagnostic_journal`, `scheduled_certificate_diagnostics`, `timing_continuation` |
| Workspace/owner/loans | `scheduled_acquisition_memory`, `scheduled_memory_projection`, `scheduled_workspace_accounting`, `live_workspace_capacity`, `live_resources` |
| Whole Live/progress | `sched_hop_receiver`, `scheduled_lazy_acquisition`, `scheduled_physical_multidwell`, `live`, `live_profiles`, `live_exact_source`, `live_receptions`, `gui_scheduled_cold_start` |
| Continuous/native arithmetic | `pattern_receiver`, `pattern_correlator`, `pattern_fft_batch`, `pattern_correlator_batch`, `receiver_strategy`, `search_arithmetic`, `search_fft`, `pattern_drift`, `pattern_differential`, `receiver_differential` |
| Configuration/estimates/UI | `gui_launch_command`, `gui_launch_settings`, `gui_controller`, `gui_link_planner`, `link_planner_curves`, `simulation_estimate`, `gui_application`, `live_planning_link`, `cli` |
| Independent wire/pending contract | `compression_short`, `transfer`, `stream_codec`, `stream_receive`, `recovery`, `attachment`, `gui_inspection`, `gui_binary_editor` and contract DSP tests |

New tests for Cards3–10 must be added to the appropriate normal groups; they do
not replace the independent preserved tests. Read each test's actual scope before
counting it: `sched_hop_receiver` includes amplitude fixtures; a green file name
is not proof of split complex execution.

Most target names are `test_<CTest name>`. Exceptions include `live_exact_source`
which builds `test_live_profiles` and runs `exact_source`; `gui_inspection` builds
`test_inspection`; `gui_binary_editor` builds `test_binary_editor`; `cli` requires
`pump` and registered Python support. Curve variants may share one target.

After the final runtime candidate is stable, run the normal complete local
general regression, not repeated calibration during diagnosis:

```sh
./build.sh test all --build-dir "$B" --build-jobs 2 --jobs 1
```

This is the straightforward configured non-display inventory and avoids repeating
overlapping contract/regular/gui groups. Alternatively select their union from
the current group inventory and record every member/status; do not omit tests
because another group sounds comprehensive. Keep normal calibration including
`receiver_probability`, `differential_probability`,
`differential_receiver_probability` and `weak_signal` gates. `devfast=true` is
only a focused diagnosis option, never final coverage. No hosted/platform/SDK
qualification is added by this local checkpoint.

Run applicable local native GUI checks separately from timing measurements:

```sh
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=1 \
xvfb-run -a ./build.sh test native --build-dir "$B" \
  --build-jobs 2 --jobs 1 -- -DGUI_SMOKE_TIMEOUT=600
```

Claim display/resource and use an available qualified local backend. Missing
display/Xvfb means zero executed native assertions until repaired or reported.
Shared GUI tests need no display but do not establish native rendering. Package
or sanitizer checks are scoped to changed responsibilities and run in separate
claimed trees; they do not justify an hours-long platform/SDK campaign here.

Many actual Original tests use exit77 with CMake skip handling. Record skips by
name/reason and supply a bounded valid Original case for each new numeric route.
`scheduled_default_point_bulk_original` currently returns77 on unsupported oracle
environment but lacks neighboring CMake skip registration: preserve actual status;
a harness-only correction cannot create executed numerical evidence.

External timeout, incomplete drain, setup failure, queued/cancelled job and
incomplete assertions are not passes. The typed exit75 progressing-budget warning
policy belongs to specified CI smoke wrappers, not arbitrary local numerical or
calibration tests. The known360s preparation timeout remains incomplete. Do not
extend individual physical/backlog/cancellation deadlines to clear a red result.

## GUI and manual testing checkpoint

Rebuild from final source with the application wrapper and record the GUI binary,
runtime collection, build-info/compiler/environment and source hashes. Save new
normal, verbose and debug launchers in the claimed delivery directory, pointing
to that exact binary. Preserve preceding launchers. Shell launchers use safely
quoted absolute arguments or a Python argv array, never concatenated untrusted
command text.

F2 launch arguments, with an absolute synthetic keyfile and named Default key:

```sh
"$DELIVERY/datapump-gui" \
  --pattern auto-keystream --tx-dbm 36.020599913279625 \
  --path-loss-db 180 --noise-dbm-hz -164 \
  --oscillator gpsdo-xo --rf-oscillator gpsdo-xo \
  --shift 30000000 --search-margin 3 --reference independent \
  --target-snr 8 --rate 36 --carrier 30001500 --doppler 0c \
  --dsp-workspace 50% --clock-sync default --audio-error 0ms \
  --dsss-mode interleave --dsss-factor 10 --dsss-version interleave \
  --search-arithmetic default --receiver-strategy auto --receiver-cpu-budget 0.6 \
  --live-duplex yes --sched-hop-dilution 0 --sched-hop 3 \
  --sched-frequency-search 1 --fhss off \
  --keyfile "$SYNTHETIC_KEYFILE" --key-name Default --tx-key named
```

Use the same profile for verbose with
`--verbose --verbose-file ABSOLUTE_FRESH_TRACE`. Use debug with
`--debug --debug-bits 001 --debug-timeout 300` and the same verbose arguments.
Fresh trace filenames are required because trace creation is exclusive.

For F1 change only the defined example fields: target40, rate360, path60,
baseband crystal, FHSSfake-fcc; preserve its loaded exact profile and compare
exported/runtime values. Supply M0/M3/M5 variants as argument presets, not hidden
environment switches. Include a normal manual launch that does not auto-transmit.

Actual debug limits from `src/gui/application.cpp` and `debug_live.hpp`:
1–16 raw bits, timeout10–300s, explicit keyfile/named TX key, forced duplex,
incompatible with Simulation/smoke/self-check. It checks all receptions and
physical completion plus capture advancement during TX, then settles two seconds.
Watchdog exit124 at timeout+15 is failure. UTC fallback can return2 and is not
requested UTC qualification. A full text message needing longer than this limit
requires a separately bounded GUI driver; do not claim `--debug-bits 1` tested it.

Verify actual example loading and atomic failed import; target persistence and
argument ordering; TX start; actual complex backend and honest estimate status;
stable pending row and each next-poll bit; observed absence and drain. Keep first
causal terminal error. Trace capacity is256 records, max16KiB per record and
max256MiB file; retain stderr's trace-finalization seal and dropped/overflow
counts. Missing finalization makes trace evidence incomplete.

Run coordinated sound-card testing if available, using existing routing. Report
separately: mock/synthetic, native GUI, real sound-card path, acoustic loopback
if actually used, and timing qualification. Do not change mixer/driver routing
without authorization. Distinguish audio discontinuity, unavailable device,
timestamp uncertainty and processing backlog. If hardware is unavailable, deliver
the rebuilt GUI/launchers and state that limit; no mock-as-hardware claim.

## Strategy disposition matrix

Status is at this planning baseline. Update each row with actual implementation
evidence; a slower microprototype does not reject its whole strategy.

| Strategy | Current disposition | Next decisive action or bounded exclusion |
| --- | --- | --- |
| Shared temporal outputs | Implemented in eligible normal/mapped and experimental lazy families; not demonstrated effective in F1 production | Cards6/8 on actual C8/C34 groups, all guards and setup included |
| Multi-dwell partitioning | Point streaming already appends disjoint multi-dwell fragments | Inspect caller repetition/tile cost; test bounded active tiles, not another claim that multi-dwell is new |
| Active-source compaction | Exact phase/map/noise equivalence not established generally | Keep physical tiles; consider only proved piece mappings, no PCM resampling without covariance derivation |
| Integrated guards | Existing point event/archive/summary machinery; full Original replay still dominates important scopes | Cards6/7 compute complete numerical state and reuse public pulse work; require positive guard adoption |
| Original refinement family batching | Detached/parallel preparation exists; colored-demand precedence may bypass batching | Cards3–6 make exact complete cohort fallback unavoidable for selected route and measure survivor storms |
| Frequency-family work | Prepared kernels/fusion implemented;43× kernel is not receiver gain | Card8 includes full real Gram and guards, actual pair count and preparation |
| Cross-rolling input reuse | Shared bytes/projection mechanisms exist; small experiment had zero hits | Card5 immutable absolute tiles + Card10 aligned actual consumers; measure complete reuse/memory |
| Exact private reuse | Permutation/address/source reuse implemented in eligible paths | Bind into lazy job lifetime, retain paired-bit/physical address identity and count genuine repeated rows |
| Native arithmetic | Native FFT tiles and point products exist; curve/packed/combined prototypes slower in named complete fixtures | Use native operands only after changed batching has full-cost and numeric advantage; no unchanged repeat |
| Moderate dilution | M5/G3 valid public internal mapping; M5/G4 possible programmatically | F5 diagnostic with actual duty/energy, full guard/frequency domain; G4 has one placement only |
| Dense/implicit crossover | Auto uses mandatory availability, not measured complete speed | Card11 measured crossover with shared loan and all required banks, retain honest model-unavailable state |
| Boundary/limiter handling | Existing Original and several certificates, scope-specific validity | Partition/support/carry proof with exact cohort edge fallback; compare full fields, not raw dots |
| Diagnostics | Journals/selected-operator diagnostics exist; repeated Original replay can add cost | Card10 retain immutable numerical results, account explicit Original queries and final retirement |
| INT8/FP8/mixed small pieces | Current tiny affine pieces/weighted widths and setup make existing routes unsuitable | No FP8 emulation. Revisit only if new long regular cohorts admit a full cost/overflow bound; secondary to exact/FP32 work |
| Encrypted EME | Separate public/private aperture/support question | F6 after ordinary regressions; no replacement for F1/F2/F3 |

No row is universally proved impossible. A quantitatively excluded scope must
state exact geometry, output/reuse count, required full-detector operations,
workspace/memory traffic and measured lower/upper cost bound. An applicable major
strategy left unattempted without a bound or concrete blocker keeps the objective
open. Another bottleneck alone is not a reason to leave a cheap material gain
unexamined.

## Stop and escalation rules for Sol

- Revalidate baseline and the relevant API/invariant before each edit. If they
  changed, reconcile the new source/evidence and retain unrelated work.
- Do not replace fixtures, lower target SNR, drop hypotheses, enable amplitude,
  increase the fixed workspace or weaken assertions. Do not turn a failed prefix
  into throughput by changing its denominator or adding silence.
- A no-op accelerator, skipped oracle, incomplete drain, non-independent producer
  or changed loaded Config fails its gate. Preserve the exact reason and follow
  the specified fallback/diagnostic branch.
- When a proposed equivalence is false, retain exact Original cohorts. Record the
  smallest counterexample: identity, rate/phase/mask, first differing row/state,
  expected versus actual dependency. Escalate that concrete mathematical issue
  for stronger-model review before replacing the detector or claiming equivalence.
- If complete cost fails, identify actual limiting work, relevant Amdahl/byte
  bound and the next applicable strategy from the matrix. Do not claim a planner
  estimate, kernel gain, one-bank result or parallel CPU redistribution as rescue.
- Keep first causal error, failing PCM/hash, seed, configuration, selected path
  and complete subprocess status. Do not overwrite original evidence.
- Exact event/trial/threshold/carry/physical-end responsibilities stay explicit.
  No worker `complete()`, silent threshold change, new wire bits or unsolicited
  transmitter/placement/waveform redesign.
- Finish the authorized local implementation/checks and GUI rebuild. Hardware,
  long-duration, ARM, broader platform/SDK and hosted qualification remain
  explicitly deferred. If required local checks fail, report actual failures and
  deliver a truthful manual checkpoint rather than claiming all-green completion.
- A negative result is scope-specific. State coverage, accuracy/workspace
  constraints, measured limiting cost, tested/rejected strategies and remaining
  blockers. Do not assert impossibility for all possible receivers.

## Required implementation report and machine readable outputs

Use a fresh delivery directory with immutable run manifests and no-replace log
names. Keep the maintained report concise but link exact local raw evidence.
Every measured row includes `status=passed|failed|incomplete|skipped|untested` and
the first causal reason. Never encode a skipped scope as a zero-cost pass.

Suggested output inventory:

```text
identity.json                  source/index/dirty inputs, binary/dependency hashes
environment.json               compiler, flags, CPU/cpuset, workers, memory, env
fixtures.json                  exact config/argv, synthetic seeds, PCM identities
census.json                    all banks/epochs/lanes, backend/precision/reasons
runs/<unique-id>.json           actual status, counters, complete measurements
runs/<unique-id>.stdout/stderr  retained first failure and finalization
runs/<unique-id>.jsonl          bounded trace with seal/drop metadata
statistics.json                full field errors, paired outcomes and uncertainty
tests.json                     configured inventory and every actual outcome
strategy-disposition.json      measured/rejected/bounded/open per matrix row
datapump-gui + runtime/         exact rebuilt manual candidate
launch-normal                  explicit final profile, no automatic TX
launch-verbose                 same profile, fresh trace name
launch-debug                   same profile, bounded001 debug
MANUAL-CHECKPOINT.md            honest result and remaining work
```

The report must answer all of these, with source/build/fixture identity next to
each claim:

1. F1/F2/F3 load, TX, RX, every stream, accepted prefixes, END and drain; first
   failures and actual selected complex route. State whether rescue passed.
2. Optimized continuous versus frozen preceding scheduled versus candidate:
   complete CPU and elapsed service; producer waiting/synthesis separately;
   CPU/source, CPU/active and CPU/physical; actual duty/energy, k and margin.
   Failed prefixes retain their own work/frontier and are not normalized totals.
3. M3/M5 G1, M3/G3, M5/G3/internalG4 results, placement limitation and extra
   aperture-required hypotheses versus cost per hypothesis.
4. Exclusive component CPU, unassigned/observer cost, serial commit and worker
   utilization; rejection/refinement reasons and costs for survivors and rejects;
   actual input/source/guard/cache reuse and fallbacks per bank/family.
5. Queue age/slope/peak, intended/actual producer cadence, drops/saturation,
   startup/first acquisition/per-bit publication latency, final drain/cancellation.
6. Mandatory/optional/peak/steady bytes and RSS, including queue/PCM, projections,
   owners, old+new growth, transforms, metadata, archives, scratch and diagnostics.
7. Original numerical agreement and all enabled guard branches; paired sensitivity
   outcomes/uncertainty, measured additional loss and inconclusive/unqualified scope.
8. Actual Default/FP32-min/FP64-force storage/multiply/accumulation/scoring types,
   transform/product counts and exact refinement precision/reasons.
9. Default70/GCD/partial-chip/phase-cell/workspace/hostile-input cliffs, retained
   three failures/one incomplete status, EME separate outcome and strategy matrix.
10. Complete affected local test inventory and actual skips/failures/incomplete
    jobs, rebuilt GUI/runtime hashes and normal/verbose/debug launcher paths;
    actual native/audio outcomes and deferred broader qualification.

Report n and range instead of overprecise universal performance claims from one
run. Raw counters and overlapping wall timers are not CPU component fractions.
An otherwise useful manual delivery with no matched sustained F3 rescue remains
an unfinished optimization attempt.

## Reference implementation cautions

The repository's own `search_fft` implementation and tests govern normalization,
buffer lifetime and thread behavior. External FFT documentation is background,
not authority to replace those semantics. If a future backend uses FFTW, its
[thread-safety rules](https://fftw.org/doc/Thread-safety.html) permit concurrent
execution but require separate treatment of planning, and its
[new-array execution contract](https://www.fftw.org/doc/New_002darray-Execute-Functions.html)
requires matching size/stride/alignment properties. Do not share mutable plan
buffers merely because a plan object is read-only.

Maintained local navigation: [Sched_Hop](scheduled-hops.md),
[receiver strategies](receiver-strategies.md), [search compute](search-compute.md),
[search arithmetic](search-arithmetic.md), [clock synchronization](clock-sync.md),
[build/test guide](building.md), [development contract](development.md).
Earlier entries and raw captures remain evidence with their own identities;
this runbook supersedes no measurement and claims no new receiver result.
