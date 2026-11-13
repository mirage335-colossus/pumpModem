# Fast acoustic short transfers

Select **Speakers / mic · short** for speaker/microphone transfers with smaller
coding blocks. In the CLI, use
`--profile acoustic-short --expected-snr -6`. This is the initial GUI channel
profile, with a default expected SNR of −6 dB. Its single-carrier waveform uses
the same public startup training and pilot-word length as other capacity
profiles; short OFDM retains its six-block startup.

## Airtime and throughput across SNR settings

Every Expected SNR selection compares convolutional coding, 16,200-bit DVB LDPC,
and 648/1,296/1,944-bit QC LDPC with locally fixed coding blocks and interleave
depth. The historical 10.5- and 12.5-second transfer targets remain family
selection heuristics; they are not latency bounds. The automatic compact
single-carrier preset now applies a selected-band fade margin, which can dominate
those airtime heuristics. Its 60-byte keyed fixture determines whether a stronger
code keeps the same physical interval count. Public training, pilot density,
coding and fade margin all contribute to the actual airtime.

The −6 dB preset caps automatic compact single carrier at about 231.130 baud,
occupying about 277 Hz around the 1,800 Hz carrier. With the 17.5 kHz reference
band, this provides a modeled 12 dB selected-band fade margin. It uses QPSK, one
1,944-bit rate-2/3 QC LDPC frame and interleave depth one. At −10 dB it keeps the
same baud cap and selects rate 1/2, with about 8 dB selected-band SNR; −13 dB
uses about 183 baud and −16 dB about 92 baud, both at rate 1/2. Those lower
settings have less than 9 dB selected-band SNR after the cap. The 60-byte keyed
fixture strengthens coding only when doing so leaves the number of physical
intervals unchanged. At the capped −6 dB rate, the estimated short `fox` transfer
is about 32.67 seconds. Use the GUI estimate or
`pump fast-info --profile acoustic-short --expected-snr -6 --estimate-bytes 60`
for current source airtime. Earlier airtimes and the 920-baud −6 dB waveform
describe superseded geometries. The −10 dB half-keyed-attachment estimate is
about 40 seconds; larger files can cross another frame interval.

The current QPSK choice uses rate 2/3 when the canonical 60-byte keyed fixture
keeps the same interval count: versus the former 3/4 code, estimated bulk rate
falls 13.7% for public data and 25% for keyed data, while `fox` and that keyed
fixture keep their airtime. Larger attachments can need an additional interval.

The profile's initial HF-worst spread and 65% selective-AM settings are always-
active receiver declarations that guide deterministic Auto selection. Choosing
clean spread/AM declarations can restore the pre-cap family and throughput.
Only Simulation and an explicitly started loopback test inject the declared
effects; Live PCM and idle loopback monitoring remain clean. Mode changes do not
change the locally selected waveform, so peers must match its profile settings.

Estimates include training, bootstrap, complete coding cycles, tracking, pulse
tails and sufficient scored physical absence, always at least six seconds.
Audio device startup can add delay. The minimum fixture is not a time limit for
arbitrary messages. Expected bitrate counts FEC, integrity and recurring framing
overhead, but excludes compression
gains, startup and final silence. The composer separately estimates each actual
compressed message, filename envelope and final padding.

The earlier compact-waveform comparison motivated the small LDPC choice: its
keyed 2.5 KiB attachment took 39.057 seconds versus 76.986 seconds for the
convolutional candidate under that test's geometry. Depth two improved that
historical attachment result to 36.136 seconds but increased minimum latency.
These findings explain the code/depth tradeoff; they are not airtime or link
qualification for the revised waveform. Both peers must use the same depth.

Under the former 920-baud −6 dB geometry, the seed-421 3/4-rate bootstrap failed
under distortion with 140 hard errors out of 1,944; tracking and ridge changes
did not resolve it. At that same baud and bandwidth, 2/3-rate coding passed the
physical `fox` transfer, and an encrypted seed-420 attachment completed in
14.4 seconds over three frames. Protected-cycle observed-residual inverse
calibration also recovers the original post-bootstrap 3/4-rate seed-420 PCM
without another DSP solve. The former 920-baud geometry passed its original GUI
checks but failed the joint −6 dB noise/HF-worst/65%-AM condition. A 9 dB margin
at 461 baud passed only the original case and failed holdouts; an 8 dB margin at
580 baud failed, as did a diagnostic 1/2-rate, depth-two 920-baud preset on
holdouts. Those prototypes were removed. The current fade-capped 231-baud stock
gate passes all three fixed joint −6 dB cases at rate 2/3, and all three fixed
joint −10 dB cases at rate 1/2. The scoped API, GUI and simulation contract
checks pass 3/3. The current ordinary Live acoustic `fox` check and default
explicit loopback checks for public/keyed text and a keyed binary attachment
pass at measured strong signal levels. An earlier −10 dB-preset physical
alignment failure has not been causally resolved; later strong-signal passes
do not qualify that failure or actual weak acoustic sensitivity. No joint
HF-worst claim is made below −10 dB.

