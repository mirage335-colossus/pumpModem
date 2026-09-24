# Fast cable throughput: live study, 2026-09-20

The connected headphone-to-microphone cable successfully transferred all six
100 KB screening files, including 256-APSK, rate-7/8 convolutional coding and
high-rate Reed–Solomon. Fourteen fresh 100 KB files at the final amplitude also
passed, and one 5 MB file at the earlier amplitude arrived exactly. The faster waveform experiments increased raw decision
errors and required RS correction, but still delivered their three 100 KB files
exactly. **No whole-file failure transition was observed within the both-channel
modulation/coding sweep.** A separate right-channel-only routing check failed;
the revised both-channel cable default then passed without a routing override.
These measurements
support a faster provisional cable default; they do not establish an 80% success
probability for 50 MB or a global throughput optimum.

The selected existing-format setting is **256-APSK, convolutional rate 7/8,
high-rate RS, interleave depth 62**, retaining 15,000 symbols/s, 9,300 Hz carrier,
20% rolloff and the existing markers/pilots. The cable-only transmit amplitude
is reduced from 0.5 to **0.35** to provide generated-PCM headroom; other channel
profiles retain their amplitudes. Cable output now defaults to **both channels**;
the other Fast channel profiles retain right-only output. Its calculated 50 MB airtime is
**2 h 1 min 15.6 s**, versus 4 h 46 min 35.1 s for the previous default. That is
a 2.36-fold increase in source rate, conditional on successful delivery. It is
not a measured 50 MB transfer time.

This report records a bounded, approximately 30-minute live investigation.
Sizes are decimal: **100 KB = 100,000 bytes; 5 MB = 5,000,000 bytes;
50 MB = 50,000,000 bytes**. The [evidence inventory](validation-data/fast/cable-live-20260920/README.md)
links trial JSON, logs, models and reproduction commands. The
[Fast format](fast-mode.md) and [development contract](development.md) continue
to define runtime behavior.

## Physical setup and level correction

The user's existing cable joins the default analog headphone output and
microphone input. Device inspection identified the ALC257 analog audio path;
the production `default` device selection and its existing ALSA fallback were
used. Initial `dmix`/`dsnoop` open warnings are preserved in logs: subsequent
fallback succeeded, with both CLI processes returning zero on successful trials.
No virtual-loopback result is substituted for these live transfers.
The screening and long/repeated file trials send the same signal on both
output channels (`--stereo`). The former right-only routing and the revised
both-channel cable default were checked separately; each runner's plan and
commands preserve its routing evidence. CLI `--mono` explicitly selects
right-only output, and `--stereo` explicitly selects both channels.

The initial microphone path had approximately **+60 dB gain** and severe
clipping: initial commissioning checks observed approximately **88–96%** of
samples at full scale. Those initial checks were observed during the session
but their raw capture was not archived; they are not part of the success-rate
denominator. Reducing the PulseAudio source control to **10%** removed that boost;
the resulting hardware Capture and Digital controls were **0 dB**, with Mic
Boost also 0 dB. Playback was set to **95%**. Pulse percentages and ALSA mixer
percentages have different scales; the saved [mixer state](validation-data/fast/cable-live-20260920/mixer-calibrated.txt)
shows the actual hardware dB values. These settings describe this host, not a
universal microphone percentage.

After correction, raw probes measured about −22.7 dBFS signal RMS and
−87.7 to −89.7 dBFS pre-transmit noise. The recorded fullband signal/noise
variance ratio was approximately **65–67 dB**. This is an uncalibrated level
measurement over separate time windows, not demodulator SNR, SINAD or a promise
of error-free decisions. It excludes waveform-dependent distortion from the
noise-only reference.

ADC capture had no full-scale samples in the original amplitude-0.5 raw/codec probes, with
peaks around 0.22–0.25. There is a separate transmitter headroom limitation:
the amplitude-0.5 generated waveform occasionally exceeded ±1 before conversion
to S16. For example, the raw baseline peak was 1.0915, with 16 near/full-scale
samples; the live audio conversion clamps excursions beyond ±1. Playback at
95% occurs too late to guarantee against this clipping. Offline controls pass
floating-point PCM directly and therefore do not reproduce that clamp. The
observed error increase cannot be attributed solely to analog noise or baud rate.

