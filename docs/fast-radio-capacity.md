# Fast IC-7100 capacity profiles

## Propagation and differential-carrier qualification

Choose the better supported sensitivity and delivered throughput, provided the
complete receiver remains practical on its intended hardware. Minimum CPU use
is not the objective. Computational tests guard against prohibitive search,
memory growth and input backlog; a costlier equalizer can be the better choice
when it recovers more data or weaker signals. A cheap unsuccessful acquisition
attempt is not evidence of a useful receiver ceiling.

Data Pump is a one-way system. The target is one standardized, resilient Fast
transmit format for both manual propagation cases, with no receiver feedback or
automatic transmitter negotiation. Receivers may choose or combine local
processing methods for that same waveform. Different constellations, codes and
pilot patterns in the investigation are competing format candidates, not a
per-link transmitter adaptation policy. Select a standard from actual protected
recovery and delivered throughput across both cases.

Fast targets conditions permitting practical direct synchronization and useful
throughput, particularly at higher SNR. Extremely weak conditions requiring
historical searches belong to Robust. The −28 dB prefix below records a tested
boundary; it is not a requirement to replace Fast with another very slow format.

The current investigation reads the ordered cases in
[`_tests_FastModem.md`](../_tests_FastModem.md). Both request encryption and the
SSB profile. The omitted noise density is explicitly set to **−174 dBm/Hz** by
the operator. With the stated transmit power and path loss, the nominal SNRs
over 2400 Hz are about 56.22 dB (HF) and 41.96 dB (EME). These link-budget
values precede the synthetic fading operator; they are not measured receiver
SNRs. The current runner uses the row's margin as a receiver search bound, so
the HF row declares 21 ms and 90 Hz while injecting the selected 7 ms and 30 Hz.
Earlier receipts used margin in the injected channel as well. Shift frequency
is an oscillator-model coordinate only and does not retune the Fast audio waveform.

Qualification starts with deterministic analytic/real waveform parity,
chunk-invariant channel state, explicit Off baselines, delayed tails, and
encrypted exact-byte completion. Then run one bounded native complete-source
simulation per manual row, retaining failures without relaxing its settings.
Only a successful row advances to two fresh channel seeds; three successes
are a smoke screen, not a reliability probability. Separate short physical
cable trials use the same recorded settings and encryption, with any shortened
coding geometry identified explicitly. Digital impairment followed by an audio
cable is neither a radio measurement nor an HF/EME field qualification.

The dual-carrier question first uses a bounded native, correctly timed
reference/data-pair experiment with equal total power, bandwidth and
constellation order for QAM and APSK. It checks clean/common-phase controls
before the two selective channels. This deliberately favorable synchronization
screen can reject a poor candidate without implementing a second operating
receiver. Its frontend/detector cost is a partial cost, never complete-receiver
capacity. Production acquisition, coding, unsuccessful searches and physical
completion remain separate required evidence for any adopted waveform.

Sensitivity below −27 dB and throughput above 10 kbit/s are separate operating
points. At −27 dB integrated over 2400 Hz the ideal AWGN capacity is only
6.90 bit/s; 10 kbit/s requires at least 12.29 dB before practical overhead.
Narrowing the signal does not change the original-band SNR definition.
Raw outputs in ignored `local-evidence/fast-channel/` serve this unfinished
qualification; retain only essential failure evidence until its diagnosis is
resolved, then keep concise findings here and remove redundant captures.

### Initial receiver baseline

These November 12 measurements are the original receiver baseline; later v3
experimental results below use a different receiver path and do not retroactively
change these failures.

The November 12 native screen uses the Ryzen 5 PRO 5650U (6 cores / 12 logical
CPUs, approximately 15 GiB RAM), GCC Release build, native CPU arithmetic, seed
417, and a deterministic encrypted 384-byte XZ attachment. The probe records
the actual complete profile, channel, seed, source hashes, receiver time and
resource quantities. Its small attachment is deliberately dominated by fixed
bootstrap/cycle overhead; bulk-rate estimates are reported separately.

| Production receiver condition | Result | Sampled audio | Receiver service / sampled second |
| --- | --- | ---: | ---: |
| SSB default, effects Off except configured thermal noise | Exact authenticated attachment; 8 LDPC frames | 51.54 s | 0.106 s/s |
| Exact HF manual row | No acquisition; zero decoded intervals | 51.76 s | 0.130 s/s |
| Exact EME manual row | No acquisition; zero decoded intervals | 51.74 s | 0.133 s/s |
| HF oscillator-only isolation, same seed/source | Exact authenticated attachment | 51.54 s | 0.105 s/s |
| EME oscillator-only isolation, same seed/source | Exact authenticated attachment | 51.54 s | 0.105 s/s |
| HF spread-only isolation, oscillators/AM Off | No acquisition | 51.76 s | 0.127 s/s |
| EME spread-only isolation, oscillators/AM Off | No acquisition | 51.74 s | 0.124 s/s |
| EME selective-AM-only isolation, oscillators/spread Off | Acquired; coding failed | 51.73 s | 0.130 s/s |

These failures are reception failures, not a demonstrated CPU ceiling. The
failed attempts did not execute payload LDPC decoding. Their waveform peaks
exceed normalized PCM full scale after synthetic fading; software simulation
does not clip those samples. Physical playback needs separate gain/headroom
checks. Seed 417 happens to produce only −0.034 Hz HF translation offset, so
its oscillator-only success does not qualify the full declared offset range.
Disabling spread also changes the selective-AM time scale, so the last row
does not isolate AM at the full EME row's time scale.

