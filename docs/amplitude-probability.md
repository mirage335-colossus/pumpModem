# Amplitude probability work and current limits

The amplitude backend has its own **conditional first-bit AWGN reference** in
`src/amplitude_probability.cpp`. It does not reuse a coherent receiver's
probability. Both the planner and Console can display a reference for an eligible
public pattern; a Console with additional receive profiles retains the explicit
matching-bank scope. No amplitude reference is labeled qualified RX confidence.

The relevant planning domain requested on 16 October 2026 is up to **three years
per symbol**, including 4 W, 1.5 kW and 15 kW. This bounds the reference calculation,
not the receiver or transmitter. A suggested loss of roughly 360 dB was a
plausibility observation, not a new modem input limit. At 220 dB path loss and
−164 dBm/Hz noise, the three powers give −19.9794000867, 5.7609125906 and
15.7609125906 dB-Hz C/N₀ respectively. Completion also needs a fully scored absent
symbol; a three-year bit can require another three years before physical END.

## Implemented reference

Nonoverlapping real-PCM FFT bins have independent complex Gaussian noise.
In normalized units an atomic retained-bin energy has law
`J ~ Poisson(Λ); E | J ~ Gamma(k + J, 1)`. Overlapping coarse channels share the
same atoms. For their common region with cumulants `v = k + 2Λ`,
`t = 2k + 6Λ`, `u = 6k + 24Λ`, and channel means `μc`, `μd`:

- `Cov(Ec, Ed) = v`.
- `Cov(Ec, Ed²) = 2 μd v + t`.
- `Cov(Ec², Ed²) = 4 μc μd v + 2(μc + μd)t + u + 2v²`.

The reference accumulates the joint means/covariances of channel energy, squared
energy and both template-weighted energies, separately in each physical quarter.
Joint normal draws feed the production `EnvelopeChannelFit` statistic, including
its Chernoff/profile minimum, every quarter omission, channel penalty, competing
bit margin and amplitude timing/rate trial charge. Channel noise is correlated;
no independent per-channel success probabilities are multiplied.

Source Fourier coefficients use 64 midpoint nodes of the independently shaped
and limited complex source. Receiver weights use the production geometry's
8–17-node quadrature. At most 512 physical blocks per quarter represent long
symbols. For subsampled profiles the Gaussian-score maximum uses the limiter's
global bound rather than an optimistic sampled maximum. Covariance factorization
rejects materially inconsistent matrices. Cancellation produces no partial result.

These approximations have no established one-percent tail bound. Source
quadrature, block subsampling, normal tails, native arithmetic, actual sinc
resampling and phase diffusion remain unqualified. The reference assumes ideal
symbol alignment and nominal clock. It omits arrival ownership, other keys/epochs,
continuation and physical completion. Hardware uses actual mean source power;
nominal-power sampled simulation and keyed transmit sources are refused. A
selected receiver with a different oscillator/phase domain or reduced channel
geometry is also refused. Complete-draft probabilities are withheld beyond one
raw bit. A reference above 99% is a candidate for testing, not an RX guarantee.

## Public arrival resource repair, 17 October 2026

The 100 W launch has C/N₀ = −6 dB-Hz. Its explicit short/long targets select
128-second transmit/live profiles even though the separate planner target
selects 2048 seconds. Matching those targets alone does not provide useful
amplitude sensitivity. The current conditional reference is below 1% at
2048 seconds, about **99.707% at 32768 seconds**, and 100% in the finite reference
draws at 65536 seconds. None is qualified whole-Live reception probability.

The previous ordinary amplitude implementation created a bank every second.
At 4096 seconds it priced 8225 simultaneous banks and a 0.797 processing ratio,
exceeding the selected 0.6 CPU budget and the observed workspace allowance.
Its optional shared FFT cache could not justify removing that worst-case price.

Eligible single-public-family, single-profile hardware reception with Clock
Default now uses finite arrival windows with at least ±33 seconds radius and
64-second spacing. Their two-second overlap preserves continuous arrival
coverage. The nominal half-chip grid must divide one second and the amplitude
clock bank must have one row, preserving the preceding timing representatives.
Keys, additional profiles/families, simulation, explicit clock policies, hopping,
DSSS and other timing grids retain their previous admission policy.

The epoch-to-input map is frozen for the capture revision. Admission, refresh,
reconstruction and idle-bank retirement use captured sample coordinates, so
fresh UTC jitter or clock steps cannot open an arrival gap or retain idle banks
beyond the modeled cohort. Future banks are prepared before their original
lower window edge, including the FFT alignment discard and bounded feed size.
Retirement still waits for the original complete search and joined processing;
synchronized tracks retain their physical completion rule. The wider arrival
radius is amplitude-specific and is priced before Auto selection. Complex
timing, the scorer, wire format and physical END are unchanged.