SNR uses the original 17.5 kHz reference band. Narrowing assumes unchanged total
received power and flat noise density. The local preset jointly selects
constellation, coding and symbol rate; it does not adjust transmit power in
response to measured link conditions. Higher-SNR wide OFDM and explicit manual
settings retain their existing geometry. The compact delay/Doppler receiver
path covers acoustic-short QPSK at 48 kHz from 180 baud to below 800 baud, with
up to ±100 Hz frequency offset. Below 180 baud, a separate coherent path has
targeted reference-AWGN checks at about 90 baud; this does not qualify physical
HF-worst reception. The existing modeled path covers 800–2,500 baud under its
documented waveform bounds. These are receiver eligibility bounds, not room
performance guarantees. The model does not account for room resonances or
non-white interference.

## Coding and physical completion

The QC family uses IEEE 802.11 matrices at rates 1/2, 2/3 and 3/4, with
fixed zero information coordinates where needed to carry a whole number of
bytes. It does not implement an IEEE 802.11 PHY. The existing DVB-S2 short
matrices and terminated K=7 convolutional paths remain supported. All retain
outer Reed–Solomon correction, complete-cycle SHA-256 or HMAC integrity,
bootstrap salt and XZ compression. See the [LDPC comparison and independent
fixtures](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/short-ldpc-20260922/README.md) for exact geometry,
error-rate measurements, alternatives and rejected candidates.

All capacity single carriers use 2,048 public startup symbols and sixteen-symbol
pilot words. Small LDPC and compact convolutional single carrier retain their
192-symbol marker: only the first 64 marker symbols fit tracking/equalization;
the remaining 128 are held out for acquisition verification. Automatic short
single carrier uses one marker per fixed interval and sixteen pilots per
32 payload symbols. Earlier sampled tests rejected sparser marker choices;
the current pilot cadence also supplies more frequent tracking evidence. OFDM
retains independent training verification and tracking pilots. The short OFDM cyclic prefix is
10.67 ms, compared with the existing acoustic profile's 85.33 ms.

For eligible narrow profiles, the receiver considers delay/Doppler and coherent
startup fits of this same waveform. The compact coherent path still needs its
independent marker signs; noncompact profiles need fresh pilot verification
before selecting coherent reception. Receiver selection does not request a
different transmission or infer the source length. It uses public startup
length and epoch span, and never derives DSP geometry from received content. The
compact acoustic-short extension requires 48 kHz QPSK from 180 baud to below
800 baud and supports up to ±100 Hz offset. The existing bounded modeled path
requires 48 kHz processing, QPSK/16-QAM, 800–2,500 symbols/s, rolloff at least
0.10, pilot spacing at most 64 and room for the translated fit band. Other
slower and wider profiles retain their applicable receivers; sharing resilient
training does not qualify every profile for every channel impairment.

At the slower baud rates, receiver acquisition unwraps the long-startup CFO
ambiguity with the public repeated 64-symbol sequence. This corrects phase
aliases that had prevented the long-preamble fit from acquiring; it does not
change preset values or remove independent marker verification.
A separate compact coherent path at 90 baud (the −16 dB setting) passed
reference-AWGN checks with flat and 30%-echo channels at +100 ppm. The
delay/Doppler path still has a 180-baud floor. No physical HF-worst qualification
is established at −16 dB.

Received lengths never choose framing, allocation or completion. Source decoding,
XZ and attachment interpretation wait for physical completion. Only fully
observed absent symbols covering a complete physical quiet unit (including its
inverse-interval bound) and at least six seconds can finish reception;
EOF, cancellation and successful correction or integrity checks cannot replace
that event. Missing data retain their positions. Both peers need matching
software, channel profiles and settings. Revised non-SSB capacity single carriers
use a v4 profile context that binds the sixteen-symbol pilot words and selected
LDPC rate, so older single-carrier peers must be updated together. SSB v3,
acoustic OFDM and explicit classic Fast keep their existing wire identities.
Their physical-end rules and ordinary modem transport are unchanged.