### Native v3 receiver evidence

The adopted public SSB format is QPSK rate-1/2, 16,200 coded bits (7,200
protected information bits), depth 1, at 48 kHz. It uses a 2048-symbol
preamble, 64-symbol marker and temporal pilots; its profile identity is
`75f50bf84bab7018b7ac51e164b72a7060088ab01b8c748dc9cf4cb3c83e9dcc`.
The earlier 64,800-bit 16/16-pilot experimental waveform, identity
`69899b29bcb0118264b1680876fffae21420aa78be2229ef44a47b10abe9b37a`, is an
experimental profile. Receiver-side clock, rate and phase refinements are
local processing; no feedback or adaptation is sent to the transmitter.

The original canonical 16/16-pilot HF417 full run decoded both LDPC frames but
failed physical-end completion at channel EOF; no authenticated file was
released. Its whole-process CPU was 208.534 s, receiver wall 206.019 s for
67.8155 s audio (3.038 receiver-wall s/audio s), with 139.705 s modeled
backlog. This remains the preserved failing baseline, distinct from later
repaired runs ([receipt](../local-evidence/fast-channel/canonical-ssb-hf417-native.json)).
A paired row-reuse change cut preparation 5.336→0.949 s and payload work
5.860→1.477 s with all 4096 LLRs bit-identical, but only on a frozen two-slot
prefix; it does not establish transfer service ([pair](../local-evidence/fast-channel/hf-dsp-profile/pair-sha256.log)).

