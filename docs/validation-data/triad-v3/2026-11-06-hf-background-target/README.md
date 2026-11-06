# HF background-noise target study

This is a focused single-slot study of HF-channel reception in the isolated
three-carrier Robust case, after the already channel-shaped real-audio waveform
is given additional shared full-rate DAC noise. The DATA and Helper source
powers are 4 W and 40 W in the physical fixture, with 180 dB path loss. The
added audio noise is a laboratory impairment: it does not preserve that 4 W +
40 W radiated-power budget, and it is not noise propagated through the
simulated HF channel.

The short-bit fixture uses the public Rate 36 / Added ×10 interleaved whole-bit
format, encrypted DATA source and Helper metadata, 6 kHz complex source and
48 kHz stereo audio, 30 MHz RF, 1725 Hz IF, automatic shaping and FHSS off. The
free-running crystal base and IC-7100 shift references are independent. The
`hf-worst` margin-3 preset has 90 Hz Doppler spread and 21 ms delay. The
stochastic fixed-time Gaussian channel uses per-ray `B=0.5 Hz`, a 0.2 µs
(`0.0002 ms`) delay excursion and 20-second knots; `each-65`
selective perturbation is enabled. Arrival uncertainty is ±7.035 seconds. The
4:40 DATA:Helper power ratio is unchanged. No receiver-side channel-state truth
or phase CSI is supplied.

## Results and scope

The exporter’s physical DATA C/N0 is 20.0206 dB-Hz. After conversion to real
audio and addition of full-rate shared AES noise, independent quiet-interval
PSD metrology measured 7.96 and 7.91 dB-Hz DATA-equivalent in the two software
replays, and 7.919 and 7.897 dB-Hz in the two short live captures;
pilot-band equivalents were 7.72–8.04 dB-Hz. The nominal noise targets were
8 dB-Hz DATA and 18 dB-Hz Helper. Full-rate 48 kHz audio-noise variance
receipts were within 0.08% of the requested variance. The noise was added in
software across the full audio band after real-audio conversion and before the
DAC; no hardware mixer settings were changed. Noise metrology sampled DATA,
pilot and upper-audio bands. All runs had zero digital DAC clipping, zero ADC
near-full-scale samples and no audio gaps. These are finite-sample software
checks; they do not bound analog distortion or qualify a physical noise null.
The separate S16 diagnostic found DATA quantization error energy 47.11 dB below
DATA energy, with nominal DATA at 98.3 DAC codes RMS. This paired full-signal
minus Helper-only result shows that the added noise did not hide a large
pre-noise DATA quantization loss; it is not a receiver result.

For software replay-0 and replay-1, post-score paired DATA subtraction measured
realized DATA C/N0 at 9.385 and 8.189 dB-Hz respectively. These controls checked
60 seconds of identical quiet-prefix channel/noise trajectory, then divided
full DATA energy including tails by the public 23.2-second active duration.
This evaluator measurement did not enter reception. For the live captures,
realized DATA C/N0 is an estimate from known matched simulated DATA energy,
scalar cable gain 0.8414025 measured at 1737 Hz, and quiet ADC PSD. It is not
direct isolation of DATA energy in the live capture. Unknown frequency-
dependent response, distortion and calibration drift are not fully bounded.
The resulting estimates are 9.344 dB-Hz (live-0) and 8.175 dB-Hz (live-1).
Live noise uses fresh AES samples, distinct from the paired replay noise, so
these comparisons do not establish that hardware caused the miss or prove a
causal duration effect.

The two noisy software replays returned the seeded bits correctly: 2 correct,
0 wrong, 0 missed. One Helper-only null replay produced 0 conditional
admissions in 1 trial. These exact counts are a small functional check, not a
statistical false-alarm qualification. The receiver reports
`physical_null_qualified: false`; its conditional Gaussian-model flag does not
qualify the actual post-DDC audio-noise null.

The two short-format live captures produced one observed correct conditional
reception and one miss. Live-0 returned candidate bit 0 and passed the
conditional score (`log-e lower bound = 50.521`, threshold `21.493`), with
estimated faded DATA C/N0 of 9.344 dB-Hz. Live-1 selected candidate bit 1,
which matched truth, but failed operational admission: the raw receiver score was
29.836, while the operational log-e lower bound was only 8.425451 against the
21.493 threshold. This is a missed reception, not a wrong-bit decode. Its
nominal quiet DATA-equivalent PSD was 7.897 dB-Hz; the estimated faded DATA
C/N0 was 8.175 dB-Hz. These are 1/2 observed live receptions, not a
physical-null or false-admission qualification. Each live capture used its
corresponding seeded software fixture channel realization (signal-0 or
signal-1) and fresh AES noise. Both ADC captures had no audio gaps and zero
near-full-scale samples; their noise receipts reported zero digital DAC
clipping. Analog distortion below full scale is not bounded here.

