# Controlled OFDM receiver diagnostics

The exact same saved raw 64-QAM speaker/microphone captures and original mapper
bits were replayed with two frozen three-block-preamble receiver sources. The
only difference is noise calibration: `receiver-before-confidence.cpp` uses a
global post-equalization pilot-error floor; `receiver-after-confidence.cpp`
estimates nearby tracking-pilot residual power in received-bin units, then
divides by each data bin's channel power. Hard decisions and framing are
unchanged. The empirical unit-LLR-scale bit-log-loss information metric rises
from **4.4265 to 4.6075 bits/data tone at amplitude .20**, and **4.7112 to 5.0707
at amplitude .40**; coded BER remains **6.5410% and 4.3111%** respectively.
This isolates the practical confidence improvement. It is not a Shannon
capacity measurement, an optimized likelihood-scaling result, or a probability
estimate from repeated transfers.

The captures used 48 kHz processing, FFT 8,192, prefix 4,096, 500–18,000 Hz,
64-QAM, LDPC 3/4, depth 4, 127 raw intervals, seed 521, and stereo speaker output.
The per-bin and per-block CSVs compare received complex symbols with the exact
original mapped symbols, including the correct partial-label fill. Erased
positions contribute zero LLR. Unweighted EVM disproportionately weights deep
nulls; the bin-resolved BER and log loss are more useful here. Additional
`coded-a40-*` CSVs show deterioration over the older multi-cycle capture with no
channel refresh; `longcp-*` retains the older 32,768/8,192 trial. These historical
receivers predate the implemented 16-block independent-phase training and
per-cycle full-band refresh, which must not be used to replay their multi-cycle
wire format.

`input-hashes.json` identifies original PCM/bit files in `/tmp` and hashes the
historical analysis executables. This compact archive intentionally retains
CSV results and frozen source rather than duplicating the captured PCM. Full
reproduction needs the original inputs matching those hashes. The frozen
source is sufficient to rebuild the historical analysis without relying on
the current receiver implementation:

```sh
c++ -std=c++20 -O2 -Iinclude \
  docs/validation-data/fast/acoustic-capacity-20260920/receiver-diagnostics/acoustic-known-symbols.cpp \
  docs/validation-data/fast/acoustic-capacity-20260920/receiver-diagnostics/receiver-after-confidence.cpp \
  build/libdatapump_fast.a build/libdatapump.a build/third_party/xz/liblzma.a \
  -lcrypto -ldl -pthread -o /tmp/acoustic-confidence-frozen
/tmp/acoustic-confidence-frozen \
  /tmp/fast-acoustic-ofdm-raw64-a40.f32 \
  /tmp/fast-acoustic-ofdm-raw64-a40-bits.u8 /tmp/acoustic-frozen-check
cmp /tmp/acoustic-frozen-check-blocks.csv docs/validation-data/fast/acoustic-capacity-20260920/receiver-diagnostics/a40-variance-blocks.csv
cmp /tmp/acoustic-frozen-check-bins.csv docs/validation-data/fast/acoustic-capacity-20260920/receiver-diagnostics/a40-variance-bins.csv
```

Both comparisons passed when archived. Substitute the before-confidence source
and `a40-known-*` CSVs for the baseline, or the a20 input pair and CSVs for the
lower-amplitude trial. The offline harness never opens audio devices.
