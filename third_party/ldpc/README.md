# LDPC matrix tables

`tables.hpp` contains verbatim struct definitions extracted from
[xdsopl/LDPC](https://github.com/xdsopl/LDPC), pinned commit
`32357d8ad55a6a302c34e093759f0454e45cca56`:

- `dvb_s2_tables.hh`: `DVB_S2_TABLE_B4` (1/2), `B6` (2/3), `B7` (3/4), `B10` (8/9), `B11` (9/10).
- `dvb_s2x_tables.hh`: `DVB_S2X_TABLE_B10` (7/9).

All have normal frame length 64,800 bits. The project uses only LDPC, without
DVB's outer BCH or broadcast framing. Original copyright and permissive license
are retained in `LICENSE`. Original tables cite ETSI/DVB S2 and S2X standards.

`src/fast/ldpc.cpp` constructs an immutable sparse check graph and supplies a
bounded, thread-safe layered log-domain sum-product decoder. The upstream
algorithm motivated the layered ordering, but no upstream decoder code is
compiled: there are no variable-length stack arrays or shared mutable decoder
objects. `tests/test_fast_ldpc.cpp` freezes codeword hashes made by the separately
compiled upstream encoder and independently checks its parity-check equations.
The rate-1/2 acoustic extension uses the same normal frame and decoder; its
[independent fixture and validation](../../docs/validation-data/fast/acoustic-capacity-20260920/ldpc-half-method.md)
are recorded separately from the earlier cable sweep.

## Decoder contract and checks

The public input is one exact, byte-aligned information block; the output is
64,800 unpacked code bits, systematic information first, with MSB-first byte
packing. Soft input to decode is `log(P(bit=1)/P(bit=0))`, clipped to ±50.
Fifty layered sum-product iterations are the default, with an explicit 1–100
limit. Syndrome success is not an integrity check. All-zero likelihoods fail;
an unsuccessful decode still supplies hard posterior information bytes for the
outer code. NaN, infinity, and incorrect sizes are rejected.

The rate-independent bit permutation is a separate explicit operation, so the
canonical codewords retain standard DVB ordering. It uses uint32 xorshift with
seed `0x6c647063` and a fully specified descending Fisher–Yates shuffle. Encoding
alone does not interleave; decoding alone expects deinterleaved likelihoods.

Standalone reproduction from the repository root:

```sh
c++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude \
  tests/test_fast_ldpc.cpp src/fast/ldpc.cpp -lcrypto -pthread \
  -o /tmp/pump-test-fast-ldpc
/tmp/pump-test-fast-ldpc 64
```

On 2026-09-20 the 64-frame-per-rate run corrected 14,853 raw BPSK bit errors in
256 random codewords, with no incorrect decoded frames. Mean complete
encoder/channel/decoder time was about 11–13 ms per frame on this host. Separate
seeded ideal AWGN checks exercise exact Gray-QAM likelihoods and bit mixing:
4096-QAM 7/9 at 31.8 dB Es/N0, and 16384-QAM at 37.8 dB (7/9), 40 dB (8/9),
and 42 dB (9/10). Those frames require 3–11 decoding iterations and approximately
26–96 ms per frame. These small deterministic checks detect implementation
regressions; they do not establish a low error floor or real cable performance.
A further 65,536-QAM 9/10 frame at 48 dB also decodes correctly. The larger
[production-decoder capacity sweep](../../docs/validation-data/fast/capacity-20260920/ldpc-awgn-method.md)
extends the ideal-channel evidence through 4,194,304-QAM.
Concurrent calls, boundary-bit flips, invalid inputs, a bounded noise-only
failure, and separately generated frozen codeword hashes are also checked.

AddressSanitizer and UndefinedBehaviorSanitizer passed the same tests with two
BPSK frames per rate. This container requires `ASAN_OPTIONS=detect_leaks=0`
because LeakSanitizer cannot run under its ptrace supervision; leak scanning is
therefore not part of that validation result.

The acoustic 2/3 extension uses B6, with an [independent upstream fixture](../../docs/validation-data/fast/acoustic-capacity-20260920/ldpc-twothirds-method.md).
