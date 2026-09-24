# 1.2 kHz planning at 170–230 dB attenuation

These results use the corrected **+3 dBm** transmit presets and the unchanged
short-message wire path: `a` is exactly `011`. The cheap `analyze-link` command
does not generate PCM or run the production receiver. Its statistical reference
results describe a hypothetical matched detector, separately from the current
receiver's coverage and fixed-laptop compute estimates.

## The requested -170 dB baseline

At 1,200 Hz, a -10 dB-Hz TX target selects 630.9575 seconds per bit. With
the free-running crystal model and the default 10 dB noise figure:

| Quantity | Result |
| --- | ---: |
| Received power | -167 dBm |
| Actual C/N0 | -3 dB-Hz |
| SNR in 1,200 Hz | -33.79 dB |
| Payload airtime, three bits | 31.55 minutes |
| Complete TX waveform | 31.60 minutes |
| Simulation, including a whole absent symbol and lookahead | 42.13 minutes |
| Fixed i9-13900H computation estimate | 24.36 minutes |
| Hypothetical RTX 4090 Laptop GPU estimate | 7.47 minutes |
| Included serial tracking component in each estimate | 5.52 minutes |

The former 18.85-minute CPU estimate omitted established-stream tracking.
The corrected model includes two remaining signal symbols and one fully
observed absent symbol. Hardware throughput assumptions were not benchmarked
or recalibrated. See [the compute model](simulation-estimates.md#fixed-reference-compute-model).

This remains an interesting candidate, but average compute below airtime does
not establish a usable live mode. The expanded FFT core needs about 287 MiB
per bank. Under the modeled half-workspace allowance, this case needs at least
575 MiB total for a single profile; other profiles/keys can need more. The
CLI defaults to 64 MiB, while the GUI derives its DSP allowance from available
memory. At `--dsp-mb 1024`, the modeled core fits.

The current receiver also batches acquisition in large FFT blocks: approximately
29.1 minutes for the first block and 18.6 minutes per subsequent hop from the
bank origin. Full-symbol tracking runs within that processing schedule. A
shorter total compute estimate cannot establish timely progress or prove that
the live capture queue will avoid overflow. Newly accepted bits still reach
the next progress poll; no acceptance or physical-end rule changed here.

A sampled seed-1 probe with a matching -10 dB-Hz RX target and 1 GiB DSP
workspace was stopped after a 240-second verification limit. It emitted no
result. Reception is **unverified**, not demonstrated to fail. This elapsed
time was not used to calibrate the reference hardware model.

```sh
./build/pump analyze-link --text a --bw 1200 --target-snr -10 \
  --simulation '3dBm -170dB' --oscillator crystal --dsp-mb 1024 \
  --trials 10000

# Actual sampled probe; the verification run was externally capped at 240 s.
./build/pump simulate --text a --bw 1200 --target-snr -10 \
  --receive-targets -10 --simulation '3dBm -170dB' --oscillator crystal \
  --dsp-mb 1024 --time 1800000000 --seed 1 --json
```

## What another 30 or 60 dB costs

Collected ideal coherent energy is `Es/N0 = 10^(C/N0 / 10) * T`. Keeping the
same power, noise density and required energy means another 30 dB of loss
requires 1,000 times the observation time; another 60 dB requires 1,000,000
times. A wide phase pattern lowers spectral power density without increasing
the total energy collected. The energy/bandwidth distinction is discussed in
[Messerschmitt's analysis](https://arxiv.org/pdf/1111.0547).

Two useful comparisons at the default 10 dB noise figure are:

| Attenuation | C/N0 | Three bits + absence at the planner's 18 dB ideal energy | Same 25 dB ideal energy as the user's baseline |
| --- | ---: | ---: | ---: |
| -170 dB | -3 dB-Hz | 8.39 minutes | 42.06 minutes |
| -200 dB | -33 dB-Hz | 5.83 days | 29.21 days |
| -230 dB | -63 dB-Hz | 15.96 years | about 80 years |

These calculations omit waveform tails and coherence losses. Neither energy
target is a universal capacity limit or a guarantee of detection. The current
receiver cannot simply scale to these lengths: the requested FFT workspace
becomes prohibitive and its 4,097-frequency cap can exclude the actual carrier.
An oscillator alone cannot supply the missing received energy.

## A conditional -200 dB candidate within one day

Use 21,590-second symbols: three data symbols plus one whole absent symbol
and existing tails fit in 86,364.25 seconds with the GPSDO/TCXO model. The
following cheap experiments use 100,000 trials, seed 1, one million prescribed
search alternatives and a total false-alarm budget of 1e-6. Timing and sample
clock are assumed acquired, residual carrier error is zero, and the two
templates are assumed orthogonal. These are **per-symbol reference detector
probabilities**, not GUI RX-success predictions or measured message reception.

| +3 dBm / -200 dB configuration | Correct detections |
| --- | ---: |
| 10 dB noise figure, free-running crystal, whole-symbol coherent | 0 / 100,000 |
| 10 dB noise figure, GPSDO/TCXO, whole-symbol coherent | 0.012% |
| 3 dB noise figure, GPSDO/TCXO, whole-symbol coherent | 97.460% |
| 2 dB noise figure, GPSDO/TCXO, whole-symbol coherent | 99.936% |
| 2 dB noise figure, crystal, three approximately two-hour segments | 98.269% |

The 2 dB noise-figure / TCXO case is a candidate for further receiver design.
Its model-only 95% Monte Carlo interval is 99.9183–99.9499% per symbol. Reducing
the effective receiver noise figure from 10 to 2 dB adds 8 dB of actual link
improvement; it requires a corresponding physical noise improvement. Selecting
the assumption does not create it. This keeps the model's thermal background;
external noise or interference can prevent the effective improvement even with
a quieter receiver front end. The oscillator scenarios are illustrative,
not measured hardware guarantees. Replacing random phase-path energy with its
mean also omits phase-fade variability, especially for long crystal segments.

```sh
./build/pump analyze-link --text a --bw 1200 \
  --simulation '3dBm -200dB' --oscillator gpsdo-tcxo \
  --symbol-seconds 21590 --coherent-seconds 21590 \
  --noise-figure-db 2 --trials 100000
```

The existing receiver reports **workspace unsupported** for this candidate,
so no current-receiver RX probability is available. Its corrected requested-work
CPU estimate is about **41.3 hours**, including 8.49 hours of serial tracking;
this is not an executable real-time claim. It still requests thousands
of carrier hypotheses; GPS discipline does not automatically provide exact
timing, clock, carrier and pattern acquisition.

At -230 dB, all tested day-long reference cases had zero detections, even
with ideal phase stability and a 0 dB noise figure. Zero events in 100,000
trials give a model-only 95% upper bound of about 0.00384%, not proof of an
impossibility. This case needs substantially more collected energy or a much
longer observation than the -200 dB candidate.

The production work needed next is bounded-window correlation with accumulated
full-symbol evidence and searches across frequency/clock drift trajectories.
Partial windows can guide accumulation but must never substitute for a fully
scored symbol when deciding absence. Coherent/noncoherent accumulation also
has a sensitivity tradeoff; see [ESA's baseband processing discussion](https://gssc.esa.int/navipedia/index.php/Baseband_Processing).
Such a receiver needs sampled signal and noise-only validation before these
conditional results can become credible RX estimates. No segmented receiver
or new wire format was introduced by this analysis.
