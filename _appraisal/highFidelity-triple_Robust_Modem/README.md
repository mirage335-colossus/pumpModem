# High-fidelity triple Robust Modem appraisal

This contained suite exercises the canonical native modem against calculated,
reference-reproduced, standard-statistical and explicitly synthetic channel
hypotheses for **sub-9 kHz / 200 ft antennas, 1 MHz groundwave, 30 MHz
groundwave, 1–30 MHz skywave, and Earth–Moon–Earth**. All appraisal code,
fixtures, documentation and generated evidence remain in this directory.
Python launches native executables and analyzes receipts; it implements no DSP.

The expanded tests now include complete physical-calendar EME episodes and a
complete VLF message representing more than nine hours of transmission. They
also expose failures: compound noise can produce false DATA, broad lunar
scattering can produce an accepted wrong message, and slow phase wander can
lose a weak VLF signal while increasing computation. These are retained test
results, not hidden by a software-check pass. Read the [validation record](validation.md)
and [coverage matrix](coverage.md) for exact outcomes, identities and limits.

No fixture has independent local field/equipment calibration. In particular,
the VLF antenna circuit and magnetic receiver levels are separate reference
planes, groundwave magnitude snapshots do not supply measured phase or terrain,
and the lunar process is a frozen monostatic statistical approximation. The
suite does not qualify environmental availability, unknown-calendar acquisition,
rare-error probabilities or complete-receiver real-time capacity. The
[models](models.md), [research reconciliation](research-reconciliation.md) and
[protocol](experiment-protocol.md) preserve those distinctions and the source
limits in [`_ref`](../../_ref/highFidelity-triple_Robust_Modem).

## Build and quick correctness checks

From the repository root:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

This configures a separate ignored `.build/`, links the production C++ modem,
and runs seven CTest groups (35.25 seconds in the final recorded run):

| Group | Main coverage |
| --- | --- |
| `channel` | Continuous state, silence/tails, interpolation, phase wander, validation and resource limits |
| `scenarios` | Physical/source tuples, normalization, reference observables and adverse numerical controls |
| `hf_scenarios` | NVIS and DRM6 delay/power/Doppler conventions and chunk invariance |
| `impairment_observables` | Independent AR(1)/shot moments and covariance, resonator response, ADC and AGC recovery |
| `lunar_spectral` | Joint delay–Doppler covariance through 360 s, independent refinement, long integration and waveform parity |
| `integration` | Actual native source/receiver, physical completion, noise/EOF, recovery and numerical-admission boundaries |
| `runner` | 33 manifest/receipt tests, immutable identity, component scope, time/resource censoring and statistical reporting |

`build.sh configure` and `build.sh build` are also available. Compatible
compiler-cache entries are read without writing outside this subtree. The
canonical modem source remains unchanged; its separately recorded 32/32 Triad
check remains historical evidence, not a freshly rerun measurement.

## Reference experiments

Inspect actual native geometry before a long run:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --list
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode plan --scenario vlf-resonant --waveform reference --pulse tapered-rrc
```

Run the four main reference campaigns serially:

```sh
for campaign in propagation-breadth receiver-controls weak-holdouts eme-episodes; do
  python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
    --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
    --campaign "_appraisal/highFidelity-triple_Robust_Modem/campaigns/$campaign.json" || break
done
```

These 41 episodes took about nine minutes on the tested Ryzen 5 PRO 5650U,
using the full automatic 24-worker budget, with zero censored executions.
They include strong/weak comparisons, land/sea reference levels, distinct HF
profiles, recovery, Helper/noise/address controls, three fresh weak seeds per
selected stratum, and full EME messages. Their heterogeneous outcomes are not
pooled into a success probability. Consult each manifest's explicit per-episode
and aggregate caps; different hardware can reach those caps.

The deliberately longer VLF tier preserves the original slow RRC observation,
compares the explicitly selected tapered RRC pulse, tests weak static and
phase-varying conditions, and checks antenna detuning:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/vlf-reference.json
```

