# 1.2 kHz planning at 170–230 dB attenuation

These results use the corrected **+3 dBm** transmit presets and the unchanged
short-message wire path: `a` is exactly `011`. The cheap `analyze-link` command
does not generate PCM or run the production receiver. Its statistical reference
results describe a hypothetical matched detector, separately from the current
receiver's coverage and fixed-laptop compute estimates.
The numerical runs recorded below predate the production four-section detector.
Link powers, exact wire counts, airtimes and hypothetical detector experiments
remain useful references. Recorded CPU/RAM and sampled-probe results describe
that earlier implementation; rerun the commands for current work estimates.

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
| Historical i9-13900H computation estimate | 24.36 minutes |
| Historical hypothetical RTX 4090 Laptop GPU estimate | 7.47 minutes |
| Historical serial tracking component in each estimate | 5.52 minutes |

The still earlier 18.85-minute CPU estimate omitted established-stream tracking.
The recorded correction includes two remaining signal symbols and one fully
observed absent symbol. Current estimates additionally budget four-section
scoring and its bounded scratch. Hardware throughput assumptions were not
benchmarked or recalibrated. See
[the compute model](simulation-estimates.md#fixed-reference-compute-model).

This remains an interesting candidate, but average compute below airtime does
not establish a usable live mode. The recorded expanded FFT core needed about
287 MiB per bank, or 575 MiB total under the modeled half-workspace allowance.
Those figures predate section scratch; other profiles/keys can need more. The
CLI defaults to 64 MiB, while the GUI derives its DSP allowance from available
memory. The recorded core fit at `--dsp-mb 1024`.

The older acquisition schedule used approximately 29.1 minutes for the first
block and 18.6 minutes per subsequent hop from the bank origin. Current
acquisition begins after a complete observation window and at most one second
of candidate starts; established tracks score complete symbols independently
of later acquisition batches. Neither schedule makes total compute time a
guarantee of timely progress or capture-queue capacity. Newly accepted bits
still reach the next progress poll.

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

The recorded receiver reported **workspace unsupported** for this candidate,
so no receiver RX probability was available. Its historical requested-work
CPU estimate was about **41.3 hours**, including 8.49 hours of serial tracking;
this is not an executable real-time claim. It still requests thousands
of carrier hypotheses; GPS discipline does not automatically provide exact
timing, clock, carrier and pattern acquisition.

At -230 dB, all tested day-long reference cases had zero detections, even
with ideal phase stability and a 0 dB noise figure. Zero events in 100,000
trials give a model-only 95% upper bound of about 0.00384%, not proof of an
impossibility. This case needs substantially more collected energy or a much
longer observation than the -200 dB candidate.

The production receiver now adds four fixed sections for sufficiently long,
dense patterns, fitting separate amplitude and phase in each quarter. It keeps
the coherent branch and accounts for both the higher-rank section fit and
choosing between two detectors. The section score excludes the strongest
quarter so an isolated burst cannot supply all its evidence. Decisions still
require the whole symbol;
the wire format is unchanged. This is not the three-segment reference detector
in the table, nor an arbitrary drift-tracking receiver.

Each quarter must still be coherent. Shorter or adaptive sections and searches
over changing clock/frequency trajectories require further work and their own
validation. The current **RX estimate** models both implemented branches with
their shared noise, finite pattern correlation and detector-choice cost. Its
coherent-only comparison is available in expanded details. This statistical
model does not execute the complete adaptive receiver. Coherent/noncoherent accumulation also has a
sensitivity tradeoff; see
[ESA's baseband processing discussion](https://gssc.esa.int/navipedia/index.php/Baseband_Processing).