The selected amplitude 0.35 addresses this issue separately from microphone
gain. The [peak-bound calculation](validation-data/fast/cable-live-20260920/tx-peak-bound.py)
uses `peak <= amplitude × max_symbol_radius × sum(abs(RRC pulses))` and covers
every fractional phase of the production interpolated pulse table. At 256-APSK
and rolloff 0.20, the factors are 1.32853747 and 1.92446484, giving a generated
peak bound of **0.8948533** at amplitude 0.35, versus 1.2783618 at 0.5.
This bound precedes optional resampling and external gain; it is not an analog
clipping certificate. Lower amplitude costs 3.10 dB of signal power at unchanged
gain. Subsequent amplitude-0.35 raw captures measured fullband variance ratios
of **64.73 dB at logical 48 kHz** and **64.64 dB at logical 44.1 kHz**; those
measurements, with their separate noise windows, are not extrapolated from 0.5.
The 0.10-rolloff experiments need separate headroom: at amplitude 0.35 their
bound is still 1.0742, whereas 0.30 bounds it at 0.9208.

Live raw probes negotiated 48,000 Hz hardware in both directions. The 44,100 Hz
logical-rate case exercised production conversion to/from that hardware rate.
It did not test two independent physical clocks, a 44,800 Hz device, or a large
long-term clock mismatch. A same-card DAC/ADC loopback can share a clock.

## Screening the existing production settings

Each row below is one independently saved and SHA-256-compared 100,000-byte
file, using the same deterministic source fixture and public SHA-256 integrity
mode, with the then-current amplitude **0.5**. All six reached the receiver's observed physical end and matched the
original file exactly. None reported RS-corrected or erased bytes.

| APSK | Convolutional rate | RS | Depth | Estimated playback, s | Observed delivery, s | Exact files |
| ---: | :---: | :--- | ---: | ---: | ---: | :---: |
| 16 | 3/4 | robust | 16 | 41.929 | 41.794 | 1/1 |
| 64 | 3/4 | robust | 16 | 31.340 | 31.175 | 1/1 |
| 256 | 3/4 | robust | 16 | 25.878 | 25.718 | 1/1 |
| 256 | 7/8 | robust | 16 | 23.202 | 23.062 | 1/1 |
| 256 | 7/8 | high-rate | 16 | 21.865 | 21.706 | 1/1 |
| 256 | 7/8 | high-rate | 62 | 22.921 | 22.756 | 1/1 |

Observed delivery starts at transmitter process launch and ends when the
receiver process exits. It excludes the 1.5-second receiver startup allowance.
The receiver can finish before the transmitter drains its full 6.25 seconds
of silence because physical completion requires six seconds of scored absence.
Complete elapsed times, including startup, are retained separately in the
[screening records](validation-data/fast/cable-live-20260920/screen-100k/trials.jsonl).
Small timing differences are not evidence that the estimator omitted framing.

These successes do not mean all profiles have identical reliability. One
success at a selected operating point gives only a 5% one-sided 95% lower
confidence bound on its whole-file success probability under independent,
stationary trials. Pooling different settings would misrepresent that evidence.

The separate [5 MB trial](validation-data/fast/cable-live-20260920/long-5mb/trials.jsonl)
delivered **5,000,000 exact bytes in 735.879 seconds**, measured from transmitter
launch to completed receiver process, or **54,356.78 bit/s**. Source and received
SHA-256 matched. It reported seven corrected bytes and zero erased bytes, with
all 31,098 physical intervals received and physical completion observed. This
run used the selected coding/waveform settings but **amplitude 0.5** and both
output channels. Its single success gives a 5% one-sided 95% lower confidence
bound under the binomial assumptions; it does not validate the final amplitude.

The held-out [100 KB validation set](validation-data/fast/cable-live-20260920/repeat-100k/summary.json)
used the final **amplitude 0.35**, the selected settings and both output channels.
All **14 of 14** fresh-seed files matched SHA-256 and completed physically,
with **zero corrected or erased bytes**. Delivery times ranged from 22.749 to
22.806 seconds, with a mean of 22.771 seconds. Under independent, stationary
whole-file Bernoulli outcomes, 14/14 gives a one-sided 95% success lower bound
of **80.7364% for 100 KB**. The fourteen distinct source hashes and complete
records are retained. These runs are consecutive observations on this host;
independence and future channel stability are assumptions, not measured facts.
The 0.5-amplitude screening and 5 MB results are not pooled into this bound.
**No 50 MB live file was tested**, and no 5 MB trial at the final amplitude
was completed in this investigation.