Its seven episodes have a 1,900-second aggregate cap; the original RRC slot
alone has a 1,200-second cap. The five-slot tapered message retains the
5,760.4-second DATA bit and all **64,806,069 samples** at 1,920 samples/s.
It completed correctly in 64.69 seconds of computation, including two scored
quiet slots. That timing includes source/channel work and is not a calibrated
receiver-capacity claim. Tapered RRC is also the canonical live shaping planner's
choice for this geometry; its input is explicitly different from the retained
RRC reference. No waveform, search hypothesis or physical clock is silently
changed to make a case finish.

`noise-ablation.json` contains three matched noise-only component diagnostics;
`eme-discriminator.json` contains a selected high-energy EME comparison and
same-aperture bit oracle. They diagnose observed failures without retuning the
receiver. The older exploratory `awgn-trend.json` remains a separate duration/
energy baseline; two seeds per cell cannot establish a smooth sensitivity floor.
`integration-smoke.json` is short-span wiring coverage, not reference sensitivity.
Superseded finite-grid lunar presets and the old weak-signal pilot manifest are
retired; their original results remain labeled historical in validation.

Each run creates a unique folder under ignored `local-evidence/`, containing
frozen inputs, exact source/binary identities, raw native JSON, a machine-readable
run report and Markdown summary. `--output-dir` must name a new directory under
that root. Communication failure, false DATA, wrong messages, physical completion,
and execution censoring are separate outcomes. A completed campaign means its
observations were collected, not that every communication succeeded.

## Native modes and bounds

- `plan` reports exact public geometry, source assumptions, workspace and sample/
  numerical-horizon admission without rendering.
- `channel` renders a bounded physical-time component.
- `receive` evaluates the complete configured message/calendar through physical
  quiet completion. EOF cannot count as successful completion.
- `slot` observes the complete first public aperture. It preserves the canonical
  source sequence and cannot claim full-message completion.
- `diagnose` measures full-prior VLF acquisition and an abbreviated persistent
  fine session from the complete first aperture. It makes no bit decision or
  complete-service capacity claim.
- `oracle` computes eligible AWGN/EME known-waveform, known-channel binary and
  presence references; `--oracle-slot N` selects a public bit. These do not pay
  blind acquisition or unknown-channel estimation and are not operational bounds.

The runner supports these modes plus explicit pulse, message, chunk, selected
oracle slot and bounded per-episode resource overrides. Pulse defaults to RRC;
`tapered-rrc` is always explicit. `duration_seconds` controls channel rendering;
receive geometry comes from the canonical calendar and message. Never compress
that clock or infer completion from the transmitted length.

The full-episode lunar renderer supports the declared 5.76 GHz, 1,920 Hz,
Helper-off source within **360 physical seconds** and its validated spectral
allowance. Unsupported longer/Helper configurations are rejected. The default
five-slot message takes 269.466 seconds. Shared spectral draws preserve joint
frequency/time structure and moving-delay effects; short blocks are not
independently re-randomized. Numerical agreement is reported separately from
physical model validity and receiver success.

## Optional cloud execution and evidence lifecycle

`cloud-bundle.py` creates a manifest-verified native source closure of about
2 MB, excluding credentials, builds and captures. `cloud-build.sh --help`
describes the pinned official CUDA development image, exact full-GPU checks and
cache recipe. Prefer a full RTX PRO 6000 Blackwell when acceleration is useful;
`--backend cuda|vulkan|gpu` must execute the requested device or report unavailable.
A prior RunPod campaign executed CUDA on a full RTX PRO 6000 Blackwell Server
Edition but was slower than CPU on its short paired fixtures. The subsequent
pod failed during provider container creation and was removed before any new
workload ran. The new complete reference results are local CPU measurements.
No cloud resource is retained by this appraisal.

Keep the current build/cache and compact receipts while their explicit
validation or failure-diagnosis purpose remains active. Remove superseded
executables, scratch sources and duplicate captures after recording their
conclusions. Preserve unresolved failure cases and exact provenance until a
replacement experiment resolves the same question. Do not retain whole obsolete
campaigns or copied source trees for speculative future use.
