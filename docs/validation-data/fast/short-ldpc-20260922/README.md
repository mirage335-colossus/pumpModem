# Small LDPC alternatives, 2026-09-22

Only Fast `acoustic-short` changes. The other four Fast channel profiles keep
identical settings and estimates in all [172 saved CLI reports](profile-isolation.log).
The regular modem transport, short dictionary, fixed framing and pending rows
are unchanged.

## Implemented alternatives

Added the IEEE QC matrices of 648, 1,296 and 1,944 bits, each at 1/2, 2/3 and
3/4, alongside the existing 16,200-bit DVB LDPC and K=7 convolutional codes.
Provenance: [tavildar/LDPC](https://github.com/tavildar/LDPC), pinned commit
`05ee7f4af36ed5dacf52861315af68b8a17e71e0`, `LdpcC/WiFiLDPC.h`, MIT license
retained under `third_party/ldpc/WIFI-LICENSE`.

Production constructs immutable sparse check graphs and a systematic parity
map by binary Gaussian elimination. The independent [Python encoder](reference.py)
uses the upstream back-substitution construction and direct cyclic parity
checks instead. Its [nine frozen hashes](vectors.json) are checked by
`test_fast_ldpc`, together with separate parity equations, bit errors,
neutral likelihoods, permutation bijection, fixed zero coordinates and invalid
inputs. All older DVB frozen vectors remain active.

When K is not byte aligned, the final K modulo 8 information bits are fixed
zero locally and still transmitted; callers supply floor(K / 8) bytes. For
example, 1,944-bit rate-3/4 has 1,458 information coordinates, carrying 182
bytes and two fixed zeros. No received length selects this geometry. Source
cycles retain outer RS, full SHA-256/HMAC, salt, XZ and the six-second physical
absence rule. All three small sizes are available through the profile API;
automatic selection compares them and the existing families. The GUI retains
valid coding-rate/depth choices for the selected family.

## Inner-code comparison

[Full ideal QPSK/AWGN results](awgn-256.log): 256 random codewords per point,
14 configurations, four Es/N0 points, 14,336 total decodes. Seed 8471 is reset
for each point. Signal Es=1 per QPSK symbol; each real coordinate has amplitude
1/sqrt(2), with Gaussian variance N0/2 and exact LLRs. LDPC uses up to 50 layered
sum-product iterations. A failure means non-convergence or incorrect source
bytes; the convolutional comparison checks all recovered bytes. No outer RS
or source compression is included in this experiment.

| Inner code | 4 dB Es/N0 | 5 dB Es/N0 | 6 dB Es/N0 |
| --- | ---: | ---: | ---: |
| K=7 punctured 3/4, 190 information bytes | 246/256 | 85/256 | 5/256 |
| QC 648, 3/4, 60 information bytes | 102/256 | 0/256 | 0/256 |
| QC 1,296, 3/4, 121 information bytes | 87/256 | 0/256 | 0/256 |
| QC 1,944, 3/4, 182 information bytes | 92/256 | 0/256 | 0/256 |
| DVB 16,200, nominal 3/4, 1,485 information bytes | 2/256 | 0/256 | 0/256 |

These are frame failures, not equal-length file failure rates. DVB's nominal
3/4 short code is actually 11/15; its nominal half-rate code is 4/9.
The larger DVB block has a better low-SNR threshold but imposes much more
minimum airtime. At rate 1/2 and 3 dB Es/N0, K=7 failed 18/256 frames while
QC 1,944 failed 0/256. There is no single universal convolutional-versus-LDPC
penalty: it depends on code rate, block length, channel and desired error rate.
Zero failures in 256 trials only bounds frame failure probability to about
1.2% at one-sided 95% confidence; it does not establish an error floor.

## Complete modem comparison at −6 dB

This SNR is over the original 17.5 kHz channel. The narrowed SC waveform has
about 1,104 Hz occupied bandwidth and 920 symbols/s, assuming constant total
received power and flat noise density. This gives a modeled 6 dB in-band SNR,
approximately 6.79 dB Es/N0 before implementation losses. It must not be
confused with the Es/N0 column above.

| Configuration | Minimum, seconds | Public bit/s | Keyed bit/s | Keyed 2.5 KiB file, seconds |
| --- | ---: | ---: | ---: | ---: |
| Previous convolutional half-rate | 9.796 | 445 | 315 | 76.986 |
| **Selected: QC 1,944, 3/4, depth 1** | **9.844** | **794** | **696** | **39.057** |
| QC 1,944, 3/4, depth 2 | 12.765 | 895 | 830 | 36.136 |
| QC 1,944, 2/3, depth 1 | 9.844 | 685 | 520 | 50.742 |
| QC 1,944, 1/2, depth 1 | 9.844 | 455 | 345 | 71.191 |
| QC 648, 3/4, depth 3 | 9.844 | 783 | 696 | 39.057 |
| QC 1,296, 3/4, depth 1 | 9.844 | 455 | 345 | 71.191 |

[Calculation source](compare.cpp), [raw estimates](airtime-comparison.log).
Minimum means 60 XZ-encoded bytes, bootstrap, final cycle, training, tracking,
pulse tails and six seconds of physical absence. File size is the actual
2,660-byte encoded named attachment used by the sampled fixture. Bitrates are
steady payload rates after FEC, integrity, recurring markers/pilots and padding;
the 50 MB estimate approximates the GUI's steady-rate calculation. They exclude
compression gains, startup and final silence, so they are not file-average rates.

The selected profile improves steady rates by **1.78× public / 2.21× keyed**
without materially increasing minimum airtime. The 12.765-second alternative
adds only 13% public / 19% keyed throughput over this new default. It is available
by changing interleave depth to 2 in Modem details; both peers must match.
The shorter option remains automatic at −6 dB.

The [selected](sampled-selected.log) and [depth-2](sampled-extended.log) profiles
each pass all ten sampled −6 dB cases: tiny UTF-8 text, named 32-byte attachments,
2.5 KiB text/file, public/keyed integrity, 0.30-amplitude 5 ms echo, clock error
up to ±100 ppm, incomplete training, EOF, partial absence, noise-only input,
and corruption of held-out acquisition signs. Byte/metadata equality, withheld
source, physical completion and bounded receiver storage are asserted.

A candidate using markers every two intervals reached 12.348 seconds and
964/894 bit/s, but [failed the keyed short attachment](rejected-sparse-markers.log).
It is rejected despite passing other long-file fixtures. Four-interval marker
spacing also failed the partial-absence assertion under clock drift. Automatic
small SC retains a marker every interval and 16 pilots per 128 payload symbols.
No physical-end or decoding assertion was relaxed for those failures.

## Automatic selection and SNR plateau

The same search runs at every Expected SNR: compare coding families at a
10.5-second minimum budget, then allow 12.5 seconds for at least 20% more
steady throughput. Keep an existing qualified choice if the shorter-budget
replacement gains less than 10%. These thresholds avoid adding latency or
changing formats for marginal modeled gains.

[Selected presets](presets.log) retain the old 3 dB/default and stronger menu
choices. At 0 dB, small LDPC OFDM improves 1,355 to 3,141 public bit/s with a
12.328-second minimum. At −6/−3 dB, SC remains capped at 1,000 symbols/s to keep
the 5 ms echo within the existing equalizer span: −6 uses about 920, −3 uses
1,000. Both choose the same QPSK and LDPC rate, explaining the similar
794/863 bit/s. At 0 dB, sufficient bandwidth becomes available for OFDM.

## Reproduction and evidence limits

```sh
cmake --build build --target fast_short_ldpc_compare test_fast_ldpc test_fast_codec \
  test_fast_acoustic_short_weak test_fast_acoustic_short --parallel 2
build/fast_short_ldpc_compare 256
python3 docs/validation-data/fast/short-ldpc-20260922/reference.py
build/test_fast_ldpc
build/test_fast_codec
build/test_fast_acoustic_short_weak
build/test_fast_acoustic_short_weak --depth=2 --markers=1 --pilots=128
build/test_fast_acoustic_short_weak --legacy-convolutional
build/test_fast_acoustic_short --snr=0 keyed_attachment
```

These are deterministic generated-audio and ideal-noise experiments, not a
physical speaker/microphone qualification or statistical whole-file reliability
claim. Other rooms, interference and noise seeds can fail. Both peers need
matching software and locally selected geometry.

## Final regression results

All 46 selected Fast/compatibility/shared GUI checks pass when combining the
[broad run](regressions.log) with the [four focused reruns](reruns.log). The
first run included a stale GUI expectation for the old convolutional menu;
that expectation now checks LDPC and manual-mode behavior. Two other tests
could not write their large key fixtures in the nearly full system `/tmp`.
The reruns use a workspace `TMPDIR`; no key-generation or receiver behavior
was changed to accommodate the environment. The unrelated 92-case Fast SNR
regression passes in 269 seconds.

Both [FLTK](native-fltk.log) and [Rev](native-rev.log) native adapter conformance
checks pass on private Xvfb displays. No native adapters changed; their complete
application workflows were not rerun in this study.

[AddressSanitizer and UndefinedBehaviorSanitizer](sanitizers.log) pass the LDPC
suite, including all nine new independent vectors and the original DVB cases.
LeakSanitizer is disabled under this container's process supervision. The
[additional −10 dB sampled text](sampled-minus10.log) also passes. The selected
and extended −6 dB ten-case suites are recorded separately above; the frozen
previous convolutional suite passes in the broad regression run.
