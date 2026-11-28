# Sub-9 kHz FSK rate and spacing: calculations and decode-rate measurements (Fable-5.2 pass, 27 November 2026)

Standalone programs behind the
[sub-9 kHz rate and spacing note](../../../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md)
and section 12.2 of the [FSK specification draft](../../matrixModem-FSK.md). They are analysis
aids, not modem code: nothing here is linked into the application or depends on repository
sources.

**Purpose and retention.** `antenna_keying.cpp`, `coherent_block_mc.cpp` and `search_ceiling.py`
are theoretical calculations (the second with a Monte Carlo part); their outputs are the reference
for the specification's sub-9 kHz spacing, tone length and search figures. `decode_bench.cpp`,
`decode_gpu.cu` and `results/gpu/` are measurements with provenance. Keep them as research
material under the [research-material rule](../../../docs/development.md#research-material).

## `antenna_keying.cpp`

The 61 m (200 ft) antenna of the
[earlier antenna note](../../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md)
with 10 kW available. Lumped short-monopole model, single-tuned series circuit, constant-amplitude
drive.

| Section | What it computes |
| --- | --- |
| 1 | Bandwidth that 10 kW buys at a top-voltage limit, `B* = P / (2π·C·V²)`, with its time constant; radiated power and current at the limit; the earlier note's four loss cases against 10 kW; radiated power against bandwidth on the power-limited branch (closed form) |
| 2 | Keyed MFSK through the antenna: exact first-order envelope recursion over 40,000 keyed tones for eleven formats and eight bandwidths. Energy per tone, coherent sum with one phase for all positions (antenna unknown, and static response known), energy leaked into other positions, static phase at the outer position, and the cost of the keyed phase against phase-continuous tone changes |
| 3 | Slow detuning of the resonance: common phase and loss, and the coherent loss left across the span |
| 4 | Stored energy at 10 kW and the energy a keyed phase step exchanges, as power at four tone rates |

```bash
cd _appraisal/algorithms-matrixModem/sub9-fsk-rate
g++ -O2 -std=c++20 antenna_keying.cpp -o /tmp/antenna_keying
/tmp/antenna_keying > results/antenna_keying.md
```

About five minutes on one thread. Deterministic (fixed key sequence).

## `coherent_block_mc.cpp`

Monte Carlo of the specification's keyed 10-bit block (32 symbols of 256 values, keyed balanced
map onto `P` positions redrawn for every tone, keyed interleaver) for formats that differ only in
cell length (spacing = 1/cell), tone length and positions. All formats last 5,242.88 s and carry
the same energy. The receiver scores all 1,024 messages on every coherent window from one cell to
the whole block and adds windows as energies; it reports the block `E/N₀` at 1% wrong decodes per
window length, for the best window, and for a receiver that picks the most significant window per
block.

- Channels: steady; phase wander as an Ornstein–Uhlenbeck process (30° rms with 1 h correlation,
  60° with 10 min); frequency wander inside ±1, ±5 and ±50 mHz with knots 1/bound apart (the
  bounded Doppler law of the Sub9 presets, common wander only); Rayleigh fading with a Gaussian
  Doppler spectrum of 1, 5, 50 and 500 mHz.
- The channel is sampled 16 times per cell when the spread is not small against the spacing, so
  energy leaking into neighbouring positions is included.
- Arguments: blocks per row; format letters or `all`; channel digits or `all`; antenna −3 dB
  bandwidth in hertz (static response only) or 0; `1` to make the keyed map send every value to a
  position and one of four quarter turns (value-dependent phase).

```bash
g++ -O3 -march=native -std=c++20 -fopenmp coherent_block_mc.cpp -o /tmp/coherent_block_mc
/tmp/coherent_block_mc 2000 IBbCDEFfGH all > results/coherent_block_mc.md      # 35 min on 12 threads
/tmp/coherent_block_mc 1200 Aa 01259      > results/coherent_block_mc_fast.md  # 0.16 s tones, five channels
/tmp/coherent_block_mc 2000 KJLb 014 0 0  > results/few_positions_plain.md
/tmp/coherent_block_mc 2000 KJLb 014 0 1  > results/few_positions_value_phase.md
/tmp/coherent_block_mc 2000 BbFG 0 86     > results/antenna_86hz.md
/tmp/coherent_block_mc 2000 BbFG 0 24     > results/antenna_24hz.md
python3 -I summarize_mc.py results/coherent_block_mc_fast.md results/coherent_block_mc.md > results/coherent_block_summary.md
```

