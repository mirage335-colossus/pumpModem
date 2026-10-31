# Three-carrier feasibility measurements — 31 October 2026

These are standalone software simulations of the
[experimental triad transmitter and receiver](../../../triad-modem.md).
The ordinary Robust receiver, wire format and GUI were not changed or qualified.
The tested source is uncommitted work based on
`3c24995f2745d08ec93250b61a08477a9abc1a6b`.
[manifest.json](manifest.json) records source/binary hashes, exact commands and
compressed evidence hashes. This is not publication, certification or promotion.

The package retains **805 sampled captures and 2,415 receiver evaluations** in
12 runs, about 134 KiB compressed. Captures normally contain four bits over
eight seconds; burst cases span 24 seconds, and the Rate-3600 cases contain
16 bits over eight seconds. The three receivers share the same capture in each
case. Some runs intentionally reuse seeds/cases for comparisons; these are not
2,415 independent statistical trials. Fixed fixture keys are published in the
source. The `--now` smoke run exercises the actual system-clock epoch path;
its logged epoch must be supplied to `Format::epoch` for an exact replay.

## Checks and measured limits

- `./build.sh test triad --jobs 2`: **2/2 passed**. Coverage includes exact
  source bits, all three noiseless receivers, mandatory diversity and margins,
  Added Symbols/interleave, burst waits, incomplete-capture rejection,
  deterministic thread/precision comparison, complete memory batching, exact
  None/Off bypass, RF/audio-clock scaling, CFO alias rejection, seeded channels,
  propagation tails and fractional-resampler endpoint tails.
- `ASAN_OPTIONS=detect_leaks=0 ./build.sh sanitize triad --jobs 2`:
  **2/2 passed** with address and undefined-behavior instrumentation.
  The original instrumented invocation failed during LeakSanitizer shutdown:
  `LeakSanitizer does not work under ptrace`. Leak coverage is **unverified**.
  [Release check output](focused-release.log) and
  [instrumented check output](focused-asan-ubsan.log) retain the final outcomes.
- A worst-position, fixed 10-ms erasure repeated every dwell removed
  5.02–5.27% of the four bits' shaped distinguishing-template energy (at most
  about 0.24 dB of that energy). This measures actual waveform differences,
  including pulse tails. It is not a universal decoder-SNR penalty bound.
- Tests do not establish ordinary Robust compatibility, physical completion,
  GUI progress, hardware timing, an emissions mask, LPI performance, or GPU
  execution. Those were outside this standalone request.

Initial development checks exposed and corrected partial-capture admission,
zero-Doppler bypass drift, missing fractional-interpolation tails, and native
audio being pulled into the experimental link. The final executables link only
the isolated experiment and OpenSSL; no ordinary receiver or audio provider is
linked. The original uncalibrated false-acceptance results below remain failures,
not repaired or reclassified passes.

## Sensitivity and propagation

SNR is signal mean transmit power divided by AWGN power in the ideal occupied
bandwidth, including cover power. Sample noise variance increases with sample
rate. There is no channel-output renormalization. Nominal Rate 1200 gives
200 chips/s/carrier and 750 Hz combined ideal bandwidth. The receivers use the
same samples, duration, format and empirical per-bit gates (score 0.03,
margin 0.005).

Accepted, completely correct AWGN captures, **16 seeds per cell**:

| SNR | Differential | Segmented complex | Amplitude only |
| ---: | ---: | ---: | ---: |
| +5 dB | 16/16 | 16/16 | 16/16 |
| 0 dB | 16/16 | 16/16 | 16/16 |
| −5 dB | 15/16 | 16/16 | 10/16 |
| −10 dB | 0/16 | 16/16 | 0/16 |
| −15 dB | 0/16 | 2/16 | 0/16 |

At −10 dB the raw error counts were respectively 19/64, 0/64 and 9/64 bits.
Thus **this differential algorithm is not uniformly more sensitive**. Its
noise-product penalty matters even though it avoids fine common-phase search.
At −5 dB, repeating a 10-ms erasure every dwell changed accepted-correct counts
to 11/16, 16/16 and 9/16; all three still had zero raw bit errors there. Added
Symbols ×2 with interleave and slow AGC gave 14/16, 16/16 and 8/16. These small
counts do not establish an AGC advantage or a precise erasure sensitivity cost.

Selected broader conditions, **four seeds per cell**, at 0 dB unless noted:

| Condition | Differential | Segmented complex | Amplitude only |
| --- | ---: | ---: | ---: |
| HF independent RF/audio errors; ±100 Hz, 135 hypotheses | 4/4 | 4/4 | 4/4 |
| HF 0.5-ms / 0.1-Hz spread, Each 65% perturbation | 4/4 | 2/4 | 4/4 |
| HF 6-ms / 10-Hz spread, Each 65% perturbation | 3/4 | 0/4 | 2/4 |
| HF 7-ms / 30-Hz spread, Each 65% perturbation | 1/4 | 1/4 | 2/4 |
| Keyed 50-channel HF FHSS | 4/4 | 4/4 | 4/4 |
| FHSS, 5-ms retune offset, 6-ms / 10-Hz spread, Each 65% | 4/4 | 1/4 | 3/4 |
| 5.8 GHz, ±11.652-kHz search, 935 hypotheses | 4/4 | 4/4 | 4/4 |
| Same microwave oscillator case, −5 dB | 4/4 | 2/4 | 2/4 |
| 10 rad/√s independent TX and RX phase diffusion | 4/4 | 1/4 | 4/4 |
| Static Doppler 43.7 Hz, coarse 25-Hz bank | 4/4 | 0/4 | 4/4 |