## Validation scope

The sampled regression fixtures cover public/keyed UTF-8 text and incompressible
files with fixed noise density, a 5 ms echo at amplitude 0.30 and clock errors
up to ±100 ppm.
They assert withheld source, EOF/partial-absence handling, noise-only rejection,
truncated acquisition, independent marker verification and bounded storage.
The convolutional coding alternative retains independent coding fixtures.

These fixtures and the ideal inner-code noise comparison do not measure
statistical whole-file reliability or qualify a physical room. The revised
headless native physical speaker/microphone probes at −6 dB for seeds 417 and
419 passed under the earlier 920-baud geometry. A seed-420 stress with that
profile acquired and reached physical completion but one of two LDPC frames
failed. These earlier passes do not establish uniform distorted-channel
reliability.
The original pre-patch −9 dB
diagnostic outside the menu failed, and its captured PCM now passes exact
reception and physical completion on replay. Earlier-geometry physical
speaker/microphone transfers at −10 and −13 dB completed in 22.525 and 40.025
seconds; measured physical reference SNR was 30.9 dB at −10 and 27.9 dB at
−13. Separate physical `fox` transfers passed in 22.668 and 40.220 seconds.
GUI telemetry on the earlier −6 dB geometry reported 30.5, 34.4 and 33.2 dB
across three runs. These high measured levels do not qualify weak acoustic
reception or the current fade-capped waveform. Deterministic reference-AWGN
checks passed all five checks, including flat and 30%-echo cases at both menu
settings with +100 ppm clock error. The −16 dB/90-baud coherent path also passed
reference-AWGN checks with flat and 30%-echo channels at +100 ppm. Eight focused
checks, quiet-tail chunk-grace check and current stock gate checks passed. The
scoped API, GUI and simulation contract checks pass 3/3. An earlier −10 dB
physical alignment failure remains unresolved; later strong-signal captures do
not establish its cause or qualify weak-signal sensitivity.

In a separate strong-signal comparison, the 231-baud profile acquired and
decoded two fixed rays. It failed to acquire two independent Gaussian rays
with 30 Hz bandwidth per ray (2σ) and 0/7 ms delay. The Gaussian model has no
added separation between the ray centers. The long coherent startup windows
and pilot gaps of that profile exceed the useful prediction interval of these
moving rays. In an optimistic representability check with the exact epoch and
delays supplied, local 4 ms fitting windows on each side of a held-out gap at
1,000 baud gave normalized prediction errors of 0.014, 0.048 and 0.201 for
gaps of 4, 8 and 16 ms. The fixed-ray control stayed near 0.000024.

The explicit 1,000-baud dense-pilot QPSK half-rate candidate uses four data
symbols between sixteen-symbol pilot words. It estimates local path gains
from received public training, verifies an independent alignment word, then
uses short local fits and a bounded joint inverse of the unknown symbols.
At 90.02 dB-Hz C/N0, public and keyed 3-byte Gaussian-only fixtures reached
exact reception with over six seconds of scored absence. The first complete
decoder passed its two protected frames but ran out of observed absence at
the old 6.25-second source tail. The dense geometry now supplies additional
public marker, group and filter-support time. A matched −76.1 ppm clock-only
failure recovered with a wider public timing fit, and a separate −26.7 Hz
carrier-offset-only control also passed. Separating the carrier proposal
from timing recovered the matched combined oscillator failure. A short real
bandpass before decimation and disjoint raw fit and prediction support then
recovered the strong keyed combined Gaussian, crystal, IC-7100 at 30 MHz
and 65% selective-AM fixture. Its receiver used 0.91 seconds of service and
12.08 MB of DSP workspace over 19.21 seconds of samples. With independently
calibrated −6 dB noise in the 17.5 kHz reference band, the Gaussian-only
fixture failed to acquire, including after aggregating more public training
and alignment observations. The matched white-noise-only control acquired
but retained neutral soft bits for most of its first interval and failed
decoding. A clean physical speaker/microphone capture also failed to acquire
with the two-path dense receiver. The existing coherent receiver replayed
that same captured PCM with identical wire geometry and recovered both LDPC
frames. The combined receiver kept the same public waveform and committed
each ordinal once, selecting complete pilot units from the more credible
receiver model. The initial timing transfer could preempt the other model's
pending startup. Deferring it until the predicted recurring marker recovered
the exact retained physical `fox` and scored its quiet tail. Updating the
observed path delay from separate pilot groups recovered exact authenticated
`fox` in both directions of a clean-to-Gaussian change within the first
interval. The common receiver time map uses recurring verified public marker
ordinals, and only actual selected soft observations feed the constellation.

