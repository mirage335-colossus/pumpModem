# Experimental three-carrier Robust modem

This is a separate sampled-I/Q transmitter, receiver and propagation experiment.
It does not change the ordinary Robust format, receiver, GUI, framing, pending
progress, or physical-completion rules. Its finite, locally configured frames
are measurement windows, not a new received-length protocol. It has no Helper
dependency and makes no claim of ordinary-receiver interoperability.

The requested minimum of eight seconds and twenty dwells is interpreted as
**20 × 400 ms**, consistent with the existing FHSS calendar. The written
“0.4 ms” would total only eight milliseconds.

## Format and design hypothesis

Three adjacent carriers carry simultaneous private amplitude/phase patterns.
The aggregate Rate retains the existing two-samples-per-pattern-chip convention:
each carrier gets one third of Rate, and its chip rate is Rate/6. Root-raised
cosine shaping uses roll-off 0.25 and a finite 16-chip span. Ideal carrier
spacing is 1.25 chip rates; at Rate 3600 this is 600 chips/s/carrier, 750 Hz
spacing, and 2250 Hz combined ideal occupied bandwidth. Finite pulse tails and
dwell windows require measured emission/leakage checks; the ideal number is
not a regulatory compliance measurement.

Every bit is distributed across at least twenty 400-ms dwells. A mandatory
diversity map moves each bit's cells around within each dwell. The transmitter
reserves 40 ms at either edge and keeps the complete RRC support inside this
aperture. This provides both ramp margin and tolerance of several milliseconds
of timing error. Geometries with insufficient cells are rejected.

Added Symbols repeats/expands complete pattern cells. Its optional interleave
permutes those cells without cutting a pattern symbol. This optional scrambling
of amplitude order addresses slow AGC; it is distinct from mandatory eight-second
diversity. Symbol Burst waits separate complete frames and cannot shorten a
bit's required twenty-dwell support.

The constellation permutes amplitudes `sqrt(0.4), 1, sqrt(1.6)` across the three
carriers, with eight keyed phase choices. Every triplet has the same total chip
energy. The
two bit candidates have different amplitude sequences as well as phase
sequences, so amplitude-only reception remains possible. Nonzero constellation
points do not guarantee a nonzero instantaneous composite envelope after pulse
shaping, carrier summation, or propagation. A sparse constellation and filtered
random patterns are noise-like; they are not mathematically Gaussian white
noise. Separately keyed cover fills unused integer chip positions, including
internal dwell margins when FHSS is off. FHSS applies a single smooth 20-ms
transmitter envelope before propagation. Complete transmission endpoints and
intentional burst waits remain visible; relationships between carriers can also
leave detectable statistics. Spectral appearance alone does not establish probability
of intercept.

The secret remains a shared cryptographic key. The system-clock epoch addresses
domain-separated experimental streams derived using the existing `Crypto`
implementation. Fixed epochs and fixture keys make experiments reproducible.
Time is not substituted for secret key material. Reuse of a key/epoch follows
the requested reuse assumption; this experiment does not add a nonce exchange
or authentication overhead.

## Receiver and independent channel

The receiver receives observed I/Q plus local format, key, epoch candidates and
declared frequency/timing/audio-clock search bounds. Transmitted bits and true
oscillator/channel/timing parameters belong only to the evaluator.

The candidate algorithms are:

- Complex differential: compare adjacent-carrier products with private pair
  templates, with short segment magnitudes allowing unknown channel phase.
  The third algebraically dependent pair is not counted as independent evidence.
- Segmented complex: correlate each carrier over bounded segments and combine
  their energies, fitting an unknown complex gain for each segment.
- Amplitude only: compare the amplitude patterns while allowing unknown gain.

All three first project the captured complex baseband onto shaped carrier cells.
The differential products are formed **after** this projection: sufficiently
fast phase changes within the pulse aperture can still destroy coherence.
Segmented complex uses the same declared frequency grid for matched comparisons;
its coarse-grid losses are not an optimized coherent-receiver sensitivity limit.
`--frequency-step` permits a finer bank without changing the transmitter.

The FFT for each cell is shared by all frequency candidates. Contiguous storage
is `[frequency][cell][carrier]`, with two candidate pattern rows per cell. FFT
projection and candidate dot-product reductions are parallel CPU tasks. Memory
batches repeat FFT work when necessary and count that work; they never silently
drop hypotheses. The default 256 MiB receiver cap is a ceiling, not an eager
allocation. The measured small cases do not justify consuming 75% of host RAM.
`--memory-mib` and `--threads` expose explicit limits. FFT arrays support FP32
and FP64; final reductions/coordinates use double precision. Automatic selects
FP32 only within the existing FFT safety envelope. There is no BLAS/GPU backend
or claimed GPU execution in this prototype.

