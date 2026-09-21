# Fast cable transmit-level calibration, 2026-09-21

Retain **0.30 generator amplitude** for the capacity audio-cable and IC-7100
audio profiles. The connected headphone-to-microphone cable supports this
level without an additional bandwidth-dependent reduction. This is a modem
waveform scale, not a recommendation to set the operating-system volume to
30%. No runtime default or mixer setting was changed by this study.

## Setup and measurement

The user connected the **left headphone output to the microphone input**.
Tests used the default physical ALC257/PipeWire devices, the production S16
audio API at 48 kHz, and identical mono PCM on both output channels. The probe
required `--stereo` because its default output is right-only. Capture was from
the physical input, not a playback-monitor source.

Output volume was 95%; hardware Capture and Mic Boost were both 0 dB.
The desktop reported microphone volume as 10%, illustrating why percentages
must not be transferred between devices. The full mixer snapshot is archived.
These are the settings at which the recommendation was measured, not a claim
that every DAC/ADC pair has the same calibration.

There were 26 live modem trials and eight steady sine levels, totaling
656.4 seconds of capture. The cable trials used the current 4,194,304-QAM,
LDPC 8/9, depth-four capacity default, with a nominal 18 kHz band. Narrower
raw-symbol trials retained that constellation and coding configuration while
reducing symbol rate by 10 and 100. The radio waveform used 64-QAM in 2.4 kHz,
and 16-QAM in 240 Hz. Every trial acquired and observed physical completion;
none reported an audio-device error or diagnostic FIFO overflow.

Known transmitted bits and observed payload I/Q were saved separately. The
offline analysis reconstructs each Gray-QAM symbol, including fixed-interval
label padding, instead of treating the nearest receiver decision as truth.
The reported **signal/error ratio includes noise, distortion and modem DSP
error**; it is not a calibrated measurement of analog AWGN SNR. The independent
bit-error calculation agrees with the probe in every raw-symbol trial.

## Cable level selection

Each short-file row is one 100,000-byte live transfer, with the same source
fixture. Wire bits can differ because each codec transfer has a fresh random
salt. Each result therefore uses its own saved transmitted reference.

| Generator amplitude | Exact 100 KB file | Known signal/error, dB | Generated PCM peak |
| ---: | --- | ---: | ---: |
| 0.10 | No | 55.93 | 0.250 |
| 0.15 | No | 59.37 | 0.361 |
| 0.20 | No | 61.68 | 0.489 |
| 0.25 | Yes | 63.32 | 0.620 |
| **0.30** | **Yes** | **64.59** | **0.755** |
| 0.35 | Yes | 65.45 | 0.873 |
| 0.40 | Yes | 66.14 | 1.000 (rounded) |

The three lowest levels failed bootstrap decoding at this very dense default
constellation. Raising drive improved most symbol decisions, but longer files
exposed output peaks absent from the short fixtures:

| Generator amplitude | Exact 1,000,000-byte file | Known signal/error, dB | Generated PCM peak | Peak above digital full scale |
| ---: | --- | ---: | ---: | --- |
| **0.30** | **Yes** | **64.50** | **0.8594** | **No** |
| 0.35 | Yes | 65.41 | 1.0213 | Yes |
| 0.40 | Yes | 63.35 | 1.1365 | Yes |

These longer tests ran in reverse level order. Each received all 4,572 intervals
and all 144 LDPC frames, with no failed frame and matching source SHA-256.
The 0.30 run had about **1.32 dB peak headroom**. Its waveform lasted 26.579
seconds, plus seven seconds of trailing observation. This is an observed
headroom for that fixture, not a peak bound or 50 MB reliability guarantee.

The audio API clamps generated PCM outside [-1, 1] before S16 conversion.
Thus the peaks at 0.35 and 0.40 demonstrate digital overload even though FEC
recovered these particular files. At 0.40, a small population of large errors
also made the total error power worse despite a lower raw bit-error count.
The probe's historical `clipped` counter actually counts samples at or above
0.999 magnitude: a near-full-scale count alone does not prove clipping.
No captured PCM sample approached full scale; the maximum over all modem
captures was 0.237. These samples are downstream of audio routing and gain,
so this alone cannot exclude upstream ADC/preamp overload. Checking only the
microphone meter also misses the demonstrated digital output overload.

