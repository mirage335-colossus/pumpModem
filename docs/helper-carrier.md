# Helper Carrier

Helper Carrier is an experimental independent three-tone reference for propagation
diagnostics and optional assistance to Robust reception, including operation
without GPSDO hardware. It can accompany an ordinary transmission or transmit
alone. Its product name does not classify an operation as an automatically
controlled radio beacon. Operators select configurations appropriate to their
frequency, jurisdiction, license, station control and other authorizations.
The software retains general-purpose power options; it does not impose a blanket
regulatory power limit or certify an installation's compliance.

## Development priority

The primary development priority is useful frequency and phase search-space
narrowing for the ordinary Robust receiver. Give particular priority to Helper
Carrier as an HF GPSDO alternative, and also support its use supplementing a
GPSDO at microwave frequencies. Assess work by its contribution to that ordinary
receiver, including its actual acquisition and search costs and the limits of
the evidence.

The auxiliary APSK carries identification for a regulatory purpose. Its inability
to decode except at point-blank conditions, perhaps around 40 dB total path loss
or an even more extreme link budget, is of no consequence to this priority; that
example is not a performance threshold or test target. Practical reception of
weak-signal beacons has generally relied on unique frequency assignments for
identification. Robust users should also transmit their callsigns appropriately
in ordinary operation, casually as with SSB voice, for practical identification.
Operators remain responsible for applicable identification requirements.

Do not prioritize implementation or testing of successful APSK decoding ahead of
ordinary Robust frequency and phase narrowing, or make APSK decode success or
weak-signal sensitivity a dependency of that work. This prioritization does not
remove existing identification behavior or applicable regression requirements:
retain and report failed or unexecuted tests rather than treating them as passes.

## Lead-in operating requirement

When Helper Carrier is enabled, the ordinary Robust receiver should begin
acquisition only after at least 36 seconds of Helper observation for frequency
narrowing. The receiver observes the Helper during this interval; it does not
start ordinary Robust acquisition merely because an earlier precision estimate
looks useful. The Helper-only transmit lead is fixed at exactly 95 dwells of
400 ms each, or 38 seconds, whether FHSS is enabled or disabled. It provides
90 dwells (36 seconds) for observation and five dwells (two seconds) for initial
Helper detection. In the intended use cases the Helper is well above the noise
floor, and this detection margin is considered excellent.

