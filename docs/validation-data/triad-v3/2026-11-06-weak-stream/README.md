# Near-8-dB-Hz acquisition and continuous cable reception

This study extends the [previous CPU receiver](../2026-11-06-realtime/README.md).
The implementation and cheap test commands are in the
[weak streaming guide](../../../triad-weak-streaming.md). Ordinary Robust
integration, general message completion, RF hardware qualification and production
release certification are outside this experiment.

The important advance is a practical weak Helper gateway and segmented complex
acquisition, followed by the existing dynamic channel likelihood. No encrypted
pattern, shaping, interleaver, Helper waveform/lead, power ratio or physical
channel effect was removed to obtain the results.

## Operating point and independent native qualification

DATA C/N₀ is explicitly **8 dB-Hz**, total Helper C/N₀ 18 dB-Hz. This is a weakened
link relative to the original 4 W/180 dB physical example's 20.0206 dB-Hz DATA.
Rate 36, Added ×10/interleave, DATA IF 1725 Hz, high pilot 2400 Hz, crystal base
clock, IC-7100 shift clock at 30 MHz, margin 3, HF-worst ray centers 0/90 Hz,
21-ms excess delay and random Each 65% modulation at 9/90 Hz are retained.
FHSS and Symbol Burst are off for each transmitted one-bit pattern. Public
arrival uncertainty is ±7.035 seconds, with 13 keyed epochs and unchanged clock
bounds. The whole-bit train fixture is separately described below.

The independent protocol froze source/binary/template hashes and 39 trials
before sampled generation. Each channel stratum has six signal trials balanced
over the two bits, six Helper-only nulls and one pure-noise control. Independent
arrival draws from {−6.9, 0, 6.9}s and actual key-epoch offsets from {−3, 0,+2}s
remain hidden from receiver construction. Scores were exclusively written
before truth was opened. The receiver retained the original public epoch,
all 13 templates and its Helper-enabled configuration for null controls.

| Channel law, 23.2 s DATA | Correct conditional admissions | Wrong admissions | Misses | Helper-only admissions | Pure-noise admissions |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fixed-center rays; random Each 65 coefficients | 6/6 | 0 | 0 | 0/6 | 0/1 |
| Gaussian ray renewal, B=0.5 Hz | 6/6 | 0 | 0 | 0/6 | 0/1 |
| Renewal plus 0.2-µs random delay excursion, 20-s knots | 4/6 | 0 | 2 | 0/6 | 0/1 |

All 18 raw bit choices were correct. The two moving-delay misses had log
e-values 20.108 and 17.085 against the frozen 21.493 threshold; their bit margins
were about 40 nats. They are genuine admission misses, not relabeled successes.
The chosen delay excursion is small in envelope time but produces up to about
1.125 Hz of phase-rate excursion at 30 MHz. B is the individual ray's Gaussian
spectral width, independent of the 90-Hz ray-center separation.

The 731.79-second campaign ran two processes with two numerical threads each on
the AMD Ryzen 5 PRO 5650U. It later overlapped the live experiment. Complete
signal-trial receiver construction plus scoring took approximately 27–30 CPU seconds for
persistent rays, 39–52 for renewal and 46–60 for moving delay. These are measured
complete targets, not million-candidate arithmetic extrapolations. They exclude
native source/channel generation, raw keyed-template generation and audio I/O.
The saved stage receipts distinguish those boundaries and changing concurrent
load; these timings are not an isolated-latency confidence bound.

Small samples do not qualify 99% reception. Exact within-bit/channel intervals
and conservative balanced-bit bounds are retained in `independent/summary.json`.
For perspective, zero admissions in six independent nulls has a one-sided 95%
upper probability bound of about 39.3% within that stratum—not 10⁻⁴/day.

### A modest duration increase resolves the tested moving-delay misses

A separate follow-up froze **34.8 seconds of DATA** (requested duration 24 s,
rounded to three existing 11.6-s tiles), retaining every other setting. One-bit
transmitted support is 73.2 seconds including the 38-second Helper lead and pulse
halo; sampled capture length is 96.279 seconds. The nominal energy increase is
1.76 dB. This is a changed complete keyed pattern, not a claim that only an
identical waveform prefix was extended.