The separate [right-channel-only check](validation-data/fast/cable-live-20260920/default-route-100k/trials.jsonl)
at amplitude 0.35 **failed its 100 KB transfer** with `Too many Reed-Solomon
erasures`. It received 710 physical intervals and observed physical completion,
but produced no verified file; final EVM was 6.92%. The runner then cancelled
the remaining transmitter tail. This outcome is retained as a routing-specific
failure and is not pooled into the fourteen both-channel successes. It prevents
treating the both-channel result as qualification of the former right-only
application routing; its cause was not isolated by this single check.

After making both-channel output the cable default, a separate
[final default-route check](validation-data/fast/cable-live-20260920/final-default-100k/trials.jsonl)
ran with **neither `--mono` nor `--stereo`**. It delivered 100,000 exact bytes
in **22.796 seconds**, with zero corrected or erased bytes, all 710 intervals,
observed physical completion and zero process errors. Its saved `fast-info`
reports amplitude 0.35 and `mono:false`. This verifies the new effective route;
the earlier fourteen repetitions used the same both-channel routing explicitly.
Keep the final check separate from the prespecified 14-trial statistical set.

## Where increased throughput starts consuming error margin

The diagnostic [cable probe](../tools/fast_cable_probe.cpp) bypassed FEC in raw
mode at amplitude **0.5** and compared **1,048,576 original-position payload bits per point**. All
five live probes retained all 512 interval identities and observed physical
completion. A separate alignment diagnostic found offset zero throughout; it
never changes the strict comparison.

| Symbols/s | Rolloff | Carrier, Hz | Logical rate, Hz | Live wrong / erased bits | Matched offline wrong / erased bits |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 15,000 | 0.20 | 9,300 | 48,000 | 1 / 0 | 0 / 0 |
| 16,500 | 0.10 | 9,400 | 48,000 | 35 / 0 | 5 / 0 |
| 18,000 | 0.10 | 10,200 | 48,000 | 73 / 0 | 10 / 0 |
| 19,200 | 0.10 | 10,800 | 48,000 | 768 / 40 | 38 / 8 |
| 15,000 | 0.20 | 9,300 | 44,100 | 1 / 0 | 0 / 0 |

The live wrong-bit fractions were respectively 0.0000954%, 0.00334%,
0.00696%, 0.07324%, and 0.0000954%; erasures are additional. These are
pre-FEC decisions from short deterministic probes, not independent residual
errors after coding or probabilities of delivering 50 MB. Rolloff, carrier and
symbol rate changed together, so the sweep identifies an observed degradation
region, not a separately isolated baud-rate threshold.

The matched noiseless controls already show errors at the tighter rolloff.
Thus increased waveform/DSP error is present before adding the physical path.
Live mean polled EVM rises from approximately 1.02% at the baseline to 2.35%,
2.40% and 2.72% at the faster points. EVM is a receiver diagnostic, not a
calibrated SNR estimate. See the [offline method](validation-data/fast/cable-live-20260920/offline-probe-method.md)
for sample alignment, the additional twelve-point exploration and its limits.

Three final headroom probes repeated selected settings with lower amplitudes,
again comparing 1,048,576 bits per point through the real cable and both output
channels:

| Symbols/s | Rolloff | Logical rate, Hz | Amplitude | Wrong / erased / missing bits | Generated peak | Measured fullband ratio, dB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 15,000 | 0.20 | 48,000 | 0.35 | 0 / 0 / 0 | 0.7640 | 64.733 |
| 15,000 | 0.20 | 44,100 | 0.35 | 0 / 0 / 0 | 0.7348 | 64.637 |
| 19,200 | 0.10 | 48,000 | 0.30 | 726 / 40 / 0 | 0.6776 | 64.598 |