With alias-filtered 24 kHz coherent processing, the exact captured-room replay
used 3.13 seconds of receiver service and 12.88 MB of DSP workspace. The
strong full-stress simulation used 2.05 seconds over 19.21 seconds of input,
13.08 MB and 6.424 seconds of observed quiet after the propagation tail.
Fixed gain factors of 1, 0.1 and 0.01 preserved exact content. A 12-second
white-noise-only run stayed unacquired with a maximum 21.8 ms processing
burst and no modeled backlog. Truncated input withheld the source, and a
complete interior physical gap retained its neutral ordinal before the
later interval resumed. The automatic preset does not select this explicit
candidate. A clean calibrated −6 dB reference-noise control then completed
exactly with both models. An adopted cadence without a usable initial model
independently recovered its later 7 ms delay on a recurring marker and emitted
usable evidence only after the withheld marker fragments passed. At −6 dB
in the 17.5 kHz reference band, the Gaussian-only seed 421 first failed its
independent startup marker. Using the locally declared Doppler and AM ranges
and an independently observed noise floor to regularize the final weak
marker fit lowered the correct held-out error from 0.557 to 0.366 in the
fixed-timing evaluator. The unchanged blind search then acquired in the
native receiver and scored 6.265 seconds of true quiet, but one of the two
LDPC frames failed. The matched flat-noise input still recovered both frames
and the exact text. Pooling eight independently fitted pilot witnesses per
unit reduced neutral soft bits in the two Gaussian intervals from 1472 and
1792 to 1088 and 1216. Applying the frozen startup prior to both local
pilot and context predictors reduced the neutral counts further to 448 and
192. The receiver still failed one frame, with 181 and 195 incorrect hard
signs among the non-neutral bits. Both alternatives preserved exact flat
reception and scored quiet, but neither repaired weak Gaussian reception.
Supplying the exact simulated start, zero clock and carrier offsets, and
336-sample path delay only to the payload inverse reduced the wrong-sign
counts from 181 and 195 to 164 and 195. The two runs consumed the same
922,112 PCM samples, verified by matching fingerprints, and accepted
the same pilot units in both intervals. The neutral counts and 6.265
seconds of scored quiet were unchanged, and one LDPC frame still failed.
Correcting payload timing alone was insufficient for this realization.
The receiver retains its original per-pilot presence rule. Calibrated weak
Gaussian reception and field reliability remain unqualified. A separate
strong digital `fox` test with seed 421 and the same
full Gaussian, oscillator and selective-AM settings passed. Playing that
same modeled realization through the actual speakers and microphone did
not acquire, despite a peak capture of 0.911 without clipping or capture
discontinuity. The matched digital pass isolates the additional physical
capture path as a factor and does not qualify the dense profile for live
combined stress. An evaluation-only fit of the regenerated pre-playback
signal to that capture found about 323 ms of latency and −25 ppm of additional
clock error. After the alignment was fixed, a static 10 ms FIR reduced
normalized prediction error on two separate public preamble windows and a
marker window from 0.148, 0.135 and 0.155 to 0.057, 0.044 and 0.050. A 22 ms
FIR gave 0.051, 0.039 and 0.046. These held-out residuals were 22–27 dB above
the observed pre-audio noise floor. The diagnostic used the exact modeled
playback as a reference. An operating receiver has only the public waveform
and captured audio. Evaluating the current receiver at that independently
measured physical timing yielded a marker holdout error of 0.339, versus
0.068 in the matched strong digital input. The actual physical phase also
missed the coarse shortlist. Better ranking alone would still leave the
physical marker unverified.
Reconstructing the frozen 10 ms room FIR reproduced all three original
holdout errors to the reported precision. Applying it and the independent
hardware clock mapping to the passing digital waveform reproduced the
acquisition failure in the unchanged receiver. The forward fixture added
no receiver noise and remained below clipping. The identity-FIR control
through the same reconstruction, onset and clock mapping also failed to
acquire before the onset repair. At that stage the comparison could not
isolate the FIR's effect from the onset and reconstruction changes.
Adding just 15,505 leading zero samples to the unchanged passing digital
waveform also prevented acquisition. The correct shifted marker still
passed its independent prediction check in an evaluator, while the
single-window startup proposal dropped the correct timing. Pooling separated
public training windows restored acquisition at all three tested starts but
left some clock estimates too biased to decode the source. Refining those
estimates from startup alone regressed existing cases. A bounded local
demapping offset, selected and checked on separate recurring pilots,
recovered exact bytes and six seconds of physical quiet at offsets of 0,
15,505 and 24,193 raw samples. It leaves the established cadence and quiet
support unchanged. The receiver now permits an epoch-only update when a
recurring marker at the scheduled ordinal independently confirms the change.
A four-interval native test applied 110 ppm after initial acquisition and
removed an intermediate marker. Later markers recovered the same ordinals
after more than 18 samples of drift, with the original slope preserved,
and the true tail still provided six seconds of scored absence. The exact full-stress
capture remained unacquired after the digital onset repair. Its bounded
replay used 4.95 seconds of receiver service, a maximum push of 98.3 ms and
4.91 MB of DSP workspace. The repaired receiver decoded the retained
identity-FIR control exactly with physical completion. The otherwise matched
frozen 10 ms room FIR still prevented acquisition. On this controlled pair,
the learned static coloration was sufficient to reproduce the failure after
the onset issue was removed. This remains an evaluation of one room and
modeled realization. A public-only diagnostic fitted a shared 25-tap room
response after two 4 ms time-varying ray trajectories. An initial fit used a
mismatched observation operator. With the exact frozen-forward operator and
fixed evaluator timing, two alternating updates gave a marker error of
0.609. Eight updates lowered it to 0.023, versus 0.098 on the matched
identity control, with both frozen-forward decoys above 13. The
evaluation-only fit using the exact reconstructed FIR had given 0.043 on
that same frozen-forward marker. On the actual microphone capture, the
eight-update fit used the native capture frontend and independently
measured timing. Its marker error was 0.517, above the current 0.14
strong-signal requirement. Blind acquisition and exact reception through
the physical room remain unqualified.

