# Identifiability and hop erasures in the FSK draft

These are finite theoretical calculations with noiseless observations and known erasure locations. The native check calls the existing 10-bit experimental placement generator and enumerates all 1,024 messages. Its statistic counts distinct surviving position sequences. For uniform payloads the best possible noiseless probability of correct decoding is this count divided by 1,024. Oracles supply timing, frequency and perfect tone labels.

## Fixed cases

- For each supported power-of-two alphabet from 2 through 256 at the draft's minimum repeat count, calculate expected distinct position sequences for one uniformly distributed byte, averaging uniform independent balanced maps exactly. Evaluate the 24-bit message-pair calculation at 16 positions for the whole 64-tone block and for six perfectly received tones on either three or six symbols.
- With independent unusable hops of probability 1/2, evaluate the exact probability of losing over 77% at 32, 33 and 34 hops. At 32 hops compare stationary symmetric two-state loss with mean bad runs of 2, 4, 10 and 50 hops. Validate the 2-hop result against the binomial sum.
- For the existing C++ generator, use 16 positions, two repeats, 64 tones and seeds 1 through 4. Per seed check the full block, every contiguous nonwrapping run of 16, 28 and 31 erased two-tone hops, and two surviving positions from the same or different symbols. Enumerate all messages for each footprint and retain the range of distinct counts. Use the first symbol's two tones or the first tone of each of symbols 0 and 1 for the last comparison.

After the initial enumeration found full identifiability for the 28-hop contiguous outages, extend the same four fixtures to every one of the 523,776 unordered message pairs. Record the smallest number of differing hops and the shortest contiguous interval containing all differing hops across any pair. These two exact distances determine the respective boundaries for arbitrary and contiguous erasures. Keep the same geometries, seeds and oracle.

Every listed case runs once. These exact distributions and enumerations have no Monte Carlo error bars. The four generator seeds test the implementation on fixed fixtures and support no inference about the frequency of bad seeds.

From this directory, run

```sh
python3 finite_code.py
g++ -O2 -std=c++20 -fopenmp fixed_schedule.cpp -o /tmp/matrix-fixed-schedule
/tmp/matrix-fixed-schedule
```

Compare stdout with [theory.txt](theory.txt) and [fixed_schedule.txt](fixed_schedule.txt). The [research note](../../../_ref/algorithms-matrixModem/fsk-code-identifiability.md) explains the decision and its limits.