All three acquired, retained every interval at offset zero and observed
physical completion, with zero generated near/full-scale samples, zero captured
clipping, and no reported FIFO overflow or capture/playback exception. The
baseline raw tests were exact at both logical rates. The tighter 19,200-symbol/s
waveform still had substantial errors with headroom, so the earlier clipping
does not account for all of its loss of margin. These three finite probes do
not measure long-file reliability. Full records are
[48 kHz baseline](validation-data/fast/cable-live-20260920/raw-headroom.json),
[44.1 kHz baseline](validation-data/fast/cable-live-20260920/raw-headroom44100.json)
and [19,200-symbol/s headroom](validation-data/fast/cable-live-20260920/raw-tight19200-headroom.json).

Three further **coded 100 KB live trials**, all at 256-APSK, 7/8, high-rate RS,
depth 62 and amplitude **0.5**, used those faster symbol rates. All three produced exact files:

| Symbols/s | Signal-only duration, s | Corrected bytes | Erased bytes | Exact files | Projected 50 MB playback, s |
| ---: | ---: | ---: | ---: | :---: | ---: |
| 16,500 | 15.155 | 15 | 13 | 1/1 | 6,614.749 |
| 18,000 | 13.892 | 12 | 12 | 1/1 | 6,064.041 |
| 19,200 | 13.024 | 74 | 38 | 1/1 | 5,685.429 |

The final column is **calculated, not measured**: production
`estimate_transmission` at the selected public 256-APSK/7/8/high-rate-RS/depth-62
geometry, with the candidate carrier/rolloff settings above and logical 48 kHz.
It includes bootstrap/final fill, markers, pilots, pulse tail and 6.25 seconds of
end silence; startup, queues, failed attempts and retries are excluded. The
15,000-symbol/s baseline is 7,275.599 s. The standalone
[estimator source](validation-data/fast/cable-live-20260920/waveform-airtime.cpp),
[CSV](validation-data/fast/cable-live-20260920/waveform-airtime.csv) and
[JSON](validation-data/fast/cable-live-20260920/waveform-airtime.json) also retain
the 100 KB and 5 MB projections for all four waveforms. No audio was generated
by this calculation.

These diagnostic durations exclude their eight-second transmitted silence and
startup, unlike the production playback estimates above. Erased and corrected
byte telemetry must not be summed as distinct independent error events.
No capture FIFO overflow or capture/playback exception was reported. Occasional
DSP chunks exceeded 50 ms; buffering was required. ALSA recovery counts are not
exposed, so an empty exception string does not certify that the driver never
recovered an xrun.

The evidence therefore locates **increasing raw errors and use of correction
between the 15,000/0.20 baseline and the 16,500–19,200/0.10 candidates**. It
does not locate where complete-file success falls below 80%. The provisional
default retains the baseline waveform while taking the measured constellation
and code-rate gains. The faster waveforms remain candidates for longer tests.
Their shorter projected airtimes do not establish better successful throughput:
raw errors already increase in offline controls, and long-file reliability at
levels with adequate transmit headroom remains unmeasured.

## Exact airtime and the depth-62 choice

The [production estimator sweep](validation-data/fast/cable-live-20260920/airtime.csv)
covers all depths 1–64, existing rates, RS choices and 16/64/256-APSK. These
figures include source cells, integrity, coding, bootstrap/final fill, markers,
pilots, pulse tail and 6.25 seconds of end silence. They exclude device startup,
queues, retries and failures.

| Source size | Previous public default, s | Selected public default, s | Selected encrypted setting, s |
| --- | ---: | ---: | ---: |
| 100 KB | 41.929 | 22.921 | 24.587 |
| 5 MB | 1,726.836 | 736.026 | 796.007 |
| 50 MB | 17,195.124 | 7,275.599 | 7,882.072 |

The previous setting is 16-APSK/3/4/robust/depth 16. The selected public
50 MB source rate is 54.978 kbit/s, versus 23.262 kbit/s previously; airtime
falls 57.69%. Encryption airtimes are calculated only: this live study used
public integrity and did not qualify encrypted transfers.