`results/coherent_block_summary.md` has one row per format and channel: the cell rung, per-tone
reception, the best window, the window picked per block, and the best the specification's default
ladder depth (1/64 of the block and per tone) would reach.

Limits: known timing and frequency; Gaussian noise (no sferics, no mains lines); message error
only (no presence test); antenna transients are in `antenna_keying.cpp`, not here; the wander
models are constructed, not fitted to a measured sub-9 kHz phase spectrum, which does not exist in
the sources found. With 2,000 blocks the 1% point rests on 20 errors: about ±0.2 dB, and ±0.3 dB
for the 1,200-block rows. The steady whole-block value reproduces the exact orthogonal reference
(12.9 dB against 12.87 dB).

## `decode_bench.cpp` and `decode_gpu.cu`

Cost of one block decode (all `2^k` messages of one hypothesis) for the specification's block
shape, 32 symbols of 256 values:

| Route | What it does | Lookups per message |
| --- | --- | --- |
| energy | Per-tone reception from one evidence table per symbol | 32 |
| fwht (CPU only) | The same scores by one fast Walsh–Hadamard transform | none |
| ladder | Coherent windows of 2, 4, 8, … tones up to the block, one maximum per rung | one per tone |
| table | The ladder restricted to `W` windows per block | 32·`W` |
| gemm (GPU only) | The `W`-window ladder as one FP16 matrix product on Tensor Cores, then a kernel for rung energies and maxima | dense product |

Every route has to return the planted message. Tables are synthetic 4-bit values.

```bash
g++ -O3 -march=native -std=c++20 -fopenmp decode_bench.cpp -o /tmp/decode_bench
/tmp/decode_bench 12 15 > results/decode_bench.md        # threads; lookups per measurement in 1e9
/tmp/decode_bench 6 10 > results/decode_bench_6threads.md
nvcc -O3 -std=c++17 -arch=sm_120 decode_gpu.cu -lcublas -o decode_gpu
./decode_gpu > decode_gpu.out
./decode_gpu sustain gemm64 8                            # inside nvidia-smi dmon for telemetry
```

The CPU program cuts each measurement into tasks of one (repetition, high half) under one dynamic
parallel loop, so every thread is busy at every block size and tone count: small blocks are
repeated, large ones are timed on a sample of high halves and scaled, and each row lists the
number of decodes actually timed. A first version parallelised only over the sampled high halves
and left threads idle at 8,192 and 32,768 tones; its outputs were replaced.

`results/decode_bench.md` and `results/decode_bench_6threads.md` were taken on the local Ryzen 5
PRO 5650U with the Monte Carlo paused; twelve and six threads gave the same sustained rate.
`results/gpu/` holds the run of 28 November 2026 01:53 UTC on a full RTX PRO 6000 Blackwell
Server Edition: `decode_gpu.out`, four telemetry captures (`tele_*.csv`), `decode_bench_pod.out`
(the CPU program on the pod's EPYC 9355 with 13 threads, its CPU quota) and `identity.out`
(device, driver, toolkit, host CPU, source and binary SHA-256). Two on-demand pods, 0.076 h and
0.095 h, about 0.46 USD together, both verified removed, no persistent volume; the stored
outputs are the second pod's, and the first pod's GPU figures agreed with them within 3%.

Limits: stock kernels without tuning; the GPU ladder kernel does a tenth of the table kernel's
lookups per second and can probably be improved; the GPU was timed for 24 bits only; the large
CPU cases are scaled from a timed sample; no transform stage, no accumulation stage and no real
tables.

## `search_ceiling.py`

Arithmetic on the measured rates and on the counting rules of calculation 18 of
[`../fsk-lattice/`](../fsk-lattice/README.md): decodes that fit into a block's own duration by tone
length, block size and device; hypotheses needed for five assumed reference classes; their ratio;
and the same ratio if frequency bins come from a transform across tone sums (derived, with an
assumed cost, not measured). It reads the decode times from the three result files, so it has to
be rerun after any new timing. Timing offsets follow the larger of an eighth of the tone and
1/(8 × span).

```bash
python3 -I search_ceiling.py > results/search_ceiling.md
```
