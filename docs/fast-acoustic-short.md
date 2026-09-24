# Fast acoustic short transfers

Select **Speakers / mic · short** for speaker/microphone transfers with smaller
minimum coding blocks and less startup training. In the CLI, use
`--profile acoustic-short --expected-snr -6`. The default expected SNR remains
3 dB. Existing channel profiles and the initial **Speakers / microphone**
selection are unchanged.

## A minimum-airtime target across SNR settings

Every Expected SNR selection uses the same search: choose the highest modeled
long-message throughput whose minimum complete transfer takes at most 10.5
seconds. The minimum fixture is 60 XZ-encoded bytes, including bootstrap and one
source cycle, with enough capacity for either public or encrypted text. If no
candidate fits, choose the candidate with the shortest minimum transfer instead.
This replaces the previous optimization for a 2,800-byte encoded message.

The search compares six-training-block OFDM with 16,200-bit LDPC or compact
convolutional coding, plus compact QPSK single carrier at weaker settings.
Coding blocks and interleave depth are locally fixed. Received lengths never
choose framing, allocation or completion. Smaller blocks add proportionally
more framing and integrity overhead; the search retains LDPC when it meets the
minimum-airtime target with better throughput.

| Expected SNR over 17.5 kHz | Selected waveform / inner code | Minimum complete transfer |
| ---: | --- | ---: |
| 13 dB | OFDM / LDPC | 9.610 s |
| 6 dB | OFDM / LDPC | 9.610 s |
| 3 dB (default) | OFDM / LDPC | 10.343 s |
| 0 dB | OFDM / convolutional | 9.981 s |
| −3 dB | Single carrier / convolutional | 9.513 s |
| −6 dB | Single carrier / convolutional | 9.796 s |
| −10 dB | Single carrier / convolutional | 15.715 s |

These are generated-waveform times, including the physical-end silence.
Audio device startup can add delay. This is a **minimum**, not a ten-second
limit for arbitrary messages. At −6 dB, the tested two-byte UTF-8 message takes
9.796 s, a named 32-byte attachment takes 11.396 s, 2.5 KiB text takes 49.790 s,
and a keyed incompressible 2.5 KiB attachment takes 76.986 s. At the unchanged
3 dB selection, the tested 2.5 KiB attachment still takes 11.794 s. The composer
estimates each actual compressed message, filename envelope and final padding.

SNR uses the original 17.5 kHz reference band. Narrowing assumes unchanged total
received power and flat noise density. At −6 dB, the occupied band is about
1,104 Hz around a 1,800 Hz carrier, with about 920 symbols/s and a modeled 6 dB
selected-band SNR. The single-carrier level preserves the OFDM average output
power. This model does not account for room resonances or non-white interference.

## Coding and physical completion

The LDPC path retains DVB-S2 short tables C4, C6 and C7, conventionally named
1/2, 2/3 and 3/4, with actual information lengths 7,200, 10,800 and 11,880 bits.
The compact path uses a terminated constraint-length-seven convolutional code
at rate 1/2 or punctured 3/4, with fixed block sizes selected locally. Both retain
the outer Reed–Solomon correction, complete-cycle SHA-256 or HMAC integrity,
bootstrap salt and XZ source compression. No source data is exposed before
physical completion. Both peers must use matching versions, channel profiles
and settings; the compact format has a distinct integrity context.

Compact single carrier uses 256 startup training symbols and a 192-symbol
marker. Only the first 64 marker symbols fit tracking/equalization; the remaining
128 are held out for independent acquisition verification. Sixteen-symbol
pilots maintain timing and physical presence. Automatic compact single-carrier presets cap the symbol rate at 1,000
symbols/s to keep a 5 ms echo within the equalizer span. OFDM retains independent
training verification and tracking pilots. The short OFDM cyclic prefix is
10.67 ms, compared with the existing acoustic profile's 85.33 ms.

Only fully observed absent symbols covering at least six seconds can finish
reception. EOF, cancellation and successful correction or integrity checks
cannot replace that event. Missing data retain their timed positions and cannot
silently join neighboring source cycles. The other Fast profiles and ordinary
modem transport keep their existing formats, identities and physical-end rules.

## Validation scope

`fast_acoustic_short` and `fast_acoustic_short_weak` exercise generated audio
with fixed-density noise at the stated original-band SNR, a 5 ms echo at
amplitude 0.30, and up to ±100 ppm clock error. Tests cover public/encrypted
UTF-8 text and incompressible attachments, partial absence, EOF, noise-only
input, truncated acquisition, and a marker whose fitted part is intact while
its independent verification part is corrupted. Transition checks cover the
single-carrier/OFDM and compact/LDPC choices.

These deterministic sampled tests do not measure statistical whole-file
reliability or qualify a physical speaker/microphone link. See the
[current validation record](validation-data/fast/acoustic-short-20260922/README.md)
and the [independent short-LDPC fixtures](validation-data/fast/acoustic-short-20260921/README.md).