Depth 62 packs 62 outer groups into 71 physical intervals per coding cycle;
depth 64 needs 74 intervals. Depth 62 minimizes 50 MB airtime among existing
settings in this exhaustive estimator sweep, including both integrity modes.
For the selected public code, depth 6 is fastest at 100 KB (21.372 s), while
depth 55 is fastest at 5 MB (735.111 s). One large-file default accepts those
small finite-size costs. A 60-byte public message at depth 62 is estimated at
9.592 s. None of these estimates establishes burst-error performance.

## What would establish the requested 50 MB reliability

For independent attempts with duration T and success probability P, expected
time to success is T/P before retry turnaround. Faster settings improve goodput
when `P_new/P_old > T_new/T_old`; the separate 80% minimum still applies.
For 50 MB, the break-even relative success ratios are 0.5502 for changing
16 to 256-APSK, 0.8637 for 3/4 to 7/8, 0.9232 for robust to high-rate RS,
and 0.9645 for depth 16 to 62, with the other settings held at each step.
They are decision thresholds, not measured success rates.

At the selected public setting, an illustrative independent coding-cycle
model gives:

| File size | Cycles including bootstrap | Maximum residual cycle failure probability for 80% file success |
| --- | ---: | ---: |
| 100 KB | 10 | 2.20672% |
| 5 MB | 438 | 0.0509330% |
| 50 MB | 4,363 | 0.00511432% |

The calculation is `(1-q)^cycles >= 0.8`. Correlated errors, loss of tracking,
interruptions and device faults violate its assumptions. Raw BER and average
RS correction counts cannot supply q. Successful short files also cannot be
treated as successful pieces of one untested two-hour reception.

Fourteen successful independent **50 MB** validation files at one preselected
setting would give a one-sided 95% lower success bound of **80.7364%**.
Thirteen successes give only 79.4183%. Fourteen such attempts require over
28 hours of airtime at this setting, before startup or failures. The observed
fourteen 100 KB successes establish that conditional bound only for their
100 KB, both-channel configuration. A bounded
30-minute investigation cannot supply this complete-file qualification.

## Why LDPC, 0.3% RS and shorter markers were not adopted

The previous [coding study](fast-coding-study.md) measured LDPC on ideal symbol
AWGN, not this live sampled-PCM channel. A replacement needs integration with
the actual demapper, integrity/layout, correction-failure handling and bounded
live buffering, followed by long-file measurements. No live result here ranks
an LDPC implementation against the existing convolutional decoder.

The current high-rate outer code uses two RS(128,120) words per group: 6.25%
of coded bytes are parity, equivalent to 6.667% parity/data. Robust RS uses
RS(128,112): 12.5% of coded bytes, or 14.286% parity/data. A roughly 0.3%
outer budget cannot be expressed by merely lowering this parity count. Even
one parity byte per 128-byte word costs 0.7874% parity/data, and correcting an
unknown byte needs two parity symbols. Sparse long-span erasure protection
requires another layout and a measured failed-inner-word distribution.

For illustration, the existing analytical LDPC/shard model assigns 24 repair
words to 7,947 data words for 50 MB: 0.3020% parity/data. It gives 80% success
only below approximately 0.2601% independent residual word failure. At 0.3%
it predicts only 56.1% file success. These are model outputs, not measured
LDPC/cable results. Full assumptions and exact calculations are retained in the
[reliability model](validation-data/fast/cable-live-20260920/reliability-model.md).

The requested accidental-marker probability must apply to the complete search
budget, not merely the marker's nominal bit count. For the current tolerant
coherence detector, a simplified independent uniform-QPSK calculation gives
about 80.735 bits of evidence per 64-symbol candidate window. It does not
give 128 bits, and a union bound over 2^30 such windows leaves only 50.735 bits.
Actual shaped APSK windows and timing hypotheses need their own bound. The
current format is therefore not newly certified to meet a file-wide 2^-80
false-marker requirement by this study, and shortening the marker would be
unsupported. Arbitrary data can also contain any fixed public pattern unless
its distribution or encoding is constrained.

Sending the existing marker once per four intervals has a calculated 15.79%
steady-rate opportunity at 256-APSK. Its tracking, erasure positions,
reacquisition and aggregate false-match behavior were not live tested here.
Markers, pilots, fixed framing, source interpretation and physical-completion
rules remain unchanged; the regular modem's separate short-message and pending
reception contract is unaffected.
