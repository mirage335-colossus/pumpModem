# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 512, frequency bins on the whole block 174, drift hypotheses 4, rungs 9 (windows of 512 to 2 tones)
transform: FFTW 3 single precision, measured plans
timed on 960 blocks of 4096 messages (3932160 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2; found 7f3a79, bin -21, drift 2: correct

| Part, per message and timing hypothesis | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values | 0.209 us | 2.45e+09 tone lookups/s |
| Rung 0, per drift hypothesis: de-chirp, transform of 1024 points, 174 bin energies | 0.439 us | 2.28e+06 transforms/s |
| Rungs 1 to 8 together | 2.874 us | |
| All of it with 4 drift hypotheses | 4.838 us | 2.07e+05 messages/s |

frequency and drift hypotheses evaluated per message and timing: 174 x 4 on the whole block, 872 with the shorter rungs

One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of 5243 s:

| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 81.18 s | 1624 s | 3.229 |
| 16 bits | 0.3171 s | 6.342 s | 826.7 |
| 10 bits | 0.004955 s | 0.09909 s | 5.291e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 6, tones 512, frequency bins on the whole block 174, drift hypotheses 4, rungs 9 (windows of 512 to 2 tones)
transform: FFTW 3 single precision, measured plans
timed on 480 blocks of 4096 messages (1966080 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2; found 7f3a79, bin -21, drift 2: correct

| Part, per message and timing hypothesis | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values | 0.215 us | 2.38e+09 tone lookups/s |
| Rung 0, per drift hypothesis: de-chirp, transform of 1024 points, 174 bin energies | 0.429 us | 2.33e+06 transforms/s |
| Rungs 1 to 8 together | 2.859 us | |
| All of it with 4 drift hypotheses | 4.792 us | 2.09e+05 messages/s |

frequency and drift hypotheses evaluated per message and timing: 174 x 4 on the whole block, 872 with the shorter rungs

One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of 5243 s:

| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 80.4 s | 1608 s | 3.261 |
| 16 bits | 0.3141 s | 6.281 s | 834.7 |
| 10 bits | 0.004907 s | 0.09814 s | 5.342e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 512, frequency bins on the whole block 174, drift hypotheses 4, rungs 9 (windows of 512 to 2 tones)
transform: plain radix-2 transform of this program
timed on 480 blocks of 4096 messages (1966080 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2; found 7f3a79, bin -21, drift 2: correct

| Part, per message and timing hypothesis | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values | 0.216 us | 2.37e+09 tone lookups/s |
| Rung 0, per drift hypothesis: de-chirp, transform of 1024 points, 174 bin energies | 2.359 us | 4.24e+05 transforms/s |
| Rungs 1 to 8 together | 11.345 us | |
| All of it with 4 drift hypotheses | 20.998 us | 4.76e+04 messages/s |

frequency and drift hypotheses evaluated per message and timing: 174 x 4 on the whole block, 872 with the shorter rungs

One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of 5243 s:

| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 352.3 s | 7046 s | 0.7441 |
| 16 bits | 1.376 s | 27.52 s | 190.5 |
| 10 bits | 0.0215 s | 0.43 s | 1.219e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 128, frequency bins on the whole block 174, drift hypotheses 4, rungs 7 (windows of 128 to 2 tones)
transform: FFTW 3 single precision, measured plans
timed on 2048 blocks of 4096 messages (8388608 messages), all threads busy

planted message be199d, frequency bin -63, drift hypothesis 2; found be199d, bin -63, drift 2: correct

| Part, per message and timing hypothesis | Time | Rate |
| --- | ---: | ---: |
| Gather of 128 tone values | 0.063 us | 2.04e+09 tone lookups/s |
| Rung 0, per drift hypothesis: de-chirp, transform of 256 points, 174 bin energies | 0.142 us | 7.03e+06 transforms/s |
| Rungs 1 to 6 together | 1.068 us | |
| All of it with 4 drift hypotheses | 1.700 us | 5.88e+05 messages/s |

frequency and drift hypotheses evaluated per message and timing: 174 x 4 on the whole block, 869 with the shorter rungs

One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of 5243 s:

| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 28.52 s | 570.4 s | 9.192 |
| 16 bits | 0.1114 s | 2.228 s | 2353 |
| 10 bits | 0.001741 s | 0.03481 s | 1.506e+05 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 8.29e+04 3.857e+04 1.798e+04 1.039e+04 6029 3923 2741

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 2048, frequency bins on the whole block 174, drift hypotheses 4, rungs 11 (windows of 2048 to 2 tones)
transform: FFTW 3 single precision, measured plans
timed on 128 blocks of 4096 messages (524288 messages), all threads busy

planted message 131c1b, frequency bin -36, drift hypothesis 2; found 131c1b, bin -36, drift 2: correct

| Part, per message and timing hypothesis | Time | Rate |
| --- | ---: | ---: |
| Gather of 2048 tone values | 1.078 us | 1.9e+09 tone lookups/s |
| Rung 0, per drift hypothesis: de-chirp, transform of 4096 points, 174 bin energies | 2.223 us | 4.5e+05 transforms/s |
| Rungs 1 to 10 together | 12.117 us | |
| All of it with 4 drift hypotheses | 22.089 us | 4.53e+04 messages/s |

frequency and drift hypotheses evaluated per message and timing: 174 x 4 on the whole block, 874 with the shorter rungs

One timing hypothesis over all messages, and the whole search with 20 timing offsets, against a block of 5243 s:

| Block | One timing hypothesis | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 370.6 s | 7412 s | 0.7074 |
| 16 bits | 1.448 s | 28.95 s | 181.1 |
| 10 bits | 0.02262 s | 0.4524 s | 1.159e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 2.675e+07 1.058e+07 6.276e+06 2.957e+06 1.633e+06 8.448e+05 3.793e+05 2.118e+05 1.205e+05 7.044e+04 4.485e+04
