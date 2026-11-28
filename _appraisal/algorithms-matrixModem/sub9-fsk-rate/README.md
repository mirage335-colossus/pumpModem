# Sub-9 kHz FSK rate and spacing: calculations and decode-rate measurements (Fable-5.2 pass, 27–28 November 2026)

Standalone programs behind the
[sub-9 kHz rate and spacing note](../../../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md)
and section 12.2 of the [FSK specification draft](../../matrixModem-FSK.md). They are analysis
aids, not modem code: nothing here is linked into the application or depends on repository
sources.

**Purpose and retention.** `antenna_keying.cpp`, `phase_slew.cpp`, `coherent_block_mc.cpp` and
`search_ceiling.py` are theoretical calculations (the third with a Monte Carlo part); their
outputs are the reference for the specification's sub-9 kHz spacing, tone length, phase ramp and
search figures. `decode_bench.cpp`, `decode_gpu.cu`, `search_bench.cpp`, `search_gpu.cu`,
`hypothesis_pass_bench.cpp` and `results/gpu/` are measurements with provenance;
`search_margins.py` is arithmetic on them. [`literature-calcs/`](literature-calcs/README.md) holds the scratch calculations of
the delegated literature searches. Keep them as research material under the
[research-material rule](../../../docs/development.md#research-material).

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

## `phase_slew.cpp`

The keyed phase reached by a linear ramp instead of a step, through the same single-tuned
antenna model, for sixteen positions 0.78125 Hz apart centred on the resonance. Added on
28 November when the specification adopted the ramp; it replaces the hand estimate of the first
version of the note.

| Part | What it computes |
| --- | --- |
| Static reference | Angle between drive and current, and standing-wave ratio against the resonant resistance, for a steady tone on an outer position |
| Receiver | Coherent loss with one phase for all positions, and mean energy per tone, for ramps of 0, 20, 40, 80 and 160 ms, antennas of 24, 57, 86 and 174 Hz and tones of 10.24 s and 1.28 s: for a reference that follows the ramp, one that assumes a plain step, and one that leaves the ramp interval out |
| Transmitter | Largest angle between drive and current, lowest delivered power, energy returned to the source per tone, largest standing-wave ratio, for a half turn in a keyed direction and for one towards the centre of the positions |
| Closed forms | Reverse-power time and returned energy after a plain half turn |

```bash
g++ -O3 -march=native -std=c++20 -fopenmp phase_slew.cpp -o /tmp/phase_slew
/tmp/phase_slew > results/phase_slew.md
```

Two minutes on 12 threads. Deterministic. With no ramp it reproduces `antenna_keying`'s coherent
losses (0.73 and 0.07 dB for 10.24 s tones on 24 and 86 Hz antennas).

Limits: a constant-amplitude source and one tuned circuit; no transmitter, matching network,
protection or voltage limit; "standing-wave ratio" is the ratio the load `R·x/y` would give
against `R`, an indicator and not a measurement; a linear ramp only.

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

## `search_bench.cpp` and `search_gpu.cu`

The whole hypothesis search of one block of the adopted configuration (512 tones, 24 bits),
organised so that the frequency and drift hypotheses are not an outer loop of decodes. Added on
28 November after the user asked why the search had not been put on Tensor Cores: the first
benchmarks had run a full decode per hypothesis and had tried Tensor Cores only on the search
over messages.

Per message and timing offset: gather the bin value of the position the message puts each tone
on (one lookup per tone); for the whole block de-chirp for each drift hypothesis and transform
across the tones (2 × 512 points, 174 bins kept); for each shorter rung transform every window on
its own tones and keep the bins that rung needs; an epilogue forms energies, adds a rung's
windows and keeps the best. Windows of eight tones and fewer are evaluated directly.

| Route | Where | The transform step |
| --- | --- | --- |
| CPU | `search_bench.cpp` | FFTW single precision when built with `-DUSE_FFTW` (prototypes declared in the file, because only the runtime library is on the host), otherwise the program's plain radix-2 loop |
| gemm | `search_gpu.cu` | One FP16 matrix product on Tensor Cores for all rungs: messages × 2·tones times 2·tones × 2·rows, where a row is one (rung, window, bin, drift); also with 8-bit integer operands, and with the three shortest rungs left to a direct kernel |
| fft | `search_gpu.cu` | cuFFT per rung after a fill kernel (de-chirp, zero-pad), shortest rungs by the direct kernel |

Every route has to return the planted message, frequency bin and drift on the whole-block rung.

```bash
g++ -O3 -march=native -std=c++20 -fopenmp -DUSE_FFTW search_bench.cpp -l:libfftw3f.so.3 -o /tmp/search_bench_fftw
g++ -O3 -march=native -std=c++20 -fopenmp search_bench.cpp -o /tmp/search_bench
{ /tmp/search_bench_fftw 12 512 174 960; /tmp/search_bench_fftw 6 512 174 480; /tmp/search_bench 12 512 174 480;
  /tmp/search_bench_fftw 12 128 174 2048; /tmp/search_bench_fftw 12 2048 174 128; } > results/search_bench.md
nvcc -O3 -std=c++17 -arch=sm_120 search_gpu.cu -lcublas -lcufft -o search_gpu
./search_gpu > search_gpu.out                 # 174 bins, batches of 131,072 messages
./search_gpu 512 696 8 > search_gpu_696bins.out   # scaling check: 8,520 rows, product route only
./search_gpu sustain gemm 9                   # also gemm8, fft; inside nvidia-smi dmon for telemetry
```

`results/search_bench.md` holds the five laptop runs (the stored file separates them with
rules). `results/gpu/` holds the run of 28 November 2026 04:13 UTC on a full RTX PRO 6000
Blackwell Server Edition: `search_gpu.out`, `search_gpu_696bins.out`, `tele_search_gemm.csv`,
`tele_search_gemm8.csv`, `tele_search_fft.csv`, `search_bench_pod.out` (the CPU program with the
plain transform on the pod's EPYC 9554, 27 threads, its CPU quota) and `identity_search.out`.
One on-demand pod, 0.116 h, about 0.31 USD, verified removed, no persistent volume. The GPU
program compiled and returned the planted signal on its first run; a second run after a change
that only added the scaling check gave the same times within 1%, and is the stored one.

Limits: synthetic 4-bit bin values and one planted signal; stock kernels (the library's general
matrix product and FFT, a plain epilogue); one timing offset timed and multiplied by twenty;
forming the bin values per timing offset and the presence test are not included; at the two
rungs below the whole block a drifting reference moves the signal between windows by up to 1.5
bins, which a real epilogue covers by summing along tilted paths; shorter tones than 10.24 s
would be summed into sub-blocks before the transform, which is not built (the 2,048-tone run
shows the cost without it); 16-bit and 10-bit figures are the 24-bit time scaled.

## `hypothesis_pass_bench.cpp`

Hypotheses whose evidence differs per hypothesis (spread bank, timing offsets of per-tone
reception, fine timing steps of a wide-band coherent attempt) scored in one pass over all 2²⁴
messages with vector table entries, against one pass per hypothesis. It tests rule 2 of the
specification's section 13.1 for the dimensions that are not a transform. For 7, 56 and 350
hypotheses, with real and with complex entries; the planted message has to win for every
hypothesis.

```bash
g++ -O3 -march=native -std=c++20 -fopenmp hypothesis_pass_bench.cpp -o /tmp/hypothesis_pass_bench
/tmp/hypothesis_pass_bench 12 > results/hypothesis_pass_bench.md
```

Under a minute on 12 threads. Both sides run symbols in the outer loop and a chunk of
consecutive messages inside, which is the faster order for both; the pass per hypothesis is the
per-tone kernel of `decode_bench.cpp` and is timed on eight hypotheses and multiplied. A first
version compared against a slower per-message loop and overstated the gain by about three; its
output was not kept.

Limits: laptop CPU only, not a GPU; synthetic 16-bit entries; the tables for 350 hypotheses
(5.7 and 11.4 MB) sit in the last-level cache of this CPU and would not on a smaller one.

## `search_margins.py`

Block duration divided by search time for five assumed reference classes and 24, 16 and 10 bits,
for a decode per hypothesis and for the transform search, on the laptop and the GPU. Reads the
measured times from `results/search_bench.md`, `results/gpu/search_gpu.out`,
`results/gpu/search_gpu_696bins.out`, `results/decode_bench.md` and
`results/gpu/decode_gpu.out`. Only the ±1 ppm class at 24 bits is a measurement; the scaling
rules for the others are in the head of the file. For the adopted configuration it supersedes
section 3 of `search_ceiling.py`, whose margins assume a decode per hypothesis.

```bash
python3 -I search_margins.py > results/search_margins.md
```

## `literature-calcs/`

Six small programs the four delegated literature searches wrote for their own calculations,
kept unchanged below a provenance header, with rerun outputs. See its
[README](literature-calcs/README.md). The searches' reports are in
[`reviews/`](../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/).

## `search_ceiling.py`

**Its margins assume one full decode per hypothesis.** That is what a naive search costs, and
`search_margins.py` replaces it for the adopted configuration.

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
