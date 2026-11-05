# Helper-assisted three-carrier acquisition

This is the isolated v3 integration of the **production Helper transmitter** with
the streaming three-carrier source and sampled channel. It does not integrate
the ordinary Robust receiver, GUI, live audio or physical radio retuning. Start
with [Helper integration](helper-integration.md), [channel models](channel-models.md)
and the [three-carrier workflow](triad-development-workflow.md). The receiver
consumes noisy samples and public keyed alternatives; evaluator truth is separate.
The [sampled validation record](validation-data/triad-v3/2026-11-04-helper/README.md)
retains successful physical-link trials, the failed 1.5 dB-Hz microwave trials,
known-channel benchmarks, measured CPU savings and reproduction commands.
The later [clock, GPU and low-IF ramp record](validation-data/triad-v3/2026-11-04-helper-gpu/README.md)
records the mixed free-running/IC-7100 HF study, the opt-in batched GPU path,
and an EME FHSS ramp that restores the 1725 Hz DATA center. It extends, and
does not replace, the earlier admission results.
The [rate-aware shaping planner](triad-shaping.md) now provides an opt-in
general DATA pulse/ramp/lattice selection. For the strong EME case it supports
1725 Hz DATA with the standard 2400 Hz high pilot, preserving the existing
calendar, Helper waveform and power allocation. Read its sampled evidence and
limits before replacing the historical conservative placement recipe below.

## Implemented composition

`triad_helper_export` uses the actual `helper::Generator`, its placement and power
plan, encrypted auxiliary metadata, and the fixed 38-second lead. DATA and Helper
are summed **before** shared TX oscillator, delay/Doppler paths, the corrected
`dispersive-dft-v1` selective operator, RX oscillator and noise. Filter state,
echoes, source guards and complete tails are preserved. No carrier-label filter
or post-propagation hop gate is introduced. The `EmittedSource` adapter in
`triad_modem_v3.hpp` allows an explicitly bounded composite source while retaining
the original `StreamingChannel` path.

The two preset exports implement:

| Setting | `hf` | `eme` |
| --- | --- | --- |
| DATA / Helper / path loss | 4 W / 40 W / 180 dB | 40 W / 15 kW / 200 dB |
| GUI Rate / Added Symbols | 36 Hz / ×10, interleaved | 360 Hz / off (×1) |
| Base and shift oscillator | GPSDO hobbyist XO | GPSDO OCXO |
| RF shift | 30 MHz | 5.8 GHz |
| Spread after margin | HF worst ×3: 90 Hz, 21 ms | EME focused ×1: 6 Hz, 0.5 ms |
| Additional modulation | Each 65% | Each 65% |
| FHSS / Symbol Burst | Off / off | Fake FCC envelope / off |
| DATA audio center / high Helper pilot | 1725 / 2400 Hz | 4000 / 6412.5 Hz |
| Explicit study arrival prior | [−7.035, +7.035] s | [−0.055, +0.125] s |
| Epoch alternatives | −6 through +6 seconds | −6 through +6 seconds |
| DATA C/N₀ at −164 dBm/Hz noise density | 20.0206 dB-Hz | 10.0206 dB-Hz |
| Separate requested planning target | 8 dB-Hz | 1.5 dB-Hz |

The legacy EME DATA center is higher to retain the production placement guard for the
fast FHSS ramps, including cover-chip energy. This complies with the requested
1725/1775 Hz **or higher** convention, but is not an SSB audio-passband
qualification. Both presets have 60 fine symbols/s/carrier, 75 Hz spacing and
225 Hz nominal triplet bandwidth. The sample rate is 18 kHz; all processing uses
complex direct conversion about the DATA center. `--cn0` weakens the entire
composite by increasing noise at fixed Helper/DATA ratio. It does not confuse
the planning target with the physical link budget.

These arrival ranges implement the requested default-style study cases; they
are explicit exporter settings, not a claim about every CLI/GUI default. In
particular, ordinary `triad_long --clock-sync default` alone gives a singleton
with FHSS off and a different interval with FHSS on; see the
[arrival-prior guidance](triad-development-workflow.md). Integration must carry
the actual configured public prior rather than infer it from the label Default.

