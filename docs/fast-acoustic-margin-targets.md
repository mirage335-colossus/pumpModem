# Acoustic throughput versus desired noise margin

This historical study records planning estimates from the 2026-09-21
profile information, not tested attenuation, distance or distortion margins.
Its model comparisons do not define additional version 001_00 features; current
supported profiles are documented in [Fast mode](fast-mode.md). All margin targets are
total margins relative to the same recorded starting condition. A 6 dB target
adds about 3 dB to the current default's estimated headroom.

The reference is the weakest frame in the recorded 16-QAM/right-only 100 KB
transfer: empirical GMI 0.93649714496 per coded bit. Matching this to ideal
uniform 16-QAM AWGN bit information gives 13.0658 dB Es/N0. The current
16-QAM/LDPC3/4 decoder's short AWGN experiment places its transition around
9.8–10 dB. See [the LDPC study](fast-ldpc-rate-margin.md) and
[the acoustic measurements](fast-acoustic-routing-diagnosis.md).

| Total margin target | Candidate/planning source rate | Loss from current ~38.8 kbit/s | Basis |
| --- | ---: | ---: | --- |
| 6 dB | 25.8 kbit/s | 33% | Existing 16-QAM, LDPC 1/2 payload mode |
| 10 dB | 17.9 kbit/s | 54% | Existing QPSK, LDPC 2/3 payload mode; baseline synchronization limits apply |
| 16 dB | ~5.7 kbit/s | ~85% | Constant-gap analytical model only |
| 20 dB | ~2.5 kbit/s | ~94% | Constant-gap analytical model only |
| 23 dB | ~1.3 kbit/s | ~97% | Constant-gap analytical model only |
| 26 dB | ~0.65 kbit/s | ~98% | Constant-gap analytical model only |

The first two source rates come from the production CLI's 50,000,000-byte
airtime estimate, including existing overhead. Their loss percentages compare
against the same default estimate, 38.674754 kbit/s. The default asymptotic
steady rate is about 38.797 kbit/s; fixed training and final fill explain the
small difference. These estimates exclude failed transfers and retries.

## New bounded decoder screening

The production mapper, max-log demapper, 64,800-bit LDPC decoder, interleaving
and whitening were tested with independent complex AWGN and known noise
variance. Each point tested 24 frames, comparing all source bytes exactly.

| QAM / LDPC | Es/N0 | Exact frames / tested |
| --- | ---: | ---: |
| 16 / 1/2 | 7 dB | 24 / 24 |
| 16 / 2/3 | 7 dB | 0 / 24 |
| 4 / 2/3 | 3 dB | 24 / 24 |
| 4 / 1/2 | 3 dB | 24 / 24 |

The reference minus 6 or 10 dB is respectively 7.0658 or 3.0658 dB. These
short screens support the two payload candidates; 24 successful frames do not
establish a rare-error rate or a file success probability. No OFDM waveform,
channel estimation, synchronization, outer RS or audio hardware was tested.

The presence detector in this recorded baseline had an additional limitation: it checked at most
16 sign errors among 256 QPSK pilot bits, plus residual power below signal
power. In ideal flat AWGN at 3 dB Es/N0, the sign test alone passes about 20%
of blocks, even though the QPSK/LDPC2/3 payload decoder passes the screen.
Actual selected pilot tones can be stronger than the information-equivalent
average, so this is not a prediction of the room's measured pilot performance.
It demonstrates why lowering the payload rate alone cannot establish 10 dB
whole-modem margin; training and pilot detection are separate constraints.

Targets of 16–26 dB imply negative per-tone Es/N0 under this reference. The
lowest capacity code in this recorded baseline was QPSK LDPC1/2. The higher
margin rows are analytical comparisons, not available profiles or measured
whole-modem margins.

## Historical analytical model

Use a constant-gap AWGN curve with the present overhead and a 1.549 dB gap,
calibrated to deliver three information bits per payload tone at a 10 dB
decoder threshold:

```text
gap = 10 - 10*log10(2^3 - 1) = 1.5490196 dB
R(M) = (38.797/3) * log2(1 + 10^((13.0658 - M - gap)/10)) kbit/s
```

This is an illustrative engineering model, not a demonstrated coding scheme
or a mathematical upper bound on the real acoustic channel. A constant gap
need not persist across different codes and SNRs; new synchronization may cost
additional airtime. With no implementation gap but the same overhead, the
ideal model gives approximately 7.7, 3.4, 1.8 and 0.93 kbit/s at 16, 20, 23
and 26 dB respectively. Thus the practical estimates allow some distance from
ideal decoding. The underlying AWGN capacity relation is explained in
[MIT's communication notes](https://ocw.mit.edu/courses/6-451-principles-of-digital-communication-ii-spring-2005/bb895c1dee9ce0b39d6846e0aa984981_MIT6_451S05_FullLecNotes.pdf).

Reducing signal level, adding noise, changing multipath, and clipping are
different impairments. Mapping them all to a uniform loss in equivalent SNR
is only a screening assumption. Strong clipping can also destroy training;
a numerical noise-margin target is not a guarantee against that condition.

## Reproduction

The [evidence directory](validation-data/fast/acoustic-margin-targets-20260921/)
contains the helper, all screening rows, formula, actual CLI rates, and source
hashes. No runtime files or defaults changed, and no audio device was opened.

```sh
c++ -std=c++20 -O3 -Iinclude \
  docs/validation-data/fast/acoustic-margin-targets-20260921/ldpc_qam_margin_modes.cpp \
  build/libdatapump_fast.a build/libdatapump.a \
  -lcrypto -ldl build/third_party/xz/liblzma.a -pthread \
  -o /tmp/ldpc_qam_margin_modes
/tmp/ldpc_qam_margin_modes 1/2 7 3 96001 16
/tmp/ldpc_qam_margin_modes 2/3 7 3 96002 16
/tmp/ldpc_qam_margin_modes 2/3 3 3 96003 4
/tmp/ldpc_qam_margin_modes 1/2 3 3 96004 4
build/pump fast-info --profile acoustic --qam 4 --code-rate 2/3 --estimate-bytes 50000000
```