For the reported launch, aligning all three targets to
`-27.154499349597182` selects 32768-second bits (9.102 hours). Current planner and
Console both select amplitude, fit clock/RAM, and expose the same unqualified
99.707% reference. The cohort allowance is 1028 banks and the engineering
processing ratio is 0.1497. A complete absent symbol adds another 9.102 hours
before END for a one-bit message. At 65536 seconds the corresponding allowance
is 2052 banks and 0.2987. These are engineering estimates, not measured throughput.
Per-refresh Live control overhead remains uncalibrated.

Focused verification passes the five arrival tests and four amplitude model/GUI
consumer checks, plus existing coherent `long_fft` and `capture_time` controls.
The arrival fixture includes Default delays 12/29/31/37/95 seconds, a sample
offset, a 500-second UTC step and return, explicit dated arrivals and the
insufficient-Audio fallback. It preserves exact pending `0`/`01`, one row and
full-symbol absence. The Default cases admit 3–5 banks rather than one per second.
The target-edit support check accepts the slower target without clamping it.
Evidence is under `local-evidence/research/eme-launch-100w-20261017/`.

No long-duration 100 W sampled capture or probability qualification was run.
Broad regression, calibration, sanitizer, SDK, native GUI, platform, packaging
and final qualification remain deferred. The earlier standalone 299-capture
study does not qualify this changed Live implementation.

## Focused evidence, 16 October 2026

Discovery captures used the actual sampled source/channel and production
`PatternReceiver`, consuming all PCM and all next-poll burst events:

| Symbol / power | Correct complete captures | Status |
| --- | ---: | --- |
| 128 s / 15 kW | 3 / 4 | Fails high-success objective |
| 512 s / 1.5 kW | 4 / 4 | Small pilot only |
| 1024 s / 1.5 kW | 40 / 40 | Discovery, not qualification |
| 512 s / 15 kW | 40 / 40 | Discovery, excluded from held-out study |
| 512 s / 15 kW | **299 / 299** | Frozen held-out single-bank condition |

