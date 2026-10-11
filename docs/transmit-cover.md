# Robust transmitter cover waveform

The cover implementation makes **Transmit noise** use the ordinary private-pattern
transmitter, and extends its surrounding cover to the selected complete outer
DSSS construction. It is a transmitter change. Receiver algorithms, hypotheses,
thresholds, framing, meaningful bits and payload cryptographic addresses are
unchanged by the cover implementation itself. The later deferred FHSS calendar
adds explicit shared TX/RX timing as described below. The manual checkpoint is not full receiver/security/platform
qualification.

## Common engine and separated secrets

Message bits and lazy, Data-masked dummy zero bits enter `PatternTransmitter`.
The GUI selects the same short or long/file profile as its current draft.
Both retain the selected optional inner DSSS mask and outer factor/version,
symbol/inner/fine-chip geometry, independent private 0/1
candidates, outer rotations and full-symbol Interleave permutation, finite RRC
pulses, headroom, radial limiter and carrier. Tuning creates a fresh `Crypto::random`
root on every start, replacing all selected communication secrets. Its purpose
keys remain independent. It never inserts that root into a receive bank or a
saved keyfile. A finite `Noise{symbols}` source is available to tests/callers;
zero retains the open-ended tuning source. The checked `2^48` sample safety bound
terminates instead of wrapping addresses or replaying a buffer.

Private-pattern prefix and suppression cover derive independent 32-byte child
roots. Data, Scrambler and the DSSS purpose (if its inner mask or outer layer is enabled)
contribute via the new
`CoverPreambleV1` (`cv-pre01`) or `CoverSuppressionV1` (`cv-sup01`) counter domain,
at the original transmission epoch and byte offset zero. Their XOR is a child
root; ordinary purpose derivation then supplies separate child Data, Scrambler
and DSSS keys. Disabled layers do not contribute. These domains do not replace
or overlap payload, private bit alternatives, outer rotations/permutations or
FHSS addresses. Child zero bits use the ordinary canonical symbol epoch/ordinal
Data mask. Cover roots are wiped and are absent from diagnostics. Existing
bounded TX scope disclosure of *generated purpose bytes* remains unchanged;
it does not disclose temporary root keys or describe tuning as message content.
Public/tone and Data-only non-private diagnostic surroundings retain their
original Preamble/Suppression mappings and independent vectors. The low-level
unencrypted diagnostic with an inner DSSS mask but neither Data key nor private
Scrambler also retains those mappings. Full cover is eligible for pattern mode
with Scrambler enabled, or with Data and the inner DSSS mask enabled. An outer
factor alone does not enable `PatternCode`'s private construction. Ordinary
encrypted GUI messages and fresh-key tuning meet that condition.

A cover section is a **portion of complete virtual symbols**. Their duration,
partial final fine-chip energy, inner pattern, full permutation and rotations
are computed as for payload. It is never a shorter permutation or a sequence of
independent replacement fine chips. Two neighboring symbol maps belong to each
`PatternCode`; one additional cover code is reused between prefix and suffix.
It is released before switching cover roles. Its 24-entry coefficient cache is
valid only within the immutable child role, key, epoch, phase and geometry;
absolute virtual chip identifies the symbol and bit. The ordinary two-map cache
retains its existing canonical identity checks. Preview may regenerate evicted
state but never advances addresses or publishes cover chips as payload.

