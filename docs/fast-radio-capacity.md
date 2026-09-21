# Fast IC-7100 capacity profiles

The Fast `ssb` and `fm` defaults now use the capacity codec already used by the
audio cable and speaker/microphone profiles. Ordinary modem transport and the
explicit `classic` Fast profiles are unchanged. These are audio waveforms for
the radio's modulation and demodulated-audio interfaces; selecting a profile
does not configure the radio or operate PTT.

## Default geometry

Both radio profiles use these local settings:

| Setting | Value |
| --- | --- |
| Audio passband | 300–2700 Hz, 2400 Hz wide |
| Modulation | Cartesian Gray 64-QAM |
| Inner code | LDPC 3/4, 64,800 coded bits per frame |
| Frames per coding cycle | 4 |
| Symbol rate | 2400 / 1.10 = 2181.818… symbols/s |
| Carrier | 1500 Hz |
| RRC rolloff | 0.10 |
| Output amplitude | 0.30 |
| Full markers | Every four 2048-bit physical intervals |
| Tracking pilots | Four known QPSK symbols per 64 payload symbols |
| Source coding | Eight bits per source byte, one protected cycle flag |
| Outer RS | 76 parity bytes / 24,224 data bytes = 0.3137% |

The lower output amplitude leaves more pulse-shaping headroom than the previous
0.50 radio default. It does not establish a safe radio input level: radio USB
modulation gain, speech compression, ALC, and FM deviation remain properties of
the external audio/RF chain. The two profiles retain distinct integrity domains
even though their waveform settings are the same.

The 300–2700 Hz passband corresponds to Icom's documented MID SSB transmit
filter. The manual also lists WIDE as 100–2900 Hz and NAR as 500–2500 Hz.
The chosen waveform therefore needs a matching or wider receive/audio filter;
the narrow setting clips it. These are documented filter settings, not a
measurement of a connected IC-7100. [Icom full manual, section 6-6](https://www.icomjapan.com/support/manual/2288/)

The FM preset deliberately uses the same audio band. Icom's published 12 kHz
FM selectivity refers to RF/IF reception and does not establish a flat 12 kHz
modulation-audio path. USB and other audio paths, FM pre/de-emphasis and deviation
must be measured before widening this preset. [Icom specifications](https://www.icomjapan.com/lineup/products/IC-7100USA/)

## Throughput

The table uses the production estimator for a 50,000,000-byte source and includes
bootstrap, final fill, markers, pilots, coding and physical-end silence.

| Profile | Previous public / keyed bit/s | New public / keyed bit/s | Speedup public / keyed |
| --- | ---: | ---: | ---: |
| IC-7100 SSB | 3102.63 / 2844.05 | 8697.46 / 8689.06 | 2.803× / 3.055× |
| IC-7100 FM | 1633.00 / 1496.90 | 8697.46 / 8689.06 | 5.326× / 5.805× |

These are transport airtime estimates, not measured RF throughput. Gross
constellation throughput is 13,090.9 bit/s. At a hypothetical flat 20 dB SNR,
the 2400 Hz Gaussian channel limit is about 15,980 bit/s; the default public
payload estimate is about 54% of that upper bound. The default reserves some
practical decoding headroom and is not claimed to reach capacity. Narrower
rolloff, denser QAM, stronger tracking and rate selection can improve that
fraction, subject to actual radio filtering and fading.

## Sampled tests and limits

No radio, speaker, microphone or audio device was used for these tests. The
tests generated and demodulated the production **48,000 samples/s real PCM**
waveform with a deterministic 751-byte keyed source, including leading and
trailing zero bytes. To bound test runtime, sampled trials use one LDPC frame
per cycle instead of the four-frame default: one bootstrap cycle plus one source
cycle, 64 fixed physical intervals, and two LDPC frames total. Separate tests
exercise the exact default four-frame geometry and source codec for both public
and keyed operation in both radio profiles.

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
| SSB and FM defaults, 20 dB | Both recovered exact bytes; no failed LDPC frames |
| SSB default, 20 dB, +3 Hz carrier offset, 15% static echo delayed 0.5 ms | Exact bytes; no failed LDPC frames |
| 64-QAM, LDPC 3/4, 18 dB | Exact bytes; no failed LDPC frames |
| 64-QAM, LDPC 3/4, 17 dB | Bootstrap LDPC/integrity failure; no file |
| 64-QAM, LDPC 8/9, 20 dB | Exact bytes; no failed LDPC frames |
| 256-QAM, LDPC 2/3, 20 dB | Uncorrectable coding cycle; no file |
| 256-QAM, LDPC 2/3, 23 dB | Exact bytes; no failed LDPC frames |

Each row is one deterministic fixture, except the first row which tests both
profile domains. It is not a packet-error-rate sweep or a statistical reliability
claim. The 18 dB success supports retaining 64-QAM 3/4 as a nominal 20 dB setting
with some headroom. It does not certify 2 dB of margin on a real radio. The
256-QAM failure also shows why ideal decoder-only thresholds are insufficient
for selecting the production default. The faster 64-QAM 8/9 result remains an
option for better measured conditions rather than the default.

The production regression additionally checks exact source bytes, opacity before
physical completion, passband geometry, approximately 0.3% RS, public/keyed
integrity contexts, unchanged classic profiles and the bulk-airtime improvement.
Source interpretation still waits for six seconds of observed absence. EOF does
not finish the reception. None of these experiments establishes performance
under real HF fading, AGC changes, RF frequency drift, FM threshold noise,
nonlinear audio processing, or arbitrary interference.

[Reproduction source and logs](validation-data/fast/radio-capacity-20260921/README.md)
