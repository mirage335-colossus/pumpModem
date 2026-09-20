# Fast Modem capacity implementation and cable experiments

20 September 2026. This report covers the new, undeployed Fast capacity format.
Regular Modem and classic Fast retain their existing wire formats. File sizes
here are decimal bytes: 100 KB = 100,000 bytes, 5 MB = 5,000,000 bytes, and
50 MB = 50,000,000 bytes.

## Result and scope

The implemented cable format supports Gray square QAM from 4 through
4,194,304 points; 64,800-bit LDPC at rates 3/4, 7/9, 8/9 and 9/10; approximately
0.3% outer RS parity/data; sparse full markers; and compact eight-bit source
bytes. Public and encrypted transfers use one integrity-protected continuation
or final flag per fixed coding cycle. The [codec specification](fast-capacity-codec.md)
defines the exact format.

**A complete 50 MB physical cable transfer passed at 4,194,304-QAM / LDPC 8/9
in 1,290.664 seconds end to end (21 minutes 30.664 seconds), or 309.92 kbit/s.**
Every source byte matched, including independent SHA-256 hashes. All 6,980 LDPC
frames converged, using 24,267 iterations in total; 5,708,452 information-bit hard
decisions changed during LDPC decoding. No outer-RS correction was needed.
There were no missing intervals, capture errors, FIFO overruns or clipped samples.
The transmitter's peak was 0.89222 full scale and receiver FIFO high-water mark
was 0.30383 seconds, below production's one-second bound.

This setting is now the cable default in the API, CLI and GUI: four LDPC frames,
marker spacing 16, 0.02 rolloff, 18 kHz occupied bandwidth and amplitude 0.30.
The measured complete-file rate is about **5.64 times** the old approximately
55 kbit/s Fast default, and **86.4% of the user's ideal 60 dB / 18 kHz reference**.
That comparison uses an ideal reference, not an assertion that the modem's
effective noise is exactly 60 dB. One successful whole-file trial does not
establish an 80% success probability.

Two development 5 MB transfers at 1,048,576-QAM / LDPC 9/10 completed with exact
source bytes and no failed LDPC frames. The later trial used amplitude 0.30,
balanced bit placement and pilot-based phase interpolation, and had no clipping.
Its signal occupied 139.043 seconds; total diagnostic wall time was 148.511
seconds including startup and an eight-second real silence tail. A fresh
100 KB transfer at the final 4,194,304-QAM / LDPC 8/9 setting also passed after
extending initial clock training.

| Live trial | Format stage | Signal duration | Diagnostic wall time | Exact file |
| --- | --- | ---: | ---: | --- |
| 100 KB, 4M-QAM / 8/9 | Final training and interleaving | 3.824 s | 13.278 s | Yes |
| 5 MB, 1M-QAM / 9/10 | Balanced interleaving, older 128-symbol preamble | 139.043 s | 148.511 s | Yes |
| 50 MB, 4M-QAM / 8/9 | Final training and interleaving | 1,281.087 s | 1,290.664 s | Yes |

Signal duration excludes the real silence tail; wall time includes device
startup, one-second pre-roll, eight-second tail, decoding and final comparison.
The 5 MB row is a separate earlier operating point, not a claimed independent
5 MB qualification of the final default.

This is evidence for the connected path, not qualification of every sound card,
independent clock pair, level setting, cable, or operating-system workload.
The prior [best-case tone SNR measurement](cable-snr-live-study.md) and actual
modem residual error are different measurements. The new results do demonstrate
that the earlier approximately 55 kbit/s ceiling was an implementation/profile
limit, rather than a measured capacity limit of this cable.

## Physical method

The default ALC257 headphone output remains physically connected to its default
microphone input. The probe uses the production capture/playback API, negotiating
48 kHz S16 hardware audio. Captures are subsequently stored as mono float32;
that storage conversion does not increase the hardware's sample precision.
Both playback channels carry the waveform. System playback remains 95%, capture
hardware gain is 0 dB, microphone boost is disabled, and the existing PulseAudio
source setting remains 10%. This study did not change those mixer settings.

The dense waveform is 17,647.0588235294 symbols/s at a 9,300 Hz carrier, with
0.02 RRC rolloff: nominal support is 300–18,300 Hz, an 18,000 Hz band. Capacity
pulses use a 640-symbol full span at this rolloff. Local waveform amplitude is
0.30. The final training preamble has 2,048 known symbols, about 0.116 seconds.
There are four LDPC frames per cycle, a full 64-QPSK-symbol marker every 16 fixed
2,048-bit intervals, and four known pilots after each payload group. With these
dense constellations, each interval fits in one group; pilot spacing 256 is an
upper bound, not a claim of exactly 256 payload symbols between pilots.