The analysis charged **both duration alternatives**, increasing the log
threshold from 21.493 to 22.187. The original receiver scores and single-duration
receipts remain unchanged; the stricter follow-up decisions are saved separately.

* Six new, independent moving-channel signal trials: **6/6 correct admissions**,
  no wrong admissions or misses. Minimum log e-value 46.885.
* Three independent Helper-only controls: **0/3 admissions**. Two were fully
  scored; one retained an explicit onset-aperture work-limit result.
* The two selected former misses, rerun with paired physical/noise seeds at the
  longer format: both admitted, with log e-values 107.316 and 49.451. These are
  development comparisons, excluded from the independent success denominator.

The 11-trial follow-up took 377.47 seconds with two workers. Independent signal
construction plus scoring took 73.03–101.30 CPU seconds and 62.95–88.43 seconds
wall under changing concurrent load. The slowest wall time is below capture
duration but above 73.2-second transmitted support; it cannot establish a cold
contiguous-bit real-time deadline. Public templates plus the unused preparation
capture took a separate 15.55 CPU seconds. The dedicated unloaded check below
separates this contention from the receiver's local CPU cost. Six successes
still do not qualify 99% reception, and the two-duration null calculation remains
conditional on the same noise assumptions.

The slowest concurrent case was then measured once in a fresh process without
the other experiments running. Imports, loading, receiver construction and
scoring took **40.36 seconds wall / 49.00 CPU seconds**. Construction accounted
for 5.46 wall / 5.47 CPU seconds; prepared receiver scoring, including Helper,
FFT proposals and dynamic refinement preparation, took 33.23 wall / 41.62 CPU
seconds. Raw keyed templates already existed. Charging the entire separate
template-plus-unused-capture generation fixture gives a conservative combined
**56.08 seconds wall / 64.55 CPU seconds**, below 73.2-second on-air support.
CPU per 96.279-second observation is 0.509 without that raw-template generation
and 0.670 with it. This is one direct cost check of the slowest observed work
case, not a future hardware-performance bound or an additional independent
sensitivity trial. It preserves the same candidate list and admission decision.

## Continuous simulation, real-audio replay and actual cable

The three-slot native fixture contains **one continuous propagation process**,
not concatenated independently reset channels. It has B=0.5-Hz renewal, moving
delay and Each 65 as above; the public source calendar emits alternating 0, 1, 0
patterns 90 seconds apart, each with a fresh 38-second Helper lead. Its 286.565-s
observation includes all intermediate guards and channel tails. This is a
development fixture used to expose/fix onset ambiguity; its three slots are not
three independent channel trials.

Initial attempts returned wide-onset or insufficient-noise-reference failures.
Those outputs are retained. The correction adds one second of observed quiet
support to analysis windows and unions all bounded onset-conditioned frequency
proposals. It does not shorten the 36-second frequency observation, narrow DATA's
timing prior or change the transmission. Native replay then admitted all three
correct bits in 25.5–30.1 seconds wall and 31.4–41.1 CPU seconds per score, before
constructor/raw-template cost.

One complete real-audio conversion followed by stateful 48→6-kHz complex DDC
also admitted all three bits. This software replay took 200.01 seconds wall and
246.88 CPU seconds while the independent campaign was running. Fast replay
deliberately queued windows; it is not the live backlog measurement.

Actual hardware was the Realtek ALC 257 headphone-to-microphone cable, `hw:1, 0`,
stereo 48-kHz S16, fixed channel zero, ALSA conversion disabled. Mixer settings
were read and left unchanged. A separate four-second multitone probe measured
DATA-band gain 0.841916, varying by approximately 0.0054 dB over 1400–2100 Hz.
The gain was frozen before reception; no DATA-fitted amplitude or alignment was
fed to the receiver. PCM used one fixed transmit gain 0.002 and retained the
complete composite signal plus injected noise.

| Live signal-train measurement | Observed result |
| --- | --- |
| Capture duration | 289.565 s including public pre/postroll |
| Correct conditional bit admissions | 3/3; no wrong bits |
| Parent CPU, including DDC and per-slot receiver construction | 228.224 s |
| ALSA child CPU | 0.534 s |
| CPU per captured second, including children | **0.790 core equivalents** |
| Total wall time including final drain | 324.494 s |
| Per-window processing/backlog after complete window | 68.86, 71.21, 36.54 s |
| Queue wait | 2.35, 0.70, 0.44 ms; high-water depth 1 |
| Ring/queued-window numeric bound | 27.72 MB; excludes receiver DSP workspaces/templates |
| Capture/playback xruns, dropped windows, clipping | None |
| Maximum ADC magnitude | 0.291 full scale |