Plan operation around a listener being present for the full lead-in. The
waveform has no mandatory cue identifying the start of that lead, so straight
pilots alone cannot tell a late listener whether it is hearing lead-in or
payload. Reception by a listener joining an ongoing transmission without the
full lead-in is not an expected Helper Carrier use case. Do not prioritize
implementation or testing of late joining for the ordinary Robust receiver, or
add onset-identification machinery solely to support it. This does not waive
applicable existing regression requirements or change how results are reported.
The detailed admission and completion policy is in [Link Planner and lead time](#link-planner-and-lead-time).

## Quiet-spectrum assumption

Helper Carrier is intended primarily for experiments in selected, relatively
quiet spectrum. Within the expected high-pilot frequency range derived from the
applicable oscillator models and bounded by the actual capture geometry, the
strongest tone may be assumed to be the correct high pilot. An unrelated constant
tone close enough to be confused with it is assumed to be extremely rare in the
intended operating environments. Identifying or rejecting such a competing tone
is not a current high-pilot detection requirement.

Examples include underutilized spectrum such as selected Part 15 frequencies,
selected Earth–Moon–Earth spectrum at GHz frequencies, licensed spectrum such as
amateur radio, and local line-of-sight links. These are choices of operating
environment, not claims that every such band is free of interference. The
experiments may demonstrate a GPSDO-equipped radio or the feasibility of an
Earth–Moon–Earth link for a specialized system with a hardware Helper Carrier
and equivalent frequency stability. Helper Carrier is not intended for important
messages; if unexpected interference affects an experiment, it can be repeated.

Do not implement more elaborate high-pilot identification or competing-tone
discrimination, or add tests requiring rejection of unrelated tones, solely to
handle interference excluded by this assumption. Such work needs an explicit
change to the operating assumptions or requirements. Continue applicable tests
of acquisition and tracking under noise, frequency offset, drift and other
in-scope conditions, and preserve the existing regression requirements.

## Transmitter waveform and receiver scope

The transmitter uses a fixed 550 Hz low pilot at −3 dB, a fixed 950 Hz
auxiliary at −9 dB, and a high pilot at 0 dB relative to the high pilot's
steady-tone power. The high pilot is at least 2400 Hz and rises as needed for
Robust occupied bandwidth and the applicable separation guard. The low pilot
and auxiliary do not move to make room for a wider Robust waveform. Robust
carrier and rate must leave enough space for them. These frequency and power
ratios describe the transmitted audio waveform; actual RF ratios depend on the
radio and audio chain.

This transmitter change is not a receiver qualification. Acquisition, tracking
and decoding for the new waveform remain under development. The Live high-pilot
path reacquires phase within each supported actual-retune dwell. An in-progress,
separately funded worker also measures the low pilot on matching high-pilot
apertures and publishes conditional diagnostics; it does not yet use those
measurements for ordinary receiver frequency or phase correction. Earlier
two-tone receiver measurements and binary bindings in
[validation](validation.md) apply only to their stated source and waveform, not
to this transmitter. Physical display, audio and radio testing and full
regression qualification remain open.

The [29 October development checkpoint](validation.md#three-tone-helper-carrier-implementation-checkpoint)
includes a rebuilt GUI and focused measurements for the earlier waveform. Its
last focused retest passed four of six cases; the 24 dB-Hz frequency-only
fallback and registered three-tone auxiliary decoder failed. A separately selected
stress and Live observation run passed 15/15. The previous focused retest passed
15/16 cases, including eight Live observation cases, and a separate 5 ms
fade-support problem remains. Later metadata results are described below; full
qualification remains open.
A private factory boundary test also found that a proposed grid heuristic could
increase templates and workspace; it is not deployed or qualified. Its specific
geometry and required guards are recorded in validation.
A private truth-seeded prefilter-mixing probe also showed transition error and
an image-crossing failure on synthetic tones; it has not qualified causal
correction or payload reception. Its reference and measurements are in validation.
A subsequent causal component probe recorded predicted uncertainty escapes in
one signed-RF case; it does not qualify production correction.
The strict rolling Live check
and a three-tone frontend stopband check initially failed and passed in later
focused reruns. The Live rerun was Helper-only and did not establish payload or
physical END. A separate actual-retune fixture passed exact `001`, pending before
Helper loss and scored END. A later matched timing-only laboratory comparison
executed 13,734 FFT jobs with the observation wait bypassed versus 882 with it
enforced; both recovered `001` and scored END. A pre-analysis Session CPU
measurement also decreased in that instrumented comparison, while sampled
workspace changed by only about 0.17%. Its scope and audit limits are in
[validation](validation.md#three-tone-helper-carrier-implementation-checkpoint).
This is not a hardware throughput or full HF/EME qualification. A diagnostic two-pilot
model also produced interval escapes in strong-reference controls. Those controls
had shared channel phase diffusion while their trackers assumed zero phase
noise. Explicit-zero diffusion controls showed no pair escapes in 32 seeds, but
do not establish operational covariance or prove the full cause of the earlier
escapes. Neither their precision nor useful narrowed acquisition is yet qualified.
Helper-enabled
CLI batch WAV/device `rx`, `status-rx` and `simulate` are currently unavailable
while their causal admission path is incomplete. Subsequent development code
also defers Live Helper-assisted admission without an oscillator-search policy.
These limits are distinct from the earlier two-tone results below.

The low-pilot worker keeps only bounded recent raw samples and a separately
funded current-aperture snapshot. It shares optional CPU credit with auxiliary
work, yields to ordinary payload processing, and discards stale work after
clear or provider changes. A conditional pair-only numeric model accepts matched
completed apertures in date order; its output is diagnostic and does not grant
phase, timing or ordinary-acquisition authority. A worker/model quantum can
still overrun its time slice before the resulting debt is charged. The current
actual-RF segment provider is the sampled simulation channel; test-only mock
injection is not a hardware retune adapter. Fixed-RF repeated-window fusion,
operation above the model's 54 MHz range, selected-tone ambiguity, and
operational pair covariance remain unqualified.

The auxiliary observer is optional and does not qualify either pure pilot. An
earlier decoder designed around two tones failed the registered three-tone
regression in all eight fixture geometries. Its scratch causal-bootstrap
experiment also produced no metadata at 30, 40 or 60 dB-Hz and selected false
edges in noise-only controls; those historical results are in the
[validation record](validation.md#three-tone-helper-carrier-implementation-checkpoint).

The earlier 240 ms-reversal metadata waveform and its decoder passed focused
`test_helper_receiver` runs for all six pair, ten two-tone and eight three-tone
cases. The three-tone cases included four with retained history and four after
reset recovery. The `live_helper_clock_bridge` checks passed at ±300 ppm and
±550 ppm for that earlier waveform. A focused test of its 24 dB-Hz high pilot
produced 11 references, with the first usable reference at 7.99998 seconds; its final sample, frequency and uncertainty
matched with 137- and 4096-sample chunks. At the same configured level the 80%
modulated auxiliary produced no usable narrow reference, while its published
frequency intervals covered the true frequency. This auxiliary result does not
qualify the pilot or imply useful metadata sensitivity.

At the configured 20.76 dB-Hz weak encrypted-auxiliary fixture, the original
HEAD waveform also produced zero references with the unchanged receiver and
profile, confirming that earlier failure. On the earlier 240 ms-reversal
three-tone waveform, the high pilot produced one truth-covering reference with
no phase or timing authority; the encrypted auxiliary produced zero references.
Its shifted auxiliary check failed its requirement for a usable reference within
three seconds. These focused observations do not qualify the newer dwell waveform
or establish acquisition qualification.

The supported explicit-oscillator Live raw-tail fixture passed exact 64-bit
payload, pending progress and scored physical END checks with the earlier
240 ms-reversal waveform. Its trace also recorded a Helper-assisted commit. The
fixture uses sampled simulation and does not qualify the revised dwell waveform,
hardware reception or performance across operating conditions. An earlier
full non-probability contract run had 36 failures out of 321 tests; the subsequent
focused checks do not replace a complete rerun. The probability stage timed out
after 1500 seconds and remains incomplete. Full qualification remains open.

A bounded [matched waveform study](../local-evidence/helper-three-tone/pair-waveform-continuation/v2/summary.txt)
fed the same actual three-tone and Robust samples to high-only and conditional
pair models. Without a near-low spur, pair estimates had no DATA or audio-scale
interval escapes in the tested cases. At the nominal 40 dB-Hz label, the pair
and high-only final bounds were similar; the nominal 30 dB-Hz pair result may
refer to an earlier date and is not a matched endpoint comparison. Stronger
tones only 0.4 or 1 Hz from the low pilot produced many pair interval escapes.
The 30/40/60 labels are configured targets, not measured pilot C/N0. These correlated
synthetic-channel observations do not qualify pair selection or ordinary
acquisition; the low-pilot model remains diagnostic.

An experimental [high-only predicted-low discriminator](../local-evidence/helper-three-tone/pair-waveform-continuation/guard/summary.txt)
retained all tested clean 40/60-label paired queries, including both signs and
±290 ppm audio error, without DATA or audio-scale interval escapes in those
small ensembles. This conditional check does not authenticate a pilot or make
the pair a receiver authority. Helper is manually enabled on quiet spectrum;
the operator may accept the strongest plausible high pilot within the declared
oscillator-derived range. The discriminator does not require auxiliary
metadata or a known transmission start.

A later experimental Live pair-authority trial failed strict reception with
700 kHz RF steps when the settled FIR/NCO-frequency hull exceeded the
constructor bound. In a separate full-support comparison with 10 kHz steps,
both pair and high-only arms completed `001` with pending identity and scored
END, but pair work was 1,806 versus 1,764 FFT jobs and Session CPU was nearly
unchanged. Its strong cable simulation and non-cryptographic input witness do
not qualify the supplied HF cases or hardware. Its source changed after the run
and a complete source binding was not found; joined snapshots for 10 of 13
owners were interrupted or incomplete, while scored END is a separate test
assertion. The q/FIR/settled-support
question also affects the earlier bare-nuisance high-only coverage claim. A
subsequent large-step comparison used a hindsight envelope and completed in
both arms, but showed no work reduction. That envelope is not a causal receiver
solution, and incomplete final owner snapshots do not prove complete scored
coverage. Pair-driven production authority remains off;
[validation](validation.md#three-tone-helper-carrier-implementation-checkpoint)
retains these outcomes and their limits.

## Manual reception

The following receiver behavior and observations describe the earlier two-tone
waveform and are not yet validated for the new transmitter. Helper reception is
manually enabled. On quiet spectrum, the receiver accepts
the strongest qualifying straight tone inside the oscillator-derived expected
pilot range, bounded by actual capture geometry. There is no artificial ±100 Hz
cap. Discovery and isolation precede local tracking. Auxiliary detection or
metadata decoding does not qualify frequency assistance or authenticate the
transmitter. Auxiliary clock/timing information requires its own measured
qualification; its modulation is never applied to payload phase.

One ordinary Robust bank receives upstream stabilized input before narrow
filtering/downsampling. Folded real mono retains its frequency-origin-only path.
Dated residual-frequency windows narrow existing and new RF catalogues while
retaining unresolved clock/timing alternatives. Dated whole-score support now
has funded minimum provenance even after optional refinement is reclaimed.
Folded real mono now receives dated raw frequency provenance through the same
bounded stage, preserving `q=0`, no phase correction and unresolved LF choices.
Additive constructor headroom restores the previously blocked early owners in
the targeted actual-RF case, without changing measured uncertainty or dropping
unresolved LF alternatives. Every
payload sample and date remains required; frequency knowledge cannot manufacture
an earlier phase observation or advance the processed frontier.

Uninterrupted pilots can retain dated continuous numeric support. After actual
retuning, each hop's phase is independent: usable dwell frequency measurements
enter the RF/audio model using actual RF and sideband, and local pilot phase
must be reacquired. The raw stage is bounded at 400 ms. Model frequency
availability is bounded to 0.5 seconds from independent evidence; retuning cannot
renew that witness. The current RF/audio evolution assumptions through 54 MHz
are explicit and uncalibrated. Numeric retention is separate from the fixed
transmit lead and must not retroactively establish phase support.

Without a fresh qualified reference, new acquisition may wait. Established
reception retains ordinary recovery, draining, per-bit progress and the same
pending-row identity. The ordinary wire format, framing, payload power targets,
Rate and keyed timing remain the contract. Neither Helper loss, metadata, EOF
nor expected length completes reception: consecutive fully scored absent-symbol
durations must cover six seconds. Optional Helper work yields to payload work;
no complete broad receiver bank runs beside the assisted bank.

Measured work changes retain their comparison scope:

| Change | Support or comparison scope | Executed FFT work | Process CPU |
| --- | --- | --- | --- |
| R4 whole-score refinement versus R2, private matched prefix | 364→268 pairs; all nine unresolved LF alternatives retained | 35,672→26,264 (−26.37%) | 65.756964→49.005478 s (−25.47%) |
| Filter/Hilbert index walks | Identical reference/support records, 785 detector bits and 260 GUI commits | 3,930 unchanged | 4.435823→2.610347 s (−41.15%) |
| HF15k initial-descriptor support, separate captures | Collision owner restored; 77/77 queries contained, strict END failure exposed | Owner 0→56; total 6,412→11,711 | 92.415521→152.656895 s; no matched-PCM ratio |

These are separate source-bound comparisons, not additive savings, calibrated
confidence or hardware-throughput guarantees. The later record
fallback adds no benefit in the HF cohort where its new branch is unexercised.
The earlier 109-pair mask lacks whole-score certification and is not a valid
baseline for R4. The R4 prefix remains two of eight bits, without END.
A separate actual-RF loss control demonstrates 30 prior-only corrections,
including five after loss without renewal/new phase, while retaining exact
`001`, ID 2 and scored END. Its FFT work rises 8.16% and CPU does not improve;
only an END-covering PCM prefix is hashed, not the entire tail.

The preceding `85a5/d8e3` supplied HF40 run passes exact eight-bit wire, public pending identity,
scored END and replay: 194.956 source-audio seconds, 103,663 FFT jobs and
869.479 process CPU/114.151 wall seconds on six physical cores. Cost increases
over the preceding `f37/949` pass (66,192 FFT jobs/562.537 CPU seconds). The
independent audit attributes 94.40% of the extra FFT work to 31 restored owners;
the rest is wider dynamic masks. This is simulator acceptance, not CPU savings,
hardware real-time or complete
block-latency/backlog qualification. Earlier HF40 failures remain preserved.

Current HF15k captures 1,188,236 samples and executes 11,711 FFT jobs/306
transforms/207 owners in 152.657 CPU/41.049 wall seconds. Initial-descriptor
support restores epoch-31 owner 39 to 56 FFT jobs, with 77/77 queries contained.
It then declares END after failed address 74 while canonical address 75 scores
110.90 in the same window and DATA continues. Four premature native ENDs make
this a strict failure despite exact canonical eight bits and intended prefixes.
The older `2101/3b7` capture has consistent END timing only with that collision
owner unexercised. Per-ID public Snapshot lifetimes remain unavailable. A
calendar-END proposal is held, unbuilt and unintegrated after bounded review:
weak-chain confirmation cannot prove a historical standalone positive.

Qualification remains incomplete. The frozen `21bf/913` Rate-360 Fake-FHSS case passes exact eight bits, one public
pending row, END and replay after the dated-provenance repair; the old zero-owner
failure remains separate and was not a physical precision limit. Every FFT job
joins dated support with all nine LF alternatives; its polls are correlated and
its incomplete timing joins do not qualify hardware backlog. The RF-only full
control exhausts both CPU budgets and revises an ID's published prefix, so it
is not accepted and gives no full cost ratio. The current physical-birth guard
passes paired actual-History checks, without a full control rerun or END fix.
The latest private EME deficit-gate attempt reaches its CPU cap before a
canonical first score or fixed-fence input binding; it supplies no cost ratio
or score gain. Stronger auxiliary markers alone cannot remove the current
backend's timing alternatives; that is an implementation limitation.
Full EME, current 1,024-byte completion,
54–450 MHz coverage and hardware retune testing remain unqualified; estimator
coverage retains documented escapes. Framed-text,
attachment and bounded retune/loss passes retain their own scope. The full
contract run has failed, with incomplete internal coverage and retained
assertion failures and has not been rerun on this app; overall qualification
remains incomplete. Gate and terminal-join prototypes remain
private. See
[validation](validation.md) for every failed case, command and exact binding.

## Earlier receiver architecture decision

For the earlier two-tone source, the bounded code/evidence review, including Astra,
froze estimator tuning:
Q, lag/history, uncertainty multipliers and extra lead remain unchanged. The
HF40 reference quality and its modest residual RF penalty are accepted as useful
on the stated six-physical-core/2 GiB simulator; matching GPSDO specifications or
an ideal CRLB is not the acceptance criterion. Complete platform acceptance remains unproved. Minimum
provenance is funded after optional-refinement reclamation;
additive constructor headroom passes the targeted counterexample. A newest-only mask or unsupported broad shadow
bank would discard the evidence obligation.

The compact [error/work budget](../local-evidence/helper-two-tone/architecture-review-r1/reference-budget.json)
is an immutable earlier-binding snapshot; [validation](validation.md) links
current audits and numbers separately. The RF-only GPSDO control retains
100 ppm audio/Default 70 ms timing, but its capped run and unnotched Helper-OFF
mixture supply no fair CPU ratio. The fully disciplined control also changes
audio and lead. Coupled RF/clock catalogues are not independent factors.
Full HF40 uses the legacy M route: its existing `D−H` shear retains pilot/audio
correlation and all nine LF alternatives. The actual-RF typed DATA route instead
marginalizes joint covariance to a conservative scalar DATA-frequency bound with
zero clock coefficient. That separate, unquantified optimization gap is neither
an HF40 cost explanation nor a correctness/physical-floor finding. A future dated
affine parameter has no implementation or CPU claim.

A whole instantaneous-frequency hull is a conservative sufficient condition,
not a proven necessary statistic for the detector; this conservative design is
sufficient for the supplied HF40 simulator case. Scores depend on accumulated
residual phase, complete FIR/support history and correlation. A later narrow
average cannot replace that history or retrodate support without a score-level proof. Constant CFO
can be searched with RF candidates; constant phase is absorbed by quadrature.
Drift, random phase and correction modulation can reduce coherence, which extra
constant-frequency lanes cannot restore. LF, UTC and key alternatives remain.
Unknown hop phase needs qualified local phase correction or the ordinary
sectioned mechanism; the RF/audio model carries frequency, not phase continuity.

Offset, drift and random instability are different quantities; static ppm is
not a drift rate. [NIST's oscillator characterization](https://tf.nist.gov/phase/Properties/three.htm)
separates systematic behavior from random noise. Overlapping polls are not
independent trials: [NIST SP 1065 §5.4](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=50505)
relates effective degrees of freedom to noise and correlation, rather than raw
point count.

[JPL 87-10](https://ntrs.nasa.gov/citations/19880010295) limits linear smoother
predictions near PLL threshold. [DSN 810-005 207A §5.3.5](https://deepspace.jpl.nasa.gov/dsndocs/810-005/207/207A.pdf)
models tracking loss through phase error under stated conditions. These support
model-aware measurements, not transplanted DSN limits or a physical SNR floor.

Funding-pressure continuity/loss and the actual-RF constructor-headroom
counterexample, prior full HF40 and frozen Rate-360 Fake-FHSS now pass. A
valid RF-only GPSDO control, its unresolved pending-prefix/fairness issues and
stage CPU remain after integration stabilizes.
Detailed failures and bindings remain in [validation](validation.md).
EME's roughly 1.52 dB component gating opportunity remains separate from the
private Live prototype's 190-CPU cost failure before a canonical score. The
pulse-lattice backend rejects the compact timing prior, and 400 ms aliases
intersect its one-second arrival cells. These are receiver implementation/work
limits, not an established physical SNR limit or pilot-frequency-loss cause.

## Controls and use

The Helper Carrier row is below Clock sync and the oscillator controls. Power
defaults to **0**, disabling transmission and the remaining helper controls.
Power remains editable as a finite, nonnegative number of watts. Presets include
15 kW, 10 kW, 1.5 kW, 100 W, 40 W and the existing Robust power presets. Invalid
edits retain the last valid setting and prevent transmission until corrected.
Ratio limits are 10, 20, 40, 60, 80 and 90 dB, with 90 dB the default. High is the
only placement. Low and 0 Hz have been removed. Saved GUI settings/imports
normalize retired values to High; new textual CLI values other than High reject.

The independent callsign accepts at most 16 printable seven-bit ASCII
characters. Editing Developer Callsign copies a valid value to the helper.
Helper edits never modify Developer Callsign or the Robust draft. Invalid or
overlength identifiers are rejected without truncation. Callsign edits apply to
future transmissions without replacing live receivers or altering active PCM.
Each metadata record carries the dwell time phase and the callsign together;
an empty callsign leaves the character slots empty. With a station key selected,
the character field is encrypted and the time phase remains clear. The callsign
control remains available in either mode. The generator can also omit optional
metadata while retaining the tone.

**Transmit** mixes the helper with data; **Transmit noise** mixes it with the ordinary
temporary-key noise waveform. **Helper** sends the reference alone, without data
or tuning noise. The GUI **Helper** action is available in Developer mode;
ordinary mixed transmission does not require that diagnostic action. These
modes also operate in Live simulation. Existing stop/abort controls apply. The
helper continues during Symbol_Burst waits and scheduled data nulls. It bypasses payload DSSS, Interleave and waveform
time-shifting streams. It uses the existing physical dwell calendar; current
Fake FHSS still does not tune radio hardware. Metadata encryption derives a
separate key from the selected station key, including during Transmit noise,
rather than using that action's temporary payload key.

Robust message payload bits are never transmitted on the helper waveform. They
remain in the ordinary low-power Robust transmission. The helper carries only
its tone, punctuation and optional time phase and callsign metadata. An assisted
receiver is an ordinary Robust receiver guided by those measurements: it retains long
text and attachment framing, decompression, recovery, per-bit progress and the
six-second fully scored physical-end rule. If the required resources cannot fit,
optional assistance yields rather than imposing a reduced decoding mode.

Helper Carrier Power specifies the combined average power of the simultaneous
active Robust and helper waveform. Robust transmit power specifies its share of
that average; the remaining power is allocated to the helpers. Helper-only
lead and tail periods and transmission gaps do not enter this reference. For
example, a configured 40 W total and 4 W Robust allocation request 36 W for the
helpers. Once actual composite average RF output is established as 40 W, the
Robust component is approximately 4 W if the radio/audio chain preserves the
component-power ratios. The controls do not command a radio's power directly.

The helper and Robust component means are estimated separately using 32,768
deterministically stratified samples in a bounded reference window of up to two
seconds. The calculation uses the actual active waveform, including auxiliary
modulation, shaping and applicable gating, while excluding gaps. The transmission
epoch, metadata and start/end eligibility enter that calculation. Robust amplitude
is set from those numerical means to establish the requested allocation and
remains fixed for the transmission. Sampling error and later changes in the
waveform can change the instantaneous or later-window average; the RF allocation
is therefore approximate. When the
existing Helper Carrier Ratio Limit prevents the requested allocation, the
effective limited result is used and reported.
Composite headroom and the physical chain still require calibration; Robust's
target SNR remains referenced to the data component.

Placement uses the shared waveform occupied bandwidth, including spreading,
pulse shaping and applicable gating allowances, with power-dependent leakage
and separation guards. The low pilot and auxiliary stay at 550 Hz and 950 Hz;
the high pilot starts at 2400 Hz and rises only as needed for the selected
Robust carrier, occupied bandwidth and guards. A configuration that cannot fit
these bands within the digital output passband is unavailable for Helper assistance.
The radio/audio path must also pass the complete group with margin below steep
filter attenuation. The physical radio passband is not established by digital
Nyquist clearance; where that passband is unknown, actual fit remains unknown.

Native and browser playback generate the helper after output resampling.
Helper-enabled audio requests a separate physical clock, with a practical
48 kHz floor and enough bandwidth for the selected helper, while Robust's
logical DSP clock and symbol geometry stay unchanged. Live/duplex observes raw
capture before payload downsampling and scheduled capture exclusion. If a
provider cannot supply the requested higher capture rate, helper observation
yields during setup and ordinary capture is retained. This fallback occurs
before PCM/format acceptance; later errors retain the original capture gap
handling.

Current mixed Live fallback can disable the helper for a helper-specific
provider-rate failure or past helper epoch before PCM delivery, reporting the
reason visibly. Ordinary output retains calibrated `data_gain`, the same logical
PCM, wire and key epoch. It does not restore full-scale data, which would violate
the operator's RF calibration. A provider setup failure can be retried once
without the helper rate request only before the first PCM callback.

Validated `calibrated_data_gain` is now separate from all placement geometry.
Placement or minimum-output-rate `Error` disables the helper before PCM while
retaining calibrated ordinary PCM. A valid FHSS engineering gate allowance may
cross DC without making ordinary transmission unusable; calibration no longer
requires a cochannel placement plan. A helper-only request fails
if its helper cannot emit, and malformed powers/settings still reject. Actual
output gaps and other ordinary transmission errors retain their normal failures
without starting a replacement transmission.

WAV helper output uses at least 48 kHz and four times the logical rate, enlarged
when the selected helper requires it. Helper-enabled batch WAV/device `rx`,
`status-rx` and `simulate` currently decline before capture or batch processing:
their causal 36-second observation and narrowed admission are not implemented.
The Live `listen` path remains separate. Adjacent helpers inside the logical
capture band remain real interference. Capture anti-alias filtering rejects a
helper outside that band when the geometry allows; it does not silently remove
an in-band helper.

## Helper presence and receiver lock

A detected helper creates a **helper pending** entry even when no callsign or
timecode decodes. Presence and synchronization are separate: a pending helper
entry does not mean that frequency correction, marker-clock masking or Robust
payload decoding is active. A continuously present tone stays pending; elapsed
presence does not complete it. After at least six seconds of contiguous captured
absence, the same helper entry moves to the received log. Missing capture,
wall-clock waiting and provider gaps do not count as observed silence. Brief
fades retain the entry's identity. The real-time detector's level and freshness
gates are engineering checks, not a calibrated 99% lifetime-confidence claim.
Terminal diagnostic updates can be retried without taking storage from ordinary
payload progress.

The entry leads with current or last pilot SNR in dB from a separate raw-ADC
spectrum measurement. A 2,048-sample Hann-to-the-sixth-power window suppresses
analysis leakage. Pilot power is integrated across its main lobe. The displayed
noise-floor estimate uses the median spectral power divided by ln(2), a
white-noise-equivalent estimate; it suppresses narrow spectral lines. A separate
mean-band SNR uses all power in the measured band, including spurs and real
helper-transition emissions. The measured interval begins at least 600 Hz above
the pilot, or 16 FFT bins when that is a larger guard. If at least 16 bins fit,
it uses the part of 300–3,000 Hz above that guard; otherwise it uses up to
2,700 Hz above the guard, limited to 20 kHz or 45% of the sample rate, whichever
is lower. The meter does not know the occupied bands of other transmissions,
which can therefore affect the measurement. The exact measured interval, given
by its first and last FFT bin
center frequencies, is displayed. Both estimates extrapolate their measured
noise densities to a 2,700 Hz reference bandwidth; they do not integrate noise
across 300–3,000 Hz. Extrapolation assumes similar noise density across the
reference band, and narrow measured intervals can fluctuate because neighboring
windowed FFT bins are correlated. The independent peak noise-floor estimate
retains its best observation. Capture gaps, stale
measurements and rate changes withdraw the current value. A complete window
and enough resolved noise bins are required; ordinary 44.1/48 kHz audio
is supported, but the fixed window cannot resolve the previous two-tone measurement at 96/192 kHz.
Simulation without the raw hardware monitor shows the measurement unavailable.
These display-only measurements do not alter receiver qualification. They are
noise estimates, not integrated ADC noise/distortion or SINAD tests; colored
noise, unguarded transmissions, omitted spurs and hardware response still matter.
In particular, noise outside the selected band need not have the same density.
Actual helper
ramps can produce temporary mean-band readings near 60 dB even with a 100 dB
injected white-noise floor. Focused synthetic 90 and 100 dB checks establish the
meter's numerical range, not physical loopback SNR.
The pilot harmonic distortion estimate reports the summed power around the
available integer multiples of the pilot relative to its fundamental, in dBc.
The row identifies the harmonic orders included; they must fit completely
within the usable capture bandwidth. It subtracts an estimated white-noise
contribution based on the measured median floor. When the corrected estimate is
too close to that noise to report, the row instead shows the uncorrected summed
harmonic-band level, explicitly including noise and other signals; it is not a
distortion estimate. If no harmonics fit, the readout is unavailable. More
negative values indicate lower measured harmonic energy. Other
signals, helper emissions, spectral leakage, colored noise and hardware response
can contribute or bias the result. The estimate is not proof that those lines
are distortion of the pilot, and it is neither a full-band SINAD measurement nor
a measurement of harmonics beyond the captured bandwidth. The row also reports
the strongest individually resolved apparent pilot harmonic, its order, and its
noise-corrected level relative to the pilot. It retains the strongest resolved
line observed during uninterrupted spectrum measurements, so a short-lived line
can be shown even when the latest window is quieter. It also flags near-full-scale
samples seen in those analyzed windows. Both reset when the spectrum is reset,
for example after a capture gap or change of sample rate. Detection remains limited by the
measured noise, the analysis windows, and other signals at harmonic frequencies;
a line near a multiple of the pilot is not by itself proof of distortion.
The entry separately retains the receiver's current or last local C/N0 and peak
C/N0 in dB-Hz, alongside SNR in a 10 Hz reference bandwidth and dated observations.
`SNR(10 Hz) = C/N0 - 10 dB`; these are different units, not two estimates of the
same bandwidth. The same row reports the raw input's peak dBFS over its latest
2,048-sample measurement window; it is not a continuously latched clipping
detector, so a transient between windows can be missed. Metadata, if decoded,
is advisory information. A pending or completed Helper entry can be copied as
the same restricted diagnostic text shown in the signal browser. Helper entries
contain no message bits, grant no payload copy/save permission and neither hold
an ordinary reception open nor supply its physical END. The ordinary Robust
receiver still needs complete scored absent-symbol durations covering six
seconds before completing payload reception.

## Link Planner and lead time

Link Planner separates the helper reference budget from the ordinary payload.
The configured total and Robust powers determine the effective helper allocation,
after applying the ratio limit. Planner presents the resulting helper and pilot
reference budgets alongside the ordinary payload. Planner uses a one-dwell
preview at epoch zero, including the selected key for encrypted metadata;
transmission uses its own active reference window. These are predicted
levels: path loss, noise density, component waveform averages and channel gains affect
the received reference. They are not a measurement of RF output or a promise
of receiver lock.

Predicted C/N0 does not grant lock, narrow the receiver by itself or change
payload SNR, Rate or PHY probability. Runtime uses the actual measured interval.
A bright reference can still leave broad uncertainty through ramps, rejected
observations, oscillator evolution, measured excess or interference. The following detector's
short-window level gate is different from SNR in a 10 Hz presentation bandwidth.
No calibrated acquisition probability or globally optimal estimator is claimed.

The required assisted schedule is a fixed **38-second Helper-only transmit
lead**: 90 dwells (36 seconds) for precision observation plus five dwells
(two seconds) for detection, exactly 95 dwells at 400 ms each. This dwell count
and duration are the same with FHSS enabled or disabled. The ordinary Robust
waveform then begins on its unchanged calendar. A forecast or receiver
uncertainty cannot shorten or extend this lead. The tail retains at least two
seconds and any greater filter, initial-FFT, pulse-support and physical-absence
obligations; expected message length never supplies completion authority.

The receiver-local admission rule requires both a minimum **36 seconds after
causal Helper presence detection and the beginning of reference observation**
and actual measured precision sufficient for useful narrowed acquisition. The
fixed transmit lead offers up to two seconds for initial detection before that
observation interval. This margin still needs demonstration for the intended
three-tone cases. A listener without the full observation interval cannot infer
an earlier onset or use an early precision estimate to enter sooner; supporting
late joining is not a development priority, as described in the
[lead-in operating requirement](#lead-in-operating-requirement). Brief fades do not restart the timer,
but contribute no measurement support. Elapsed time and predicted C/N0 alone
do not qualify a reference. If measured precision is inadequate, new ordinary
acquisition remains gated, while established reception retains ordinary draining
and fully scored physical completion after Helper loss. This is the required
policy; its three-tone integration and validation remain in progress.

The forecast separates received pilot C/N0, independent coherent apertures,
configured phase noise and estimator/model assumptions. Predicted ±0.1 Hz
eligibility is a comparison milestone, not a runtime qualification or a
sufficient search width for every configuration, including 128-second symbols.
The independent-retune comparison assumes constant drift and configured Brownian
phase noise. It excludes operational RF/audio acceleration, retune scatter and
integrated-model precision; it is not a prediction of measured retune performance.

Two sufficiently measured pure pilots can estimate a relative audio-frequency
scale and common displacement using `f_received = a * f_transmitted + b` for a
compatible RF and sideband context. Their separation and measurement covariance
determine the uncertainty in the correction at a payload frequency. Two tones
do not separately identify every transmitter and receiver oscillator; a single
qualified pilot leaves additional clock uncertainty. Each retune can introduce
an independent phase, so only compatible, dated frequency observations are
combined across hops and the current phase must be reacquired. This model does
not make auxiliary metadata a prerequisite for frequency assistance.

For pilot C/N0 in linear Hz, the ideal uninterrupted noise-only three-times-CRLB
standard-deviation scale is

`S3(T) = 3 sqrt(3 / (2 pi² (C/N0) T³))`.

It is a theoretical comparison, not an operational confidence guarantee. The
pure-tone bound assumes unknown phase and known frequency rate; high-CNR and
model assumptions matter, and nonlinear threshold behavior is not a general
physical limit. See printed pp. 19–20 of
[JPL Publication 88-21, A Comparison of Frequency Estimation Techniques for
High-Dynamic Trajectories](https://ntrs.nasa.gov/api/citations/19890011016/downloads/19890011016.pdf).

| Pilot C/N0 (dB-Hz) | Ideal ±scale at 1 s (Hz) | At 10 s (Hz) |
| --- | --- | --- |
| 40 | 0.0117 | 0.000370 |
| 30 | 0.0370 | 0.00117 |
| 20 | 0.117 | 0.00370 |
| 10 | 0.370 | 0.0117 |
| 6 | 0.586 | 0.0185 |
| 3 | 0.828 | 0.0262 |
| 0 | 1.17 | 0.0370 |
| −3 | 1.65 | 0.0522 |

For a common constant frequency with an independent unknown phase per aperture,
replace `(C/N0) T³` by `sum_i((C/N0)_i T_i³)`. Thus 25 ideal 400 ms apertures
at 30 dB-Hz give ±0.02924 Hz; 25 × 384 ms gives ±0.03108 Hz, rather than the
uninterrupted-ten-second ±0.0011695 Hz. Ninety ideal 400 ms apertures give
approximately ±0.01541 Hz at 30 dB-Hz or ±0.004873 Hz at 40 dB-Hz. For
amplitude envelope `g(t)`, the
information uses `g²`-weighted centered aperture moments, not elapsed time cubed.
These are derived independent-phase references. Ramps, rejected contexts, drift, clock ambiguity, local model fits
and estimator inefficiency need additional treatment. Practical measured
intervals and their observed escapes remain separately reported in validation.
An earlier two-tone adaptive-factory consumer passed 236 frequency fits, including
21 outside the earlier phase-qualified subset, and 237 next-hop predictions
without observed escapes. Fits use 384 ms apertures in independently phased
7/21/35/49 MHz hops. Model storage is 42,816 bytes; measured component model CPU
is about 0.0004 seconds per 32 audio seconds. Hardware noise, positive Q and the
nominal Hann-kernel approximation remain uncalibrated. These controls do not
erase the original proof failure or earlier WLS/drift-case escapes.
A private practical four-second forecast met 25 seeded milestones while retaining
eight earlier escaped estimates. It does not change the fixed schedule; prediction
alone supplies no runtime qualification or confidence calibration.

The ordinary baseline remains visible. Planner development includes total Helper
and separate pilot C/N0, ideal uninterrupted and segmented references, practical
model prediction, the fixed schedule, residual RF uncertainty and unresolved
clock/timing work. Conditional initial and sustained receiver prices use the
existing receiver-cost model, not frequency-interval ratios, and remain distinct
from discovery, tracking and total helper-assisted runtime cost. The current
forecast/pricing update is not a calibrated overall CPU forecast: tracking,
conversion, audio, transmit and GUI work are separate. The configured 0.6 receiver
budget is an Automatic backend-selection objective in modeled receiver seconds
per audio second, not a hard ceiling on whole-GUI CPU. Geometry, central-carrier
redistribution or fewer pairs alone does not establish a throughput gain.

## Physical simulation

Helper-enabled sampled simulation generates the helper at a separate physical
sample rate and applies bounded anti-alias conversion before the unchanged
logical modem input. Data and helper share their sample-clock error, carrier
offset and oscillator phase trajectory. The simulation exercises the same manual
tracker/ordinary-receiver route; enabling a helper does not confer synchronization
or waive its actual measurement gates.

Physical AWGN preserves the original in-data-band noise spectral density and
the existing `modem::nominal_signal_power` convention. Interleaved V2 DSSS has a
−6.0206 dB sampled digital power gain: DATA C/N0 is the nominal reference minus
6.0206 dB,
and the helper/data allocation is referenced to the active waveform. The simulator does not reduce noise or boost Robust PCM to
make assistance succeed. Physical cable tests do not inject the configured RF
path loss/noise and therefore cannot qualify the corresponding weak RF budget.

An adjacent helper inside the logical capture band remains real interference;
out-of-band isolation depends on the actual filtering geometry. Bright-reference phase-noise
sidebands, DAC distortion and RF-chain dynamic range can still limit data
reception. Disabling the helper preserves ordinary transmit/channel arithmetic.
Simulation tails contain actual modeled noise long enough for the original
receiver aperture and absence scoring; neither EOF nor synthetic filter flushing
supplies physical END. No general sensitivity or throughput qualification follows
from digital isolation or a single controlled payload result.

## Dwell waveform and metadata

Every dwell is exactly 400 ms on the physical calendar, including when FHSS
is off. With FHSS disabled, the low and high straight pilots remain continuous
in amplitude and phase at dwell boundaries. With FHSS enabled, their boundary
ramps protect retunes. Transmission start and end remain shaped in either mode.
The auxiliary retains its dwell-boundary gate and phase reset, shaped reversal,
and metadata transitions regardless of FHSS.

Only the auxiliary has a shaped reversal. Its amplitude tapers from the straight
tone to zero over 152–160 ms, then rises in the inverted phase over 160–168 ms.
The inverted straight tone continues through 212 ms; phase then follows the
metadata symbols until the boundary. The auxiliary resets while its amplitude
is zero at the boundary. Auxiliary reversal and metadata blends use a septic
smootherstep with three continuous derivatives at their endpoints. A waterfall
phase column can change sharply as the auxiliary passes through zero amplitude,
where phase is undefined even though the emitted waveform is smooth. Earlier
240 ms-reversal measurements do not establish compatibility or qualify the
revised path.

Partial dwells omit punctuation unless its required support fits. Graceful
streaming stop protects a reversal already begun: helper-only continuation can
extend to 192 ms, retaining a 20 ms inverted straight flank and final 4 ms ramp.
This adds no Robust bits or progress and does not supply physical END. Immediate
abort can truncate output. Earlier paired-reversal spectrum measurements apply
to the superseded single-tone format, not this waveform.

Metadata decoding is optional and does not qualify the pure-pilot frequency
reference. Its intended readability is primarily nearby or directly in the
antenna beam; distant assistance need not support metadata decoding.

Metadata uses APSK-16384 at 50 baud on the auxiliary only. Each record is
126 bits in nine 14-bit symbols, sent from 212 ms to 392 ms of a complete
400 ms dwell. The final APSK value is held while its amplitude ramps to zero
from 392 ms to the boundary; there is no return to the inverted straight tone.
Auxiliary transitions use septic interpolation in the complex plane. The high
seven bits of each symbol select one of 128 radii `0.2 + 0.8 * index / 127`; its low seven
bits select one of 128 phases `2*pi*index/128`. This is 80% amplitude modulation
depth. The next dwell is a hard boundary; records never continue across dwells.

For a complete dwell with punctuation and metadata enabled, the auxiliary
envelope and symbol windows are:

| Time within dwell | Auxiliary waveform |
| --- | --- |
| 0–8 ms | Ramp up from zero |
| 8–152 ms | Straight tone |
| 152–160 ms | Taper amplitude to zero |
| 160–168 ms | Rise from zero in the inverted phase |
| 168–212 ms | Inverted straight tone |
| 212–392 ms | Nine 20 ms APSK symbols |
| 392–400 ms | Ramp the final APSK value to zero; reset at boundary |

The helper format has one record type and no header, transmitted length, CRC or
authentication tag. All fields are MSB first:

| Field | Bits | Meaning |
| --- | ---: | --- |
| Time phase | 14 | Unix time at nominal physical dwell start modulo 8.125 seconds, quantized down to steps of 65/131072 seconds (about 495.91 microseconds) |
| Callsign | 112 | Up to 16 seven-bit ASCII characters, followed by zero-valued slots; NUL is reserved for padding |

The time phase repeats every 8.125 seconds and does not itself convey a complete
absolute time. This is a deliberate helper-format change; Robust's ordinary
wire format remains unchanged. Local settings select clear or encrypted metadata;
there is no encryption flag on the wire.

With a selected station key, the time phase stays clear and the 112 character
bits, including padding, use AES-256-CTR. The AES key is the station key's
HMAC-SHA256 of ASCII `datapump/helper-aux/v3/ctr-key`, separating this stream
from other protocol purposes. The 128-bit IV contains the absolute six-second
Unix bucket as a big-endian 64-bit integer, the 400 ms dwell index within that
bucket as a big-endian 32-bit integer, and a zero 32-bit block counter. The
receiver reconstructs the absolute dwell from the clear phase and its capture-time
UTC estimate, without searching across candidate clock errors; keyed decoding
requires that estimate to be within approximately four seconds. Only the first
selected receive key is used. Cipher failure rejects encrypted output before PCM
instead of substituting clear text.

Use a station-specific key and transmit at most one fixed character record in a
given absolute dwell: a different record or another transmitter sharing the same
key and dwell would reuse the CTR keystream. Metadata has no authentication or
replay protection, and a wrong key can yield plausible text. Decoded time phases
never set an internal UTC correction or qualify GPS synchronization; metadata
also does not prove that the tone and a separately received Robust payload share
a transmitter. The format alone does not establish identification compliance
for every emission, band or radio installation.

## CLI examples

```
pump tx --text hello --carrier 2400 --output helper.wav --helper-power 1.5kW --helper-data-power 4W --helper-callsign N0CALL
pump listen --bw 200 --carrier 1725 --helper-power 100W --helper-callsign N0CALL --helper-only --seconds 30
pump listen --simulation '3dBm -60dB' --helper-power 100W --helper-only --seconds 30
```

`--helper-ratio-limit`, `--helper-frequency` and `--helper-callsign` are included
in GUI command import/export. `--helper-only` is a CLI listen action and requires
positive helper power and no outgoing message input. Helper-enabled batch
reception and `simulate` currently decline as described above. Live helper
events include a display description. Generated WAV separation tails remain
real quiet capture after both components end.

## Evidence and remaining qualification

[Validation](validation.md) retains current focused observations and separately
bound historical failures. The current 20 dB-Hz component declines qualification;
a short-window gate, poor bootstrap or inefficient aperture is not proof of a
physical precision limit. The actual-waveform matrix and seeded uncertainty
escapes must be resolved before operational interval claims. Capture passband,
real-mono ambiguity, unresolved RF/audio-clock relationships and actual rig
retuning are separate limitations.

Complete supplied HF and 5.8 GHz EME acceptance, causal CPU comparisons, strict
cable identity/prefix behavior, complete long text/attachments, payload progress,
fully scored END, measured receiver savings and current contract/native/sanitizer
coverage remain outstanding. Component acquisition and precision do not complete
those requirements. This is a manual experimental preview, not release
certification.