`fast_cable_probe` streams deterministic, incompressible source bytes (seed 417)
through the production encoder, transmitter, physical cable, receiver and
decoder. Success requires every recovered byte to equal the independent source
fixture, matching SHA-256 digests, physical absence completion, and no capture
gap/overrun. SHA-256 is also recorded independently of the wire digest.
No transmitted decisions are passed to the live receiver. Known-symbol
diagnostics use saved recordings only after the live trial.

Failed candidates remain in the evidence. `--abort-on-failure` stops further
payload generation after terminal decoding failure but plays the actual silence
tail; it never synthesizes a physical-end event. An aborted run whose requested
source size is 50 MB is **not** a 50 MB transfer.

The probe's diagnostic FIFO holds 60 seconds, whereas production Session has a
one-second FIFO. Consequently FIFO high-water marks are checked separately:
the successful later 5 MB trial peaked at 0.386 seconds, the final-preamble
100 KB 4M-QAM trial at 0.225 seconds, and the 50 MB trial at 0.304 seconds.
Failed candidates sometimes required more
than three seconds because nonconvergent LDPC exhausted its iteration budget.
Those operating points cannot sustain production reception.

## What actually limited the first attempts

The mechanisms were implemented and tested; they were not rejected based on the
old modem's throughput. Several independent defects appeared only at the new
constellation densities:

1. **Pulse truncation and interpolation.** The old eight-symbol-radius RRC
   filter was inadequate at 2% rolloff. A 320-symbol radius, precision fractional
   interpolation, and a trained fractional equalizer reduced deterministic ISI.
   Both old and new sampled failures/successes are retained.
2. **Nonrandom fixed fill.** Unwhitened bootstrap and final zeros biased the QAM
   distribution. A fixed, public whitening mask now covers the entire coded
   cycle, including interval padding. It adds no bits and provides no secrecy.
3. **Carrier tracking.** Dense decision-directed phase errors wrap within tiny
   decision cells. A short marker's noisy phase-slope estimate also repeatedly
   disturbed frequency tracking. Known pilots now provide phase/frequency
   evidence, and one bounded payload group is retrospectively phase-corrected
   between pilot anchors before forming LLRs. On the retained 4M-QAM recording,
   a worst interval's common phase error fell from about 0.00276 radians to
   less than 0.0004 radians.
4. **Likelihood calibration.** Residuals from the same samples used to fit the
   equalizer underestimated error variance. Held-out known symbols now estimate
   variance. This alone did not rescue failed recordings; the archive records
   that negative result.
5. **Unequal bit-plane exposure.** Plain column interleaving at depth four and
   20-bit QAM assigned only two frames to the weakest axis bit positions. A
   deterministic balanced rotation now distributes every frame over the actual
   QAM planes, including the 2,048-bit label reset. Across all 176 supported
   depth/order combinations, frame counts differ by at most five bits per plane.
6. **Initial clock precision.** The remaining fresh 4M failure started with an
   approximately −0.6336 ppm clock estimate. Timing error grew within sparse
   marker spans, despite nearly correct average gain and phase. Extending
   known training from 128 to 2,048 symbols changed the fresh estimate to about
   +0.00289 ppm and recovered all 20 LDPC frames of a 100 KB transfer. The added
   0.109 seconds is negligible for bulk files.

Independent known-source analysis of the successful final-preamble 4M recording
measures **0.05838% RMS EVM**, or **64.67 dB signal/error power**, and **1.359%
raw bit errors before FEC**. All recovered file bytes were nevertheless correct.
The interval diagnostic's LLR information estimate is 20.662 bits per payload
symbol, above a conservative nominal 19.366-bit mapped-rate reference. That
reference treats fixed cycle alignment fill as coded bits; the exact LDPC
information load is 19.300 bits per payload symbol. The diagnostic also includes
those fixed alignment observations, so neither number is an exact code threshold.
This residual includes noise, distortion and receiver error; it is not a
measurement of pure AWGN or Shannon capacity. Decision-directed displayed EVM
was smaller and must not be substituted for the known-source measurement.
See the [reproducible known-symbol analysis](validation-data/fast/capacity-20260920/qam-known-symbol-method.md).

## Throughput and remaining overhead

These are exact geometry estimates from `pump fast-info --estimate-bytes`, with
the 2,048-symbol preamble, four LDPC frames, marker spacing 16, amplitude 0.30,
and the same 18 kHz waveform. They include 6.25 seconds of end silence but not
device startup/queue-drain time. Estimates do not assert successful reception.

