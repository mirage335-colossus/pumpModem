# Adverse HF validation and findings

This record covers the November 2026 HF expansion. The native modem source is
unchanged. A completed experiment is not a successful communication, and these
bounded, deliberately heterogeneous observations are not independent samples
from a worldwide ionospheric population. See [models and scope](hf-experiments.md),
[primary research reconciliation](hf-research.md), and the
[actual upstream codec comparison](competitor/README.md).

## Executed campaigns

All **112 campaign episodes** completed without execution failures or
resource censoring on the local Ryzen 5 PRO 5650U. The receiver used its normal
12 effective logical threads / 24 software workers. These elapsed times include
source/channel work and are not isolated receiver capacity measurements.

| Campaign / receipts | Episodes | Aggregate elapsed seconds | Principal observation |
| --- | ---: | ---: | --- |
| [Unusual mechanisms](local-evidence/hf-unusual/report.md) | 20 | 128.415 | Bright controls work; weak observations and temporary mode absence expose different failures. |
| [Measured replay](local-evidence/hf-measured-replay/report.md) | 15 | 6.455 | Bright finite observations decode; weak ones do not. No invented full-message replay. |
| [Time/energy trend](local-evidence/hf-history-trend/report.md) | 18 | 137.180 | Longer integration can lose an interior bit in moving refraction despite more energy. |
| [Sensitivity brackets](local-evidence/hf-sensitivity-brackets/report.md) | 30 | 42.646 | Phase-bearing and amplitude-only measured controls differ at +4 dB-Hz. |
| [Common HELLO payload](local-evidence/hf-common-text/report.md) | 5 | 236.150 | Four isolated erasures reject the uncoded quiet-HF message; other bright controls complete. |
| [Phase-history follow-up](local-evidence/hf-phase-followup/report.md) | 16 | 144.476 | A fixed phase family recovers conditional evidence on the exact failed aperture; fresh noise seeds qualify the duration finding. |
| [Native coded HELLO](local-evidence/hf-native-fec/report.md) | 3 | 337.964 | RS60 repairs ten erasures; RS20 rejects seven. All added airtime and energy are charged. |
| [RS60 matched energy](local-evidence/hf-fec-equal-energy/report.md) | 1 | 243.900 | Lower per-slot energy causes repeated premature quiet-slot completion; no message is accepted. |
| [Helper plus DATA O/X](local-evidence/hf-helper-controls/report.md) | 2 | 61.720 | Bright DATA completes after unsuccessful Helper search; weak active observations are quiet. |
| [Helper noise-support controls](local-evidence/hf-helper-noise-support/report.md) | 2 | 30.170 | Longer leading-noise support advances the AWGN Helper to its final frequency stage; both messages complete. |

The frozen manifests specify path strata, budgets, controls and stopping rules.
Fresh noise seeds are repetitions of the same specified channel/event, not
independent disturbed days. No pooled success percentage or rare false-alarm
qualification is inferred. Component-only replay failures are reported by their
slot decisions; the runner's complete-message failure counter excludes them.
The two separate FEC control pilots and rejected Helper/eikonal admission
preflight are not included in the 112 campaign episodes. Summed campaign elapsed
time is **1,369.077 seconds (22.8 minutes)** on this host, excluding build,
research, separate control pilots and comparator setup. It is a reproducibility
cost observation, not a guaranteed future runtime or real-time capacity result.

## Separated-frequency Helper coverage

The additional O/X cancellation rows execute the canonical noisy Helper search,
DATA scoring and physical completion at C/N0 20 and 0 dB-Hz, seed 10103. They
use 6,000 samples/s, 2,662,471 samples, a 443.745-second calendar and the full
24-worker budget. Both rows report **`no_coarse_pilot` in all five slots**.
The bright row nevertheless recovers all three DATA bits through the full
public fallback and completes correctly; the weak row scores all three active
observations quiet and accepts no message. Neither row demonstrates successful
Helper acquisition. Helper-enabled geometry and sample-clock noise differ from
the earlier Helper-off rows, so these are operating controls rather than
identical sampled realizations.

Review of the older bright AWGN/quiet-HF controls found no successful Helper
lock there either: they report `insufficient_filtered_level_support` on active
slots and recover through DATA fallback. The source's Helper tail is complete,
but the appraisal's default 0.25-second capture lead matches a canonical
minimum that is insufficient for this particular onset-noise estimator. For
zero endpoint skew, retained noise support is roughly capture lead plus the
earliest arrival, minus two 0.3-second filter-edge exclusions and a 0.1-second
guard. Requiring 0.25 seconds of retained support needs approximately 0.95
seconds of capture lead on the narrow arrival prior. Existing native full-Helper
tests instead use 2 seconds.