The current physical speaker/microphone HF and EME manual trials overflowed
the eight-second capture FIFO before acquisition. On a two-second retained
HF prefix, repeated finite pulse and periodic-template construction accounted
for 48.5% of model learning. The receiver now prepares the folded transmit and
public word spectrum once per fitter, then applies the clocked filter on the
public period. It subtracts the original finite support exclusions from both
the pulse and tap-time moment. The same 19-fit rejection took 6.506 seconds
after the change versus 11.203 seconds before, with identical recorded
candidate and marker decisions. The recorded early marker still fails its
prediction and coherence checks. These results preserve the physical failures
and do not establish acoustic reception or sustained capacity. See the
[current validation](validation.md#fast-acoustic-acquisition-and-noise-reference-follow-up--12-november-2026).

For the fixed native SSB geometry, independent fit workers now score those same
public windows from immutable observations. The receiver commits results in
order and cancels younger fits before allocating the winning equalizer. Each
worker's complete fit and snapshot has a bounded memory lease, and the caller
and helpers share twice the effective logical CPU budget. An early version
left acquisition pending at the end of the existing one-interval test. The
receiver now settles overdue fits by the first complete public interval,
checking at each frontend callback and retaining the earlier pilot evidence.
The original short test, resumed transmission, and weak-terminal checks pass
with scored physical quiet.

The same two-second HF rejection replay preserved all 19 ordered fit outcomes.
Total elapsed time fell from 6.518 to 1.789 seconds, while total CPU time
increased from 6.517 to 15.553 seconds. On the 12-logical-CPU host, 15 fits ran
concurrently under the 24-worker allowance. The peak reservation was 815 MB
including worker stacks, with 193,024 KiB of resident high-water in the probe.
The replay submitted a burst faster than live audio arrives and still rejected
the public marker. Separately, the current HF manual simulation recovered its
384-byte encrypted attachment exactly and scored 6.122 seconds of quiet. It
accepted the third fit, canceled 12 younger fits, and took 18.230 seconds of
elapsed time for 22.272 seconds of input. Sustained acquisition service and
live acoustic reception need separate checks.

A final refresh of the EME row also receives the exact encrypted 384-byte
attachment and scores 6.128875 seconds of quiet. Its 23.213 seconds of receiver
work exceed the 22.272 seconds of input; whole elapsed time is 24.001 seconds
and process CPU time 34.660 seconds. This is a finite simulation success,
not a real-time capacity pass. The parallel acquisition repair does not by
itself qualify the complete payload service on this row.

The native probe's `--replay-paced` option then replayed each full retained
physical failure at 48 kHz with the original source, profile and receiver
assumptions. It dispatched the unchanged PCM on a monotonic 4,096-sample
block cadence. Both original captures reached EOF without acquisition after
21 ordered fits each. The HF and EME captures contained 475,200 and 466,800
samples. Peak scheduled arrival backlog was 4,220 and 10,884 samples against
the live FIFO's 384,000-sample capacity, and receiver completion of the replay
was 4.55 and 3.40 ms behind the respective sample timelines. Aggregate CPU was
12.99 and 14.27 seconds, while elapsed time was 9.92 and 9.74 seconds. The
backlog measure reflects scheduled sample arrivals through the probe and
excludes fresh audio-device work. This establishes service on those two
retained captures and leaves their marker rejection unresolved.

Fresh physical HF and EME manual trials ran through the available speakers and
microphone without a capture error. Their actual capture backlogs peaked at
6,000 and 26,400 samples against the 384,000-sample FIFO. Both still failed to
acquire. The original selected 100% HF setting clipped 914 of 1,183,200 captured
samples. A separate explicit 50% diagnostic kept the requested playback peak
below full scale and recorded no input clipping, yet all 21 ordered fits still
failed to lock. A diagnostic using the canonical emitted public preamble
estimated the arrival at sample 15,512. Two eligible fits proposed epochs
within about one sample of that arrival, with independent public-word NMSE of
0.217 and 0.252 against the 0.20 gate. The matched digital HF fit passed with
0.0159 public-word and 0.0228 marker NMSE. This isolates the physical failure
before marker verification even when clipping is absent.

The fixed-8 HF417 native simulation later recovered the exact encrypted
367-byte source over 64 intervals, passed both LDPC frames in four iterations,
accepted two rate updates and scored 6.10833 s of tail quiet. Receiver wall was
62.8153 s for 67.7547 s audio (0.927 s/s); whole-process CPU, including probe
generation, was 65.5078 s. A single physical headphone-to-mic test also passed:
exact authentication, 64 intervals, two LDPC frames/four iterations and
6.10342 s quiet; RX 67.0086 s/68.0793 s audio (0.9843 s/s), wall 70.6547 s.
Its 8-second FIFO peaked at 6.448 s and drained to 2.121 s. Audio-ready VmSize
was 534,344 KiB, so the run used a 1 GiB virtual-memory limit. Earlier 512 MiB
trials remain failures (FIFO overflow before lock; a separate `std::bad_alloc`
with allocation stage unknown). These are one-seed simulation and one cable
fixture, not reliability, RF, general capacity, default dispatch or app-route
qualification. See [simulated HF](../local-evidence/fast-channel/ssb-v3-fixed8-hf417-native.json),
[cable pass](../local-evidence/fast-channel/ssb-v3-hf417-cable-va1024.json),
[FIFO failure](../local-evidence/fast-channel/ssb-v3-hf417-cable-va512.json), and
[allocation failure](../local-evidence/fast-channel/ssb-v3-hf417-optimized-cable.json).
Fixed-8 quiet/interruption and positive/null controls passed, including 5.5 s
resume ([controls](../local-evidence/fast-channel/ssb-v3-fixed8-tests.log)).

For the public 16,200-bit QPSK depth-1 geometry, actual protected length is
7,200 bits (rate-1/2). Both manual rows passed 16-interval native simulation,
but the 895 bit/s bulk estimate is below 1,052 bit/s for 64,800 bits. Short
physical cable runs also passed: EME recovered/authenticated the 367-byte file
over 16 intervals/two LDPC frames/six iterations with 6.0427 s quiet; HF passed
with two iterations and 6.1034 s quiet after an earlier matched-refinement
version overflowed. The corrected HF cable run accepted two rate refits
(1.445/1.441 s); its 8-second FIFO peaked at 7.388 s and drained to 3.700 s.
EME peaked at 7.167 s; that result used a pre-value-only binary. These single
cable runs are close to or slower than real time (HF source-cycle service
.9366 s/s, overall 1.123; EME source-cycle service .894 s/s, overall 1.275),
not sustained capacity evidence. The matched HF retained-prefix comparison
kept all 32,768 LLRs
identical while refits fell from about 4.8 to 1.14 s. See the [HF
pass](../local-evidence/fast-channel/ssb-v3-hf417-short-ldpc-optimized-cable.json),
[EME pass](../local-evidence/fast-channel/ssb-v3-eme419-short-ldpc-cable.json),
[initial HF overflow](../local-evidence/fast-channel/ssb-v3-hf417-short-ldpc-cable.json),
and [matched replay](../local-evidence/fast-channel/hf-short-refinement/comparison.json).

An optimized recurring EME simulation with a 1,000-byte source completed
three cycles exactly, accepted two phase updates and scored 6.12881 s quiet.
Post-bootstrap service improved 1.0196→0.9102 s/s; individual cycles were
0.9214/0.8990 s/s, but overall service remained 1.0244 s/s with 3.642 s modeled
backlog. This is one matched simulation, not a real-time or capacity claim
([receipt](../local-evidence/fast-channel/ssb-v3-eme419-qpsk16200-recurring-3cycle-kernel-optimized.json)).

Canonical 64,800-bit EME remains problematic. Its full cable attempt ended with
an 8-second FIFO overflow after 22 intervals and 80 no-detections from 22.14 s;
there was no bootstrap, authentication or physical completion. A separate
canonical midpoint simulation did pass one exact 367-byte source, but at
68.980 s RX/67.730 s audio (1.018 s/s) and does not qualify EME cable service.
The earlier sparse EME full simulation also failed late (57/64 intervals,
three erasures); a forced-local prefix failed startup, and a fresh pooled-start
pair exceeded its fixed gate. Keep these failure branches separate from the
midpoint diagnostic. See [cable failure](../local-evidence/fast-channel/ssb-v3-eme419-cable-va1024.json),
[midpoint simulation](../local-evidence/fast-channel/acquisition/eme-clock-midpoint/run.json),
[sparse failure](../local-evidence/fast-channel/ssb-v3-eme419-full-native.json), and
[startup gate](../local-evidence/fast-channel/ssb-v3-eme419-local-pooled-fresh-pair-3p2.log).

Other EME estimator evidence remains diagnostic: removing a fixed 4% pivot
truncation reduced early hard-sign errors 1098→149 of 8192 at similar cost,
but the 32-interval bootstrap still failed with 13,776 wrong signs concentrated
late. A separate sparse branch's single interval had 49/2048 wrong signs
(2.39%), while its matched bootstrap failed at 50 LDPC iterations with
17,795/65,536 wrong signs. Neither result contradicts the distinct midpoint
simulation pass. Older scratch receivers also decimated before filtering and
used a known startup window; those results do not qualify continuous unknown-
start EME. The [full regularized-tap result](../local-evidence/fast-channel/acquisition/eme-419-sparse-late-free-joint-4qam-prefix-bracket-soft-interval-codeword-ridge-prefix-full.log),
[raw interval](../local-evidence/fast-channel/acquisition/eme-419-sparse-late-free-joint-4qam-prefix-bracket-soft-interval.log),
and [failed bootstrap](../local-evidence/fast-channel/acquisition/eme-419-sparse-late-free-joint-4qam-prefix-bracket-soft-interval-codeword.log)
remain active evidence for this unresolved branch.

The current higher-order screen selected QPSK for the common manual format; a
16-QAM/16,200-bit screen passed HF and failed EME, so no additional
constellations were pursued ([HF](../local-evidence/fast-channel/ssb-v3-hf417-16qam-short-bootstrap.json),
[EME](../local-evidence/fast-channel/ssb-v3-eme419-16qam-short-bootstrap.json)).
The 4096-QAM, LDPC 8/9 configuration has a separate
bounded flat-channel result: seed 417 recovered/authenticated a 23,000-byte
encrypted source over 160 intervals/five LDPC frames and five iterations, with
6.149 s quiet. RX was 20.645 s for 33.792 s audio (0.611 s/s); whole-fixture
file goodput was 5.445 kbit/s. Across three full source areas, 21,357 encoded
XZ bytes over 15.978667 s audio and 9.308223 s receiver wall yielded a derived
10.692757 kbit/s sustained interval rate; this excludes bootstrap/final fill
and does not replace the directly observed whole-file rate. A fresh seed-418
32-slot run accepted bootstrap but stopped at its intentional 6.45 s audio cap,
without file or exported feedback event. This one flat 39.5 dB fixture does not
qualify the higher-order alternate on manual fading rows or generalize it to
another seed. The adopted common manual standard remains short QPSK rate-1/2,
16,200 bits; these high-rate results do not establish broader reliability. The
prior 32-slot calibration showed the corrected `dmin²/16` guard pass
authenticated bootstrap where the old 0.015 floor failed LDPC; retain the
discriminator and
[full result](../local-evidence/fast-channel/high-order-calibration/full-417-native.json),
[fresh capped run](../local-evidence/fast-channel/high-order-calibration/bootstrap-418-native.json),
[calibration](../local-evidence/fast-channel/high-order-calibration/run.log), and
[focused tests](../local-evidence/fast-channel/high-order-calibration/production-tests.log)
as calibration evidence; high-rate reliability remains unqualified.

The public Fast SSB default and typed facade now dispatch the qualified
48 kHz QPSK 16,200-bit profile. The exact 4096-QAM, LDPC 8/9, 64,800-bit profile
also uses the native modeled receiver; unsupported sample rates and unqualified
variants fall back to the standard SSB receiver. The focused public dispatch
group passed 9/9 tests. A bounded shared Session loopback test on the EME419
case also passed: its encrypted 31-byte diagnostic authenticated over 16
intervals/two LDPC frames/four iterations and completed at input tick 1,084,512.
The test returned 512 actual input I/Q points, exposed no file, and peaked at
279,520/384,000 queued samples (5.823 s). It surfaced no negotiated audio
format; ALSA warnings were retained in stderr, with no Session error. This is
functional Session/cable evidence, not a native GUI-button hardware test or a
full EME file transfer. Retain its compact receipt, source, build log and hashes
as the bounded shared-Session validation record; the compiled proof wrapper is
excluded from retained evidence. Current HF/EME cable receipts and measured
ADC captures support physical-input and bit-exact replay claims that synthetic
channel runs cannot reproduce. Keep those captures only while these claims are
current; retire them when superseded. Obsolete copied sources, executables,
archives and dumps from the scratch campaigns have been removed.

### Acoustic-short manual-row follow-up

The initial FOX/long-source exploration below used an intermediate
`datapump/fast/v2`, `short/v1`, `sc-training/v2` wire identity; retain it as
historical evidence of the failure and receiver-only feedback approach. The
current v4 profile was rechecked with matched chunks. The focused receiver group
passed 7/7, including its six-interval regression
([log](../local-evidence/fast-followup/resilient-capacity-focused.log)); final
v4 results and their limits follow.

Additional bounded Session simulations used the unencrypted 48 kHz acoustic-
short QPSK profile: 1,944 coded bits, rate 3/4, depth 1, nominal −6 dB SNR.
For the HF row (40 W,
130 dB path loss, −174 dBm/Hz, crystal base oscillator, IC-7100 shift at
30 MHz, independent reference, 30 Hz Doppler, 7 ms delay, both selective depths
0.65, margin 1), the original seed-417 three-byte `fox` baseline failed before
acquisition. A 1,024-symbol training/16-pilot-per-16-data candidate also failed.
Its wrapper JSON is truncated; the initial partial outputs are retained only as
failure evidence ([baseline](../local-evidence/fast-followup/baseline.json),
[candidate](../local-evidence/fast-followup/compact-candidate.json)).

Changing the preamble to 2,048 symbols and spacing 16 pilots per 32 data
symbols recovered the exact three-byte `fox` in two coding cycles for seeds 417
and 418, including physical-quiet completion ([seed 417](../local-evidence/fast-followup/fox-session-417.json),
[seed 418](../local-evidence/fast-followup/fox-session-418.json)). On seed 418,
a 640-byte, six-interval source first failed three of six LDPC frames. The
receiver-only protected-feedback version, using three disjoint FIR fits and
excluding the 104-bit alignment fill, passed all six physical intervals/frames
and recovered the exact
source after physical quiet ([before](../local-evidence/fast-followup/long-session-418.json),
[updated](../local-evidence/fast-followup/long-session-feedback-418.json)). These
are a few unencrypted software realizations, not encrypted/manual-row reliability
or throughput qualification.

Current v4 full-core receipts recovered the same three-byte public `fox` with
seeds 417 and 418: each completed two LDPC frames with no failed frame and
physical quiet, in 3.795 s and 3.696 s respectively (12.181 s simulated audio).
For seed 418, the 640-byte adverse fixture then passed all six physical
intervals with no failed frames under three input chunk sizes (1,201, 257 and
4,096 samples). The matched runs reported the same result hash, 58,899,880-byte
peak workspace, 5.43–5.46 s receiver time for 19.82 s audio, and a 6.114 s quiet
tail. A same-seed weak-AWGN v4 `fox` also completed exactly in two intervals for
all three chunk sizes using the selected coherent `standard-fast` receiver.
This was not a separate full sparse-only run, so it does not establish a current
v4 sparse-route failure. The weak case reported 31,361,984-byte peak workspace
and 1.76–1.81 s receiver time. These matched v4
simulations confirm chunk-invariant outcomes for these fixtures, not a manual
channel sensitivity threshold, population reliability or sustained capacity.
Receipts: [seed 417](../local-evidence/fast-followup/fox-final-417.json),
[seed 418](../local-evidence/fast-followup/fox-final-418.json),
[adverse chunk replay](../local-evidence/fast-followup/paired-adverse-418.log),
[weak-AWGN chunk replay](../local-evidence/fast-followup/paired-weak-418.log),
[binary hashes](../local-evidence/fast-followup/final-core-binaries.sha256).

An intermediate-format weak-AWGN seed-418 comparison also separates
acquisition choices. At 40 W,
−174 dBm/Hz, 183.5902 dB path loss and with oscillator/spread/AM effects Off,
the sparse delay-Doppler path did not acquire; the competing standard-fast
coherent path recovered the exact three-byte file in two LDPC frames. This
intermediate-format result does not establish a sensitivity threshold or
long-bulk performance; the current v4 same-waveform chunk replay is reported
above ([intermediate sparse](../local-evidence/fast-followup/weak-session-feedback-418.json),
[intermediate coherent](../local-evidence/fast-followup/weak-session-dual-418.json)).

The Live loopback UI forwards the selected nonempty Console message or file
through Session, publishes captured input, and uses fixed native gain/time bounds
without truncating the source.
The selected-source cable check now passed with the current v4 profile in both
public and encrypted modes. These used the acoustic-short −6 dB preset; this is
not a measured cable SNR. Each transmitted the exact three-byte Console
`fox`, completed two of two LDPC frames with no failures, and reached scored
physical completion; the encrypted case authenticated. Neither exposed a file.
Peak captured backlog was 150,128 samples (public) and 141,728 (encrypted).
Both checks used the default analog 48 kHz left-mono path at gain 0.1 and
retained 512 actual input points in the final diagnostic. Input polling
observed 125 capture-progress updates per run, with maximum gaps of about
100.4 ms; this is polling cadence, not a GUI paint or scheduling bound. ALSA slave-open
warnings occurred before the working PipeWire path and produced no Session
error. The Session completion tick is input-clock based; it cannot be subtracted
from the transmit-side quiet-start clock to claim a six-second cable tail.
These are two short same-host cable checks, not a physical manual-row,
sensitivity or sustained-capacity qualification ([public](../local-evidence/fast-followup/fox-cable-public.jsonl),
[encrypted](../local-evidence/fast-followup/fox-cable-encrypted.jsonl),
[parsed evidence](../local-evidence/fast-followup/fox-cable-evidence.json),
[public stderr](../local-evidence/fast-followup/fox-cable-public.stderr),
[encrypted stderr](../local-evidence/fast-followup/fox-cable-encrypted.stderr)).

The receipts, thin native proof wrappers, independent fixture output and three
final tab images in `local-evidence/fast-followup/` support these current local
claims. Retire them when replacement validation supersedes those claims, keeping
the findings and failure boundaries here. Superseded probe implementations,
binaries and redundant screenshots have been removed.
The Loopback Details note was shortened after source review identified an
uncovered text-fit risk. The expanded native fit check and focused GUI test now
pass for all profiles, SNR modes, Live/Loopback text and file states at 720 px
height and both 1,194/1,244 px widths ([fit receipt](../local-evidence/fast-followup/native-loopback-fit-receipt.json),
[native log](../local-evidence/fast-followup/native-loopback-fit.log),
[GUI test](../local-evidence/fast-followup/gui-loopback-fit-test.log)).

The generated digital SSB PCM spectrum was measured on a 2–18 s excerpt with a
65,536-sample Hann window, 50% overlap and 0.7324 Hz bins (22 periodograms).
The narrowest intervals containing 99% and 99.9% of power were 404.66–2593.87
Hz (2189.21 Hz) and 338.75–2648.07 Hz (2309.33 Hz); power outside 300–2700 Hz
was −62.08 dB relative to total. These are digital PCM measurements only, not
audio-device, radio, RF-emission or regulatory qualification. See the
[report, provenance and analyzer](../local-evidence/fast-channel/spectrum/README.txt).

### Bounded outer-pilot comparison

A separate reviewed diagnostic placed low-power pilot tones at 546.875 and
2453.125 Hz around a 1000-baud center waveform, with 16 known pilots per 16
data symbols. The pilots used 20% of total power, leaving 80% for center data;
the layout carries 54.2% less baud than the 2181.8-baud full-band format and
reduces center data power by 0.97 dB versus the equal-power 1000-baud pilot-free
control. A controlled flat case improved held-out center/marker NMSE to
0.00000680 / 0.0000103 from 0.00003746 / 0.0001039 without pilot assistance.
In seed-417 HF the pilot-assisted fit remained worse than the pilot-free
control (held-out NMSE 0.002543 vs 0.001679; marker NMSE 0.010778 vs 0.008830).
All EME arms failed the independent marker check. This single-seed, known-window
prefix comparison establishes no benefit for adoption on either manual row;
it did not run FEC, physical completion, live acquisition or a full transfer.
The pilot-free arm was not an automatic tone-loss fallback test. See the
[flat](../local-evidence/fast-channel/outer-pilots/flat.log),
[HF](../local-evidence/fast-channel/outer-pilots/hf.log), and
[EME](../local-evidence/fast-channel/outer-pilots/eme.log) receipts.

Removing the EME estimator's fixed 4%-pivot truncation and retaining all eight
regularized tap directions reduced the first four intervals' errors from
1098 to 149 of 8192 signs at essentially unchanged CPU cost. Clean decisions
were byte-identical. The full 32-interval bootstrap still failed, with 13,776
wrong signs, concentrated late. This identifies a harmful estimator shortcut,
but leaves sustained timing/support tracking unresolved; low-rank freezing is
not justified merely by an unregularized conditioning threshold.

In the earlier native receiver revision, the operator-confirmed headphone-to-
microphone cable recovered a shortened, explicitly depth-1 encrypted SSB
baseline in 18.60 seconds of captured audio. Both exact manual rows then ran
for a bounded 60 seconds and failed to acquire. Capture peaks were
0.1062 (HF) and 0.1002 (EME), with no clipped capture samples and at most
1200 queued samples (25 ms). The default PipeWire/ALC257 route used 48 kHz
mono capture channel 0 and stereo playback; output gain was 0.10. These are
observed failures from that earlier receiver and digitally applied channel
effects, not inferred results from offline runs. Those attempts did not
authenticate or deliver a file. The later fixed-8 HF cable pass is recorded
above and does not retroactively change these outcomes.

A temporary native differential-pair screen used two 1000-baud RRC arms at
900/2100 Hz, rolloff 0.10, constant total symbol energy, and one joint QAM/APSK
label per carrier pair. It was supplied correct nominal timing and trained a
relative channel vector from known pilots; it did not implement acquisition,
ISI equalization, coding or physical completion. The GLRT decisions reject a
common complex gain without assuming that independently faded carriers retain
the same gain. Clean 4/16-point and common +17 Hz 64-point controls had no errors.

| Seed 417 condition | 16-QAM BER, 20% / 50% pilots | 16-APSK BER, 20% / 50% pilots |
| --- | ---: | ---: |
| HF manual channel | 40.76% / 39.42% | 45.02% / 43.46% |
| EME manual channel | 32.10% / 30.40% | 34.86% / 35.45% |

With 50% pilots and selective AM disabled, QAM BER remained 36.52% HF and
26.40% EME. Removing excess delay while retaining Doppler and AM reduced it
to 20.41% and 14.81%; neither change repaired the design. These are bounded
negative findings about this pilot-only pair, not every possible dual-carrier
equalizer. Do not replace the working formats with that failed candidate.

For 1.215 seconds of emitted standard-pilot waveform, both matched filters used
6.35–6.43 ms CPU and the two 16-point fixed/tracked decision passes used
0.25–0.27 ms. Buffered receive storage was approximately 740 kB. Those partial
costs do not distinguish a complete QAM/APSK computational ceiling. At −28 dB
over 2400 Hz, an ideal post-filter one-baud QPSK pair made 17.61% raw bit errors
versus 2.53% for a known-phase coherent QPSK reference at equal power. The ideal
AWGN capacity there is 5.48 bit/s. For this one-label-per-pair layout, native
256-APSK has at most 7.85 kbit/s at rate 9/10 and 0.10 rolloff, even before
pilots and framing. Larger constellations, a different mapping or a different
waveform would require new evidence.

An earlier 256-QAM coherent-receiver geometry completed one flat-channel
high-rate trial: LDPC 8/9, depth 1, 35 dB SNR over 2400 Hz, all modeled
oscillator/spread/AM effects Off, seed 417, and an encrypted 80 KiB
incompressible attachment. Exact authenticated recovery took 61.952 seconds of
sampled audio including startup and scored quiet, giving **10,578.51 bit/s**
delivered source throughput. End-to-end processing took 7.740 s; receiver
service was 6.517 s, or 0.1052 s/s. All 13 LDPC frames passed. DSP workspace
was 1,934,904 bytes; probe PCM storage was 16,384 bytes and the opaque decoder
spool peaked at 85,440 bytes, separate from its 256 MiB local resource cap.
The 13,476.70 bit/s bulk airtime estimate is not the measured fixture rate.
This is one clean-channel success for that earlier 256-QAM geometry, not a
dual-carrier or fading qualification and not a throughput forecast for the
public SSB default (QPSK, rate 1/2, 16 pilots per 16 data symbols). Its keyed
encrypted bulk estimate is 895.317 bit/s; the finite 50 MB public-file estimate
is 912.222 bit/s including fixed overhead. Neither inherits the earlier
10.58 kbit/s observation.

At −28 dB over the original 2400 Hz, a one-baud 4-QAM, LDPC 1/2, depth-1
trial measured a 160-second prefix. It used 0.2348 seconds of receiver time
and 2,880,952 bytes of DSP workspace. It stopped before the 2048-symbol
preamble ended and performed no payload decoding. Its full 384-byte protected
fixture models 72,985 seconds (20.27 hours) of audio; that forecast was not
run. Cheap narrowband frontend processing does not qualify complete
weak-signal reception, tracking, coding cost or search over unknown RF offsets.
A second, single bounded prefix reached 2304 seconds of audio, covering the
entire initial training word, first marker and filter/lookahead latency. It
still did not acquire or decode any LDPC frame, stopping at its planned audio
limit. Receiver service was 3.601 seconds (0.001563 s/s), with the same DSP
workspace. The native test wrapper only raised its local audio cap; the receiver
was unchanged. This establishes failure at that first opportunity for seed 417
in ideal AWGN with zero oscillator error, not every later opportunity or a
complete low-SNR computational ceiling.

QAM symbols are complex I/Q amplitude coordinates, not frequency-shift bins.
Each symbol encodes its amplitude and phase in the two I/Q coordinates, so
waterfall magnitude peaks alone cannot decode its labels. QAM and APSK still
share carrier mixing, synchronization and channel equalization; a cheaper
demapper by itself does not establish a lower complete-receiver ceiling.
The production Cartesian QAM demapper factors its distance calculation into
Gray-coded PAM axes; classic APSK enumerates ring points and label bits.
The classic and capacity paths also use different codecs and minimum symbol
rates, so comparing their whole-receiver timings would confound waveform,
coding and detector costs. Four-point QAM and QPSK have the same point set.

The public SSB default and typed receiver dispatch use the geometry below.
Ordinary modem transport and explicit `classic` Fast profiles are unchanged.
These are audio waveforms for the radio's modulation and demodulated-audio
interfaces; selecting a profile does not configure the radio or operate PTT.

## Public default and explicit alternate geometry

The SSB column is the public default. The 4096-QAM alternate is supported only
for the exact explicit 64,800-bit profile described above. The FM column retains
the earlier capacity geometry as a reference.

Both profiles use the 300–2700 Hz audio passband with 2400 / 1.10 =
2181.818… symbols/s, a 1500 Hz carrier, 10% RRC rolloff, and amplitude
0.30. Each starts with a 2048-symbol preamble and includes a 64-symbol
marker every four 2048-bit physical intervals. The capacity source uses
eight bits per source byte and a protected cycle flag.

| Setting | SSB public default | FM capacity reference |
| --- | --- | --- |
| Modulation | QPSK | Cartesian Gray 64-QAM |
| Inner code | LDPC 1/2, 16,200 coded bits / 7,200 information bits | LDPC 3/4, 64,800 coded bits |
| Frames per coding cycle | 1 | 4 |
| Tracking pilots | 16 pilots per 16 payload symbols | Four pilots per 64 payload symbols |
| Outer RS | 16 parity bytes / 4,034 data bytes = 0.3966% | 76 parity bytes / 24,224 data bytes = 0.3137% |

The public SSB identity is
`75f50bf84bab7018b7ac51e164b72a7060088ab01b8c748dc9cf4cb3c83e9dcc`; the
experimental 64,800-bit profile has identity
`69899b29bcb0118264b1680876fffae21420aa78be2229ef44a47b10abe9b37a`. Both bind
the 16 pilot positions defined by
`sync_symbol(11 + 13*j)` for `j = 0…15`. The same sequence applies when
the operator selects another SSB capacity symbol rate. Both ends must
use matching local waveform settings.

The lower output amplitude leaves more pulse-shaping headroom than the previous
0.50 radio default. It does not establish a safe radio input level: radio USB
modulation gain, speech compression, ALC, and FM deviation remain properties of
the external audio/RF chain. The profiles use separate integrity domains.

The 300–2700 Hz passband corresponds to Icom's documented MID SSB transmit
filter. The manual also lists WIDE as 100–2900 Hz and NAR as 500–2500 Hz.
The chosen waveform therefore needs a matching or wider receive/audio filter;
the narrow setting clips it. These are documented filter settings, not a
measurement of a connected IC-7100. [Icom full manual, section 6-6](https://www.icomjapan.com/support/manual/2288/)

The 2400 Hz design width includes the RRC rolloff. Finite pulse shaping and a
transceiver's non-ideal filter skirts still require measured occupied-bandwidth
and RF-emission checks. For US amateur operation, the 2.8 kHz limit in
§97.307(f)(3) applies where the band/emission rules invoke that paragraph; it
is not blanket permission for every HF frequency or emission.
[47 CFR §97.307](https://www.ecfr.gov/current/title-47/section-97.307)
Encryption here is tested only in simulation and cable audio. An amateur
license alone does not permit messages encoded to obscure their meaning;
§97.113(a)(4) generally prohibits that practice, subject to its stated exceptions.
Public signed files and encrypted files are distinct operating choices.
[47 CFR §97.113](https://www.ecfr.gov/current/title-47/section-97.113)

A production S16 PCM capture of the earlier SSB capacity 64-QAM waveform, including its
complete pulse tail, measured a 99%-energy interval of 388.46–2609.90 Hz
(2221.44 Hz). Integrated energy outside 300–2700 Hz was −64.56 dB relative
to total energy; outside 100–2900 Hz it was −81.25 dB. The narrower
500–2500 Hz window excluded 8.09% of energy. These are one finite generated
waveform's zero-padded DFT measurements, including quantization; they do not
measure connected-radio filtering, distortion or FCC emission compliance.

The FM preset deliberately uses the same audio band. Icom's published 12 kHz
FM selectivity refers to RF/IF reception and does not establish a flat 12 kHz
modulation-audio path. USB and other audio paths, FM pre/de-emphasis and deviation
must be measured before widening this preset. [Icom specifications](https://www.icomjapan.com/lineup/products/IC-7100USA/)

## Throughput

These are modeled bulk-airtime estimates for a 50,000,000-byte source with
bootstrap, final fill, markers, pilots and coding; they are not observed
throughput. The public short QPSK default's 50 MB finite-source estimate is
912.222 bit/s including fixed overhead; encrypted bulk is 895.317 bit/s. The
64,800-bit QPSK experimental geometry's encrypted bulk estimate is 1.052 kbit/s.
This table preserves the
earlier Classic-to-64-QAM comparison and FM settings; its 8.69 kbit/s SSB
estimate and the separate 10.58 kbit/s 256-QAM trial belong to previous
geometries, not the adopted default.

| Profile | Classic public / keyed bit/s | 64-QAM capacity public / keyed bit/s | Speedup public / keyed |
| --- | ---: | ---: | ---: |
| IC-7100 SSB | 3102.63 / 2844.05 | 8697.46 / 8689.06 | 2.803× / 3.055× |
| IC-7100 FM | 1633.00 / 1496.90 | 8697.46 / 8689.06 | 5.326× / 5.805× |

The 64-QAM geometry maps 13,090.9 gross bit/s. At a hypothetical flat
20 dB SNR the 2400 Hz Gaussian channel limit is about 15,980 bit/s, and
the earlier 64-QAM public payload estimate is about 54% of that bound.
Actual RF throughput depends on filtering, fading and receiver service.

## Cable tests of radio-profile output level

The production radio waveform was also tested through the user's physical
headphone-left-to-microphone cable at both 2400 Hz and 240 Hz occupied bandwidth.
All tested raw decisions were exact at amplitude settings 0.10, 0.20, 0.30 and
0.40. However, the 2400 Hz waveform at 0.40 generated a peak of **1.030**, above
the normalized PCM clipping limit of 1.0. The radio defaults therefore retain
**0.30**. Single-carrier normalization already keeps average PCM power
approximately constant as symbol rate and bandwidth decrease; no additional
bandwidth-dependent gain is needed. These are audio-cable results, not tests of
an IC-7100 or its RF chain.
[Level measurements and evidence](fast-cable-level-calibration.md)

An actual radio needs its own level calibration. Icom's `USB MOD Level` adjusts
USB transmit input, while `ACC/USB AF Level` controls received audio output.
Data mode disables speech compression and fixes transmit audio to 300–2700 Hz.
The cable measurement procedure can be reused, but the chosen level must also
be checked for RF linearity and ALC behavior in SSB, or deviation in FM. A clean
computer loopback does not establish these properties.
[Icom full manual, sections 17-24 and 18-3](https://www.icomjapan.com/support/manual/2288/)

## Earlier sampled-noise tests and limits

No radio, speaker, microphone or audio device was used for the earlier
sampled-noise tests in this section. Those tests generated and demodulated the
production **48,000 samples/s real PCM**
waveform with a deterministic 751-byte keyed source, including leading and
trailing zero bytes. The trials used the earlier four-pilot radio
geometry at one LDPC frame per cycle. One bootstrap cycle and one source
cycle occupied 64 fixed physical intervals and used two LDPC frames.

Noise was real white Gaussian PCM, scaled by

```
noise_variance = measured_signal_power * Fs / (2 * 2400 * 10^(SNR_dB / 10))
```

Thus SNR is total signal power divided by expected noise power in the positive
frequency **300–2700 Hz audio band**, not noise power across the full PCM Nyquist
band and not an information-equivalent number. The signal power measurement
includes the generated training and pulse tail. This is a reproducible expected
noise-power calibration, not an independent measurement of each finite noise
realization. Silence after the waveform is supplied separately to test physical
completion; the model does not include continuous idle noise or squelch.

| Sampled case | Outcome |
| --- | --- |
| Earlier 64-QAM SSB and FM waveforms, 20 dB | Both recovered exact bytes with no failed LDPC frames |
| Earlier SSB capacity, 20 dB, +3 Hz carrier offset, 15% static echo delayed 0.5 ms | Exact bytes with no failed LDPC frames |
| 64-QAM, LDPC 3/4, 18 dB | Exact bytes; no failed LDPC frames |
| 64-QAM, LDPC 3/4, 17 dB | Bootstrap LDPC/integrity failure; no file |
| 64-QAM, LDPC 8/9, 20 dB | Exact bytes; no failed LDPC frames |
| 256-QAM, LDPC 2/3, 20 dB | Uncorrectable coding cycle; no file |
| 256-QAM, LDPC 2/3, 23 dB | Exact bytes; no failed LDPC frames |

Each row is one deterministic fixture, except the first row which tests both
profile domains. It is not a packet-error-rate sweep or a statistical reliability
claim. The 18 dB success helped select 64-QAM 3/4 in the earlier capacity
study. Higher-order results remain scoped to the sampled fixtures.

The production regression additionally checks exact source bytes, opacity before
physical completion, passband geometry, approximately 0.3% RS, public/keyed
integrity contexts, classic profile geometry, and estimated bulk airtime.
Source interpretation still waits for six seconds of observed absence. EOF does
not finish the reception. None of these experiments establishes performance
under real HF fading, AGC changes, RF frequency drift, FM threshold noise,
nonlinear audio processing, or arbitrary interference.

[Reproduction source and logs](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/radio-capacity-20260921/README.md)
