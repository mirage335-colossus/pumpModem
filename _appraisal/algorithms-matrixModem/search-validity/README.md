# Coherent search geometry checks

The deterministic [C++ reference](search_validity.cpp) checks the frequency and drift coordinates used by the [native search kernels](../sub9-fsk-rate/search_geometry.hpp). It integrates in seconds and hertz independently of the templates. The aperture checks use time samples and the timing check includes actual adjacent tone samples. The model uses a known tone sequence, rectangular windows and unit amplitude. There are no noise trials or receiver-capacity measurements in this calculation.

Run from the repository root.

```sh
g++ -O2 -Wall -Wextra -pedantic -std=c++20 _appraisal/algorithms-matrixModem/search-validity/search_validity.cpp -o /tmp/mm-search-validity
/tmp/mm-search-validity
g++ -O2 -Wall -Wextra -std=c++20 -fopenmp _appraisal/algorithms-matrixModem/sub9-fsk-rate/search_bench.cpp -o /tmp/mm-search
/tmp/mm-search 1 512 174 1 2
g++ -O2 -Wall -Wextra -std=c++20 -fopenmp -DUSE_FFTW _appraisal/algorithms-matrixModem/sub9-fsk-rate/search_bench.cpp -l:libfftw3f.so.3 -o /tmp/mm-search-fftw
/tmp/mm-search-fftw 1 512 174 1 2
```

Both CPU search routes recovered message `7f3a79`, frequency array index 66 (printed as −21), drift index 2 and timing index 1 among 4,096 candidate messages with two synthetic timing offsets. Both also recovered their planted cases for the odd 173-bin grid and the 2,048-tone/512-sub-block case. The small FFT and FFTW four- and eight-sub-block routes recovered their planted drift. Two symmetrically placed samples have identical squared time. A diagnostic with 512 tones, two sub-blocks and one frequency correctly recovered the message but tied all four drift rows, selecting row 0 instead of planted row 2. Both routes now reject that geometry before execution. Each invocation is a geometry check. The programs also print extrapolations from these short runs that have no performance-qualification role here.

The reference returned unity power at both physical drift extremes. Its separate half-cell frequency and drift losses were 0.9121 and 0.9094 dB, and the joint corner was 1.7522 dB. At the last tone of the extreme tested trajectory the complex difference between a 4,096-sample full-chirp integral and the simulator's within-tone model was 0.0000057 for the selected position and below 0.0000003 for both adjacent positions. All tolerances are in the source. See the [interpretation and counterexamples](../../../_ref/algorithms-matrixModem/fsk-search-validity.md) and the [separate device experiment](device-validation.md).