The contained capture-lead override preserves the original 0.25-second default
and records the executed geometry. A two-second AWGN positive control separates
this fixture limitation from an ionospheric effect. Changing pre-roll also
shifts the absolute capture-time O/X phase relative to transmission, so the
longer-pre-roll O/X row is not a perfectly phase-matched causal ablation of its
earlier row. Neither a positive control nor DATA fallback qualifies the default
as a successful Helper-acquisition setup.

Both [two-second controls](local-evidence/hf-helper-noise-support/report.md)
complete correctly. Bright AWGN advances past the noise/onset support gate and
reports `onset_branch_frequency_fallback` on its three active slots. Its declared
frequency prior has zero width, and the canonical Helper deliberately declines
to narrow a zero-width interval; this is not a successful narrowed proposal.
The O/X row still reports `no_coarse_pilot` and recovers through public DATA
fallback. These results resolve the suspected fixture support problem without
claiming new Helper sensitivity or tuning a prior to obtain a preferred status.
Existing canonical positive Helper tests exercise nonzero priors separately.

The [Helper/eikonal admission preflight](local-evidence/hf-helper-eikonal-admission.json)
is explicitly rejected with `HF dispersion approximation exceeds its admitted
phase or sample-band bound`. It is a numerical model restriction, not a reception
failure in a physical channel. The O/X model admits the full emitted band
without that local-dispersion approximation; the deposited scalar replay remains
Helper-ineligible. No guard was relaxed to obtain a result.

## A concrete nonmonotonic time/energy result

For the analytic moving parabolic layer at C/N0 = 0 dB-Hz, seed 10201:

| Actual DATA bit duration | Moving-layer result | Matched static-layer result |
| --- | --- | --- |
| 23.2 s | All three active observations quiet | All three active observations quiet |
| 69.6 s | Three correct bits and complete message | Three correct bits and complete message |
| 139.2 s | Interior bit 1 quiet; integrity rejects message | Three correct bits and complete message |

The long moving observation's log-evidence is **8.42**, below the unchanged
operational threshold **21.493**. Its two neighboring bits have log-evidence
81.50 and 46.90. The matching static interior observation reaches 116.70.
The longer episode receives approximately twice the energy of the medium one;
this is not a reduced-energy comparison. Short-block, coherent-gain and drift
supplements actually ran on the failed aperture: 1,118 short blocks, 3,756,998
basis rows, and a 512-slope by 1,017-frequency drift calculation. Thus an absent
fallback does not explain this particular failure.

At -6 dB-Hz the known-channel, known-waveform presence reference improves from
roughly 0.003 to 0.424 to 0.988 detection probability as the observation grows.
That reference fixes the hypothesis and phase and omits acquisition and channel
estimation. It demonstrates available information under private conditioning,
not an achievable unknown-channel receiver threshold.

The original clean-reference projection has **zero reference delay**, whereas
the static channel delays the waveform about 2.099 ms. Its approximately 45.7%
whole-aperture retention therefore cannot be called an irreducible phase loss.
Pairwise static/moving comparisons retain that same conditioning. Follow-up
diagnostics explicitly declare a 2 ms reference delay, the center of the public
arrival prior; the production receiver receives no channel truth.

The follow-up preserves contrary observations: with fresh seed 10601, both the
medium and long moving episodes complete; with seed 10602, both lose one bit.
The latter medium case has an interior erasure; the long case loses its last
active bit and physically completes prematurely on that quiet observation plus
the next quiet slot. Both messages are rejected.
Both fresh static long controls complete. Thus the original nonmonotonic result
is an observed realization, not a general law that a longer duration always
hurts. The separate long noise-only episode produces no false DATA or physical
completion.

### A bounded alternative actually tested

The frozen 97-model smooth-phase dictionary, including its normalized model and
bit priors, is evaluated on the **exact missed interior aperture**. At 0 dB-Hz,
its signal-versus-noise mixture log-likelihood is **120.127**, versus **-2.704**
for the zero-phase model and **-1.765** on matched noise-only input. At -6 dB-Hz,
the corresponding mixture is **25.858**, versus **-2.567** and **-0.534**.
The correctly delayed static controls produce mixture values 121.539 and 21.974.
The fixed dictionary concentrates on its 12-radian, 120-second trajectory for
the moving model without reading the channel parameters or realized waveform.

