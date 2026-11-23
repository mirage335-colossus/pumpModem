# Sampled channel models

The current Robust channel is the native streaming channel used with the
canonical `IndependentApskV2` waveform. Its public configuration and report are
in [`triad_modem_v3.hpp`](../include/datapump/triad_modem_v3.hpp); its streaming
implementation is [`triad_simulation_stream.cpp`](../src/triad_simulation_stream.cpp).
The GUI session uses this same native channel between emitted DATA/Helper and
the complete native receiver. Python may test or compare the implementation but
is not in that operating path. A channel connection or component parity result
does not qualify physical hardware or all receiver configurations; see
[current validation](validation.md#native-gui-integration).

Fast Modem has a separate native sampled channel in
[`fast/simulation.cpp`](../src/fast/simulation.cpp). `ChannelAssumptions` are
the local receiver declarations: they are validated in every mode and help
deterministically resolve Auto, independently of test-effect injection.
Simulation starts from a local Fast transmission and sends its resulting
waveform through the Fast receiver. Transmit power, path loss and
noise controls define the simulated link ratio in normalized receiver-input
units: transmit power and loss set the equivalent received level relative to
independent receiver noise. The samples remain normalized rather than being
scaled to a physical DAC voltage; noise is added after propagation at the
simulated receiver boundary. Common receive gain therefore does not change the
modeled ratio. Other effects include oscillator offset and phase, selected
delay/Doppler spread, and optional selective AM. The channel reuses the neutral
spread-ray and selective-perturbation kernels, but does not instantiate the
Robust receiver or Robust DATA/Helper waveform. It preserves delayed and
filter-decay tails and supplies a quiet interval for Fast completion. Simulation
runs entirely in software.

**Live loopback test** uses physical audio capture and playback with the Fast
receiver. Transmit power, path loss and modeled receiver noise are Simulation
assumptions: in Live loopback they are disabled and retained and do not alter the
playback signal or captured input. Only an explicitly started test copies the
declared oscillator, delay/Doppler-spread and selective-AM effects into the
waveform before the DAC as synthetic stress; the copied search margin is one
because margin affects receiver bounds only. Idle loopback monitoring and
ordinary Live use clean input. Playback uses the selected TX
volume and does not change the system mixer or apply an automatic gain change.
The selected nonempty Console text or file is preflighted before audio opens;
it is neither truncated nor replaced by a fixture. Capture must start within
5 seconds. The existing maxima remain 75 seconds of audio and 110 seconds
elapsed overall. After the full emitted source and canonical quiet tail, at
most 2 seconds of captured samples are allowed for receiver completion. This
grace can only report failure; it cannot synthesize physical completion.
Silence is maintained during this wait to avoid waiting indefinitely for audio
server playback drain. Progress distinguishes audio samples queued for playback
from samples captured at the receiver. The queued count includes silence and
does not confirm speaker output. A verified result becomes an ordinary Signals
or Files record with LOOPBACK TEST provenance only after physical completion,
integrity checks and exact comparison with the selected source. Public checksums
do not authenticate a sender; encrypted mode requires authentication. Live
loopback's synthetic effects describe a modeled stress overlaid on an acoustic
path, not RF propagation performance.

While Live loopback mode is selected and idle, the GUI runs ordinary receive-only
monitoring. Before a test, that session closes so the bounded duplex test can
own the device; monitoring restarts afterward. Tab switches do not stop it, and
switching to another modem releases an idle Fast capture for the shared audio
handoff. Ordinary **Live audio** also uses the selected device without the
synthetic propagation or noise model. Realized channel reports never select
receiver geometry or establish physical completion; declared assumptions can
select the deterministic local Auto profile. Profile and waveform changes
revalidate the declarations. An invalid receiver declaration reports the
required frequency range, available sampled band and oscillator/channel
allowances, and blocks listening and transmission in every mode while leaving
fields editable. Invalid injected test effects disable Simulation and explicit
loopback, but not ordinary Live. On the current automatic 231-baud profile, a
CLI-only strong-signal Gaussian-ray diagnostic (30 Hz bandwidth per ray,
2σ; 0/7 ms delay) fails acquisition while its fixed-channel control passes.
An explicit experimental 1,000-baud dense-pilot candidate passes strong digital
Gaussian, oscillator and selective-AM checks, plus a clean −6 dB reference-noise
check; Auto does not select this candidate. The matched full-stress physical
speaker/microphone trial failed acquisition. Weak Gaussian reception and full
physical stress remain unqualified. See the [scoped acoustic results](fast-acoustic-short.md#validation-scope)
and [validation record](validation.md).
See [Fast mode](fast-mode.md#fast-simulation) for the test workflow and limits.

## Robust simulator boundary

Robust prepares the shared transmit waveform and public receiver bounds from
local settings before drawing the simulation's oscillator errors and arrival
offset. The sampled channel draws the echo delay after it receives the
transmitter's source. It applies delay, Doppler and the other selected effects
to that source on its way to the receiver. The transmit calendar and DATA
samples use only the agreed profile. The configured simulation gives the
receiver the agreed calendar and the expected noise variance from the link
budget. Exact simulated arrival and sample-clock values also position the
display replay. [Profile preparation](../src/robust_profile.cpp)
· [Simulation and replay](../src/live.cpp)

A native regression uses the 6 kHz, 3.6 Hz Helper-off profile with HF Worst,
Each 65%, 30 MHz shift and GPSDO XO. Two seeds vary both endpoint clocks, the
arrival and the realized echo delay. With the thermal noise seed held fixed,
the receiver I/Q differs. Both simulations agree with live preparation on
the first two transmit slot coordinates and all 192,000 checked pre-channel
samples through the first DATA onset. The check covers waveform provenance
and uses no receiver scoring. [Native regression](../tests/test_gui_robust_live.cpp)
· [Cause of the inter-bit gap](symbol-burst.md#baseline-timing-with-helper-off)

## Baseline and channel stages

`StreamChannelConfig::effects` selects the shared synthetic impairments.
Zero-valued spread and perturbation settings with noise disabled are an
explicit channel baseline. Defaults are simulation settings, not calibrated
field conditions. Keep the complete emitted waveform as input: channel
propagation acts on the sum and does not identify or decode carrier streams.

The channel may model transmitter and receiver oscillator error, sampled
two-ray excess delay and Doppler separation, frequency-selective perturbation,
receiver noise, optional physical-path evolution, sampled frontends, and
observation erasures. Their parameters and independent seed roots are declared
in `SimulationConfig`, `PathEvolutionConfig`, `SelectiveModelConfig`, and
`SampledFrontendConfig`. The models are explicit synthetic assumptions, not
measurements of a named device or propagation path. Record the full config,
waveform format, seed roots, and source revision with results.

`StreamingChannel::read` preserves state across chunks. Noise, path, and
frontend states advance through actual channel processing even when an
observation is marked erased. The stream preserves delayed echoes and filter
decay tails; do not truncate at the emitted source endpoint. Its report
separates source length, complete output, tail, observation padding, erased
samples, converter clipping, and model diagnostics.

## Stream evaluation, workers and capture identity

A capture is identified by the SHA-256 of its samples and validity mask, and
validation records cite those hashes. The channel and the pattern source
therefore keep every floating-point operation and its order. Reordering one
sum, or replacing a library call by an identity, changes the low bits of the
samples and makes every earlier capture a different input. The changes below
left the arithmetic alone and changed where and how often it runs.
[Measurements and checks](validation.md#simulator-throughput-and-capture-identity--20-november-2026).

`StreamingChannel::read` prepares its rows in blocks of at most 128, or 1,024
with workers. For each block it generates the source history that the last
row needs, evaluates the terms that depend on a row's index alone, and then
advances the sequential states row by row in the original order. A read still
consumes the emitted source exactly as far as its own rows need, which the
GUI session relies on when it reports transmit progress after each read.

- **Independent rows.** The two sixteen-tap source interpolations and the
  fixed-time ray and selective coefficients of a row read only its index and
  the generated history, so rows can be evaluated in any order. The
  interpolations were 31–48% of the simulator's time on the matched captures
  and the selective coefficients 13% of the severe one.
- **Silence.** An interpolation whose sixteen history samples are all zero
  returns zero without evaluating its 48 sines and cosines. Every product is
  a zero, the sum starts at and stays positive zero, and the coefficient sum
  is within 1.3e-4 of one, so the quotient is that same zero.
- **Repeated values.** Coordinates and rotations that are the same for every
  row are computed once with their original operand order. A bounded curve
  keeps its two knot values for the interval it is in, and the pattern source
  keeps the coefficients of the sixteen or seventeen cells under its pulse,
  which change once in 10,000 samples at 3.6 Hz.
- **Rounding.** Floors, ceilings and integer conversions of `long double`
  coordinates use forms that return the standard values without switching
  the x87 rounding mode. Those switches are the likely explanation of the
  40% difference between binaries with equal simulator objects.
  [Mechanism and guidance](triad-performance.md#code-placement-and-x87-rounding-mode-switches).
- **Unread work.** The delayed interpolation runs only when a two-ray or
  bounded law reads it, which removes 48 of the 96 sines and cosines of a
  row on the unspread stable and five-slot message captures. Rows carry their
  echo delay into the sequential pass instead of evaluating the path again;
  a row whose coordinates failed is evaluated again, so the failure is
  reported at the same row. The selective filter updates and renormalizes
  its phases only for the sinusoidal law. Without FHSS the transmit dwell
  index is skipped when a bound found at construction proves that every
  transmit calendar coordinate converts.

`StreamChannelConfig::lanes` lends a channel the caller's workers. Up to
sixteen pool jobs claim a block's rows in shrinking runs from one counter,
then fixed chunks of the staged chains below, so a job that starts late,
such as the one after the pass, takes only what is left. Rows can be
prepared in any order and by any reader copy; runs are claimed in order, so
every row before a recorded failure is prepared. One further job each
advances the two bounded path trajectories through the block. None of these
states reads the signal, so their values depend on the row alone.

The receiver oscillator, the thermal noise and, on the split path below,
the transmit oscillator are staged. Each draws its standard normals from one
`std::mt19937_64` through `std::normal_distribution`, which libstdc++
evaluates by the polar method in pairs. Only the acceptance loop over the
engine's canonical uniforms depends on earlier draws. A pair's two values,
`y*m` and `x*m` with `m = sqrt(-2 log r2 / r2)`, depend on that pair alone.
[`GaussianStream`](../src/triad_gaussian_stream.hpp) runs the loop in order
and forms accepted pairs anywhere, with the library's operations in its
order. One job per chain accepts the draws, and one job per oscillator runs
its recurrence: the FHSS dwell phase, the white, random-walk and flicker
frequency, the Wiener step and the `remainder` of the phase, then the white
PM draw. The pool jobs form the draws and the rotations. Every stage reads
what the blocks before left: a block's jobs form the rotations of its own
items, carry the recurrence one block further, and form and accept draws two
and three blocks ahead, in rings charged to `channel_workspace_bytes`. A
chain that is behind catches up on the reader first. An oscillator item that
fails in its recurrence is recorded and reported where the unbatched reader
meets it. Draw for draw, the stream equals the distribution: the channel test
checks about 400,000 draws on each of four seeds with pairs formed out of
order, and a one-off check matched 10^8 draws on each of seven seeds. The
split is used only with libstdc++, and only after the first 1,024 draws of
three seeds, formed this way, equal the compiled distribution's at first use;
the channel test fails if a libstdc++ build no longer selects it. Otherwise,
as with other standard libraries, the acceptance job runs the distribution
whole, which stays exact but sequential.

The next block's source history is generated on one of two paths:

- **Split.** When the source offers `EmittedSource::readers` and neither a
  converter nor diagnostic I/Q noise reads source values, or when the signal
  is disabled, the transmit oscillator is staged. Its rotations,
  `signal_gain * polar(phase + white_pm)`, have the same operations and
  random-number order. Up to eight readers evaluate disjoint
  parts of the source range at once. The owner then accounts the samples in
  order exactly as `sample_at` would: forward cursor, stop checks every 256
  samples, slot transitions and the DATA and Helper energy sums. The
  earliest failure wins, the oscillator ahead of the source at the same
  index. Each block's sequential pass runs as one more job beside the
  preparation of the read's next block. The read's last block, and a block
  with a recorded failure, is passed alone. A pass that fails inside that
  overlap discards the range evaluated beside it, so the emission report
  equals a lone reader's.
- **Fused.** Without readers, with a converter or diagnostic noise, or with
  a lone reader, one job generates the source and transmit oscillator chain
  of the next block in the original order.

`Emission` gives each reader its own DATA `PatternSource` for each slot and
its own Helper `Generator`, and the probe's composite source forwards to
every component's readers, including the interferer. `EmissionOptions::lanes`
evaluates the 32,768-node DATA power reference, the sixteen-tile
`bounded_tile_power` and the Helper reference norms in parts, each with its
own source and per-part copies bounded to 32 MiB; the sums keep node and tile
order. The receiver's `data_power_reference` stays sequential.

The reader applies the prepared terms in order with the selective filter,
front end, gain control and report sums. Samples, masks and reports are
identical for every worker count. Two things differ from a lone reader: the
source, its readers and the sequential pass can run on a worker, and a read
that fails ends the channel. The probe lends the receiver's executor, whose
workers are idle while the capture is generated. It charges their stacks to
its memory limit before it sizes its two read buffers, which start at 4,096
rows and grow to at most 65,536, so a larger read never removes a simulator
worker. Its SHA-256, copy and spool append run in 4,096-row pieces, one
piece as an extra job of each lent preparation. The workspace grows with
workers: at 16 workers `channel_workspace_bytes` rose from 314,897 to
1,640,313 on the stable capture and from 3,281,961 to 26,279,953 on the FHSS
capture, whose readers copy the pattern-source caches, all inside the 65 MiB
channel bound. The staged chains' rings take about
`lead × (56 × draws + 120)` bytes per oscillator chain and
`lead × 2 × 56` bytes for the thermal noise, where `draws` is four plus the
oscillator's flicker bands and `lead` is the block of up to 1,024 rows, or
the 2,064-row transmit lookahead on the split path. On the matched captures
that added about 1.1 MB at 12 workers: 1,639,865 → 2,759,425 bytes on the
stable capture and 26,241,105 → 27,360,665 on the FHSS capture. With 64
flicker bands on both oscillators and the split transmit path it is about
12 MB, so a large selective or FHSS-reader channel that no longer fits in
the 65 MiB bound drops its source readers, then its lanes, with the same
values. A lone reader carries 8,176 more bytes, the idle chain objects.
Finite GUI and lab simulations can stage channel preparation with the same
native receiver executor. A wave borrows helpers only while no receive slot
is ready or in flight. Once a slot is ready, the simulation controller
prepares its samples serially and the full receiver worker budget is available.
The staged channel and started worker stacks are accounted in the receiver's
memory admission. Before allocating the staged channel, the simulator quotes
each public receive window through the second quiet slot at full worker width.
It checks the endpoints first to catch clock growth quickly, then checks the
interior because the DSP plan can change discretely. The startup planning
budget is 100 ms, checked between quotes. A tight memory limit, cancellation or
an unfinished scan keeps the lone reader. This protects the first preview on
long messages. Reconsider the startup budget using complete simulation time
if these planning calls regularly prevent a useful staging gain. Paced
open-ended noise uses the lone reader.

`triad_receiver_probe --capture-cache DIR` skips the simulator on repeated
receiver runs of one probe binary, such as backend, precision or worker
comparisons on the same capture. The first run stores each capture as
`DIR/<key>.capture`, created privately on POSIX systems (on Windows it
inherits the directory's access control) and renamed into place when
complete. The key is the SHA-256 of the executable's own SHA-256, on Linux
the glibc version and the SHA-256 of each loaded C, math, C++ and GCC
runtime library and the dynamic loader, the CPU features and
`GLIBC_TUNABLES` value that select glibc's math variants (on x86, FMA, FMA4
and AVX2; receipt `key_math_dispatch`), the command, every given option
except those that select receiver arithmetic, backends, allocation, budgets
or deadlines, and the generating call's extent, episodes and interferer.
Receipts name the covered libraries under `key_runtime_libraries`; on other
systems `key_binds` says only the executable is covered, so remove `DIR`
after any runtime library update there. A later run with the same key reads
the file instead of simulating. Before any receiver work it checks the
magic, version and header size, the header's own SHA-256 (taken with that
field zeroed, so it covers the capture SHA-256 and every report field), the
canonical encoding, key, extent, exact length, each validity byte and the
rows' SHA-256, and it fails on any difference. The capture keeps the same
budgets, memory preflight and spool path; the file is charged to the spool
budget for a spooled capture and to the sample budget for one held in RAM.
Reports say `"source": "reused"` under `observation.capture_cache`, keep
`observation.input_sha256`, report `observation.channel_workspace_bytes` as
null because no channel ran, and replace the `simulator` phase by
`capture_reuse`, which keeps the generating run's workspace as
`stored_channel_workspace_bytes`. A rebuilt probe has another key and never
reuses a capture. The file proves integrity, not provenance, so keep `DIR`
private, for example under ignored `local-evidence/`, and remove it when its
comparison ends. A killed run can leave a `<key>.capture.tmp-*` file, which
no run reads; delete those when no probe is writing. On the 5650U the severe
capture's 20.7 MB file was read and verified in 0.03 s and the five-slot
message's 106 MB in 0.27 s, and the receiver receipts equaled those of the
run that generated them apart from timings and run-dependent memory fields.

With lanes, prepared rows also carry their physical time into the pass,
which reads the receiver's dwell index from its staged recurrence. When the
selective filter's shortest lag reaches back past two blocks, which holds
for the matched captures (2,666 rows at 0.75 Hz spacing and 6 kHz against two
1,024-row blocks), the pool also evaluates its two delayed branches, an
eight-tap FIR over rows that earlier passes filtered, with the filter's
operations in their order. The pass keeps the branch combine, the report
sums and every recurrence: the Gaussian FIR path law, the
Ornstein–Uhlenbeck ray gains, the sinusoidal selective phases, the bounded
path trajectories' diagnostics and the sampled front end.

Before the staged chains, with twelve workers on the Ryzen 5 PRO 5650U the
severe slot took 0.56–0.63 s against 1.06 s before the readers and the
overlapped pass, and twenty-four workers were no faster
([measurements](validation.md#exact-receiver-and-simulator-speedups--20-november-2026)).
A section-timed copy then showed that the receiver oscillator chain bounded
the stable and five-slot message captures, 212 µs of each 244–247 µs block,
and that on the severe capture the busiest lane ran the 276 µs pass and then
a 237 µs rows job. Larger blocks did not help. The staged chains and the
pool remove those dependencies without changing a sample. On the 5650U with
no other agents running, three alternated pairs per cell, the `simulator`
phase fell by about 10% on the severe capture at twelve workers
(0.579–0.587 s to 0.522–0.531 s) and 7% on the quiet capture at
twenty-four, in every pair, and by 2–19% (median 8%) on the severe capture at
twenty-four. The stable capture, which the receiver chain bounds, gained
1–5% at twelve workers, below the predeclared 3% median threshold, and ran
2–13% slower at twenty-four. Those two twenty-four-worker cells had up to
one CPU of other load in some runs, so they are indicative. Four workers were
unchanged
([measurements](validation.md#gpu-offload-survey-and-gated-device-routes--20-november-2026)).
The Blackwell host's EPYC is not yet measured. On the severe capture the
pass and the two bounded path trajectory jobs, about 150 µs each, are the
likely next bound; moving the pre-selective products out of the pass would
need those trajectories staged a block ahead too.

Proposals that change sample values, measured and not adopted:

| Proposal | What changes | Measured or estimated gain |
| --- | --- | --- |
| Interpolation coefficients from one sine and one sine/cosine pair per coordinate, with angle sums for the sixteen taps | Coefficients differ by at most 3.3e-16 and their sum by 1.1e-15, so every sample with signal changes in its last bits | Measured on the 5650U: the severe slot from 2.70–2.73 s to 1.78–1.79 s with one worker and from 1.05–1.06 s to 0.89–0.90 s with twelve; the stable slot from 2.04–2.14 s to 1.15–1.16 s with one worker |
| Gaussian draws from a table method in place of the standard library's polar method | Every noise and oscillator draw, so each capture becomes another realization of the same laws | Estimated: ten draws per sample cost about 24 ns each. They are about a tenth of a lone reader's severe slot and most of the oscillator and noise jobs. Not implemented |
| A tabulated polyphase interpolation kernel | Coefficients to the table's accuracy | Not measured. It removes the remaining interpolation arithmetic, which workers already absorb |

Adopting any of them needs new capture hashes for every record that cites
one, so it belongs with a change that already invalidates them.

A change to this code is checked against the build before it, not against a
description. Run both on every path law, with and without workers and with
small and large reads, and compare samples, masks and reports bit for bit.
[`test_triad_v3_channel`](../tests/test_triad_v3_channel.cpp) keeps the parts
that need no second build: workers against the lone reader, blocks against
single rows, a source failure in the middle of a block, and the rounding
forms against the standard ones. The validation record lists the configurations that the current
code was compared on.

## Robust GUI changing-channel realization

The Robust GUI now treats the selected spread as support for a changing
synthetic realization. `HF Worst` declares 7 ms excess-delay support and
30 Hz differential Doppler before the configured margin. One echo delay is
drawn inside that support. The receiver does not use the realized delay; it
scores separately declared bounded delay bases.
Its echo/direct amplitude ratio varies smoothly between zero and one, and
its instantaneous frequency varies within the signed Doppler support.
The two path powers sum to one before their interference and selective
filtering; the direct path stays present. Effective delay spread changes with
the echo power. This is not rapid physical motion of a millisecond-delay path.

The selected spread Doppler also supplies a common atmospheric excursion
allowance; the magnitude in the explicit Doppler field adds to it. Thus
HF Worst still permits common frequency motion with exact oscillators and
the explicit Doppler at zero. The explicit field no longer selects one
constant simulated offset. Echo
and common frequency are integrated into continuous phase, with independent
seed domains and no resets across reads and public slots throughout one emitted
source. A separate simulation creates fresh channel state. Common wander and
relative echo Doppler add: at HF Worst with 3× margin and explicit Doppler zero,
the direct path can reach ±90 Hz and the echo ±180 Hz absolute shift. Capture
allowances account for this sum. Common wander, echo strength and echo frequency
use smooth independently drawn knots at `max(1, 1/max(bound, 1/3600))`
seconds. These are reproducible synthetic timescales, not a calibrated HF
law. The report distinguishes configured bounds from extrema actually visited;
a short realization need not visit the extremes.

Selected amplitude perturbations use the existing bounded Gaussian-erf
coefficient law. Slow and fast depths remain separate (Each 65% sets both to
0.65); they do not promise a total ±65% waveform envelope. The underlying fast
spectral width is one tenth of the existing selected modulation allowance,
with slow width another factor of ten smaller. This matches the channel's
ten-width-per-edge capture convention without enlarging the GUI's declared
audio passband. That convention is an allowance for spectral tails, not a
hard bandlimit. Off retains exact bypass.

Robust Setup's minimum DATA carrier spacing changes where the waveform samples
this response in frequency. The GUI's synthetic selective correlation scale
continues to follow the nominal single-carrier pulse width, so changing the
minimum alone does not retune the filter taps. Receiver delay hypotheses and
capture tail bounds use the same pulse-derived scale.

Low-level `StreamingChannel` defaults retain the fixed two-ray diagnostic
control; `PathEvolutionConfig` explicitly enables bounded echo variation and
common wander. A single channel instance processes the entire emitted composite,
including Helper, silence, delayed tails and erased observations. The waveform
generator and real playback remain clean. **Simulation = No** bypasses synthetic
effects in both duplex states; **Live/Duplex** changes capture ownership.

The default two-path realization does not cover direct-path disappearance, arbitrary
diffuse scattering, all independent carrier fades or arbitrary variation rates.
Other native path laws remain available as independent stresses. Simulation
support and a receiver parameter are not themselves a reception guarantee.
See [research reconciliation](triad-stochastic.md#nonstationary-channel-research-reconciliation)
for the distinctions and contrary findings that guide qualification.

The native bounded-ray simulator also supports an explicit fixed direct-power
fraction for independent qualification. It replaces only the normalized ray
amplitudes with `sqrt(p)` and `sqrt(1-p)`, retaining the same delay, integrated
common/relative frequency, selective filtering, noise and advancing random
state. It is available only with the bounded-ray law; the default GUI law and
its arithmetic remain unchanged. Reports identify fixed direct/delayed powers
and actual echo/direct amplitude ratio, with a null ratio at zero direct
power. Both endpoint histories and tails remain present in the simulation's
support accounting. Bounded-ray reports also record simultaneous common-plus-
relative Doppler extrema in physical Hz over the full processed history,
including preroll and silence. These exclude static Doppler, oscillator,
diurnal and selective terms and precede receiver-clock scaling; a zero-power
ray still has a reported trajectory. They are not selected-bit or measured
received-carrier extrema. This enables delayed-dominant or missing-direct stress
without claiming that a selected power ratio, seeded delay or motion law is a
calibrated channel replay. See the
[measured motivation and limits](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#persistent-path-ordering-clock-age-and-noise).

### Midlatitude moderate HF

The Robust spread preset **HF typical 0.5Hz 1ms**
(`hf-midlatitude-moderate`) uses two independent, equal-mean-power Gaussian
fading paths at 0 and 1 ms. Each path has a 0.5 Hz full 2σ Doppler
power-spectrum width. This is the midlatitude moderate test law in
[ITU-R F.1487 Annex 3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf).
The separate **HF moderate 10Hz 6ms** preset retains its bounded echo and
common wander. HF typical starts with selective amplitude perturbation
Off. An explicit selection still applies, as does any explicit Doppler.

At Off, the two delayed copies still add coherently. The relative phase
changes by `2π Δf τ` across a frequency separation `Δf`. The 1 ms delay
therefore makes different frequency components reinforce or cancel at the
same instant. The 0.5 Hz Doppler width governs how the path gains evolve
over time. For equal, independent Gaussian paths the ideal received-power
correlation is `cos²(π Δf τ)`. At 265 Hz DATA carrier spacing it is about
0.45 between neighbors and 0.009 across the 530 Hz outer span. At 7.5 Hz
natural spacing it is about 0.998 across the 15 Hz outer span.

A bounded native check replayed individual tones with the same path
seed and both added perturbation and receiver noise off. The complex
gains at ±265 Hz matched the 1 ms two-path response within 1e−12.
The zero-delay control produced equal complex gains at the two frequencies
within 1e−15. These are channel-mechanism checks. The five-seed message
test used 7.5 Hz spacing. The separate 265 Hz message check used one seed.
Keep the compact `local-evidence/hf-typical-selectivity-u84gtfpl` receipt
until a superseding native channel check records the same comparison.
[Receiver test coverage](../_appraisal/highFidelity-triple_Robust_Modem/hf-validation.md#moderate-continental-and-atlantic-hf)

The extra control applies a separate time-varying dispersive filter.
Its coefficients and frequency scale are synthetic stress settings.
Watterson's selected Illinois–Colorado measurements contained three or
four unequal propagation modes and 20–100 µs of delay smear within a mode.
Additional, weaker or moving paths can change the frequency correlation
predicted by the two-ray preset. Those measurements give no additional-AM
percentage for this control. [Measured path summaries](https://its.ntia.gov/publications/download/87-221_ocr.pdf)

The receiver scores the nominal 1 ms path spacing with time-varying gain.
Search Margin expands the public delay support and spectral allowance
while preserving the two physical ray delays and their fading widths.
At the GUI's default Margin 3 the receiver reserves 3 ms of excess delay,
an additional ±3 Hz for the search center and 4.5 Hz of total atmospheric
allowance per band edge before oscillator, explicit Doppler and optional
selective-modulation terms. The receiver can use up to 0.25 Hz of the
existing center allowance for a linear-drift fallback. Margin 1 supplies no
extra drift allowance. The Gaussian simulator remains centered unless
explicit Doppler is set. A different actual tap spacing can require its
own channel calibration. The Doppler width describes the model's power
spectrum, and 1 ms is its nominal path spacing. The search allowance has
no associated percentile of ionospheric conditions or message delivery rate.

The high-fidelity appraisal also provides continental models derived from
measured delay/Doppler summaries and eastbound and westbound Atlantic
engineering cases with independent realizations. The Atlantic cases
assume a usable 12 MHz path and share a reciprocal multipath model.
[Regional HF validation](../_appraisal/highFidelity-triple_Robust_Modem/hf-validation.md#moderate-continental-and-atlantic-hf)

### Distinguish HF stress profiles

GUI **HF Worst** and appraisal `hf-disturbed` share nominal 7 ms / 30 Hz
labels, but are different channel laws. GUI values are pre-margin support;
Margin 3 declares 21 ms / 90 Hz, and the changing realization includes common
frequency wander as well as relative echo motion (the direct ray can reach
±90 Hz and the echo ±180 Hz). The standard `hf-disturbed` case is the existing
fixed-grid Gaussian comparison: two independent zero-mean proper-complex taps
at 0 and 7 ms, each with 30 Hz full 2σ power-spectrum width. It does not add the
GUI's common-wander mechanism or define a new channel law; see the [HF model
catalog](../_appraisal/highFidelity-triple_Robust_Modem/models.md#hf-standard-and-nonstationary-controls).
ITU-R F.1487 Annex 3 calls 2σ the frequency-spread convention and lists
high-latitude disturbed at 7 ms / 30 Hz, while disturbed NVIS is 7 ms / 1 Hz
with a groundwave component added only if required [F.1487 Annex 3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I!!PDF-E.pdf).

A separate moving-plasma two-hop realization produced about 0.17 Hz frequency
excursion and 0.325 ms delay with zero declared GUI spread and a 5 Hz receiver
allowance. Its small delay/Doppler spread does not remove its deep cancellation
or weak-reception failures, but it is not a severe-spread qualification.
Keep the channel's realized spectrum distinct from its declared
search/capture allowance: test calm realizations inside broad public bounds as
well as separate severe statistical and moving-plasma cases. Neither profile
guarantees a fixed transmission time, energy, or reception outcome relative to
AWGN.

### Noise and frequency assumptions in the 2 mW HF tests

The November 2026 HF comparisons kept the same broad receiver search for
all three physical channels. Stable controls disabled simulated path motion,
delay spread and selective modulation while retaining oscillator errors and
receiver noise. The Gaussian comparison replaced the GUI path evolution with
the independent 0/7 ms fading taps described above and retained Each 65%
modulation on the signal. The two hobbyist GPSDO XO presets provided
illustrative 0.0001 ppm accuracy and 0.5°/√s phase-diffusion parameters.
The 30 MHz translation and 1,725 Hz upper-sideband tone implied a nominal
30.001725 MHz carrier coordinate. Fake FCC applied its dwell envelope in
baseband. [Oscillator conventions](oscillator-models.md#independent-physical-quantities)
and [editable HF trials](../_tests-RobustModem-HF-simulations-2026-11-18.md).

The assumed −164 dBm/Hz equivalent receiver-input background was realized as
proper-complex Gaussian white noise. At 2 mW and 180 dB loss, the nominal
C/N₀ was −12.9897 dB-Hz. The native 6,000 Hz complex stream therefore used
normalized variance `6000 / 10^(-12.9897/10) ≈ 119432`. A site estimate
requires the actual RF band, latitude, longitude, month, hour and local noise
environment. Relating that estimate to the receiver input requires the
antenna and feeder response, bandwidth and receiver noise. Qualifying
lightning impulses, local interference or front-end overload additionally
requires a measured episode or declared amplitude, timing and equipment
model, with a stated severity percentile or other selection rule.
These effects remain unmeasured in the HF trials.
[ITU-R P.372 radio noise](https://www.itu.int/rec/R-REC-P.372-18-202609-I/en)
and [ITU-R Study Group 3 calculation inputs](https://github.com/ITU-R-Study-Group-3/ITU-R-HF/blob/master/P372/Src/AtmosPlots/README.txt).

The separate `noise-bursts` appraisal fixes its colored and impulse terms
to the white variance at +32 dB-Hz. Their combined mean power is about 1.25
times that reference variance. Within that fixture, entering −12.9897 dB-Hz
for the white term would put their combined mean about 44.02 dB below it
(`10 log10(1.25) − 12.9897 − 32`). That calculation describes the fixture's
scaling. Receiver errors still depend on burst peaks and correlation. At the
original +32 reference the five-slot noise-only trial produced five false
DATA decisions. On that same capture a diagnostic 128.735× receiver variance
gave five quiet decisions and recovered the wanted message on the paired
capture. This calibration applied to that constructed noise process.
A site-matched impulsive-noise comparison would need wanted
and silent episodes covering the same physical quiet slots.
[Noise fixture definition](../_appraisal/highFidelity-triple_Robust_Modem/models.md#noise-and-equipment-controls)
and [quiet-slot outcomes](validation.md#persistent-channel-and-calendar-age-follow-through--17-november-2026).

## Interpretation limits

- The fixed two-ray control has equal-power direct and delayed rays; the GUI
  changing realization varies their ratio. Neither is a measured delay or
  scattering spectrum. Static separation and selected
  stochastic ray evolution are distinct assumptions.
- Selective perturbation is a synthetic signal-level stress model. It is not a
  calibrated per-carrier fading prediction, and its diagnostic energy reports
  are not receiver estimates.
- Gaussian and OU path models have their stated finite implementations and
  capture assumptions. A spectral allowance is not a hard bandlimit or proof
  that a narrow sample rate captures all useful energy.
- Narrow complex-I/Q diagnostic noise is not full-rate audio/DAC noise; the
  two have different bandwidths and cannot be equated by variance alone.
- Simulation results show behavior under the recorded model and inputs. They
  do not substitute for field measurements, physical-device checks, or
  receiver-system qualification.

For concise retained experimental findings and their dated evidence, see the
[triad channel technique summaries](triad-stochastic.md) and
[validation record](validation.md#historical-provenance). The abbreviated
performance method is documented in
[`triad-performance.md`](triad-performance.md); no full or sustained campaign
is a fallback for it.