RF bounds are ±1 ppm **per end**, an explicit operational assumption rather
than the ordinary GUI's generic 100-ppm relative crystal profile. Actual HF
TX/RX RF offsets are +0.8/−0.2 ppm; microwave +0.8/−0.6 ppm. Audio clocks are
+37/−13 ppm. Wiener phase diffusion, affine drift, two-ray spread and the shared
additional selective perturbation are illustrative processes, not calibrated
device measurements. FHSS uses an idealized narrowband receiver-selectivity
model; echoes are not re-gated to recreate transmitter silence.

Finer search is a useful alternative: at −10 dB in four fresh HF oscillator
cases, segmented complex improved from **0/4 with 25-Hz bins to 4/4 with 1-Hz
bins**, using median 0.397→1.142 process CPU seconds. Differential was 0/4 with
both grids. This is not a calibrated operating point: the finer bank still
needs matching false-alarm qualification.

At Rate 3600 and 16 bits/frame (**2 bit/s, still eight seconds per bit**), the
two-seed 0-dB HF and microwave oscillator cases passed differential and
amplitude reception 2/2; coarse-grid segmented complex passed 0/2. Selective
HF and the spread/retune case failed all three. This is limited rate coverage,
not a general SSB-radio qualification.

## False acceptance is unresolved

Fresh **64-seed** microwave-width noise-only search, using the same 935-bank
search as the positive microwave cases:

| Receiver | False accepts | Conditional Wilson 95% interval |
| --- | ---: | ---: |
| Differential | 5/64 | 3.4–17.0% |
| Segmented complex | 2/64 | 0.9–10.7% |
| Amplitude only | 6/64 | 4.4–19.0% |

The broad run also includes matched wide-bank wrong-key and wrong-epoch controls.
The one-bank null/wrong-key/epoch sensitivity controls had no false accepts,
which plainly does **not** qualify the broader search. The format/receiver
must not be promoted on its positive examples alone. Score calibration, useful
bit-level admission evidence and a receiver choice that accounts for coherence
remain necessary before integration.

## Resources and spectrum

Host: AMD Ryzen 5 PRO 5650U, 12 logical threads reported; GCC 14.2.0, Release
`-O3`, separate rounding/no fast-math. For the 935-bank microwave search, three
matched captures per setting gave these differential medians:

| Workers / FFT arithmetic | Wall seconds | All-thread CPU seconds | Accounted DSP MiB |
| --- | ---: | ---: | ---: |
| 1 / FP32 automatic | 0.2303 | 0.2303 | 20.94 |
| 8 / FP32 automatic | 0.0617 | 0.4276 | 21.43 |
| 32 / FP32 automatic | 0.0581 | 0.5689 | 23.11 |
| 8 / forced FP64 | 0.0677 | 0.4598 | 42.54 |

All recovered the three captures. Thirty-two workers oversubscribe this host
and buy little extra latency; parallelism is not free CPU reduction. Storage
is contiguous frequency/cell/carrier projection matrices with parallel FFTs and
candidate dot products. There is **no executed GPU or BLAS backend**. These CPU
results neither predict GPU speed nor prove a benefit from allocating 75% RAM.
Working bytes exclude caller-owned captures, TX buffers, simulator state and
thread stacks; CPU/wall measurements exclude waveform generation/channel work.
Per-case channel wall time is retained separately.

At Rate 1200 the no-FHSS waveform's median 99%-power bandwidth was about
708 Hz, crest factor about 7.04 dB and outside-ideal-band energy about
`1.1e-5`. These are finite 4096-point Hann/Welch diagnostics of the transmitted
samples. Resolution changes with sample rate, so the wider microwave capture
has a coarser diagnostic; do not compare its leakage figure as a different
transmitter mask. Roll-off/spacing were chosen from the existing RRC convention,
not optimized across a full emissions/intercarrier-interference sweep. Noise-like
appearance and keyed cover do not demonstrate low intercept probability.

## Reproduce and inspect

Every retained JSONL is gzip-compressed without timestamps. For example:

```sh
./build.sh test triad --jobs 2
build/dev/triad_study --suite sensitivity --trials 16 --threads 8 > local-evidence/replay.jsonl
python3 tools/triad-summary.py local-evidence/replay.jsonl
python3 tools/triad-summary.py docs/validation-data/triad/2026-10-31/triad-wide-null.jsonl.gz
```

Use the exact commands in the manifest for other runs. Summarize reused cases
separately: the utility rejects duplicate experimental identities rather than
silently inflating a sample count. Its frame intervals assume independent noise
seeds within the reported condition; it reports raw bit errors without treating
correlated bits as independent Bernoulli trials. It also rejects incomplete logs.

No full ordinary Robust regression, platform campaign, GPU test, radio test,
release assurance, commit, push or publication was performed for this study.