Every worker finished before the next 90-second-spaced analysis window became
ready, even with background qualification. There was no accumulating queue.
The final result arrived about 35 seconds after device capture ended. This
demonstrates sustained throughput for the scheduled fixture, not zero decision
latency or an arbitrary continuous-message receiver. Raw keyed templates were
already generated; the separate native fixture generation, including all three
slots' templates and channel simulation, consumed 32.2 CPU seconds. Charging that
entire mixed-purpose generation cost conservatively would still leave this
fixture below 0.91 CPU-seconds per captured second, but it is not a measured
template-only preparation timer.

The receiver preserved continuous sample counts, oscillator/FIR state and input
validity. All scored windows were fully observed; only initial FIR history and
synthetic final drain support were invalid. ALSA provides frame counts and host
read times here, not calibrated analog UTC timestamps. The public playback
offset is a scheduling estimate; broad acquisition absorbs physical latency.
These are pending decoded bit candidates, not a completed `fox` message.

A second continuous cable run replaced only the I/Q input with the paired
Helper-only control, retaining the signal train's public templates/configuration.
All three complete windows were scored and rejected: log e-values were
−25.11, −23.84 and −20.60 against 21.49. There were no xruns, clips, invalid scored
windows or overloads. Queue wait stayed below 2.4 ms; full per-window backlog
was 46.36, 81.68 and 75.29 seconds, again below the 90-second spacing under concurrent
qualification. Parent plus ALSA CPU was 265.07 seconds for 289.565 seconds of
capture, or 0.915 core equivalents. Total wall time including PCM preparation
and the final worker drain was 370.70 seconds. These three correlated physical
null windows are a streaming rejection check, not a rare-event calibration.

## Audio/model margins and Helper leakage

At IF 1725 Hz, real conversion of the full 6-kHz analytic capture folds some
negative absolute-frequency content. It is retained, not hidden by per-carrier
processing. A separate software white-noise diagnostic measured approximately
1.004× native DATA-core PSD and 2.066× low-pilot PSD. A 15-second observed
pre-Helper cable noise interval measured −0.037 dB relative to native DATA-core
PSD, +2.965 dB near the low pilot and +0.112 dB near the high pilot. These are
finite-realization diagnostics, not covariance confidence envelopes. They
explain why nominal analytic and real-audio Helper SNR cannot simply be equated.

Paired same-seed DATA/Helper/noise controls isolate complete Helper residue.
The longer pure-noise control has a different inactive source calendar, but
its fixed thermal-noise prefix was verified exactly equal to the original
999320-row noise capture; the first 60000 samples of all controls match exactly.
Source clocks, noise roots, variance, channel law and relevant signal-calendar
identities were checked. The analysis uses original absolute timestamps.

For the three slots, full Helper energy was about 4728–4807 N₀. The fixed DATA
projection retained only 0.379–0.503 N₀ in active DATA blocks. The more relevant
realized likelihood-direction penalty, max μᴴKᵢμ over selected candidates,
both bits and fixed channel models, was **0.00279–0.00593 nats**. Its OU recursion
matched direct dense arithmetic within 1.42×10⁻¹⁴. This is strong evidence of
negligible Helper leakage for these tested directions, not a uniform bound over
unseen channels or the entire original candidate bank.

The same projection retained 99.844–99.848% of actual DATA energy. Realized
post-channel energy was 176.8–185.0 N₀, 0.82–1.02 dB above nominal 146.38 N₀ because
of fading and additional modulation. That energy change is explicitly separate
from receiver improvement; no received waveform was renormalized.

## Statistical and theoretical limits

The fixed-original-prior e-value and fixed attempt union bound support
10⁻⁴/day **under the declared covariance-dominated zero-mean Gaussian null**.
Adaptive pruning cannot inflate the selected mixture because omitted atoms
retain zero contribution rather than redistributing their prior mass. The
initial phase/channel fits are nuisance proposals, not independent evidence.
Every runtime receipt keeps physical-null qualification false.