Power and C/N₀ use one fixed public active-DATA reference. Frame-average DATA
energy is lower with FHSS quiet time; per-trial emitted DATA, Helper and summed
energies are retained separately. No per-bit or per-channel renormalization
removes fades, modulation-induced energy changes or guard overhead.

The EME source explicitly opts into `fhss_center_aligned`: chip centers lie at
integer fine periods instead of half periods. At 60 Hz this places a protected
chip at each 200 ms dwell center, with full pulse support clear of ramps. The
old half-period lattice contains no such chip with these guards. The opt-in
changes the keyed format namespace; defaults preserve the earlier waveform and
keystream. Added Symbols and interleaving still move whole cells. Cover chips
and finite waveform skirts remain outside protected payload support.

## What assistance does

`triad_helper_assistance.py` profiles low/high pilot lines, with arbitrary
complex coefficients for the public union
`{0,D} + {0, ±0.1D, ±D}` when both modulation components are selected. Exact
finite-window Gram matrices retain dependence between lines. A phase-centered
FP32 FFT supplies inexpensive correlations; small Gram solves and reductions use
FP64. For a nontrivial public audio-clock bound it separately fits transmitted
audio scale, common CFO, and receiver time scale for Doppler/modulation offsets.
The last parameter is not assumed equal to the audio TX/RX ratio. It keeps all
competitive low/high pilot proposal basins through joint refinement. Pilot and
independent half-window searches, optimizer checks, competing peaks, boundary
checks and direct-dot spot checks can force fallback to the full public CFO
and clock banks.

The returned CFO and clock intervals are **engineering allowances**, not
calibrated confidence regions. On the sampled free-running-crystal / IC-7100
HF set, Helper narrowed 16,803 CFO candidates to 4–6 in eight paired replays
and two fresh captures; all ten CFO and clock intervals covered simulator truth.
The replays check a convergence-tolerance correction; only the final two captures
are independent of that correction. This is
finite-sample evidence and does not show continuous-search completeness or
operational lock reliability. In Fake FHSS, pilot phase is continuous but
amplitude gating adds unfitted harmonics; independent arrival/gate-phase trials
are required. Actual RF retuning or arbitrary dwell phase resets explicitly
fall back. The model does not represent arbitrary stochastic Doppler spectra.

`triad_helper_receiver.py` keeps the legacy FFT likelihood receiver as its
default and adds an opt-in batched path. Both fit unknown complex direct and
delayed-Doppler DATA coefficients. The whole capture is filtered continuously,
then decimated to 900 Hz. The legacy frontend uses 330/430 Hz pass/stop edges.
The experimental `wide` frontend uses 400/440 Hz to admit the new broad HF
oscillator search; use the exporter's `--receiver-tail-seconds .25` to retain
its longer causal FIR tail. The batched path caches public channel/Gram factors,
processes CFOs in bounded batches, reuses one observation FFT for aligned CFO
lattice shifts, and streams the likelihood mixture without storing the complete
timing-by-frequency score cube. Its clock and RX-rate alternatives are explicit
public grids. Filtered I/Q and unchanged banks are reused across baseline and
assisted arms. No candidate is removed on the basis of the transmitted bit,
actual arrival or oscillator draw. A constant phase rotation is already
absorbed in fitted coefficients, so it offers no extra sensitivity to this
receiver. Time-varying Helper phase correction is not implemented or claimed.

The new GPU comparison ran on an RTX PRO 6000 WK against four EPYC 9355 CPU
threads using the same EME fixture and strict FP32 bulk arithmetic (TF32 off;
sensitive preparation/reductions remain FP64). The resident EME correlation
stage was about 82× faster on CUDA, and the resident complete paired score was
about 8× faster (3.57 s CPU versus 0.45 s CUDA). Bits, winning epoch, timing and
CFO matched; the largest paired per-bit score difference was `2.29e-5`. The
first comparison including its constructor was about 1.52× faster because workspace
preparation is CPU-bound. These three paired replays measure that named EME
path on that GPU/CPU allocation; they are not a general hardware forecast or
a confidence-qualified regression threshold. The
older path's 1.18× result exposed repeated CPU preparation and small transfers
inside the frequency loop. Batched preparation reuse and frequency batching
address that bottleneck.