Keeping 0.30 balances drive and waveform headroom. There is no nominal bit-rate gain
from increasing amplitude by itself, and these tests do not justify trading
that headroom for 0.35 or 0.40 as a bulk-file default.

## Narrowing bandwidth and separating hardware from DSP

Single-carrier modulation already has nominal PCM RMS `amplitude / sqrt(2)`.
At 0.30 this is 0.2121, or -13.47 dBFS relative to unit RMS. A settled
production-waveform control measured 0.21392, 0.21391 and 0.21391 RMS at 18 kHz,
1.8 kHz and 180 Hz: less than 0.001 dB difference across 100-fold narrowing.
The modest offset from nominal is finite-fixture symbol energy. The radio
control was within 0.03 dB across 2.4 kHz and 240 Hz.

Consequently, narrowing concentrates the existing power into a smaller band;
it does not require increasing amplitude. Reducing amplitude with bandwidth
would surrender part of the intended narrowband sensitivity gain. The earlier
acoustic correction addressed a different issue: switching from OFDM to single
carrier had changed software normalization by about 10 dB. Cable and radio
already use single carrier on both sides of the bandwidth change.

At 0.30, the live cable signal/error ratios were 64.59, 70.99 and 70.35 dB for
18 kHz, 1.8 kHz and 180 Hz. The latter plateau does not establish an analog
noise or compression floor:

| Configured band | Noiseless float TX-to-RX signal/error, dB | Live at 0.30, dB |
| ---: | ---: | ---: |
| 18 kHz | 72.75 | 64.59 |
| 1.8 kHz | 72.54 | 70.99 |
| 180 Hz | 70.54 | 70.35 |

The mid/narrow controls used byte-identical transmitted raw bits. Their live
and noiseless error patterns had squared coherence 0.713 and 0.971 respectively:
the narrow residual is predominantly intrinsic modem DSP error. The wide
control used a shorter, different fixture and supports a less direct comparison.
These are diagnostic comparisons, not a calibrated decomposition of all noise
sources. At 0.40, both narrow cable fixtures also exceeded digital full scale
and had worse error ratios than at 0.30.

Separately, a 997 Hz sine swept eight peak levels from 0.03 through 0.90.
The fitted captured/generated PCM amplitude ratio varied by only **0.00666 dB** across that
30-fold range. SINAD in 300–18,300 Hz improved from 47.63 to 76.79 dB;
no full-scale overload occurred. This supports linear operation of this
connected hardware path at that frequency and mixer setting. The largest
sine is not an appropriate modem amplitude: pulse-shaped QAM has much larger
peaks relative to RMS.

One separate implementation limit was exposed: the manually selected 1.8 kHz
cable geometry consumed PCM more slowly than real time and accumulated up to
4.64 seconds in the diagnostic's 60-second queue. The application's queue is
one second. Those raw trials remain valid offline amplitude measurements but
**do not qualify that manual setting for real-time reception**. The successful
nominal-band file trials stayed below 0.24 seconds of queued audio. Resolving
the manual mid-rate CPU cost would be a DSP optimization, not level calibration.

## IC-7100 applicability and evidence limits

The radio-profile audio traversed the same physical cable, not an IC-7100.
All four tested amplitudes recovered every raw bit at both 2.4 kHz and 240 Hz;
the 2.4 kHz fixture at 0.40 generated a peak of 1.030. Retain 0.30 and the same
constant-RMS narrowing policy for these profiles.

That result does not calibrate a radio's USB/ACC modulation gain, receive-audio
gain, ALC or FM deviation. Those require a test through the actual radio chain;
the [radio profile documentation](fast-radio-capacity.md) distinguishes the
controls. No RF transmission was made in this study.

No 50 MB file-success probability, universal device calibration or optimal
level to arbitrary precision is claimed. The deliverables are a measured
operating point, evidence against increasing the default, and a reusable
known-source analysis tool. Runtime behavior, GUI controls and wire formats
are unchanged. [Complete tables, commands, tests and hashes](validation-data/fast/cable-level-calibration-20260921/README.md)
are retained with the study.