The long-aperture evaluator uses 103,710,460 reported complex multiply-accumulate
equivalents, 17,808 bytes of accumulator workspace and about 0.747 CPU seconds.
The latter times just the bank, excluding source preparation, channel rendering,
template production and operational acquisition. Correlations and likelihoods
use `long double`, with `double` oscillators; this is diagnostic arithmetic, not
a production automatic-precision or GPU result. On the measured +4 dB-Hz
excerpt, the same untuned family gives mixture values **23.362** (complex),
30.028 (amplitude-only) and 29.056 (phase-only), with paired null **-2.547**.
For the complex excerpt, the zero-phase model actually favors the wrong bit;
the phase family favors the correct bit. Its winning 8-radian / 90-second
trajectory is a local approximation over 19.4375 seconds, not measurement of
a 90-second ionospheric period. The zero-phase baseline omits ordinary carrier
frequency compensation, so this measured comparison alone does not establish
a benefit beyond an affine-frequency tracker. The native receiver already
searches frequency but has different conditioning and work limits. The long
analytic aperture spans more than a complete nonlinear phase cycle.

This provides a concrete candidate beyond unlimited independent short blocks:
bounded, structured phase-history hypotheses can retain information at low
nuisance rank. It does **not** show operational reception at those scores.
Template timing and carrier are supplied, the signal family is finite, and no
public arrival/frequency search, broad unknown phase distribution or continuous
false-alarm budget is included. A production candidate must account for those
costs and priors, preserve all public hypotheses and qualify complete receiver
service. The suite exposes the opportunity without installing an unqualified
production change or claiming a universal optimal receiver.

## Measured phase and finite information

The deposited Ding et al. complex event, its amplitude-only counterfactual and
its phase-only counterfactual all decode at +8 and +12 dB-Hz and in three fresh
+8 dB-Hz noise realizations. At +4 dB-Hz, amplitude-only decodes (log-evidence
23.1), while complex and phase-only observations are quiet (15.9 and 18.1).
The matched known-channel presence reference for the complex observation is
approximately 0.993 at that energy; its conditional binary error is a different,
less demanding decision problem.

Here the actual receiver reports `fine_cells_too_slow_for_short_block` and
`coherent_gain_center_grid_exceeds_work_limit`; the drift supplement still runs.
The 25 Hz public frequency prior makes these work limits consequential.
Removing them without bounding complete-service cost would violate the
receiver's computational-ceiling objective. Candidate improvements must retain
the public hypotheses and account for nuisance models and false admissions.

The original complex replay retains approximately 2.9% of signal energy in a
single unshifted clean-template projection, versus 92.7% for amplitude-only.
Short complex-gain blocks retain much more (about 99.7% at 0.25 s and 75.8% at
4 s), but their additional fitted dimensions consume evidence. Energy retention
alone is not a successful detector. The late excerpt is the same event, and
the alternate clock is metadata sensitivity, not another measured channel.

The 2,048 deposited values have no deposited sample clock or complete receiver
transfer calibration. Neither tested detrending convention reproduces the
paper's reported phase deviation. Exact sample provenance and that disagreement
are retained in the [fixture record](fixtures/hf-ding-2021/README.md). The model
does not tile the record, assign it a fictitious long stationary lifetime, or
apply its scalar narrowband envelope across an unqualified broadband signal.

## Physical absence, capture loss and coding

The constructed mode-transition case produces two quiet active observations,
premature physical completion and later DATA. The recovered part does not make
the original message complete. The receipt originally counted zero resumed
slots because its reporting boundary considered only explicit fade controls;
the corrected boundary includes the last scheduled mode-voltage transition.
The old raw receipt remains unchanged. Seven subsequent DATA observations lie
wholly after 300 s; an eighth begins just before that boundary.

An independent bright RS20 capture-gap pilot has two incomplete observations,
34 later valid DATA observations and two final quiet observations, yet **no
physical message completion**. This matches the canonical receiver contract:
invalid captured input invalidates the episode, and the quiet observations only
rearm it. Capture invalidity is not a located FEC erasure. Its receipt remains
[here](local-evidence/hf-fec-capture-gap-pilot.json), rather than being converted
into a claimed positive repair test.

