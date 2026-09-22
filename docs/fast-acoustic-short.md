# Fast acoustic short transfers

Select **Speakers / mic · short** for speaker/microphone transfers with smaller
coding blocks and less startup training. In the CLI, use
`--profile acoustic-short --expected-snr -6`. The default expected SNR remains
3 dB. Other channel profiles and the initial **Speakers / microphone** selection
are unchanged.

## Airtime and throughput across SNR settings

Every Expected SNR selection compares convolutional coding, 16,200-bit DVB LDPC,
and 648/1,296/1,944-bit QC LDPC with locally fixed coding blocks and interleave
depth. The search first targets a minimum complete transfer of 10.5 seconds;
it allows up to 12.5 seconds when that buys at least 20% more steady throughput.
An existing qualified choice is retained when a replacement within the shorter
budget gains less than 10%. If no candidate fits, the search favors the shortest
minimum transfer. The fixture is 60 XZ-encoded bytes with enough capacity for
public or encrypted text, including bootstrap and one source cycle.

| Expected SNR over 17.5 kHz | Selected waveform / inner code | Minimum complete transfer | Expected public bit/s |
| ---: | --- | ---: | ---: |
| 13 dB | OFDM / 16,200-bit LDPC | 9.610 s | 32,681 |
| 6 dB | OFDM / 16,200-bit LDPC | 9.610 s | 16,244 |
| 3 dB (default) | OFDM / 16,200-bit LDPC | 10.343 s | 7,956 |
| 0 dB | OFDM / 1,944-bit LDPC | 12.328 s | 3,141 |
| −3 dB | Single carrier / 1,944-bit LDPC | 9.557 s | 863 |
| −6 dB | Single carrier / 1,944-bit LDPC | 9.844 s | 794 |
| −10 dB | Single carrier / 1,944-bit LDPC | 15.278 s | 316 |

Times include training, bootstrap, complete coding cycles, tracking, pulse tails
and six seconds of physical-end observation. Audio device startup can add delay.
This is a **minimum**, not a time limit for arbitrary messages. Expected bitrate
counts FEC, integrity and recurring framing overhead, but excludes compression
gains, startup and final silence. The composer separately estimates each actual
compressed message, filename envelope and final padding.

At −6 dB, the new one-block rate-3/4 LDPC choice provides 794 public / 696 keyed
bit/s, compared with 445 / 315 for the previous convolutional profile, with
almost identical minimum airtime. The tested keyed incompressible 2.5 KiB
attachment drops from 76.986 to 39.057 seconds. Selecting interleave depth 2
in Modem details increases the minimum to 12.765 seconds and steady throughput
to 895 / 830 bit/s; its tested keyed file takes 36.136 seconds. That smaller
additional gain does not justify changing the automatic minimum at −6 dB.
Both peers must use the same depth. At unchanged 3 dB, the tested 2.5 KiB
attachment still takes 11.794 seconds.

SNR uses the original 17.5 kHz reference band. Narrowing assumes unchanged total
received power and flat noise density. At −6 dB, the occupied band is about
1,104 Hz around a 1,800 Hz carrier, with about 920 symbols/s and modeled 6 dB
selected-band SNR. Single-carrier output preserves the OFDM average power.
Automatic single-carrier presets cap the rate at 1,000 symbols/s to keep a
5 ms echo inside the equalizer span. This cap explains the similar −3 and −6 dB
bitrates; 0 dB can use OFDM. The model does not account for room resonances or
non-white interference.

## Coding and physical completion

The new QC family uses IEEE 802.11 matrices at rates 1/2, 2/3 and 3/4, with
fixed zero information coordinates where needed to carry a whole number of
bytes. It does not implement an IEEE 802.11 PHY. The existing DVB-S2 short
matrices and terminated K=7 convolutional paths remain supported. All retain
outer Reed–Solomon correction, complete-cycle SHA-256 or HMAC integrity,
bootstrap salt and XZ compression. See the [LDPC comparison and independent
fixtures](validation-data/fast/short-ldpc-20260922/README.md) for exact geometry,
error-rate measurements, alternatives and rejected candidates.

Small LDPC single carrier shares the compact 256-symbol startup and 192-symbol
marker. Only the first 64 marker symbols fit tracking/equalization; the remaining
128 are held out for acquisition verification. Automatic small SC uses one
marker per fixed interval and sixteen pilots per 128 payload symbols. Sparser
markers failed sampled tests and are not selected. OFDM retains independent
training verification and tracking pilots. The short OFDM cyclic prefix is
10.67 ms, compared with the existing acoustic profile's 85.33 ms.

Received lengths never choose framing, allocation or completion. Source decoding,
XZ and attachment interpretation wait for physical completion. Only fully
observed absent symbols covering at least six seconds can finish reception;
EOF, cancellation and successful correction or integrity checks cannot replace
that event. Missing data retain their positions. Both peers need matching
software, channel profiles and settings. Other Fast profiles and ordinary modem
transport keep their existing formats, identities and physical-end rules.

## Validation scope

Sampled tests cover public/keyed UTF-8 text and incompressible files with fixed
noise density, a 5 ms echo at amplitude 0.30 and clock errors up to ±100 ppm.
They assert withheld source, EOF/partial-absence handling, noise-only rejection,
truncated acquisition, independent marker verification and bounded storage.
The previous convolutional waveform retains its own frozen regression fixture.

These deterministic checks and the ideal inner-code noise comparison do not
measure statistical whole-file reliability or qualify a physical room. The
[current record](validation-data/fast/short-ldpc-20260922/README.md) includes the
results and reproduction commands; the [previous convolutional record](validation-data/fast/acoustic-short-20260922/README.md)
remains available.
