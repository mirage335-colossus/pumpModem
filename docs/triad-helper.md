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

The EME DATA center is higher to retain the production placement guard for the
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

`triad_helper_assistance.py` profiles a common CFO against low/high pilot lines,
with arbitrary complex coefficients for the public union
`{0,D} + {0, ±0.1D, ±D}` when both modulation components are selected. Exact
finite-window Gram matrices retain dependence between lines. A phase-centered
FP32 FFT supplies inexpensive correlations; small Gram solves and reductions use
FP64. Low/high pilots, disjoint time halves, competing peaks, boundary checks and
direct-dot spot checks can force fallback to the complete original CFO bank.

The conservative interval is an **engineering allowance**, not calibrated
confidence. In Fake FHSS, pilot phase is continuous but amplitude gating adds
unfitted harmonics; independent arrival/gate-phase trials are required. Actual
RF retuning or arbitrary dwell phase resets explicitly fall back. The model
does not represent arbitrary stochastic Doppler spectra.

`triad_helper_receiver.py` retains the existing FFT likelihood receiver and fits
unknown complex direct and delayed-Doppler DATA coefficients. It filters the
entire capture continuously, then decimates to 900 Hz. Its 1105-tap FIR retains
the selected DATA spread while rejecting distant Helper energy. All thirteen
epoch banks and the complete declared integer timing lattice enter the mixture.
Helper narrows only the original CFO bank. FFT/Gram workspaces and filtered I/Q
are shared between arms; unchanged banks reuse the same outcome. No candidate
is removed on the basis of the transmitted bit, actual arrival or oscillator
draw. A constant phase rotation is already absorbed in fitted coefficients, so
it offers no extra sensitivity to this receiver. Time-varying Helper phase
correction is not implemented or claimed here.

The timing grid is 4/900 s, with one fractional anchor and an explicitly included
endpoint; sample-clock alternatives are held at zero for these GPSDO cases.
This is a declared finite bank, not continuous maximum likelihood. The DATA
two-ray model omits the additional modulation structure. Complete noisy sampled
experiments, rather than a nominal channel match, determine its performance.

With the broad HF study timing prior, a guaranteed full Helper window ends at 43.435 s
on the nominal source coordinate. The EME window ends at 36.525 s. The former
uses buffered DATA; neither is mislabeled a result available at exactly 36 s.
The fixed 38-second transmitted lead is unchanged. Very short HF fixtures may
not contain the required guaranteed window and are rejected by the comparison.

### Why phase and frequency benefits differ here

For these selected oscillator presets, the static frequency bound is about
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

The Helper interval currently describes static CFO under its finite-line model;
it has no future DATA horizon parameter or guaranteed stochastic phase coverage.
Full later DATA reception is tested directly, rather than inferred from pilot
fit confidence. Frequency search work, cold setup, Helper processing, buffered
latency and RF transmission duration must be reported separately.

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
sensitivity or runtime qualification. Run `./build.sh test triad --cli` for changes to native
triad source/channel behavior. Do not run the ordinary receiver campaign here.

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

Reuse fixtures and prepared DSP workspaces. A changed modulation/clock/channel
distribution needs new evidence; an unchanged receiver does not need every
historical research campaign repeated. Follow the
[efficient regression policy](triad-performance.md#required-efficient-regression-workflow)
for performance decisions. CUDA moves template/observation FFT correlations
and selected likelihood work to the GPU. Helper estimation, FIR conversion,
Gram preparation/refinement and the sampled native channel remain CPU work.
Measure complete cold and resident costs before claiming acceleration; the new
Helper composition has not yet been qualified on CUDA.

## Limits retained during integration

There is no general guarantee that a pilot channel phase transfers to the DATA
carriers through frequency-selective fading. All components traverse the same
physical operator here, but its frequency responses differ. Likewise Helper
does not rescue signal outside the captured passband. Fixed headroom, clipping,
real audio hardware, finite RF front-end rejection and actual retune phase need
their own Live/Duplex or RF validation. The finite spectral allowance in the
exporter is not a proof of compact support. Weak-noise admission, false bits,
misses, leakage and numerical limits must remain separate reported outcomes.
