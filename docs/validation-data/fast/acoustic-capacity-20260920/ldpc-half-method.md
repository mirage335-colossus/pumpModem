# Rate-1/2 LDPC extension and explicit acoustic capacity profile

This entry records the half-rate extension and its initial single-carrier
configuration checks. Later OFDM development changes acoustic modulation;
those changes require separate waveform and live validation.

The existing production decoder now also supports the normal DVB-S2 B4 matrix:
N = 64,800, K = 32,400, quasi-cyclic group size 360, 226,799 graph edges and
maximum check degree seven. The matrix struct was copied verbatim from
`dvb_s2_tables.hh` at xdsopl/LDPC commit
`32357d8ad55a6a302c34e093759f0454e45cca56`, the same pinned source and permissive
license used by the cable code rates. No decoder algorithm, cable matrix or
frozen bit permutation changed. `CodeRate::half` already existed for classic
Fast's convolutional code; `capacity_mode` selects LDPC instead.

## Independent codeword

[`ldpc-half-fixture.cpp`](ldpc-half-fixture.cpp) uses only the upstream matrix,
upstream `LDPC` iterator and upstream `LDPCEncoder`, plus OpenSSL for hashing.
It does not include production LDPC code or the vendored table. Input byte `i`
is `(i*73 + i/7 + 0x5a) & 255`; bits are MSB-first. The result is SHA-256 over
64,800 unpacked zero/one code bits:

`ae4a2202bd42e246e012b7f095abe6fcbd7c42def4e1f2414c29907e32079aea`

Reproduction from the repository root, with the pinned checkout at the path
shown (or substitute its local location):

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic \
  -I/tmp/pump-ldpc-shannon-bench \
  docs/validation-data/fast/acoustic-capacity-20260920/ldpc-half-fixture.cpp \
  -lcrypto -o /tmp/pump-ldpc-half-upstream-fixture
/tmp/pump-ldpc-half-upstream-fixture
c++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude \
  tests/test_fast_ldpc.cpp src/fast/ldpc.cpp -lcrypto -pthread \
  -o /tmp/pump-test-fast-ldpc-half
/tmp/pump-test-fast-ldpc-half 12
```

The production test freezes this independently obtained result and separately
evaluates the DVB accumulator parity equations. Existing four rate fixtures
still match. Tests cover clean words, flipped systematic/parity boundaries,
observed all-zero words versus absent evidence, bounded noise failure, invalid
input and concurrent calls. All passed; the complete output is retained in
[`ldpc-half-tests.log`](ldpc-half-tests.log).

The half-rate high-margin BPSK test decoded 12 independently generated frames,
correcting 679 raw hard-decision errors with no wrong frames. Two additional
seeded tests use independently implemented Gray QAM, the production frozen bit
permutation and exact separable AWGN likelihoods: QPSK at 2 dB Es/N0 corrected
6,650 hard-decision errors in six iterations; 16-QAM at 7 dB corrected 7,727 in
nine iterations. Their measured decoder times were approximately 65 and 99 ms
on this host, respectively. These are one-frame regression cases, **not** a
measured waterfall, error-floor estimate, acoustic SNR measurement or acoustic
delivery guarantee. All likelihoods assume ideal channel knowledge; no audio
hardware was used by this validation.

## Profile integration

At the time of the configuration tests below,
`capacity_profile(Channel::acoustic)` constructed an explicit experimental
single-carrier candidate: 16-QAM, LDPC 3/4, one LDPC frame per cycle, 2,000
symbols/s, 4,000 Hz carrier, rolloff 0.20, amplitude 0.20, a full marker every
interval and four pilots per 32 data symbols. `--profile acoustic --format
capacity` selected it; `--code-rate 1/2` explicitly selected the new code. Plain
`--profile acoustic` remained the classic preset. Cable default settings and
profile identity serialization were unchanged. The channel was included
in integrity context, so selecting acoustic does not silently use cable
identity.

`cmake --build build --target pump test_fast_ldpc -j2` completed successfully.
`python3 tests/test_fast_cli.py build/pump -v` passed all 17 tests in 23.306 s,
including explicit acoustic defaults/channel selection, half-rate geometry,
rejected radio capacity combinations, unchanged classic presets and the eight
public/keyed dense cable S16 WAV roundtrips. Output is retained in
[`acoustic-profile-cli-tests.log`](acoustic-profile-cli-tests.log). The new
acoustic tests exercise configuration; they do not claim a real acoustic
transfer or qualify the proposed acoustic preset.

Normal-frame half-rate parity/data is 16/4034 = 0.39663% at depth one because
the outer code uses an even number of two-byte parity symbols. At depth four
it is 52/16148 = 0.32202%. The percentage is approximate, and reducing the
inner rate changes the smallest retained source area; the
[integrity bound](../../../fast-capacity-integrity.md) has been updated.
