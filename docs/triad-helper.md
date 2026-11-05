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
The [rate-aware shaping planner](triad-shaping.md) now provides the default
general DATA pulse/ramp/lattice selection in the exporter and campaign runner.
Use `--shaping legacy` to reproduce the historical waveform explicitly.
For the strong EME case it supports
1725 Hz DATA with the standard 2400 Hz high pilot, preserving the existing
calendar, Helper waveform and power allocation. Read its sampled evidence and
limits; the conservative legacy placement recipe below remains available explicitly.

**DATA power calibration now defaults to `--power-reference whole-active`.**
Exact historical sampled reproduction also requires
`--power-reference legacy-window`; shaping and power-reference policies are
separate. The old two-second power window made low-rate DATA more powerful
than declared; read the correction and evidence limits below before reusing
old sensitivity results.

For the factorized Helper configuration catalog, distinctions between planner
and sampled evidence, and economical campaign guidance, see the
[Helper coverage map](triad-helper-coverage.md). The frozen broad sampled
cohort's outcomes will be recorded in the
[November 5 campaign report](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
when integrated; do not treat catalog or planner rows as receiver trials.

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
| DATA audio center / high Helper pilot (default auto) | 1725 / 2400 Hz | 1725 / 2400 Hz |
| Explicit study arrival prior | [−7.035, +7.035] s | [−0.055, +0.125] s |
| Epoch alternatives | −6 through +6 seconds | −6 through +6 seconds |
| DATA C/N₀ at −164 dBm/Hz noise density | 20.0206 dB-Hz | 10.0206 dB-Hz |
| Separate requested planning target | 8 dB-Hz | 1.5 dB-Hz |

With `--shaping legacy`, EME defaults to DATA 4000 Hz / high pilot 6412.5 Hz
to retain the production placement guard for the fast FHSS ramps, including cover-chip energy. This complies with the requested
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

Power and C/N₀ use one fixed public bit-0 reference over the **complete active
source**, including its nonzero birth, ending and dwell ramps. The denominator
excludes FHSS quiet intervals and Symbol Burst waiting tiles. The resulting
DATA-only gain is frozen for both bits, all thirteen candidate epochs and every
signal/null control. A different transmitted bit may have different energy; it
is not independently normalized. Frame-average DATA energy therefore retains
FHSS quiet and burst-wait losses. Per-trial emitted DATA, Helper and summed
energies remain separate, and no received channel, outage, fade or
modulation-induced energy change is renormalized.

The production Helper plan still uses its unchanged 39–41 s preview and
composite gain. A separate constant DATA correction enforces the active-power
contract while preserving Helper samples, placement, waveform and lead-in.
`triad_helper_export --power-plan-only` reports this calculation without
allocating a capture or template bank. Public metadata identifies the policy,
reference extent, gain and bounded quadrature refinement. Agreement between
successive integration grids within 0.1% is an engineering convergence check,
not a proven integration-error bound; unresolved refinement is rejected.
Independent full sampled-source checks cover the qualified rates, FHSS and
burst cases, rather than treating this metadata as measured energy.

The earlier `legacy-window` policy normalized the first 1–3 s of DATA. At
Rate 3.6 Hz / Added Symbols ×1 and Rate 10 Hz / ×1, this window lay inside the
long birth ramp, making actual bit-0 active DATA power approximately **28.26 dB
and 6.35 dB above nominal**, respectively. Those Monte Carlo outcomes remain
historical evidence for their emitted waveforms; they do not qualify the
advertised low C/N₀. Default HF36×10 and EME360×1 corrections are only about
+0.024 and +0.039 dB, but old observations and admission gates still belong to
their original source/power identity. See the
[power audit and corrected-source validation](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
before transferring any historical sensitivity or duration conclusion.

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

For bounded GPSDO cases with known missing ADC samples, the optional
[`triad_helper_raw_study.py`](../tools/triad_helper_raw_study.py) entry point
reuses the same batched FFT/Gram machinery directly at 18 kHz. It excludes only
the missing raw rows, with their original white-noise variance, rather than
discarding every FIR output touched by a gap. The paired 10-ms-per-dwell EME
failure lost 29–38 dB of pattern-distance energy through the wide FIR exclusion,
versus 0.024–2.96 dB in the actual raw gaps. The raw path recovered both original
wrong admissions and subsequently received all sixteen independent-key signal
captures correctly, with zero admissions on sixteen null captures. Those small
sets do not establish a daily false-admission or 99% reception guarantee.
See the [broad study record](validation-data/triad-v3/2026-11-05-helper-broad/README.md).

This optional path currently requires 18 kHz sampled fixtures, singleton nominal
audio/RX clocks with a checked negligible-drift approximation, CFO within
±0.6 Hz, at most thirteen epochs and explicitly bounded FFT/cache storage.
Helper remains in its observations and residual-energy estimate. It needs its
own Helper-present null calibration; filtered gates cannot be reused. Run the
entry point from the repository root. Its source-binding label
`raw_study_adapter.py` is retained from the byte-identical qualification adapter.

Use campaign `--assisted-only`, or broad-protocol `"arms": ["assisted"]`, when
measuring the Helper-enabled receiver without a repeated unaided comparison.
Helper selection runs first; a required full-bank fallback still runs exactly
once. Paired comparison remains the default. Gates record the measured arms
and execution-policy identity; an unmeasured baseline has no outcome or timing.
Retain occasional paired cost anchors, rather than repeating an expensive
unaided search for every calibration capture.

The batched cache has at most `epochs × audio-clock values × RX-rate values`
reachable keys; CFO alternatives reuse those workspaces. Memory planning caps
an overlarge requested cache capacity at this count and reports both values.
Subset views sharing a cache keep its original superset capacity. A requested
400-entry cache therefore does not reserve400 workspaces for a singleton-clock
13-epoch case. This is a declared array bound, not a guarantee about allocator
overhead or total RSS. The [broad study](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
retains the original false memory rejection and its separate repair evidence.

The campaign's receiver geometry check is derived from the emitted public
manifest: half the declared DATA occupied bandwidth, the maximum selected
channel-line offset (including the configured independent selective-modulation
rate), and the public clock/time-scale and CFO bounds. It rejects nominal
envelopes that do not fit the selected FIR pass edge (330 Hz legacy, 400 Hz
wide). DATA Doppler must also remain below the 450 Hz Nyquist frequency of the
900 Hz post-DDC stream. This replaces earlier fixed `112.5 Hz` source-width and
`2|D|` line-width shortcuts in campaign geometry checks. It is only a public
nominal-support guard: pulse/filter tails remain in the samples, and fitting the
FIR is not a sensitivity or compact-support guarantee.

Capture validity is an immutable public receiver input. For known ADC erasures,
missing raw rows are zero-filled **before** the same continuous causal FIR used
for the observation; output samples are invalidated whenever any raw row in
their complete FIR support window is missing. Invalid outputs are excluded from
correlations, Gram matrices and residual energy. Raw and expanded-mask hashes
participate in receiver/procedure identity; changing a mask requires matching
new calibration. This covers known erasures, not unrecognized clipping,
dropouts, or hardware faults inferred from sample values.

Selective-modulation rate is independent of its amplitude/depth preset. An
explicit rate can exercise modulation with `spread-preset none`; an omitted rate
retains the preset-derived default. Zero is an explicit exact bypass. Do not
silently equate this rate with Doppler spread or oscillator frequency.

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

The default timing grid scores every integer 900 Hz lag (1/900 s), with one
fractional anchor and an explicitly included endpoint. An explicit
`--lag-step 4` reproduces the older 4/900 s grid. That coarse grid selected a
wrong bit in a low-SNR sampled realization despite its thirteen-epoch search;
post-outcome isolation with lag step one restored the correct bit at nearly the
same comparison cost. This repair addresses one failure; it is not independent
sensitivity qualification. Earlier GPSDO fixtures use one sample-clock
hypothesis; the newer oscillator study declares seven audio-clock samples and
three receiver time-scale samples. Its 17,035-point aligned CFO bank covers the
margin-scaled public bound; the original 16,803-point diagnostic grid is used in
the independent Helper fitting measurements. These are declared finite banks,
not continuous maximum likelihood. The DATA two-ray model omits the selected
additional modulation structure. Complete noisy sampled experiments, rather
than a nominal channel match, determine its performance.

The historical `--shaping legacy --ramp-seconds .006 --data-if 1725`
EME export retains the 400 ms dwell, 55 ms quiet time, 5 ms margin, pulse support and 64 protected inner
symbols across the 26.4 s DATA tile. The production Helper planner raises its
high pilot to 2654.17 Hz. In the matched sampled ramp comparison, the new ramp
increased Helper energy at the DATA FIR output from 104.8 dB below measured
noise to 64.3 dB below noise. Leakage remained below noise in this fixture, but
the 40.6 dB relative change makes this a measured placement tradeoff, not a
metadata-only guard proof. Legacy mode keeps the old ramp unless explicitly
overridden; default auto mode jointly plans the pulse and ramp instead.

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

Generate public alternatives once, then reuse them. The broad campaign runner
also caches source templates across compatible cohorts and shares Helper
estimation plus continuous-FIR capture preparation across compared lag policies
for each physical capture. This keeps lag-step comparisons paired without
re-exporting the waveform or filtering the same I/Q once per arm. Each mask is
frozen into protocol identity; do not reuse gates after changing validity,
public geometry, channel or source templates:

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