The held-out condition was selected and frozen before generating 299 fresh random
seeds. Success required one exact public raw bit, one stream, one physical
completion, no extra/missing symbols, stable pending identity, workspace bounds
and accepted-clock complete-symbol absence. All six capture jobs completed. The
one-sided exact 95% lower bound is `0.05^(1/299) = 0.9900308532071007`.
This establishes a greater-than-99% lower bound for **that standalone, finite
arrival-window, single-public-bank AWGN ensemble**, not for the general model.
See [NIST's binomial interval guidance](https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm).

That frozen study retains its original executable hash and predates the Live
arrival/presentation repair. Two current-build native bridge captures (seeds
501/502, opposite bits) also pass exact content, next-poll identity, full absence
and workspace checks. They do not repeat or extend the 299-capture statistical
claim to the changed executable or continuous Live cohort.

Before the arrival repair, corresponding actual Live probes **failed reception**, with public-only and
one extra synthetic receive key. Both delivered all 6,447,921 PCM samples without
a receiver-health failure and produced no signal row. Exact 512-second receive
geometry and amplitude dispatch were verified. The public trace completed with
no dropped records. The mixed trace exhausted its size budget and dropped
records, so it is incomplete instrumentation, not full trace qualification.

The public deduplication policy had treated amplitude like continuous FFT search,
although amplitude searches a fixed arrival window. The initial seven-second
window missed this waveform's sixteen seconds of padding plus startup. The
candidate now admits fresh public amplitude windows as capture time advances;
continuous FFT retains its existing singleton. Receiver scoring, wire bits,
pending publication and the complete-symbol physical-END rule are unchanged.

Strategy/planner accounting now reserves the unconfirmed rolling amplitude
cohort through its first fully observed symbol, including nearly two symbols
for truncated origins and the slowest admitted clock row. Synchronized longer
streams and stalled work can exceed that engineering allowance; runtime still
checks actual retained bytes and never retires unscored work to satisfy it.
For the 512-second EME point this prices 1057 public windows, or 2114 with one
synthetic loaded key. Pure preflight admits public-only at 2 GiB and the two-family
case at 4 GiB. Auto can choose complete amplitude coverage within the CPU budget
even when an incomplete complex search is cheaper. These are estimates, not
measured hardware throughput.

Amplitude's reported frequencies are coarse channel centers. Presentation now
matches overlapping retained FFT passbands between two amplitude candidates,
preventing a single physical transmission from acquiring duplicate rows when
different windows select different centers. Nonoverlapping passbands remain
separate; any comparison involving a coherent carrier retains its existing
frequency tolerance. Signals inside overlapping amplitude passbands are not
independently identifiable from these channel-center observations alone.

The focused arrival group exercises late public sends at 12 and 37 seconds,
exact two-bit pending prefixes and stable identity, fully scored absence,
concrete workspace accounting, slower clocks, strategy policy, detector
compatibility and passband arbitration. This is functional integration evidence;
it does not turn the standalone 299-capture result into full Live probability
qualification.

Final complete Live controls at 512 seconds/15 kW pass for public-only reception
(2 GiB) and public plus one synthetic loaded key (4 GiB). Each delivered all
6,447,921 samples, exposed exact pending bit `1`, remained pending at six seconds
after the source payload, and produced one completed row with no health failure.
Their admissions/peak resident counts were 1075/1033 and 2162/2060 respectively.
Capture remained open through the full absent-symbol observation. These are
one functional capture per configuration, not a statistical >99% Live claim.

A launch consumer check using the original radio/oscillator arguments, 15 kW
(`--tx-dbm 71.76091259055681`) and all three target values set to
`-9.0926996097583093` resolves to 512-second symbols. With 50% workspace and one
synthetic extra receive key, planner and Console expose an **RX reference >99%**,
with qualified confidence still false and model limits visible. The original
4-W command retains a Console matching-bank reference; its roughly 2.35-day
preview explicitly reports incomplete oscillator coverage after truthful
rolling-resource accounting. The root-level historical executable is unchanged;
run the affected build at `build/dev/datapump-gui`.

Evidence is retained under
`local-evidence/research/amplitude-rx-model-20261016/`: frozen seeds and harness hashes
in `heldout-freeze.json`, exact aggregate gate in `heldout-result.json`, native
capture logs, Live logs and traces. These are local candidate evidence, not
release certification.

## Reproducing a new standalone study

Build the focused model/consumer tests with
`./build.sh test amplitude_probability`. Native capture qualification is opt-in;
ordinary test runs do not launch it. Prepare a new, absent output directory:

```sh
python3 tools/qualify-amplitude-probability.py prepare \
  --binary build/dev/test_amplitude_probability --study /absolute/new/study
python3 tools/qualify-amplitude-probability.py run --study /absolute/new/study
```

Preparation freezes binary identity, condition, confidence level and fresh seed
lists. Run refuses changed binaries/seeds and existing outputs. Assessment checks
all expected seeds, capture outcomes and completion status; incomplete captures
are never passes. Additional selected conditions require a declared multiplicity
or separate confidence claim. Reusing a discovery result as a holdout is invalid.

## Explicit amplitude arrival bounds

The follow-up timing repair applies explicit Clock sync accuracy, propagation
region/offset and Audio error to ordinary public/long amplitude acquisition when
capture dates satisfy the existing strict Audio allowance. It refines the
half-chip lattice by binary midpoints and selects neighboring quarter-chip
coverage cells through the existing FFT energy frontend. Default without hopping retains broad rolling search. Missing
or insufficient timing, including positive provider uncertainty with Audio0,
retains the visible broad fallback. These arrival assumptions do not select an
oscillator model or qualify a success probability.

Public dated banks prepare future epochs before the first retained arrival can
open. The full original window, one FFT block, bounded input chunk and capture
slope determine the lead; extra cohorts are priced before Auto selection. The
amplitude-specific prior and count do not narrow continuous complex searches.
Original full-window allocation preflight remains conservative, so reduced scored
work is not a proportional RAM reduction.

A strong fractional-symbol control exposed a sensitivity gap at an old coarse
arrival representative: its first bit admitted, then its second bit failed the
unchanged envelope gates. The dated path uses quarter-chip intensity resolution
and resolves nearby symbol-zero alternatives before fixing immutable physical
ownership. Common FFT-block publication preserves a one-bit retention budget;
EOF retains pending evidence without manufacturing completion. This addresses
initial dated acquisition, not general later-ordinal timing recovery or a
statistical Live success claim.

## Work still required for a qualified general model

Qualify complete continuous Live cohorts, including arrival ownership, competing
windows/profiles/keys, continuation and physical END. The arrival repair and
matching CPU/workspace accounting have focused functional checks; they do not
supply statistical success bounds for the full cohort. Do not drop loaded keys
or fix a timestamp to suppress refresh in qualification captures.

For long 4 W configurations, avoid years of PCM by certifying actual source-band
energy and the original sufficient statistics. Uniform finite-population block
sampling can bound template variance, source/template covariance and retained
mean energy without assuming independent shaped chips. A rigorous certificate
must include actual resampling, source normalization, phase/arithmetic error,
all unsafe competing admissions, every quarter omission, continuation and END.
Sampling only some keys/epochs cannot rule out an exceptional wrong candidate.
A fast Gaussian reference alone cannot close that qualification gap.

Broad regression, unrelated calibration, sanitizer, SDK, native GUI, platform,
packaging and release qualification remain deferred at the user's request. Prior
strict EME three-bit 2/3 recovery and unrelated ALSA/native workflow failures are
unresolved; focused model passes do not clear them.