For the free-running-audio / IC-7100 HF case, actual full GPU runs at the
physical link and at 8 dB-Hz reduced 17,035 CFOs to five and seven clock values
to three. Cold receiver processing improved about 4.4×, including Helper and
filtering; the physical case's repeated receiver processing improved 42.0×
with all public workspaces retained. That reuse requires a measured 207 s
prewarm. Both full cases selected the correct raw bit; no calibrated admission
or sensitivity improvement is claimed. At 8 dB-Hz the winning clock/timing
changes, showing that a mismatched DATA model need not maximize at the true
clock even when Helper covers it. The report retains both outcomes.

The batched receiver uses one global residual-confidence allocation across the
epoch, clock, rate, frequency and timing family. That changes the multi-epoch
working score relative to the legacy receiver's per-epoch allocation. Treat it
as a changed likelihood statistic: recalibrate any admission rule for the exact
new procedure. GPU parity is a CPU/GPU implementation comparison within the
new statistic, not legacy-statistic equivalence.

The timing grid is 4/900 s, with one fractional anchor and an explicitly included
endpoint. Earlier GPSDO fixtures use one sample-clock hypothesis; the new
oscillator study declares seven audio-clock samples and three receiver time-scale
samples. Its 17,035-point aligned CFO bank covers the margin-scaled public bound;
the original 16,803-point diagnostic grid is used in the independent Helper
fitting measurements. These are declared finite banks, not continuous maximum
likelihood. The DATA two-ray model omits the selected additional modulation
structure. Complete noisy sampled experiments, rather than a nominal channel
match, determine its performance.

The opt-in `--ramp-seconds .006 --data-if 1725` EME export retains the 400 ms
dwell, 55 ms quiet time, 5 ms margin, pulse support and 64 protected inner
symbols across the 26.4 s DATA tile. The production Helper planner raises its
high pilot to 2654.17 Hz. In the matched sampled ramp comparison, the new ramp
increased Helper energy at the DATA FIR output from 104.8 dB below measured
noise to 64.3 dB below noise. Leakage remained below noise in this fixture, but
the 40.6 dB relative change makes this a measured placement tradeoff, not a
metadata-only guard proof. The exporter keeps the old ramp unless explicitly
overridden.

Fresh 6 ms / 1725 Hz sampled campaigns admitted all eight physical-link EME
messages, but only five of eight at an injected 1.5 dB-Hz, for both receiver
arms. A paired old-ramp / 4000 Hz campaign at 1.5 dB-Hz produced the same
admission/miss decisions. No wrong message was admitted; the DATA-absent
holdouts produced one full-bank false admission and zero assisted admissions
per campaign. These small per-capture gates and broad uncertainty intervals
do not qualify a daily false-admission rate or establish equal population
sensitivity. Retain the weak-case misses as unresolved.

With the broad HF study timing prior, a guaranteed full Helper window ends at 43.435 s
on the nominal source coordinate. The EME window ends at 36.525 s. The former
uses buffered DATA; neither is mislabeled a result available at exactly 36 s.
The fixed 38-second transmitted lead is unchanged. Very short HF fixtures may
not contain the required guaranteed window and are rejected by the comparison.

### Why phase and frequency benefits differ here

For the original GPSDO oscillator presets, the static frequency bound is about
±0.009 Hz after HF margin, versus ±0.580 Hz for EME. The declared whole-pattern
CFO grids therefore contain three and 125 points. A conservative Helper result
can greatly reduce the latter bank while leaving the former unchanged. This
does not predict savings for other oscillator/radio configurations.