| QAM points | LDPC | 100 KB | 5 MB | 50 MB | 50 MB payload rate |
| ---: | --- | ---: | ---: | ---: | ---: |
| 4,096 | 7/9 | 12.844 s | 265.333 s | 2,575.08 s | 155.3 kbit/s |
| 16,384 | 8/9 | 11.981 s | 202.727 s | 1,952.92 s | 204.8 kbit/s |
| 65,536 | 9/10 | 11.297 s | 176.707 s | 1,692.79 s | 236.3 kbit/s |
| 1,048,576 | 9/10 | 10.398 s | 145.402 s | 1,382.79 s | 289.3 kbit/s |
| 4,194,304 | 8/9 | 10.074 s | 135.597 s | 1,287.34 s | 310.7 kbit/s |
| 4,194,304 | 9/10 | 10.074 s | 134.132 s | 1,271.19 s | 314.7 kbit/s |

The classic 256-APSK / 7/8 / high-rate-RS / depth-62 profile estimated 7,275.60
seconds for 50 MB. New dense candidates are roughly 5.3–5.7 times faster.
The [machine-readable geometry](validation-data/fast/capacity-20260920/airtime-geometry.json)
retains exact samples and cycle counts.

At 4M-QAM / 8/9 the mapper carries 22 bits/symbol, or 388.235 kbit/s. Each interval
uses 94 data symbols, including 20 mapper fill bits; four pilots; and an average
four marker symbols. Four LDPC frames occupy 127 intervals with another 896
cycle padding bits. The information region is 28,800 bytes; RS consumes 88 bytes,
the public digest 32, and the continuation flag one, leaving 28,679 source slots.
The final cycle reserves one of those slots for its delimiter, so carries at
most 28,678 source bytes; an exact multiple uses an extra empty final cycle.
Final fill, one bootstrap cycle and end silence account for the rest.
These costs, rather than an unspecified modem ceiling, explain the approximately
311 kbit/s file rate.

| Cumulative accounting at 4M-QAM / 8/9 | Remaining rate |
| --- | ---: |
| QAM mapper, 22 bits/symbol | 388.24 kbit/s |
| After per-interval mapper fill | 384.48 kbit/s |
| After pilots | 368.79 kbit/s |
| After full markers every 16 intervals | 354.33 kbit/s |
| After LDPC-cycle alignment fill | 353.10 kbit/s |
| After LDPC parity | 313.87 kbit/s |
| After outer RS parity | 312.91 kbit/s |
| After public digest and continuation flag | 312.55 kbit/s |
| 50 MB, including bootstrap/final fill and 6.25-second end silence | 310.72 kbit/s |

The first rows are mapper/coded rates, not independently recoverable file data.
This is airtime accounting; the order of rows does not describe processing order.

Outer RS parity/data is **88 / 28,712 = 0.30649%** at 8/9, and
**88 / 29,072 = 0.30270%** at 9/10. Rounding to even 16-bit parity symbols causes
the small excess over 0.3%. Classic high-rate RS used **16 / 240 = 6.6667%**;
classic robust RS used **32 / 224 = 14.2857%**. Changing high-rate RS alone to
0.3% would improve otherwise identical throughput by about 6.3%.

Replacing nine-bit source cells with raw bytes gives up to **12.5% more source
throughput** before other fixed costs. The new flag costs one byte per roughly
28–29 KB, about 0.0035%. The public digest costs about 0.112%; keyed mode adds
an IV and small alignment fill. Shortening these further offers little gain.
At 4M-QAM the full markers average 3.92% of transmitted interval symbols and
pilots another 3.92%. Less pilot evidence is a possible future gain, but this
experiment found that accurate pilot tracking is necessary at this density.

An exhaustive geometry calculation for depths 1–16 finds depth 6 marginally
fastest for an exact 50 MB public source: 1,283.610 seconds versus 1,287.337
at depth 4, a 0.29% gain. Depth 4 is the whole-file live-test setting; the deeper
cycle changes burst distribution and decoder work per callback, so arithmetic
alone is insufficient to promote it. The [depth sweep](validation-data/fast/capacity-20260920/interleave-airtime.json)
records every result. Cross-interval pilot/payload packing is another untested
proposal, estimated to recover roughly 3.4%; it would require new wire-format
and long-file validation. These are remaining opportunities, not implemented
or measured throughput gains.

## Capacity comparison and reliability limits

For an ideal 18 kHz Gaussian-noise channel, `B log2(1 + S/N)` is 239.18 kbit/s at
40 dB and 358.77 kbit/s at 60 dB. The 1M and 4M candidates' calculated bulk rates
are about **81% and 87% of the 60 dB example**, respectively. Uniform square
QAM, finite-length coding, excess bandwidth, synchronization, padding and
integrity retain a gap to that ideal. A result above the 40 dB example is not a
violation: the measured cable under these conditions has substantially better
signal/error performance. Best-case near-full-scale tone SNR cannot simply be
inserted as the modem's effective noise margin.

