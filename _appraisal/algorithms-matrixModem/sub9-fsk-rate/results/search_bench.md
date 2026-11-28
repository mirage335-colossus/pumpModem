# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 512, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 1, rungs 9
transform: FFTW 3 single precision, measured plans
timed on 960 blocks of 4096 messages (3932160 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2, timing offset 0; found 7f3a79, bin -21, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values and positions, once per message | 0.419 us | 1.22e+09 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: de-chirp, transform of 1024 points, 174 bin energies | 0.480 us | 2.08e+06 transforms/s |
| All other rungs, per timing offset | 2.844 us | |
| All of it, per message, with 4 drift hypotheses and 1 timing offset | 5.181 us | 1.93e+05 messages/s |

One timing offset over all messages, and 20 timing offsets as 20 times that, against a block of 5243 s:

| Block | One timing offset | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 86.92 s | 1738 s | 3.016 |
| 16 bits | 0.3395 s | 6.791 s | 772.1 |
| 10 bits | 0.005305 s | 0.1061 s | 4.941e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 6, tones 512, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 1, rungs 9
transform: FFTW 3 single precision, measured plans
timed on 480 blocks of 4096 messages (1966080 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2, timing offset 0; found 7f3a79, bin -21, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values and positions, once per message | 0.459 us | 1.11e+09 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: de-chirp, transform of 1024 points, 174 bin energies | 0.458 us | 2.18e+06 transforms/s |
| All other rungs, per timing offset | 2.896 us | |
| All of it, per message, with 4 drift hypotheses and 1 timing offset | 5.187 us | 1.93e+05 messages/s |

One timing offset over all messages, and 20 timing offsets as 20 times that, against a block of 5243 s:

| Block | One timing offset | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 87.03 s | 1741 s | 3.012 |
| 16 bits | 0.34 s | 6.799 s | 771.1 |
| 10 bits | 0.005312 s | 0.1062 s | 4.935e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 512, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 1, rungs 9
transform: plain radix-2 transform of this program
timed on 480 blocks of 4096 messages (1966080 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2, timing offset 0; found 7f3a79, bin -21, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values and positions, once per message | 0.482 us | 1.06e+09 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: de-chirp, transform of 1024 points, 174 bin energies | 2.397 us | 4.17e+05 transforms/s |
| All other rungs, per timing offset | 10.544 us | |
| All of it, per message, with 4 drift hypotheses and 1 timing offset | 20.616 us | 4.85e+04 messages/s |

One timing offset over all messages, and 20 timing offsets as 20 times that, against a block of 5243 s:

| Block | One timing offset | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 345.9 s | 6917 s | 0.7579 |
| 16 bits | 1.351 s | 27.02 s | 194 |
| 10 bits | 0.02111 s | 0.4222 s | 1.242e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.488e+06 7.179e+05 3.7e+05 1.567e+05 9.364e+04 4.552e+04 2.677e+04 1.621e+04 1.044e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 128, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 1, rungs 7
transform: FFTW 3 single precision, measured plans
timed on 2048 blocks of 4096 messages (8388608 messages), all threads busy

planted message be199d, frequency bin -63, drift hypothesis 2, timing offset 0; found be199d, bin -63, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 128 tone values and positions, once per message | 0.112 us | 1.14e+09 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: de-chirp, transform of 256 points, 174 bin energies | 0.139 us | 7.18e+06 transforms/s |
| All other rungs, per timing offset | 1.082 us | |
| All of it, per message, with 4 drift hypotheses and 1 timing offset | 1.751 us | 5.71e+05 messages/s |

One timing offset over all messages, and 20 timing offsets as 20 times that, against a block of 5243 s:

| Block | One timing offset | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 29.38 s | 587.6 s | 8.922 |
| 16 bits | 0.1148 s | 2.296 s | 2284 |
| 10 bits | 0.001793 s | 0.03587 s | 1.462e+05 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 8.29e+04 3.857e+04 1.798e+04 1.039e+04 6029 3923 2741

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 2048, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 1, rungs 11
transform: FFTW 3 single precision, measured plans
timed on 128 blocks of 4096 messages (524288 messages), all threads busy

planted message 131c1b, frequency bin -36, drift hypothesis 2, timing offset 0; found 131c1b, bin -36, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 2048 tone values and positions, once per message | 2.506 us | 8.17e+08 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: de-chirp, transform of 4096 points, 174 bin energies | 2.191 us | 4.57e+05 transforms/s |
| All other rungs, per timing offset | 12.443 us | |
| All of it, per message, with 4 drift hypotheses and 1 timing offset | 23.712 us | 4.22e+04 messages/s |

One timing offset over all messages, and 20 timing offsets as 20 times that, against a block of 5243 s:

| Block | One timing offset | 20 timing offsets | Block duration / search time |
| --- | ---: | ---: | ---: |
| 24 bits | 397.8 s | 7956 s | 0.659 |
| 16 bits | 1.554 s | 31.08 s | 168.7 |
| 10 bits | 0.02428 s | 0.4856 s | 1.08e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 2.675e+07 1.058e+07 6.276e+06 2.957e+06 1.633e+06 8.448e+05 3.793e+05 2.118e+05 1.205e+05 7.044e+04 4.485e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 512, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 20, rungs 9
transform: FFTW 3 single precision, measured plans
timed on 96 blocks of 4096 messages (393216 messages), all threads busy

planted message 7f3a79, frequency bin -21, drift hypothesis 2, timing offset 1; found 7f3a79, bin -21, drift 2, offset 1: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 512 tone values and positions, once per message | 0.460 us | 1.11e+09 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: share of turn and summing, de-chirp, transform of 1024 points, 174 bin energies | 0.477 us | 2.1e+06 transforms/s |
| All other rungs, per timing offset | 2.966 us | |
| All of it, per message, with 4 drift hypotheses and 20 timing offsets | 97.917 us | 1.02e+04 messages/s |

The whole search with its 20 timing offsets executed, against a block of 5243 s:

| Block | Search time | Block duration / search time |
| --- | ---: | ---: |
| 24 bits | 1643 s | 3.191 |
| 16 bits | 6.417 s | 817 |
| 10 bits | 0.1003 s | 5.229e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 1.48e+06 7.101e+05 3.703e+05 1.662e+05 9.114e+04 4.816e+04 2.515e+04 1.573e+04 1.043e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 2048 summed into 512 sub-blocks of 4, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 20, rungs 11
transform: FFTW 3 single precision, measured plans
timed on 48 blocks of 4096 messages (196608 messages), all threads busy

planted message 131c1b, frequency bin -36, drift hypothesis 2, timing offset 0; found 131c1b, bin -36, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 2048 tone values and positions, once per message | 2.683 us | 7.63e+08 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: share of turn and summing, de-chirp, transform of 1024 points, 174 bin energies | 0.698 us | 1.43e+06 transforms/s |
| All other rungs, per timing offset | 3.477 us | |
| All of it, per message, with 4 drift hypotheses and 20 timing offsets | 128.047 us | 7.81e+03 messages/s |

The whole search with its 20 timing offsets executed, against a block of 5243 s:

| Block | Search time | Block duration / search time |
| --- | ---: | ---: |
| 24 bits | 2148 s | 2.441 |
| 16 bits | 8.392 s | 624.8 |
| 10 bits | 0.1311 s | 3.999e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 2.434e+07 9.564e+06 5.663e+06 2.662e+06 1.488e+06 7.78e+05 3.457e+05 2.016e+05 1.143e+05 6.776e+04 4.413e+04

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 8192 summed into 512 sub-blocks of 16, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 20, rungs 13
transform: FFTW 3 single precision, measured plans
timed on 12 blocks of 4096 messages (49152 messages), all threads busy

planted message 4a39ce, frequency bin -53, drift hypothesis 2, timing offset 16; found 4a39ce, bin -53, drift 2, offset 16: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 8192 tone values and positions, once per message | 10.561 us | 7.76e+08 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: share of turn and summing, de-chirp, transform of 1024 points, 174 bin energies | 1.524 us | 6.56e+05 transforms/s |
| All other rungs, per timing offset | 7.284 us | |
| All of it, per message, with 4 drift hypotheses and 20 timing offsets | 278.181 us | 3.59e+03 messages/s |

The whole search with its 20 timing offsets executed, against a block of 5243 s:

| Block | Search time | Block duration / search time |
| --- | ---: | ---: |
| 24 bits | 4667 s | 1.123 |
| 16 bits | 18.23 s | 287.6 |
| 10 bits | 0.2849 s | 1.841e+04 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 4.186e+08 1.987e+08 1.033e+08 4.276e+07 2.497e+07 1.16e+07 6.399e+06 2.968e+06 1.664e+06 8.895e+05 4.863e+05 2.81e+05 1.803e+05

---

# Hypothesis search by a transform across tone sums, CPU

threads 12, tones 32768 summed into 512 sub-blocks of 64, frequency bins on the whole block 174, drift hypotheses 4, timing offsets 20, rungs 15
transform: FFTW 3 single precision, measured plans
timed on 4 blocks of 4096 messages (16384 messages), all threads busy

planted message 183339, frequency bin 79, drift hypothesis 2, timing offset 0; found 183339, bin 79, drift 2, offset 0: correct

| Part | Time | Rate |
| --- | ---: | ---: |
| Gather of 32768 tone values and positions, once per message | 217.617 us | 1.51e+08 tone lookups/s |
| Rung 0, per drift hypothesis and timing offset: share of turn and summing, de-chirp, transform of 1024 points, 174 bin energies | 6.757 us | 1.48e+05 transforms/s |
| All other rungs, per timing offset | 34.142 us | |
| All of it, per message, with 4 drift hypotheses and 20 timing offsets | 1441.007 us | 694 messages/s |

The whole search with its 20 timing offsets executed, against a block of 5243 s:

| Block | Search time | Block duration / search time |
| --- | ---: | ---: |
| 24 bits | 2.418e+04 s | 0.2169 |
| 16 bits | 94.44 s | 55.52 |
| 10 bits | 1.476 s | 3553 |

The 16-bit and 10-bit rows are the 24-bit time divided by 2^8 and 2^14, not separate timings.

best energy per rung: 6.522e+09 3.09e+09 1.603e+09 8.118e+08 4.078e+08 1.66e+08 9.722e+07 3.721e+07 2.43e+07 1.306e+07 6.769e+06 3.563e+06 1.941e+06 1.123e+06 7.193e+05