An independent longer-format trial uses 34.8 seconds of DATA, seed
`2026112404`, bit 1 and arrival 2 seconds. Before scoring, a frozen
matched-noise calibration found this channel adds 0.309112 dB; it set the
nominal target to 7.690888 dB-Hz. The completed software replay measured
7.682431 dB-Hz nominal and 7.991543 dB-Hz calibrated DATA C/N0. It returned
the correct bit with operational log-e lower bound 30.743081, above the
unchanged 21.493378 threshold. The matched live capture also returned the
correct candidate bit 1 and passed conditional admission, with operational
log-e lower bound 33.028772 against the same threshold. Its nominal quiet
DATA-equivalent PSD was 7.649849 dB-Hz; estimated faded DATA C/N0 was
7.958961 dB-Hz. It had zero digital DAC clipping, no audio gaps and zero
near-full-scale ADC samples. This is one observed correct software/live pair
at the longer format; the physical false-admission guarantee remains
unqualified. Keep results for the two DATA durations separate; they are not a
single pooled sensitivity rate.

In both longer-format runs the Helper frequency stage narrowed its 365-point
coarse bank to 15 proposals, a 24.3-fold reduction in frequency candidates.
Runtime speedup relative to unassisted acquisition was not measured here.

| Duration basis | 23.2-second DATA format | 34.8-second DATA format |
| --- | ---: | ---: |
| Observed audio support | 109.547 s | 121.1485 s |
| Analysis envelope | 85.547 s | 97.1485 s |
| Transmitted Helper plus DATA support | 61.4675 s | 73.0675 s |
| Active DATA | 23.2 s | 34.8 s |

The short-format transmitted support excludes a 22-second UTC wait before the
fixture. All four durations are distinct denominators and must not be
substituted for one another.

Keep these denominators separate. They answer different questions and do not
imply that an unrestricted packet stream runs indefinitely in real time. This
setup still incurs per-slot preparation and reception cost. For a single bit,
the short-format calendar estimate is 61.6 seconds; the longer format is
73.2 seconds including the same 38-second Helper lead. The encoder's `fox` text
is 22 bits (`1110001001111101111000`); its short-format calendar estimate is
`22 × 23.2 + 38 + 0.4 = 548.8 s` (9 min 8.8 s). For the 34.8-second DATA
format, the corresponding estimate is `22 × 34.8 + 38 + 0.4 = 804 s` (13 min
24 s). Neither estimate measures a completed end-to-end message. Across the
two short-format live captures, receive CPU including audio children was
0.3927–0.4205 seconds per
observed-audio second (0.4181–0.4465 including public preparation),
0.5029–0.5385 per analysis-window second, 0.6998–0.7494 per transmitted
Helper-plus-DATA second (0.7452–0.7958 including preparation), and
1.8542–1.9856 per active DATA second (1.9744–2.1085 including preparation).

Timing was measured on an AMD Ryzen 5 PRO 5650U (6 cores, 12 logical CPUs),
using two numerical threads. Bulk processing used FP32; preparation and
reductions used higher precision. On the measured CPU short-format replays,
prepared reception used 42.9–43.6 CPU
seconds; public receiver preparation was separately 2.69–2.82 CPU seconds, and
imports cost 1.8–2 CPU seconds. Observation-dependent candidate refinement
preparation is included in reception; the separate preparation number is the
public per-slot receiver setup. The short-format live captures used 42.90–45.95
receive CPU seconds plus 0.11–0.12 audio-child CPU seconds; receiver
preparation took 2.79–2.85 CPU seconds. The raw keyed-template export
increment was estimated at 1.9175 CPU seconds by subtracting a same-seed run
with the same channel and I/O but without `--templates`; this is a difference
estimate, not an isolated API timer. The actual
fresh public-template export took 4.925 CPU seconds, including unused channel
and I/O work. Reusing cached raw templates leaves about 2.7–2.9 CPU seconds of
public workspace preparation. For a fresh key/epoch, adding the measured
template-export and workspace costs gives about 7.6–7.8 CPU seconds, plus
roughly 1.8–2 CPU seconds for startup/imports. A simple component sum estimates
about 0.90 CPU seconds per transmitted second on first use when it charges the
full template export; using the estimated template increment gives about
0.855. These are engineering estimates, not statistical upper bounds or
measured prepared-API costs. The receives used about 43–46 CPU seconds per
23.2-second active DATA interval, or roughly 1.85–1.98 CPU cores. Scoring took
28.9–31.2 seconds wall per bit, longer than the 23.2-second DATA cadence. The
single-bit on-air CPU ratio of 0.70–0.75 CPU seconds per transmitted second
therefore does not qualify contiguous `fox` transmission in real time. Receive
timing includes DDC work, but excludes TX generation and the audio
noise-generation command. Live wall time includes device wait and final drain;
the receive schedule had a 28.98–31.26-second maximum completion backlog.
The 34.8-second software replay used 65.61 receive CPU seconds and 48.51
seconds wall; public receiver workspace preparation used 5.43 CPU seconds and
5.41 seconds wall. Its prepared receive ratios were 0.5415, 0.6753, 0.8979
and 1.8852 CPU seconds per observed-audio, analysis, transmitted-support and
active-DATA second respectively; including workspace, those were 0.5863,
0.7312, 0.9722 and 2.0412. The long-format live capture used 61.76 receive CPU
seconds plus 0.123 audio-child CPU seconds; workspace preparation used 5.03
CPU seconds. Including audio-child CPU, its ratios were 0.5108, 0.6369, 0.8469
and 1.7781 per observed-audio, analysis, transmitted-support and active-DATA
second; including workspace they were 0.5523, 0.6887, 0.9157 and 1.9227.
Fresh long-format template export took 7.1899 CPU seconds; with 5.03–5.43 CPU
seconds of measured workspace preparation, this is 12.22–12.62 CPU seconds
plus about 1.8 seconds startup. No isolated template increment was measured
for the long format. The software replay's 48.51-second reception wall time
and the live per-slot scoring wall time of 42.75 seconds both exceed the
34.8-second DATA cadence. Live-long total wall time was 162.31 seconds,
including device wait and final drain, with a 42.87-second completion backlog.
These results do not qualify contiguous `fox` transmission in real time.
These timings are specific to this CPU, software revision, fixture, and thread
configuration.