A separate [one-slot fade pilot](local-evidence/hf-fec-one-fade-pilot.json)
then supplies a genuine located erasure: RS20 recovers `A` from 71 observed bits
and one missing bit at public slot 36, and completes only after quiet slots
72 and 73. Raw-clean success remains false, while decoded success is true.
The erased bit was zero, so no byte value changes; the native integrity region
still reports one erased/repaired byte. This checks that repaired erasures are
not incorrectly equated with changed byte values. `--fade-slots 1` is explicit;
the original two-slot fade control and its premature-completion behavior remain.

The appraisal now selects the existing native Off/RS20/RS60 codecs. The original
raw-clean `correct_complete` result is preserved. Explicit coded trials instead
use a separate `correct_decoded_complete` endpoint requiring exact decoded bytes,
the entire source span, consistent located erasures and FEC region statistics,
exactly two scored final quiet slots, and no premature/extra physical completion
or false DATA outside the message. Short-message CRC is not authentication.
No packet length is used to choose receiver DSP geometry or physical completion.

The coded HELLO campaign changes the uncoded comparison materially. At the
same +8 dB-Hz quiet-HF operating point, RS20 rejects seven erased bits, but
**RS60 recovers all payload bytes despite ten erased bits**, with one complete
physical message and no false DATA, wrong observed bits or premature end.
Its bright AWGN control is raw-clean and decoded-correct. The RS60 quiet case
has 184 wire bits plus two final quiet observations, spans 11,123.765 seconds,
and transmits 4,171.469320 normalized signal-seconds. That is 2.830761 times
the Off fixture's energy; the larger successful repair must not be attributed
to a free sensitivity gain. These are existing canonical codec options, not
new FEC algorithms.

The follow-up [equal-energy manifest](campaigns/hf-fec-equal-energy.json) reduces
RS60 C/N0 to **3.480967777893 dB-Hz**, using the ratio of those clean transmitted
energies before inspecting the new outcome. It preserves the same seed and
public receiver configuration, charges the full coded calendar, and stops after
one episode. No received-fade normalization or additional level search is used.
That episode completed uncensored and **failed delivery**: 100 active bits were
scored quiet, producing 26 physical completions, 25 premature, and 26 codec
rejections. There were no accepted messages, wrong observed bits or false DATA.
FEC cannot recover a message that physical completion has already fragmented.
The +8 dB-Hz repair is real, but it does not survive the original total-energy
budget in this stationary-model realization. This is not proof that every
possible coded configuration fails at that energy.

The remaining useful receiver investigations are consequently specific:
retain bounded phase sufficient statistics across an observation instead of
rescanning history; compare structured low-rank phase families against the
existing affine/drift alternatives after accounting for public acquisition;
and measure the resulting complete-service cost with unsuccessful candidates.
For coding, distinguish correction of isolated valid erasures from the
per-slot sensitivity needed to avoid premature physical completion. Merely
adding parity or lifting all work guards does not resolve these observed
failure mechanisms. These are evidence-backed implementation candidates,
not changes to the operating modem in this contained appraisal.

## Comparison against existing native applications

The optional harness executed **36 channel/capture observations** through native
FLDigi 4.2.06 and WSJT-X 3.0.2, plus **15 decoder replays of retained captures**.
Replays are not additional channel observations. Its independent calibration
checks real/complex noise conventions, sideband conversion and source energy.
Decoder versions, switches, complete output and actual waveforms are retained.

At the ordinary points, quiet HF at +8 dB-Hz defeats PSK31, Olivia and FT8 in
these episodes while WSPR's different structured beacon service succeeds.
Disturbed HF at +20 dB-Hz defeats all four tested configurations, including the
bounded stronger FT8/WSPR decoder replays. All bright AWGN controls succeed.

The HELLO energy comparison charges the entire clean transmitted waveform.
Robust Off sends 65 wire bits, approximately 1,473.62 normalized signal-seconds
over a roughly 66-minute calendar. At +8 dB-Hz in the stationary quiet-HF model,
four isolated erasures reject its message. At the same total transmitted
energy/noise density, PSK31, Olivia and FT8 recover HELLO in their shorter
observations; streaming decoders also output junk. These outcomes do **not**
establish a best-codec ranking: time, coding, physical completion, incorrect
output and structured-content contracts differ. The long Gaussian realization
is an explicit stationarity assumption, not validation of a real 66-minute path.

Likewise the O/X cancellation test halves mean single-port power. Olivia's
failure at +20 dB-Hz is also seen in the static case at +17 dB-Hz, while PSK31
passes both polarizations at +30 dB-Hz. This contradictory control prevents
attributing every O/X loss to a special phase-processing weakness. FT8 and WSPR
already use phase/coherent-block processing and FEC; they are not amplitude-only
QRSS baselines. No orders-of-magnitude Robust advantage has been established.