A Gaussian renewing Helper is conditionally Gaussian after fixing the random
modulation/delay/oscillator paths. A rigorous projected covariance envelope
could therefore extend the bound. It needs augmented real covariance when
conditional real/imaginary knot grids differ. A global bound from bounded
Each 65 modulation is unusably loose: its L2 power multiplier is at most 10.573;
an illustrative unprojected bound would inflate noise by roughly 34.6 dB.
Typical PSD or a few benign nulls cannot justify the required exponential
moment. A useful next step is a projection-specific envelope or a
high-probability modulation event with an explicit daily error allocation.

An ideal known deterministic received waveform, illustrative broad-search false-alarm
accounting and 99% detection gives an optimistic 68.57 N₀ requirement, or 10.87
seconds at 8 dB-Hz. The 23.2-second allocation is about 3.29 dB above that benchmark.
That calculation uses eleven clock atoms and two channel alternatives rather
than the runtime's nine clock atoms and three alternatives. A scalar check with
the runtime's literal atom count gives 68.82 N₀, 10.91 seconds and 3.28 dB instead;
neither is an achieved receiver operating point.
It is not evidence of a 3.29-dB achieved 99%-sensitivity gap. Ideal unknown Gaussian
renewal with B=0.5 has about 42 effective modes and expected log likelihood 79.15
at 23.2 seconds; a harsher 0.5-second OU law gives 50.53. These omit actual keyed
patterns, acquisition mismatch, selective modulation and hardware. Doubling
duration improves those ideal calculations but is not demonstrated modem
performance. Full formulas and explicit assumptions are archived in `theory/`.

## Reproduction, provenance and exclusions

The compact archive retains frozen sources, input identities, native argv,
protocols, raw result JSON, timing receipts, failed attempts, tests and theory.
Large I/Q, PCM and template arrays remain excluded; regenerate them with the
recorded native binary/source identity and arguments. The packager is an
inventory/hash operation and never runs a receiver campaign.

The original truth-conditioned alignment diagnostic mistakenly interpreted
bit 0-vs-H0 evidence as a binary LLR. Its invalid interpretation and raw outputs
are preserved, and the corrected report uses both bit evidences. All six
truth-aligned development captures actually chose the correct bit. No new
receiver run was needed for that correction.

The focused new and reused tests passed **65/65 in 2.18 seconds**. Direct SVD,
FP32/FP64, rank/fallback, fixed-mass admission, chunk/FIR continuity, erasure,
queue and slot-rebasing checks are separate from sampled sensitivity and physical
statistics. Existing ordinary-receiver tests were not run or altered. No GPU
instance was rented: these experiments were tractable locally, and the relevant
real-time/audio acceptance target was this CPU.

Verify the [compact evidence archive](compact-evidence.tar.gz) against its
[artifact manifest](artifact-manifest.json) without rerunning computation:

```sh
python docs/validation-data/triad-v3/2026-11-06-weak-stream/package_evidence.py --verify
```

The main39-trial protocol is
`4565cdee4f328f91c6452192c0f5ff26ff3e521a24111667b89146b021f720b7`;
the separate longer protocol is
`e7b0dd51dd613be1547899fbe9a4b7ca25fa261a8db217769fa05f667728a82a`.
Current Python source hashes match those frozen identities. The reused native
exporter binary is identified by hash, without claiming a fresh source-to-binary
release certification. No cloud resources or audio processes remain running.

The native continuous fixture can be reproduced with:

```sh
build/dev-cli/triad_helper_export --case hf --base-oscillator crystal --shift-oscillator ic-7100 --channel-evolution stochastic --random-doppler-width-hz 0.5 --delay-excursion-ms 0.0002 --delay-knot-seconds 20 --cn0 8 --sample-rate 6000 --key-seed 2417 --epoch 1793850000 --seed 2026111600 --bit 0 --arrival-seconds 0 --whole-bit-count 3 --whole-bit-wait-count 0 --templates --output NEW_TRAIN
```

The Helper-only control uses the same arguments with `--data off` and no
templates. Live/replay commands are in the maintained guide. The system-clock
key format uses the fixture's declared virtual UTC epoch for reproducibility;
this does not qualify live wall-clock epoch discovery. Maintain the public
13-epoch search rather than injecting the evaluator's true epoch.
