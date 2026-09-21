# Acoustic −10 dB preset: premature-end correction

The reported “Incomplete … 0 source bytes … fast stream ended within fixed
coding geometry” while transmission continued was a receiver defect. It was
reproduced on the default physical speaker/microphone devices on 2026-09-21.
The narrow acoustic preset also had an unintended output-power increase.

## Why the receiver stopped

The acoustic −10 dB preset uses 16-QAM, LDPC 3/4, depth 1, 73.089804649 baud,
an 87.708 Hz band around 1.8 kHz, four-symbol pilots after each 64 data symbols,
and a full marker every four intervals. Full markers are about 30.65 seconds
apart. A short source needs two 32-interval coding cycles, including the
protected bootstrap, before physical completion.

Previously, one poor pilot set `marker_good=false`. Following good pilots
could not recover because their processing required that flag to be true.
The receiver then counted each following symbol as absent, based on the old
flag, and declared physical completion after six seconds. The next full
marker was too far away to prevent this. The codec correctly reported its
truncated input; zero source bytes were a consequence of that premature end.

Capacity single-carrier reception now separates established interval cadence,
current pilot/marker presence, and whether a group can be demapped reliably.
A damaged pilot erases its own group while later valid pilots can resume
reception. A coherent phase jump re-anchors phase for the next group without
mistaking that jump for a carrier-frequency change. Coherent full markers can
establish continued physical presence
without weakening the exact-sign requirement for accepting framing.

For an already scheduled marker, a separate presence check also recognizes
three coherent quarters if a phase change cancels the whole-marker correlation.
This prevents a long marker at five baud from incorrectly contributing 12.8
seconds of absence. This check cannot acquire, authorize framing or adjust
timing. Damaged-marker intervals remain erased until a later valid full marker
restores decoding; a regression verifies those positions and true silence/noise
completion independently.

On the first failed pilot only its four observed symbols count toward absence.
A later fully observed failed group can add its duration; a valid observation
resets the count. This prevents one brief pilot disturbance from declaring a
whole long payload group absent at very low baud. Low-rate transmitted silence
covers the longer observation windows. EOF, cancellation and decoded source
validity still cannot complete reception. Missing intervals retain their
positions; trailing silence cannot append speculative erased intervals.

## Output power

The acoustic OFDM transmitter's nominal PCM RMS is `amplitude / 4.5`.
The single-carrier transmitter's nominal RMS is `amplitude / sqrt(2)`.
Reusing amplitude 0.4 for both increased average transmitted power by
**10.055 dB** when Auto switched to a narrow carrier.

The narrow acoustic preset now uses amplitude
`0.4 * sqrt(2) / 4.5 = 0.1257078722`, preserving nominal average PCM power.
This leaves the nominal OFDM preset unchanged. Actual received acoustic power
can still vary with frequency response and the room; equal PCM power is not
a measured SNR guarantee. Explicit local amplitude overrides remain available.

## Physical evidence

The default physical output and input were a 48 kHz analog stereo sink and
microphone source, with speaker volume 100% and microphone volume 27%.
Tests used mono/right-channel playback through the production S16 audio path.
System mixer settings were not changed. No capture overrun or PCM clipping
was reported in the two initial trials.

| Test | Received intervals | Raw errors / erasures | Physical result |
| --- | ---: | ---: | --- |
| Original receiver, amplitude 0.4 | 1 of 8 | 41 / 0 in the received interval | Ended around 46 s while the 90.18 s signal continued |
| Fixed receiver, replay of that same recording | 8 of 8 | 272 / 516 across 16,384 bits | Ended at 97.8 s, after the signal |
| Original receiver, new live trial at amplitude 0.1257078722 | 8 of 8 | 2 / 0 across 16,384 bits | Stayed through the signal and completed |
| Fixed receiver, replay of lower-level recording | 8 of 8 | 2 / 0 across 16,384 bits | Ended at 97.85 s, after the signal |

Final constellation EVM improved from approximately 0.262 in the failed
original reception to 0.113 in the lower-level trial. This supports reducing
output power on this setup; it does not isolate which analog or software stage
caused the higher-level distortion. The replay comparison independently
demonstrates that the receiver change fixes premature completion on identical
captured audio.

A complete **60-byte live protected-source transfer then passed**. All 64
intervals arrived; both LDPC frames converged, correcting 714 bit decisions,
with no failed coding cycles. Sent and received SHA-256 hashes matched.
Physical completion occurred at 526.869 capture seconds, after the 519.238-second
transmitted signal and actual trailing absence. Total diagnostic capture was
531.284 seconds, including pre-roll and an intentionally longer tail.
The constellation became noisier late in the trial (maximum polled EVM 0.455),
but reception continued and error correction recovered the exact source.
There was no audio overrun, reported device error or PCM clipping. The maximum
queued audio was 42.7 ms and maximum DSP chunk time 45.3 ms.
Replay through the final build recovered the same source and correction count,
with physical completion at 526.9 seconds within replay chunk resolution.

The [test records](validation-data/fast/acoustic-low-snr-recovery-20260921/README.md)
include commands, results and recording hashes. These tests diagnose a real
channel defect; they do not establish a whole-file success probability or
qualify reception at a calibrated −10 dB reference-band SNR.