Only completely captured local frame calendars may be admitted. Every bit must
pass the empirical score and margin gates; a frame average cannot certify a
weak bit. Those gates are deliberately exposed and remain uncalibrated for
production acquisition. EOF, known fixture bit count and correct plaintext are
not receiver completion or authentication evidence.

Common phase cancellation does not remove frequency capture, timing search,
sample-clock error, or frequency-dependent propagation. In particular, two
independent oscillators each bounded by 1 ppm require an RF-error allowance of
approximately ±11.6 kHz at 5.8 GHz before Doppler/drift allowances. A capture
sample rate must contain that range; a simulation must not alias it into a
narrower passband or use truth to recenter the receiver.

The independent channel uses the shared sampled `SpreadRay` and
`FrequencySelectivePerturbation` implementations. None/Off is an exact added-stage
bypass. Selected spread/modulation is never replaced with a symbol-only estimate.
Independent transmitter oscillator phase is applied before propagation;
receiver oscillator phase is applied afterward. Frequency error, drift, Wiener
phase diffusion and independent audio clocks are explicit assumptions, not
calibrated claims for particular radios or GPSDOs. The named `hf-free` and
`microwave-free` studies use a **±1 ppm per-end RF bound**, not the ordinary
GUI's much broader generic 100-ppm relative crystal profile. Their independent
audio clocks are +37/−13 ppm. This tests one plausible undisciplined RF scenario;
it does not establish feasibility for every crystal, synthesizer or sound card.
See [oscillator models](oscillator-models.md) for the separate ordinary profiles.

The transmitted waveform already contains its aperture/ramps. Delay echoes and
filter state persist across dwells, with full delayed and filter tails. The
channel does not impose a second transmitter gate. An explicit hop calendar
and receive-retune timing model use equivalent baseband mixing; RF paths outside
the capture passband must be accounted for as selectivity loss rather than
aliased. The present selectivity is an ideal hard rejection of hop mismatches
outside the occupied half-band, with in-band phase integration; it is not a
wideband analog retune/filter model. Additional frequency-selective amplitude perturbation is independent
of the two-ray model, including its documented possible polarity reversals.

## Scope of evidence

Run short, format-conforming cases before larger seed sweeps. Record BER, complete
frame recovery, null/wrong-key/wrong-epoch results, acquisition coordinates,
search work, memory, wall/process CPU time, SNR reference, and exact seeds. A
successful deterministic smoke case is not a sensitivity curve or a false-alarm
calibration. GPU-friendly work decomposition is not evidence of GPU execution.
Physical radios, regulatory masks, GPU performance, and comparison against the
ordinary Robust receiver remain separate validation work.

## Run the experiment

```sh
./build.sh test triad --jobs 2
build/dev/triad_study --suite edges --trials 1 --threads 8
build/dev/triad_study --suite broad --trials 4 --threads 8 > local-evidence/triad-broad.jsonl
build/dev/triad_study --suite sensitivity --trials 16 --threads 8 > local-evidence/triad-sensitivity.jsonl
python3 tools/triad-summary.py local-evidence/triad-sensitivity.jsonl
```

The CLI exposes `--case`, `--seed`, `--snr`, `--rate`, `--bits`,
`--bits-per-frame`, `--segment-cells`, `--frequency-step`, `--memory-mib`,
`--threads`, and `--arithmetic automatic|fp32|fp64`. `--now` addresses the keyed
stream with the current system-clock epoch; the default fixed fixture epoch and
published fixture key are for reproducibility, not operational secret storage.
`--help` lists syntax. Rate must be a multiple of 15 Hz for an exact integer
chip grid per 400-ms dwell, and infeasible bit/Added-Symbol geometry fails
explicitly. At Rate 3600, `--bits 16 --bits-per-frame 16` demonstrates 2 bit/s
while every bit still spans eight seconds.

SNR is input mean transmit power divided by complex AWGN power in the **ideal
occupied bandwidth**. Noise sample variance scales with capture sample rate;
increasing the microwave capture bandwidth does not create free sensitivity.
There is no post-channel renormalization. Receiver workspace excludes the
caller-owned capture, transmitted samples, and simulator storage. Receiver CPU
includes all its worker threads; generation/channel costs are separate.

The dated [measurement package](validation-data/triad/2026-10-31/README.md)
contains actual successes, failures and limits. These are software measurements,
not a release certification or physical-link qualification.

The shared-channel assumptions and limitations are in
[channel models](channel-models.md). Practical pulse truncation and roll-off
tradeoffs are discussed in [ITU-R SM.328-11](https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.328-11-200605-S%21%21PDF-E.pdf).
Block differential detection provides a useful alternative to raw noisy
products, but its published AWGN/fading results do not establish this format's
performance: [Divsalar et al., differential amplitude/phase detection](https://ieeexplore.ieee.org/document/275303).