The live capture reports, raw PCM and receiver outputs are retained in
`local-evidence/triad-hf-background-target/live-0/`, `live-1/` and `live-long/`.
The 34.8-second software and live results are each 1/1 correct conditional
admissions. No results are pooled across formats, and the physical
false-admission guarantee remains unqualified.

## Reproduction and retained evidence

[`summary.json`](summary.json) contains the measured outcomes, CPU denominators,
noise levels and hashes of all seven complete reports. The 3.3 MB
[`evidence.tar.gz`](evidence.tar.gz) preserves raw reports, receiver outputs,
protocols, generation timing, controls, noise receipts, source snapshots and
hashes/sizes of the locally retained waveforms. The waveforms themselves stay
in ignored `local-evidence/triad-hf-background-target/`.

The frozen exporter commands are in [`protocol.json`](protocol.json) and
[`long-protocol.json`](long-protocol.json). The longer trial's pre-decode
noise calibration is in [`long-noise-calibration.json`](long-noise-calibration.json).
The separate packing check is in
[`quantization-diagnostic.json`](quantization-diagnostic.json).
Early short replays recorded end-of-run wrapper hashes while reporting-only
fields were being improved; their receiver and noise algorithms did not
change. The longer runs record invocation-time source identities and verify
that they remain unchanged. Each admission receipt budgets one duration; this
exploratory campaign is not itself a joint daily false-admission qualification.

Use a compatible `build/dev-cli/triad_helper_export` and
`build/dev-cli/background_noise` built through `./build.sh`, plus the isolated
Python environment with NumPy, SciPy and CPU PyTorch. Regenerate fixtures using
the archived argument vectors; output directories must be new. On this host:

```sh
export OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 MKL_NUM_THREADS=2

/tmp/triad-weak-env/bin/python tools/triad_hf_noise_measure.py \
  --public local-evidence/triad-hf-background-target/fixtures/public-template \
  --capture local-evidence/triad-hf-background-target/fixtures/signal-1 \
  --output local-evidence/triad-hf-background-target/replay-new-short

/tmp/triad-weak-env/bin/python tools/triad_hf_noise_measure.py \
  --public local-evidence/triad-hf-background-target/fixtures-long/public-template \
  --capture local-evidence/triad-hf-background-target/fixtures-long/signal \
  --target-cn0 7.690888248081334 \
  --output local-evidence/triad-hf-background-target/replay-new-long
```

For the cable procedure, add `--mode live --exclusive --device hw:1,0
--analog-gain 0.8414025015631204` and use a new output directory. That gain is
specific to the independently measured, unchanged cable/mixer setup here;
remeasure it for another setup. Use `fixtures/helper-null` with the short
public template for the bounded Helper-only software control. No GPUs or
ordinary-receiver campaign are required for these focused measurements.

The tool excludes transmitter/channel synthesis and AES impairment generation
from receiver CPU time. It includes continuous DDC, Helper, acquisition,
observation-dependent refinement and drain. Functional execution, conditional
admission, physical-null qualification and completed-message reception are
separate fields. Noise is freshly generated per invocation, so commands
reproduce the procedure, not byte-identical noise or receiver outcomes.
Syntax, whitespace and archived-report hash checks passed; all seven
measurement runs completed and all audio subprocesses exited.