Under the simulator's **oscillator-only Wiener-phase assumption**, let phase
increment variance be `q × elapsed_seconds` and pattern duration be `T`. The
expected retained energy of a constant-frequency coherent integral, normalized
to a phase-stable signal, is `2 × (x − 1 + exp(−x)) / x²`, where `x=qT/2`.
For 23.2-second HF and 26.4-second EME patterns, the selected relative diffusion
gives approximately 0.00256 dB and 0.00000029 dB loss respectively. These are
analytical expectations for that component, not measured end-to-end bounds.
They exclude channel evolution, frequency mismatch, acquisition and filtering.
Complex propagation variation can be much larger, but transferring pilot phase
across a frequency-selective channel is a different estimation problem. A more
elaborate phase tracker is not justified by this tiny oscillator-only loss.

The Helper interval describes static CFO and audio-clock scale under this
finite-line model; it has no future DATA horizon parameter or guaranteed
stochastic phase coverage. Full later DATA reception must be tested directly,
rather than inferred from pilot-fit confidence. Frequency search work, cold
setup, Helper processing, buffered latency and RF transmission duration must be
reported separately.

## Cheap checks and reproducible experiments

Build once in a compatible profile:

```sh
./build.sh configure --cli
cmake --build build/dev-cli --parallel 2 --target triad_helper_export
python3 tests/test_triad_helper_export.py build/dev-cli/triad_helper_export
```

Use the existing research Python environment with NumPy, SciPy and PyTorch:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 -m unittest discover -s tests -p 'test_triad_helper_*.py'
```

The native exporter smoke script takes its executable as a positional argument
and runs separately from unittest discovery. These are functional checks, not
sensitivity or runtime qualification. The joint-clock, batched receiver, clock
grid, GPU benchmark, and ramp diagnostics each have focused Python tests under
the same discovery pattern. Run `./build.sh test triad --cli` for changes to
native triad source/channel behavior. Do not run the ordinary receiver campaign
here.

Generate public alternatives once, then reuse them:

```sh
build/dev-cli/triad_helper_export --case eme --seconds 16 --seed 87302 \
  --templates --output local-evidence/helper-eme-templates
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 tools/triad_helper_study.py \
  --fixture local-evidence/helper-eme-templates \
  --output local-evidence/helper-eme-score.json --evaluate
```

`--evaluate` loads truth only after scoring. A raw winning bit is **not** an
admission. Calibrate the entire adaptive procedure with Helper-present,
DATA-absent samples, then freeze the gate before independent validation.
Calibration must bind the original public bank, complete selection policy,
source/templates, arithmetic, noise reference and channel configuration; the
realization-dependent narrowed-bank identity cannot be the gate identity.
Repeated acquisitions, duration alternatives and daily false-admission targets
need their own operational calibration.

Reuse fixtures and prepared DSP workspaces. For GPU work, the paired benchmark
is explicitly invoked with `--fft-backend batched`; the legacy backend remains
the default. For the broad HF case use `--frontend-profile wide`, explicit
`--clock-step-ppm`, `--doppler-scale-count`, and `--aligned-fft-grid`, with
memory and workspace-cache limits chosen from measured hardware. Run the public
clock-grid loss diagnostic before treating a sparse finite bank as adequate; its
sampled cell results allow continuous retiming and exclude the actual timing-lattice
loss. They are not a complete-bank or continuous worst-case bound. A changed
modulation/clock/channel distribution needs new evidence; an unchanged receiver
does not need every historical research campaign repeated. Follow the
[efficient regression policy](triad-performance.md#required-efficient-regression-workflow)
for performance decisions. CUDA moves batched template/observation FFT
correlations and likelihood work to the GPU. Helper estimation, FIR conversion,
waveform/channel generation, and public-workspace preparation remain CPU work.
Measure cold and resident costs for the actual bank and batch size before
claiming acceleration.

## Limits retained during integration

There is no general guarantee that a pilot channel phase transfers to the DATA
carriers through frequency-selective fading. All components traverse the same
physical operator here, but its frequency responses differ. Likewise Helper
does not rescue signal outside the captured passband. Fixed headroom, clipping,
real audio hardware, finite RF front-end rejection and actual retune phase need
their own Live/Duplex or RF validation. The finite spectral allowance in the
exporter is not a proof of compact support. Weak-noise admission, false bits,
misses, leakage and numerical limits must remain separate reported outcomes.
