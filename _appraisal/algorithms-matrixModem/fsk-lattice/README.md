# FSK lattice calculations and GPU stage benchmarks (Fable-5.2 pass, 26–27 November 2026)

Standalone programs for calculations retained in the [dated FSK design study](../../../_ref/algorithms-matrixModem/fsk-design-study.md). They support
the [lattice design note](../../../_ref/algorithms-matrixModem/Fable-5.2/fsk-lattice-design.md) and
the [computation note](../../../_ref/algorithms-matrixModem/Fable-5.2/computation-and-simulation-speed.md).
They are analysis aids, not modem code: nothing here is linked into the application or depends on
repository sources.

**Purpose and retention.** `lattice_receiver.cpp` is a theoretical calculation with a Monte Carlo
part; its output is the reference for the specification's dwell, alphabet, quantization and
duration figures. `lattice_gpu.cu` and `results/gpu/` are a measurement with provenance. Keep both
as research material under the [research-material rule](../../../docs/development.md#research-material).
A native Matrix Modem simulator that reproduces these tables is a cross-check, not a reason to
remove them.

## `lattice_receiver.cpp`

Cell-domain model: complex baseband at 3,200 Hz, cell = 512 samples = 160 ms, bin = 6.25 Hz, one
tone per dwell, phase and fading independent from dwell to dwell (except section 10), Gaussian
noise, known timing.
It extends the method of [`../coherence-limits/`](../coherence-limits/README.md).

| Section | What it computes |
| --- | --- |
| 1 | Energy and weak-signal evidence captured by the 0.4 s dwell for three transmit envelopes and three receive windows (closed form) |
| 2 | Exact large-alphabet GMI gap to the peak-limited bound for dwell periods of 0.4, 4 and 40 s: per-cell single bin, dwell-coherent bin, spread-matched weighted periodogram, and the two-stage form with one or three cell bins; spread-mismatch table for the 0.4 s dwell |
| 3 | Efficiency of quantizing per-dwell evidence to 1–5 bits (closed form) |
| 4 | Monte Carlo GMI with 64 or 256 candidate tones on a fresh keyed placement per dwell, minimum spacing 1 or 5 positions, 1–64 dwells per symbol, 0.4 s dwell |
| 5 | The same for the 4 s dwell |
| 6 | Unknown Doppler shift within ±1 Hz: one fixed smeared weighting against a centre-hypothesis bank |
| 7 | Block duration estimate from a presence threshold (Gaussian accumulation, 32-dwell floor) |
| 8 | Sensitivity cost of reduced transforms (tone-grid bins only, one bin) and of shedding dwells. Not in the default set; run with the section argument `8`. Output: `results/lattice_receiver_section8.md` |
| 9 | Spreads of 100–500 Hz with the 0.4 s dwell (large alphabet and Monte Carlo), and the same dwell shape with cells from 40 ms to 11 minutes on channels from phase-stable to 100 Hz. Run with `9`. Output: `results/lattice_receiver_section9.md` |
| 10 | FHSS off. (a) Dwells back to back without the guard. (b) Coherent accumulation over 1 to 65,536 dwells on channels from phase-stable to 1 Hz. (c) Coherence per tone position only. (d) Monte Carlo of the 10-bit keyed block on 2, 4, 8, 16 and 256 positions with a keyed balanced value-to-position map, decoded coherently, against the exact orthogonal reference. (e) Block duration under a chi-square presence test for per-dwell reception and coherent windows. (f) Threshold against hypothesis count, and antenna roll-off with few positions. Run with `A`; 24 s. Output: `results/lattice_receiver_section10.md` |
| 11 | FHSS with a new phase in every dwell. (a) Energy per dwell and gap on a steady path for dwell periods of 0.4 to 4,000 s. (b) Block duration against dwell period on fading paths, with the dwell centre known or drawn afresh within ±0.1 Hz or ±1 Hz for every dwell. (c) Monte Carlo of the 10-bit block of 4 to 128 dwells on a steady path with 1 to 1,000 possible centres per dwell, against the exact chi-square values; the dwell and block times that follow; block time on the dwell ladder. (d) An observer without the key: the margin of a matched filter bank over 352 to 3.5 million cells and of an energy detector, against the number of dwells per block. Run with `B`; about 1.5 minutes. Output: `results/lattice_receiver_section11.md` |
| 12 | A keyed quarter-turn phase in every cell of a dwell. (a) Loss of the keyed receiver against timing error, timing grid and path delay (closed form). (b) Extra C/N₀ an observer without the key needs for the 32-dwell block: plain tone, and with the phase key by the largest sum of cell energies over a dwell (exact chi-square), by all the block's energy, by squaring a two-phase key and by the fourth power of a four-phase key (Gaussian sums). (c) Block energy if the receiver did not know the phase changes. (d) The line a plain tone leaves on few positions without FHSS. (e) The timing search: spacing as a fraction of the coherent stretch for plain and keyed dwells, rate hypotheses against clock error for the 32-dwell block, and the observer's margin against a phase interval of 1 to 8 cells. Run with `C`; under a second. Output: `results/lattice_receiver_section12.md` |
| 13 | Tone length: one cell per tone against a tone held for the hop, with 0.4 s and 4 s hops and without FHSS (2 and 24 cells). Gap to the bound and 10-bit block duration. Run with `D`; under a second. Output: `results/lattice_receiver_section13.md` |
| 14 | One-cell tones on 352 keyed positions with keyed hopping: extra C/N₀ an observer without the key needs, against hop length (0.4 to 4,000 s) and hop channels (1 to 10,000), at 0, −10 and −20 dB-Hz. Gaussian deflection; marks where the weak-tone formula stops. Also the hopping benefit given back against hops per block per channel, the longest hop that keeps it, and the time a hop loses to its edges. Run with `E`; 3 s. Output: `results/lattice_receiver_section14.md` |
| 15 | Ten bits against one bit at the same confidence: deflection and exact block energy for 1, 10, 16 and 24 bits under the presence test, for 1, 400 and 4,000 hypotheses. Run with `F`; under a second. Output: `results/lattice_receiver_section15.md` |
| 16 | Hops a block has to span to survive losing a quarter, half or three quarters of them, against the margin, exact binomial. Run with `G`; under a second. Output: `results/lattice_receiver_section16.md` |
| 17 | Tones without a keyed phase: the cell-rate pulse an observer can add coherently without knowing the positions. (a) Energy per tone needed against observation time, exact, with the carrier frequency known and with 10,000 frequency bins, against the energy observer of section 14. (b) Observer margin over one block. (c) Time-domain Monte Carlo of the pulse and squared-pulse statistics with no key, a two-phase key and a four-phase key. Run with `600 H`; 35 s. Output: `results/lattice_receiver_section17.md` |
| 18 | Below 9 kHz: frequency-bin and drift-rate spacing rules, bins per coherent window for six assumed reference classes, and decodes needed per second against the measured decode rates. Run with `I`; under a second. Output: `results/lattice_receiver_section18.md` |
| 19 | [One-bit and ten-bit presence at selectable null-block budgets](results/admission_tradeoff.md). Compares common budgets and a policy that keeps one-bit admission at 10⁻⁶ while relaxing ten-bit admission. Run with `1 J`. |
| 20 | [Attended HF per-tone duration](results/attended_hf_durations.md). Rounds ten-bit allocations to 32-symbol repeat groups and occupied hop periods for two FHSS profiles. Run with `1 K`. |
| 21 | [Attended steady-path and coherent-window duration](results/attended_coherent_duration.md). Recalculates the steady-path reference under the stated reception and calendar assumptions. Run with `1 L`. |

```bash
cd _appraisal/algorithms-matrixModem/fsk-lattice
g++ -O3 -march=native -std=c++20 -fopenmp lattice_receiver.cpp -o /tmp/lattice_receiver
/tmp/lattice_receiver 20000 > results/lattice_receiver.md
/tmp/lattice_receiver 1 K > results/attended_hf_durations.md
/tmp/lattice_receiver 1 L > results/attended_coherent_duration.md
```

Regenerate the selectable-budget table with `/tmp/lattice_receiver 1 J > results/admission_tradeoff.md`.
It uses the section 15 Gaussian weak-tone and steady-path chi-square calculations. The denominator
of the false-admission target is one completely searched null block. Any additional public
searches must be included in its hypothesis count. A true-template miss of 10⁻³ sizes the
presence threshold. Successful recovery of the transmitted value also depends on competitors'
scores. The table gives idealized energy and duration ratios before dwell floors or rounding to the
agreed transmit schedule.

Section 20 applies the spread-matched per-tone model of section 13b to the HF geometry in
[the historical sizing calculation](../../../_ref/algorithms-matrixModem/fsk-design-study.md#10-sizing-and-expected-duration).
It compares the earlier per-tone allowance of 10⁻⁶ with 10⁻⁴ and 5 × 10⁻⁵ using identical
block and hop rounding in all three columns. The last allowance reserves the other half of
the attended target for coherent attempts, matching the split in the
[sample-level receiver experiment](../block-reception/README.md#one-false-admission-per-ten-thousand-quiet-blocks).
Only the per-tone branch contributes to section 20's signal model. Every row gives the
repeats per symbol, occupied hops and elapsed seconds, including the last hop's edges
and any unused flat slots. Continuous fading across tones and errors from a competing
word require separate checks. The 400 nuisance hypotheses are held fixed across durations.
A search whose timing or frequency-drift grid grows with block length needs a fresh count
and calibration. The original section 13b retains its 32-tone floor.

A direct cosine sum of the triangularly weighted channel covariance at every sample lag,
the matched-metric expression `2 Σ(λ − log1p(λ))` and independent normal quantiles
reproduced all 64 per-tone metrics and all 192 repeat, hop and duration choices in
section 20. Of those choices, 39 reached the 32-hop floor and 21 used a partially filled
final hop. Applying section 13b's coarser numerical integration at the same budgets
and on the same calendar changed the per-tone metric by at most 0.052%. Integer rounding
then changed 36 durations, with a largest difference of 0.70%. The new table uses the
full sampled aperture.

With 400 public nuisance hypotheses per payload, the ten-bit threshold at 10⁻⁴ calls for
3.1–5.4% more energy than one bit at 10⁻⁶ on a steady path, depending on the look count.
The weak-tone model gives 5.9% more unrounded airtime. Equal per-template tail allowances
occur at a ten-bit target of 5.12 × 10⁻⁴ because the ten-bit search has 512 times as many
payloads. At 10⁻³ the ordering reverses for all three hypothesis counts examined.

A second argument selects sections, for example `/tmp/lattice_receiver 20000 67`. The stored
output took 4 min 44 s on the 12-thread Ryzen 5 PRO 5650U while other jobs were running. Sections
1–3, 6 and 7 are deterministic. Sections 4 and 5 use 20,000 symbol trials per point (fewer for many
dwells per symbol) with one generator per thread, so values move within their scatter with the
thread count: about ±0.1 dB where a symbol carries more than half a bit and up to ±0.5 dB in the
low-rate 10 and 30 Hz rows. Section 10d uses 20,000 blocks per row; its 1% column repeats within
0.03 dB between runs and its 0.1% column rests on about 20 errors per point. Section 11c uses
20,000 blocks per point at a miss rate of 10⁻³ (about 20 events, ±0.1 dB) and a threshold bound
that reads 0.3–0.4 dB high; section 11d resamples 20,000 blocks from pools of 2²⁰ per-dwell
statistics. Sections 11c and 11d repeat exactly for a fixed thread count.

Conventions: `N0 = 1`, received C/N₀ `P` in hertz, Doppler spreads as F.1487 `2σ`. A gap is
`10·log10(R_bound·D / GMI)`: extra transmit time at the same power, guard time included. Limits:
known timing; ideal coding; no interference; no acquisition; sections 7 and 10e are estimates, not
coded simulations; section 10 assumes the frequency known to the window's resolution and a path
phase that is steady over each window; section 11d is an idealised observer on a steady path.

## `lattice_gpu.cu`

<a id="gpu-run"></a>Stage benchmarks on an NVIDIA GPU: the dwell transform as cuFFT or as one
matrix product (FP16 or INT8 operands), an exhaustive 24-bit block decode in four forms, a
sample-level simulation from noise generation to 4-bit evidence, the transform routes against
the batch size with a CPU route added, and the transform routes under a concurrent search with a
probe-and-exploit race. Each mode verifies its routes
against each other or a host reference before timing.

```sh
nvcc -O3 -std=c++17 -arch=sm_120 -Xcompiler -pthread -Xcompiler -march=native lattice_gpu.cu -lcufft -lcublas -lcublasLt -lcurand -lpthread -o lattice_gpu
./lattice_gpu transform 32768 704     # also 352 and 128 bins
./lattice_gpu decode 16               # also 64 hypotheses per call
./lattice_gpu simulate 32768
./lattice_gpu chunks 13               # argument: host threads of the CPU route
./lattice_gpu contend 13
./lattice_gpu schedule 13             # running scheduler test: fixed rules against the cost-based rule
./lattice_gpu sustain dft16h 10       # routes: fft dft16 dft16h dft8 dft8c4 gemm16 gemm8 direct fwht
```

For telemetry, run a `sustain` route inside
`nvidia-smi dmon --format csv --gpm-metrics 1,2,3,5,7,10,12 -o T -d 1` and use the interior
one-second samples.

`results/gpu/` holds the outputs of the run of 26–27 November 2026 (UTC) on a full RTX PRO 6000
Blackwell Server Edition: `transform_704.out`, `transform_352.out`, `transform_128.out`,
`decode_16.out`, `decode_64.out`, `simulate.out`, six `tele_*.csv` telemetry captures and
`identity.out` (device, driver, toolkit, host CPU, source and binary SHA-256). Device, cost and
method are in the [computation note](../../../_ref/algorithms-matrixModem/Fable-5.2/computation-and-simulation-speed.md#the-run).
The telemetry captures were taken with the first build of the same source, before the `simulate`
mode was added; the timing files were regenerated with that run's final binary.

A second run on the same device type added `chunks.out`, `contend.out` and `identity_run2.out`
(source and binary SHA-256 of the build with the `chunks` and `contend` modes; host quota 13.6
CPUs). A third run added `schedule.out` and `identity_run3.out` (the build with the `schedule`
mode; host EPYC 9335, same quota). The stored `lattice_gpu.cu` is that last source; the earlier
modes are unchanged apart from an optional stream argument on their kernel launches.

## `scheduling_model.py`

Arithmetic on the measured rates: whether a shared GPU is work-conserving, and the time three
scheduling rules take to clear four workloads on the measured device and on a hypothetical GPU a
hundred times slower. Theoretical; not a running scheduler.

```bash
python3 -I scheduling_model.py > results/scheduling_model.md
```

Limits: synthetic data, stock cuBLAS and cuFFT, no fused kernels, one device. The decode test uses
64 tones and 24 symbols, not the specification's 256 and 32.