## Software assurance and evidence lifecycle

The final build passed **13/13 CTest groups in 68.01 s**
([log](.build/hf-expansion-complete-checks.log)), including 323 native phase-bank
assertions, 40 strict runner cases, actual RS20 one-erasure recovery and the
two-second Helper noise-support regression. The
canonical modem source SHA remains
`be47453d07fb27afe4cf4c168f76390de193cf57a403009a1078dccca6f01aa2`.
The earlier canonical Triad 32/32 run remains applicable to that unchanged
source and is not reported as freshly rerun. Raw receipts bind each campaign
to its exact appraisal source and binary; later fixes do not rewrite them.

The final [build identity](local-evidence/hf-final-build-identity.json) has
appraisal source SHA-256
`f931308a0906e1719de56d7961598a47f25a44c1442c5e371851750c6418a4d4`
and binary SHA-256
`96526c56c50975c254866024e7dce72d23b9d86607012a13186c5d0268bc8dc3`.

The main HF campaign source identities are:

| Campaign generation | Appraisal source SHA-256 |
| --- | --- |
| Unusual mechanisms / measured replay | `b6653f2fd62489ec67d5a8680827c49169cc1bb2c1fb75ed84b058231fb90152` |
| History / brackets / common text, with corrected mode-boundary reporting | `359d2977b91a58a7256f214012e2dce46d436c68b221abfe8634ee9749b6332e` |
| Phase follow-up, with later-slot reference cursor correction | `edf2623b1c05d5ea1db3f60f81ff2bf60e585fca9d4ccd1ab72828240785ccf7` |
| Native FEC / equal-energy / Helper campaigns and one-slot fade pilot | `a2293961c0069191189198d8f4c45a1305438024f14f975f8e6e380a2d0239cf` |
| Explicit two-second Helper noise-support controls | `8ab1c2c39a109011eb436c3e11fb94443c38ea466839a0fe2ad4235cb0806098` |

These builds use GNU 14.2.0 Release, OpenSSL 3.5.7, base commit
`d37df41e33e49ef70b4d09b1dbebd81e17c93b8b` plus contained changes, and build
options SHA-256
`3636f58f5e5803a48c78c56be3de84fce810a285a8b7c7222781040e596e6206`.
Source identity includes tests and orchestration, so a later added assertion
can change that identity without changing the modeled channel or receiver.

Initial setup/check failures were repaired without weakening physical tests:
deferred CMake registration uses an include rather than a forbidden subdirectory;
finite-tone assertions now compare the intended interior support; measured
noise-slot controls and comparator test paths are accepted correctly. Review
also found a later-slot evaluator source cursor that had skipped preceding
samples. Independent forward reference channels now advance continuously from
sample zero. This affects private selected-slot diagnostics, not the native
receiver. FEC receipt mutations exposed unchecked slot counts, authentication,
repair counts and region extents; strict validation now rejects those mutations.
The first final integration attempt also compared changing OS free-memory
snapshots as if they were waveform geometry (12/13 groups passed). The repaired
plan-only comparison supplies the same explicit 1,024 MiB allocation to all
three calls and retains exact equality of their complete plans; runtime defaults
and the full worker budget are unchanged. Its focused integration passed, then
all 13 groups passed in 65.84 seconds. The later capture-lead assertions and
actual Helper control are included in the final 68.01-second result above.
Original failure and intermediate pass logs remain distinguishable.

Keep compact campaign receipts, frozen inputs and current comparator captures
while they support these unresolved sensitivity findings and their regression
tests. Remove redundant captures after a superseding matched comparison has
preserved the relevant failure, conditions and identities. The compiled measured
fixture is indispensable to the finite-event question; the small original MAT
and interpretation text support provenance. The 21 MB upstream archive and
unused simulated arrays were removed after verification. Build/dependency caches
serve repeatable current tests. No new RunPod resource was rented: these bounded
native experiments complete locally in minutes, and GPU rental would not add
an actual-device or capacity claim needed for this HF investigation.

The [source-closure verification receipt](local-evidence/hf-source-bundle-verification.json)
records the packaged file count, byte sizes, archive/manifest hashes and successful
extracted-source verification. The closure includes the exact measured fixture
and comparator sources, and excludes credentials, captures, binaries and caches.
Only that compact verification receipt is retained; the redundant archive and
extracted source copy are removed after checking. External native codec packages
remain optional separate bootstrap inputs rather than growing the source upload.
