# Temporary sampled soft-channel audit

This experiment does not change production code. It links the existing Release
`libdatapump_fast.a` and uses the actual public Transmitter and Receiver interfaces.

Each point uses 64 independently generated 2,048-bit physical intervals from
`std::mt19937(5719)`, all uniformly distributed hard bits. No FEC, source format,
encryption, or source decoding is involved. The modem uses its current marker,
pilot, acquisition, tracking, equalizer and soft-bit rules.

Profiles: wire, SSB audio, acoustic. Constellations: 4, 16, 64, 256. In-band SNR:
20 and 30 dB. AWGN uses `std::mt19937_64(417)` and fullband real-sample variance
`P_received * 10^(-SNR/10) * Fs/(2*B)`, with `B=Rs*(1+rolloff)`. Signal power is
measured over the complete clean received waveform, before additive noise. With
no echo this is the same power-normalization procedure as `fast_regression`.

The additional acoustic echo case is exactly the existing regression's longest
static impulse response: h[0]=0.6, h[216]=0.36, h[432]=0.12 at 48 kHz. For this
comparison the AWGN reference power is measured AFTER that echo response, so
the labeled SNR is the ratio of clean received signal power to in-band noise,
not transmitter power to noise. The existing echo regression instead specifies
a fixed additive-noise sigma of 0.001; these are new SNR-controlled measurements.

A 137-sample zero prefix precedes reception. After the waveform the receiver
gets seven seconds of continued AWGN with no desired signal. Physical completion
is recorded from the receiver's own observed-absence event. EOF does not force it.

Before noisy measurements, each profile/constellation must recover every bit of
a clean, unechoed waveform with no erasures and the same interval count. For each
noisy result the complete interval count must match. Every received interval
containing at least 128 nonzero soft values is matched against all 64 known random
interval identities; its best candidate must be its actual transmitted
index and have under 40% hard disagreement. Otherwise the metric is unavailable.
Entirely erased interior intervals remain at their positions and contribute
zero information. Reported known-hard BER excludes soft-zero erasures.

The empirical bit-metric information estimate uses one GLOBAL nonnegative
scale, over the current demapper's soft values, with erasures included:

    m * max_s [1 - mean(log2(1 + exp(-s * signed_soft)))]

where m=log2(M), signed_soft=(2*known_bit-1)*soft. The grid is s=0 plus
10^(-3+i/8), i=0..40. It optimizes an empirical achievable-rate expression for
this bit metric; it is not a decoded LDPC result, a hardware measurement, or a
proof of coding performance on correlated erasures. Numerical values equal to
m mean the finite sample found effectively no contradictory evidence, not proof
of zero failure probability. The same realization used to estimate the rate
selects its one scale parameter. No confidence intervals are established.

The `current_framed_metric_bps` column multiplies normalized bit-metric rate by
the actual physical coded-bit rate: 2048*Rs/interval_symbols(profile). Thus it
includes existing marker and pilot overhead. It excludes LDPC/BCH/RS, source-cell
overhead, checksums, encryption, startup, final fill and end silence.

SSB and FM presets have identical PHY behavior at the same constellation; their
default constellation differs. These audio calculations contain no RF channel
simulation and cannot certify SSB or FM radio-link performance.

Build from the project root:

    g++ -std=c++20 -O3 -DNDEBUG -Iinclude docs/validation-data/fast/coding-study-20260919/sampled-channel.cpp -o /tmp/fast_soft_channel_audit build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl build/third_party/xz/liblzma.a

Run:

    /tmp/fast_soft_channel_audit 64 > /tmp/fast-soft-channel-audit.csv 2> /tmp/fast-soft-channel-audit.log

An optional second argument selects wire, ssb or acoustic instead of all profiles.