The first recurring update stopped looking once three markers were missed.
The receiver now caps the search extent and continues checking the next
scheduled marker. A later marker within that extent can reanchor future
samples while retaining every prior wire ordinal. A seven-interval test
suppressed four recurring markers under a 55 ppm clock change. Markers five
and six verified at raw epoch corrections of 70 and 84 samples, with zero
hard-sign errors in their two payloads and 6.273 seconds of true scored
quiet. A strong flat-channel test at −99.822 Hz originally failed before
acquisition. The correct public phase ranked 249th of 256 at zero trial
frequency, outside the strong beam of 16. At a −75 Hz trial it ranked first
and passed the delay proposal and independent marker checks in the
evaluator. The receiver now evaluates up to five frequencies at 75 Hz
spacing before ranking phases, retaining one frequency per phase and
the original marker checks. Strong flat-channel tests at −99.822 and
+99.661 Hz each recovered the exact three bytes, two LDPC frames and
6.273 seconds of observed quiet. The matched −99.822 Hz receiver used
2.04 seconds of service with a 117 ms maximum push.
A 12-second unacquired white-noise control had no reception or completion,
with 4.18 MB of DSP workspace, a 148 ms maximum push and 105 ms peak
modeled backlog. The bank adds work when a noise proposal proceeds into
delay search. The declared envelope still does not establish coverage of
all combined propagation, frequency and clock conditions.

A 128-byte encrypted attachment with seed 419 used the explicit 1 kbaud
profile and the same strong 30 Hz Gaussian, 7 ms delay, crystal,
IC-7100 at 30 MHz, and 65% selective-AM realization. All five LDPC frames
decoded, the receiver recovered the exact authenticated attachment, and
physical completion followed 6.264 seconds of observed quiet. Receiver
service was 4.97 seconds over 35.15 seconds of input, with a 115 ms maximum
push and 13.09 MB of DSP workspace. This exercises recurring coded
intervals under one simulated realization.
Current waveform results and remaining GUI plot/tab-monitor and weak-physical-signal qualification belong in the
[validation record](validation.md). The
[earlier small-LDPC results](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/short-ldpc-20260922/README.md) and
[convolutional results](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/acoustic-short-20260922/README.md)
retain their original source and compact-waveform scope; neither qualifies the
revised 2,048-symbol startup, denser pilots or receiver selection.