The [production LDPC AWGN benchmark](validation-data/fast/capacity-20260920/ldpc-awgn-method.md)
retains 1,560 frames over 104 invocations, including failures. On its tested grid,
4M-QAM / 8/9 first passed 64/64 independent frames at 62 dB Es/N0; 9/10 did so
at 63 dB. Both failed 8/8 one dB below those respective points. Choosing 9/10
instead of 8/9 gives only about 1.25% additional code-rate throughput while
spending roughly one dB in that model. The AWGN experiment is distinct from
the live cable and does not certify file success.

Sparse RS corrects only small residual errors: 8/9 and 9/10 at depth four can
repair at most 22 unknown 16-bit symbol errors. It cannot rescue a lost LDPC
frame. A missed acquisition, clock excursion, clipping burst or sample gap can
still lose an entire file. There is no retransmission or automatic rate fallback.
The observed stability of one run does not prove an 80% 50 MB success rate.
With independent whole-file trials, 14 successes out of 14 would put the one-sided
95% binomial lower bound above 80%; the authorized 30-minute live budget cannot
support that demonstration. Projecting from shorter zero-failure trials also
requires stationary, independent error assumptions that these experiments do
not establish.

The final short live margin sweep held the device mixer and all other geometry
fixed, changing only the LDPC rate and waveform amplitude:

| Amplitude | Level relative to default | 100 KB, 8/9 | 100 KB, 9/10 |
| ---: | ---: | --- | --- |
| 0.30 | 0 dB | Pass; also the 50 MB pass | Pass |
| 0.27 | −0.92 dB | Pass | Pass |
| 0.24 | −1.94 dB | Fail | Fail |

Each short point is one independent transmission, not a probability estimate.
The 0.24 / 8/9 trial failed in the bootstrap (one of four LDPC frames failed);
0.24 / 9/10 decoded initial cycles before failing four frames. Neither produced
a completed file, and both failures were retained. Thus the live evidence places
a practical operating transition between those tested levels for these runs,
without identifying a universal SNR threshold or proving that 8/9 wins every
individual realization. The small 9/10 throughput gain remains available via
local settings; the default uses the whole-file-tested 8/9 setting. For a less
stable cable, 1M-QAM / 9/10 trades about 7% nominal throughput for substantially
more margin in the independent AWGN tests.

[Known-source analysis of the margin recordings](validation-data/fast/capacity-20260920/qam-margin-method.md)
measured roughly 63.7–63.9 dB signal/error power at the passing 0.27 level and
about 63.0 dB at failing 0.24. Timing and mean phase remained controlled, and
amplitude times true EVM stayed nearly constant, consistent with a fixed residual
noise floor. Some failing cycles' likelihood variance estimates were 1.7–2.0 dB
below their actual data error; that is a plausible additional near-threshold
limitation, not a conclusively isolated sole cause. The live max-log demapper and
non-Gaussian residuals also differ from the ideal benchmark's exact PAM
likelihoods. Further likelihood calibration is a practical candidate for future
recorded-replay testing; no untested gain is included in the default's results.

Total diagnostic live wall time, including failed and aborted trials and their
real silence tails, was **1,796.165 seconds (29 minutes 56.165 seconds)**. No
long-file success is inferred from an aborted requested-size trial.

Markers provide provisional synchronization; acceptance of any source area also
requires its fixed-cycle SHA-256/HMAC and canonical structure. The scoped
[accidental-integrity bound](fast-capacity-integrity.md) is much stronger than
2^-80 under its stated ideal-verification model. No claim is made that the raw
analog sync detector itself has a measured 2^-80 false-lock rate. Public digests
do not authenticate an adversarial sender. Physical completion still requires
six seconds of observed absence; a valid final flag cannot end reception early.

## Reproduction and evidence

Build `fast_cable_probe` and `fast_ldpc_benchmark` with CMake. A final-preamble
100 KB cable trial uses:

```sh
build/fast_cable_probe --capacity --mode codec --bytes 100000 \
  --qam 4194304 --code-rate 8/9 --depth 4 --marker-spacing 16 \
  --stereo --abort-on-failure --require-success
```

The 50 MB trial changes `--bytes` to `50000000`. Add `--capture-save PATH.f32`
and `--tx-bits-save PATH.bits` for small diagnostic recordings. The latter holds
literal uint8 values 0 or 1, not packed bytes. `--replay` reads a saved mono
float32 capture with the matching profile, fixture and wire revision. Replay is
an offline diagnostic, not another independent live trial.

[Archived run records](validation-data/fast/capacity-20260920/live-run-index.json)
include failures, raw-format capture hashes and software-development stages.
Early captures used the old frame mapping and/or 128-symbol preamble; they must
not be decoded as if they used the final format. The known-symbol archive includes
frozen diagnostic code and matching old DSP where needed. No microphone content
outside the explicitly authorized loopback tests is part of the fixtures.
