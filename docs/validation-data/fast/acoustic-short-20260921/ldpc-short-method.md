# Independent short LDPC fixtures

The separate Fast `acoustic-short` profile adds the DVB-S2 short C4, C6 and C7
matrices. Each has 16,200 coded bits; their information sizes are 7,200, 10,800
and 11,880 bits. The nominal 1/2 and 3/4 labels therefore differ from their
actual short-frame rates. Source capacity and reported parity ratios use the
matrix information size. Existing profiles retain their 64,800-bit matrices.

The vendored structs come unchanged from `dvb_s2_tables.hh` in xdsopl/LDPC
commit `32357d8ad55a6a302c34e093759f0454e45cca56`. The independent checkout at
`/tmp/pump-ldpc-shannon-bench` was verified at that revision with no changes to
`encoder.hh`, `ldpc.hh`, or `dvb_s2_tables.hh`. The full table file also matched
the independently fetched pinned copy in `build/fast-ui-tmp` byte for byte.

[`ldpc-short-fixture.cpp`](ldpc-short-fixture.cpp) includes only the upstream
matrix/iterator/encoder and OpenSSL. Source byte `i` is
`(i*73 + i/7 + 0x5a) & 255`, packed MSB first. Positive upstream signs denote
zero; SHA-256 covers 16,200 unpacked zero/one bits. It produces:

| Matrix | Information bits | SHA-256 |
| --- | ---: | --- |
| C4 | 7200 | `9a2d76ba1fac4eba7b231c4cc05ece1feb36b0d9ca30f9c3441da20dc3831571` |
| C6 | 10800 | `d63534b523ba3b1d36929ca092424920b11a1b94c34b2be9b6d0f1685121a8b4` |
| C7 | 11880 | `29d9669c1679fe3bf3923f0551273a22d95b23d10f50379f99af5528ea0677f5` |

The short interleaver retains the same specified uint32 xorshift seed and
descending Fisher–Yates algorithm, applied to 16,200 positions. Independent
Python generation gives first/last positions 12410/1583 and SHA-256
`0c13ce492c31036c52f4477af11bfa28f63a7f67533bee135410270dded85111`
over all positions serialized as big-endian uint16 values:

```python
import hashlib, struct
p = list(range(16200))
state = 0x6c647063
for remaining in range(len(p), 1, -1):
    state ^= (state << 13) & 0xffffffff
    state ^= state >> 17
    state ^= (state << 5) & 0xffffffff
    pick = state % remaining
    p[remaining-1], p[pick] = p[pick], p[remaining-1]
print(p[0], p[-1], hashlib.sha256(b''.join(struct.pack('>H', i) for i in p)).hexdigest())
```

Reproduction from the repository root:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic \
  -I/tmp/pump-ldpc-shannon-bench \
  docs/validation-data/fast/acoustic-short-20260921/ldpc-short-fixture.cpp \
  -lcrypto -o build/fast-ui-tmp/ldpc-short-upstream-fixture
build/fast-ui-tmp/ldpc-short-upstream-fixture
c++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude \
  tests/test_fast_ldpc.cpp src/fast/ldpc.cpp -lcrypto -pthread \
  -o build/fast-ui-tmp/test-fast-ldpc-short
build/fast-ui-tmp/test-fast-ldpc-short 12
```

The production test passed on 2026-09-21. It checks these frozen independent
codewords and permutation, direct accumulator parity equations, clean decode,
flipped systematic/parity boundary bits, observed all-zero words versus absent
evidence, invalid rates/sizes/nonfinite likelihoods, and bounded noise failure.
Three seeded interleaved BPSK frames corrected 95, 105 and 105 raw bit errors,
respectively, with exact source recovery. The full unchanged normal-frame
fixtures, noisy frames, concurrent calls and Gray-QAM checks also passed.
The [test output](ldpc-short-tests.log) is retained. These deterministic codec
checks do not establish acoustic link reliability or the short profile's
end-to-end timing; those require separate waveform tests.