The enclosing transmitter owns the one dedicated keyed FHSS schedule, including
while cover is generated. Noise uses its fresh temporary FHSS purpose. Fake FHSS
keeps physical audio frequency and radio tuning fixed. Its optional production
power ramps apply the same smooth envelope after the common shaper/limiter to
payload and cover, and again after native resampling for exact hop silence; see
[hop transitions](spread-spectrum-controls.md#hop-power-transitions-local-manual-candidate).
The three useful seconds then exclude hop gaps/transitions, with a longer tail.
They no longer imply uninterrupted masking. FHSS Off retains the behavior below.

## Edges, short sections and stop latency

The prefix retains the existing nearest-whole-symbol duration around two
seconds (absent when the symbol exceeds four seconds). Its overlap, payload
alignment and all communication coefficients/addresses stay unchanged. At the
other end, payload and independently keyed cover coefficients now meet BEFORE
the common finite shaper and limiter. Payload contributions stay intact; PCM in
the final overlap changes, so this is a changed-waveform comparison.

For shaped private output, write H for the pulse half-support (eight fine chips), B for the logical payload
end, and L for three seconds. Payload coefficients centered before B ring out
by E = B + H. Cover starts at that coefficient boundary, reaches ordinary
statistical power by E, and continues at least until E + L. Cover centers are
retained through B + 2H + L; their shutdown support and a terminal zero sample
end at B + 3H + L + 1 sample. The three useful seconds are never shortened to fit
ramps. Normal shaped duration is prefix + payload + 4H + L + 1 sample. The public and
unsupported low-level diagnostic reference retains its established three-second
total tail. Central duration helpers feed airtime, simulation and planner estimates.

Full virtual-symbol permutations and rotations remain intact even when only a
small portion is emitted. The independently derived suffix keeps a role-local
origin, fixed once its first contribution is emitted. A stop during an existing
tail neither restarts that stream nor replenishes its duration. There is no
post-filter compact window, per-chip normalization, constant-amplitude section,
or narrow-pulse bandwidth exception on the protected private path.

At a graceful request at source coordinate P, retain every payload/prefix
coefficient centered before P + H: it may already have contributed. Only
untouched coefficients are replaced by cover. The worst additional shaped-source
latency is **3 seconds + 4H + one sample**, plus already-generated converter and
device buffering; normal completion can shorten this bound. A bounded 32-entry
prefix snapshot handles early cancellation while the single full-symbol cover
map switches roles. It is reserved and charged to workspace at construction only
when a shaped prefix exists; long symbols without a prefix do not allocate it. No whole-symbol PCM buffer or wait is needed. Before the
first emitted sample, finish is immediate. Unshaped private output finishes the
current fine chip, retains three useful seconds, then uses a separate nominal
10 ms C2 shutdown; unshaped chip transitions retain their diagnostic spectrum.

First Cancel/Stop noise requests this protection and drain. **Abort now**, second
Cancel, device discontinuity and shutdown remain prompt and explicitly forfeit
protection. With 200 microsecond chips, protected source stopping takes at most
3.0064 seconds plus one sample; two-second chips require about 67 seconds, still
far shorter than a four-hour symbol. At extremely narrow bandwidths this airtime
is the cost of retaining pulse support and useful masking power. No payload bit,
marker, padding or receiver absence is appended.

The immutable original waveform extent remains available to the hardware
key-exposure guard after shortening output. Finishing cannot release previously
exposed future symbol epochs. TX stays busy while the converter/DAC drains.
A stopped simulation releases TX/replay work; its independent receiver continues
on ordinary idle channel samples and must establish absence itself.

## Playback and timing

Noise and messages now use the same fresh scheduled payload epoch, leading
padding/prefix offset, output provider, source thread, buffered drain, and
playback-discontinuity abort. Noise retains fresh temporary keys and does not
consume the selected communication key's reuse history. Noise does not wait
behind a previous message's saved-key/long-symbol separation lock.

Both source kinds now use one length-independent timing controller and the
same protected-stop preparation horizon. Gain and channel routing are frozen
with the queued profile. One original source origin/rate is kept throughout:
there is no resetting at callbacks, symbols or repeated finite horizons. The
controller reserves braking distance, actual converter lookahead, source
prefetch and the protected tail. Every generated block is checked before device
submission. See [continuous timing](clock-sync.md#timing-and-private-pattern-addresses).

A stationary native clock remains an engineering assumption. When UTC accuracy
and the original receiver-compatible map cannot both continue, a protected stop
is requested while its forecast still fits. If even stopping is no longer safe,
or a discontinuity occurs, playback aborts visibly. No post-start fallback or
private-source restart occurs. This is not indefinite tracking under arbitrary
clock wander, nor measured hardware latency qualification.

## Scope of the observer claim

With matched public geometry, duration/power constraints, hardware and channel,
noise and encrypted messages share their core waveform construction under
independent secrets. This is not perfect or unconditional indistinguishability,
thermal noise, or concealment of duration/operator activity. Remaining observable
structure includes finite burst edges and cover durations, bandwidth/carrier,
clock/oscillator imperfections, selected outer-code structure, finite inner
coefficient histograms and whole-bit energy, limiter/hardware nonlinearities,
stop/abort behavior and timing qualification/failure. Different keys produce different finite spectra
and amplitude histograms even with the same construction. Repeated forced
communication-key/epoch use retains its existing risks.

Cryptographic independence is not a false-acceptance proof. Structured cover
must be rejected by the unchanged physical receiver. Completion still requires
fully scored unsuccessful symbols covering six seconds. Source EOF, cover
generation, draining and cancellation do not create that event.

The complete receiver regressions exposed a concrete limit: new full-symbol
cover in the unencrypted DSSS-only diagnostic admitted an extra 13-bit prefix
burst at 6000 samples/s and 32 chips/symbol. The preceding candidate passed that
same fixture; both decoded the actual 1536 payload bits. No address collision
was found. The diagnostic fallback above restores its established surrounding
PCM, with a frozen prefix/suffix vector and the original receiver assertion.
It does not qualify the rejected full-cover variant. Saved failing captures
remain evidence for further false-admission investigation; no receiver threshold,
seed, assertion or framing was changed to hide the failure.

## Validation checkpoint

`transmit_cover` independently reconstructs child purposes and complete virtual
symbols, checks exact gain/limiting, payload preservation, full and partial
chips, fragmented reads/previews, supported and four-hour geometry, finite
noise/FHSS, workspace rejection, graceful stop and immediate abort. Existing
Data-only vectors remain intact. `streaming_modem` compares lazy dummy and
ordinary encrypted bits for all four DSSS factors and feeds full surrounding
PCM to acquisition, including a separate EOF check. `noise_receive` adds
structured DSSS cover and strong-cover controls; zero accepts in finite tests
are not a lifetime false-alarm estimate. `live_transmit_lock` checks held physical
drain, second-click abort, selected long-draft geometry, UTC preflight/fallback
for both source kinds, discontinuity rejection and original key-exposure protection.

### Continuous timing and protected cover checkpoint (2026-10-11)

Baseline GUI SHA256 `03d74447afb10698c393c0f31047e4737227fd4473472f11afa6a240fb678702`
is the preceding cover/audio-default source rebuilt before edits, including the
released short-dictionary fix. Source is HEAD `889bb4a2325ca9b249eb33dd19d5e56579acdf60`
plus preserved local overlays. Frozen source, compiler/configuration, libraries,
probes and raw results are under the ignored
`.agent-work/artifacts/receiver-opt-20261008/timing-echo-baseline/` and adjacent
`timing-*` captures. The manual report is `MANUAL-TRANSMIT-TIMING-ECHO.md` there.
This checkpoint is not full timing, receiver, security or platform qualification.

**Echo scope.** Forty paired complete-channel captures inspect every received
stream: 24 normal receptions (four DSSS factors, six channels) and 16 cancellations
(symbol/half-chip boundaries, two channels, four factors). Four no-suppression
controls are included; Off and 10× expose extra echo-derived bits in this
geometry, while 100× and 1000× do not. The same synthetic keys/channel/noise
seeds and unchanged digital gain are used; changing cover changes transmitted PCM.
At 2048 samples/s, 512 Hz carrier and 512 Hz total configured bandwidth, symbol
lengths are 0.5/0.5/3.125/31.25 seconds. Channels include a 0.2-amplitude echo at
3 or 3.6 seconds, three reflections at 2.8/3.000244/3.2 seconds, 21 decaying
alternating reflections from 2.5 to 3.3 seconds, and a 0.6-amplitude reflection
at 2.999756 seconds. The 0.375-sample start and fractional echoes use the same
two-tap, frequency-selective interpolation, not an ideal all-pass delay.

All new captures retain the intended complete stream without wrong or
out-of-range bits. Total surplus admissions across these pairs fall from 22 to
8. The remaining eight are duplicate reflected streams in **two strong-echo
cases (Off and 10×)**; those reflected streams are still pending at the bounded
capture endpoint. This is a real limit, not a suppression pass. Two preceding
Off cancellation/reverberation captures lacked the completed intended stream;
the new counterparts have it. Three seconds does not protect arbitrary echoes,
and these synthetic cases do not characterize a physical room or radio.
First-bit publication is unchanged in all 40 pairs; normal last-bit publication
is unchanged in all 24. Poll granularity is 317 input samples. These measurements
do not resolve every individual bit's sub-poll latency or completion latency.

**Threshold evidence.** A bounded DSSS10 pilot uses 128 paired synthetic key/noise
seed clusters at each of seven SNR settings, three bits, 512 samples/s, 2.5-second
symbols, 3 frequency × 3 clock lanes, boundary impairments, random fractional
starts and 0.5-degree/sqrt(second) phase diffusion. One initial epoch/key bank is
used; the entire cover is passed through the channel and all streams inspected.
Full-message counts at -8/-6/-4/-2/0/2/4 dB are identical:
31/42/69/115/127/128/128 out of 128. Empirical linear interpolation gives **0 dB
observed difference** at both 90% and 99% detection. One final-bit outcome at
-6 dB is lost (49 becomes 48); other final-bit counts are unchanged. No wrong or
out-of-range admissions appear in this finite pilot.

This does **not** establish a sub-0.1 dB loss bound. With zero discordance, a
bootstrap's zero-width difference interval merely resamples the identical
observed outcomes. At each level, the two-sided 95% binomial upper bound on an
unobserved discordance probability is 2.84 percentage points. Reusing seed
clusters across levels does not multiply the independent trial count. At -2 dB,
detection is 89.84% (Wilson 95% interval 83.40–93.97%); at 0 dB it is 99.22%
(95.71–99.86%). Even 128/128 has a one-sided 95% lower bound of 97.69%, not 99%.
The additional C/N0 difference remains inconclusive at the requested precision.
This pilot neither qualifies multiple-key/rolling-epoch acquisition nor supplies
a rare false-accept rate; cumulative raw-reference sensitivity is outstanding.

**Execution cost.** Seven isolated, alternating pairs on CPU4, AMD Ryzen 5 PRO
5650U, GCC14.2 Release `-O3 -DNDEBUG -ffp-contract=off`, include source construction,
full generation, channel construction, receiver execution and result scans at
unchanged input rate. No application-rate speedup is claimed. Setup is included;
receiver component timings also contain small power/audit scans.

| DSSS | CPU before / after (median ms) | After/before ratio (paired log t 95% CI) | Wall before / after (median ms) | Charged source bytes before / after |
| --- | ---: | --- | ---: | ---: |
| Off | 12.676 / 15.405 | 1.216 [1.205, 1.227] | 12.714 / 15.465 | 16,196 / 17,772 |
| 10× | 22.817 / 25.824 | 1.140 [1.129, 1.150] | 22.821 / 25.845 | 107,220 / 108,796 |
| 100× | 41.070 / 45.939 | 1.118 [1.102, 1.135] | 41.169 / 46.009 | 117,972 / 119,548 |
| 1000× | 221.961 / 248.307 | 1.126 [1.089, 1.164] | 222.025 / 248.388 | 233,172 / 233,212 |

This is a **12–22% total CPU increase**, principally transmitter construction of
protected cover; it is not a receiver optimization. TX component medians are
7.326→10.004, 8.094→11.068, 14.091→19.545 and 62.559→93.954 ms; receiver plus audit
medians are 4.527→4.540, 13.692→13.854, 24.585→23.672 and 148.272→144.890 ms.
Charged source storage rises by 1576 bytes with a shaped prefix, or 40 bytes
without one. The original 16 KiB long-symbol storage assertion still passes.
Process peak RSS is 16,712 KiB in all final pairs. Setup medians are 42–106
microseconds. Throughput changes from 1.141/0.634/0.932/1.182 to
0.947/0.565/0.836/1.057 million input samples per CPU second, respectively.
These figures exclude hardware waits and continuously steered output conversion.
Final performance captures supersede the earlier measurements before the prefix
snapshot allocation was made conditional. All sixteen spectral bursts and the
partial four-hour-symbol cancellation are bit-identical across that memory-only
change, allowing their existing waveform evidence to be reused.

The separate 12-second, 48 kHz conversion/guard probe (seven isolated pairs)
measures 106.958→508.300 ms median CPU, ratio 4.962 [4.529, 5.436], with unchanged
12,440 KiB process RSS. The new per-frame original-map checks are appreciable:
about 4.24% of one core at this rate, versus 0.89% before. These measurements
include conversion and checks, not waveform generation, device I/O or RX.
They are not added to the differently configured end-to-end numbers above.
Reducing the conservative per-frame long-double bookkeeping remains a possible
CPU improvement; the manual candidate retains those checks.

**Power, spectrum and stopping.** Four synthetic roots per factor at 12 kHz PCM,
3 kHz carrier, 2400 Hz total bandwidth and 16 inner chips compare whole analytic
bursts at unchanged digital gain. Median energy outside carrier ±1200 Hz changes
by less than 0.004 dB: Off approximately -36.12 dB; 10×/100×/1000× approximately
-54.62 dB. The DSSS cases have zero limiter hits in these fixtures; Off still
limits (8645→8704 samples over four bursts). No general clipping guarantee is
claimed. Three-second cover/payload average-power ratios span 0.736–1.201 for
DSSS and 0.882–1.045 for Off. Within cover, observation windows of one symbol
(or three seconds if shorter than a symbol) retain ordinary secret-dependent
power variation; no per-chip or per-window normalization is introduced.

A partial four-hour-symbol test uses 64 samples/s, 16 Hz carrier, two-second
fine chips and H=16 seconds. Stopping after four emitted seconds takes
32.015625→67.015625 source seconds. Old drain-only output has no cover in the
protected interval; new average analytic power there is 0.04352. Integrated
energy outside carrier ±0.5 Hz is -54.015→-54.236 dB for these unequal-duration
bursts. This illustrates the airtime/power tradeoff, not an equal-energy spectrum
or complete long-symbol reception. At the faster echo fixture, cancellation
source duration grows from 62.99 ms to 3.12549 seconds. Actual device stop adds
queued/converter time; physical loopback latency has not been measured here.

**Retained failure.** The existing legacy-V1 ALSA clipping assertion fails both
the exact frozen baseline and current candidate. Its fixed 0.375-sample conversion
probe has peak 1.00868, 61 clipped samples and relative clamp error
2.35887e-8→2.35761e-8, above the unchanged 1e-8 assertion. No seed, gain, threshold
or assertion was changed to hide this. The supported Interleave native fixture
passes the same conversion/clipping checks and the new queued protected-stop
scenario. Legacy clipping, strong-echo duplicates, full-bank sensitivity,
real-device timing/spectra and broader qualification remain unresolved.

**Final affected checks.** Focused checks passed, followed by all 60 affected
cases in 416.03 seconds: GUI 38/38 and runtime/native 21/22 pass. The one
failure is the retained legacy-V1 ALSA clipping assertion described above.
No skipped/cancelled case is counted as a pass. Native graphical display,
sanitisers and broader platform/SDK qualification were not run. This candidate
is rebuilt for local manual testing, not declared fully qualified.

### Earlier manual-transmit-cover measurements (historical, 2026-10-11)

These measurements precede the protected-interval/continuous-timing follow-up.
Their exact-duration and drain-only stop results are historical, not results for
the current waveform. Current evidence is recorded at the checkpoint above.

Frozen preceding GUI SHA256
`f1b7eccbc0933c4e017a21cf9e32116ba03055b4bd23afd8fd2bbe05e6ea8d34`;
current source base `889bb4a2325ca9b249eb33dd19d5e56579acdf60` plus this local
delta. AMD Ryzen 5 PRO 5650U, GCC 14.2 Release, `-O3 -DNDEBUG -ffp-contract=off`, same input geometry
and synthetic key. Seven alternating process pairs after warmup, pinned to one
CPU, with no concurrent builds/tests. Figures include setup, generation and a
common energy/checksum scan from the `PatternTransmitter` constructor onward.
Initial application/key seeding, message encoding, hardware waiting and receiver
CPU are outside this timer; the new child-cover derivation is inside it.

| Outer DSSS | Burst seconds | Before / after CPU ms (median) | Paired CPU ratio, after/before (95% t interval on log ratios) | Before / after charged bytes |
| --- | ---: | ---: | --- | ---: |
| Off | 5.067 | 11.919 / 29.306 | 2.457 [2.438, 2.477] | 13,692 / 16,196 |
| 10× | 5.547 | 13.304 / 29.764 | 2.231 [2.219, 2.244] | 59,460 / 107,732 |
| 100× | 11.013 | 27.606 / 46.732 | 1.701 [1.683, 1.719] | 70,980 / 130,772 |
| 1000× | 56.347 | 141.923 / 160.372 | 1.131 [1.106, 1.156] | 183,364 / 361,172 |

The complete full-symbol cover costs more than the old direct noise mapping.
At these geometries after-change CPU is 0.28–0.58% of emission time; this is not
a bound on all configurations. Median wall times were respectively
29.349/29.803/46.777/160.394 ms. Process peak RSS was approximately 11.9–12.0 MiB.
All use 12 kHz PCM, 3 kHz carrier, 2400 Hz total configured bandwidth, 16 inner chips,
four payload bits and the optional inner DSSS mask enabled. This is a fixed-rate
comparison; no application-selected-rate speedup is claimed. New setup was
38–123 µs. Prefix/suppression dominate the short cases; D1000 payload generation
was 134.467→146.827 ms and suppression 6.610→12.838 ms. These are transmitter costs;
receiver code and its arithmetic policy were not changed. Timing/spectrum/pilot
evidence was captured before the final diagnostic eligibility fallback and
long-draft GUI selection fix; those changes do not alter the measured seeded
Scrambler-enabled engine path. Complete affected suites were rerun on the final
candidate.

Four independent synthetic roots per factor, complete finite analytic bursts:
D10/100/1000 median energy outside the configured ±1200 Hz band remained about
−54.6 dB before and after; no limiter samples occurred. Payload samples beyond
the prefix overlap matched within 1e−9. DSSS-Off retains the existing limiter:
complete-burst median out-of-band energy was −36.56→−36.08 dB and clipped samples
8,266→8,888 over the four captures. No new power backoff was applied to hide
that difference. Cover histograms, whole-symbol energy and chip-lag correlations
vary with independent keys; they are retained in the evidence, not treated as
proof of indistinguishability. FFTs of isolated sections include the analysis
cut's edge leakage; use the complete-burst figures for emitted-spectrum claims.

Two illustrative stopping captures compare the same pre-stop PCM with graceful
tails versus immediate truncation. At 2400 Hz bandwidth, 13.417 ms additional source
tail changed normalized out-of-band energy −51.58→−54.61 dB. At 1 Hz bandwidth,
32.016 s tail changed it −25.45→−36.34 dB. The tails add energy and duration
(respectively 0.4% and 243% of the truncated capture's energy), so these are stop
tradeoffs, not matched-duration/power communication comparisons. They exclude
queued hardware latency and do not qualify RF amplifier transients.

A separate paired full-waveform threshold pilot used 128 synthetic seeds at each
of seven SNRs, D10, 512 sample/s, 128 Hz carrier, 12.8 Hz inner bandwidth, 16 inner
chips, 2.5 s symbols and three bits. The ordinary acquisition bank retained 3
frequency hypotheses (±0.2 Hz, 0), 3 clocks (±50 ppm, 0), fractional starts and
0.5 degree/√s diffusion; initial stream alternatives were limited to one, with
one key/epoch. Changed cover PCM used identical channel/noise seeds and equal
configured gain; this is not an identical-PCM receiver-only comparison.

| Nominal sample SNR dB | Before exact three-bit detections /128 | After /128 |
| ---: | ---: | ---: |
| −10 | 4 | 5 |
| −8 | 31 | 31 |
| −6 | 42 | 42 |
| −4 | 69 | 69 |
| −2 | 114 | 115 |
| 0 | 127 | 127 |
| 2 | 128 | 128 |

Empirical linear-interpolation shifts (after−before) were −0.151 dB at 90% and
−0.0036 dB at 99%. A 10,000-resample paired seed bootstrap gives conditional 95%
percentile intervals [−0.345, 0] dB and [−0.0533, 0] dB respectively, using monotone
linear interpolation. These intervals resample only observed outcomes; their
zero upper endpoints cannot exclude unseen harmed cases. **They do not
establish negligible population sensitivity loss.** There were no harmed paired detections, but the same 128 seed clusters
were reused across the grid: the one-sided 95% upper bound on an unobserved
cluster having any harmed case is 2.313 percentage points, not zero/896.
The population 99% point is not confidently bracketed: 128/128 alone has a
one-sided 95% lower bound of 97.687%. Full-bank sensitivity and a tight dB loss
bound remain unqualified.

The longest reported stream contained 1,860→1,864 accepted bits, with zero wrong
bits in that stream in either version (other stream errors were not scored by
this pilot). All 693 pairs with nonempty output on both sides had identical
first/last publication sample indices at 317 sample poll resolution. This does
not replace the per-bit next-poll regression assertions. Physical completion
was 693→694, at most one per trial; the pilot supplies real additional noise
samples, and separate regressions prove EOF alone does not complete reception.
Structured D10/100/1000 cover-only fixtures at 1× and 4× amplitude admitted no
bits; their finite duration cannot estimate rare false-acceptance rates. Valid
message controls also decode with independent structured cover at 0.1× amplitude
and a 0.03-amplitude tone interferer, a strong-signal regression rather than a
threshold qualification.

Exact programs, JSON captures, spectrum summaries, logs and source/build manifest
are retained under `.agent-work/artifacts/receiver-opt-20261008/` (`cover-*`,
`transmit-cover-final`, `MANUAL-TRANSMIT-COVER.md`). The launcher is
`manual-transmit-cover.sh`. Broader qualification remains outstanding: full acquisition-bank sensitivity and rare-event calibration,
multiple keys/rolling epochs, broad impairments/interference, native physical
audio timing/stop spectra, ARM/Windows/Web/SDK and packaging/release coverage.


## Cover during deferred FHSS waits

The [deferred calendar](spread-spectrum-controls.md#deferred-whole-symbols-local-manual-candidate)
retains complete payload symbols in safe dwell slots and fills other admitted
coefficient positions with full-DSSS virtual cover. The dedicated `cv-wai01`
child-secret role is independent of payload, prefix and suppression. One bounded
additional virtual code/map cache is charged to the transmitter workspace; no
whole-symbol PCM buffer or replay is introduced. Role origin remains fixed.
Cancellation retains every already-contributing wait/payload coefficient, then
uses the existing protected suppression and final-shaper shutdown. Hop gaps still
interrupt echo masking, and the three useful seconds exclude those gaps.
Airtime includes initial wait, interslot waits and the extended suppression tail.
The new calendar changes physical symbol positions and therefore actual UTC
payload addresses; both peers must run matching calendar geometry. Bits, framing,
private stream separation and full observed-absence completion remain unchanged.


## Smooth FHSS gaps and cover

Production uses the common C2 envelope on payload, prefix, waits, temporary-key
noise and protected suppression. Short bits use full-gain slots; long bits span
gaps and the receiver uses its original full-energy templates, with measured
fading mismatch. The previous exact pulse-omission diagnostic and failure evidence
remain available. See [FHSS controls](spread-spectrum-controls.md#hop-power-transitions-local-manual-candidate)
for full/local timing guards, 200 Hz allowance, cache rules and measured limits.

The useful cover tail excludes quiet intervals AND both ramps. It still does not
provide continuous echo masking through hop silence. Normal completion and first
Cancel preserve the common graceful-stop state machine and contributed source
pulses; their output is multiplied by the same mandatory hop envelope. Abort now
remains prompt and forfeits protection. The last native resampler stage restores
exact software-output silence, but DAC reconstruction/radio settling still need
hardware validation. No gap, EOF, cover generator or drain completes reception.
