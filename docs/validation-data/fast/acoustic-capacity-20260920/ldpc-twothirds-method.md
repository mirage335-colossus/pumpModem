# Independent LDPC 2/3 check

DVB-S2 normal-frame table B6 has N=64800 and K=43200. Its struct was copied
verbatim from the same pinned xdsopl/LDPC source as the existing tables. The
separately compiled upstream encoder in `ldpc-twothirds-fixture.cpp` supplies the
frozen codeword hash; production tests independently evaluate the parity equations,
flip boundary bits, and exercise noisy decoding and concurrent calls. The new
enum value is appended, preserving every pre-existing profile identifier.

Reproduce the independent fixture with a checkout of commit
`32357d8ad55a6a302c34e093759f0454e45cca56`:

```
c++ -std=c++20 -O2 -I/PATH/TO/LDPC ldpc-twothirds-fixture.cpp -lcrypto -o /tmp/ldpc-23-fixture
/tmp/ldpc-23-fixture
```

`build/test_fast_ldpc 4` passed all six rate fixtures and the existing dense-QAM
checks, plus an independently generated 256-QAM/2/3 AWGN frame at 20 dB Es/N0.
These are implementation checks, not a measured live error-floor claim.
