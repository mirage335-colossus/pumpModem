# High-fidelity triple Robust Modem appraisal

Status, 13 November 2026: **the contained native appraisal is runnable and its
four CTest groups, 32/32 canonical Triad checks, 20 runner unit cases, and
bounded smoke campaign pass.** The reference weak-signal pilot executed all
12 episodes but exposed two time limits and one valid EME reception failure;
VLF reference reception has not scored a DATA slot. Matched 65,536-cell EME
trials received one full DATA observation correctly at C/N0=30 dB-Hz and
returned an ambiguous decision at 10 dB-Hz; neither tested full-message
completion. The suite has material diagnostic results and unresolved
physical/receiver limits; it is not an environmental or universal sensitivity
qualification. See the [validation record](validation.md), [implemented model
inventory](models.md), [research reconciliation](research-reconciliation.md),
and [experiment protocol](experiment-protocol.md).

The current scenario catalog covers the requested 8.27 kHz, 60.96 m top-loaded
vertical; 1 MHz groundwave; 30 MHz nominal groundwave; 10 MHz HF skywave
controls; and 5.76 GHz EME. These are explicit calculated, reference-reproduced,
standard-statistical, and synthetic channel hypotheses. In particular, the
VLF circuit and magnetic receiver levels are separate reference planes; the
groundwave responses use fixed LFMF magnitude snapshots with synthetic phase;
the lunar channel is a finite frozen monostatic sphere approximation. See each
model's limits before treating an input as a physical link.

No scenario has independent environmental or local equipment calibration.
The suite has no LWPC/LMP VLF solve, local ground/terrain path survey, calibrated
HF transfer replay, or whole-disk 5.8 GHz EME IQ measurement. A passed renderer
check verifies its declared computation; it does not validate the environmental
model. A receiver outcome establishes behavior only for the frozen waveform,
public configuration, and modeled episode.

## Build and run

From the repository root:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

This uses a separate build directory at
`_appraisal/highFidelity-triple_Robust_Modem/.build`, links the canonical
native modem targets through a CMake project hook, and runs four CTest groups:
channel primitives, scenario models, source/channel/receiver integration, and
campaign-runner validation. The final post-correction run passed all four in
9.35 seconds; its runner group covers 20 unit cases. A separate canonical
Triad run passed 32/32 checks in 64.90 seconds. Its contained test harness used
a forced include for a missing header in the pre-existing CPU-only canonical
probe; the original setup failure is preserved and no modem source was changed
for this workaround. Full provenance is in [validation](validation.md). The build and log
remain ignored under the appraisal subtree. `build.sh configure` and `build.sh
build` are also available. The wrapper reads compatible compiler-cache entries
without writing outside the appraisal tree.

The native executable supports `plan`, `channel`, `receive`, `slot`, and
`oracle` modes. `plan` reports exact native bit/slot geometry, physical horizon,
sample/workspace limits, assumptions and source identities without rendering
or scoring. `channel` renders a bounded sample span. `receive` passes a
continuous stream to the canonical receiver through physical quiet completion.
`slot` receives only the first complete public DATA observation and cannot
establish message completion. `oracle` supports eligible Helper-off AWGN or
EME cases with white noise; it reports known-waveform binary error and a
separate known-signal presence calculation at the receiver's design false-alarm
level. Both oracle outputs assume known signal/channel details and exclude
blind acquisition or full-message success.

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --list
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal --mode plan --scenario hf-disturbed --waveform reference
```

For the bounded lunar comparison and selected-bit oracle:

```sh
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode slot --scenario eme-disk-slot --waveform reference --seed 8101 \
  --cn0 10 --chunk 256 --max-samples 60000 --max-wall-seconds 600
_appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --mode oracle --scenario awgn --waveform reference --oracle-slot 1 --cn0 -4
```

The lunar comparison takes several minutes on the tested CPU. Its channel
numerics are compared only through 30 physical seconds; the driver rejects
longer full-message runs for this profile. `slot` and `--oracle-slot` are direct
native options; the campaign manifest currently supports the other four modes
and the default oracle slot only.

For bounded repeatable campaigns, Python only reads the manifest, launches one
native process per episode, checks receipts and reports statistics:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/integration-smoke.json
```

The runner chooses a unique folder under ignored `local-evidence/`; an explicit
`--output-dir` must also be new and remain under that directory. The 10-case
integration manifest uses the quick `smoke` waveform, renders all five
environment families, checks a modest AWGN contrast, and includes noise-only,
EOF, and Helper controls. It exercises wiring and behavior on short geometry;
it is not the slow reference waveform, an environmental fidelity check, or a
sensitivity qualification. It completed 10/10 episodes in 31.255 seconds;
details and failure classification are in [validation](validation.md). The
separate 12-case `weak-signal-pilot.json`
uses reference waveforms and includes four physical fade/resume controls plus
AWGN duration and capture-gap controls. It is a bounded failure-finding pilot;
its 80,000,000-sample episode ceiling admits the long VLF geometry, while its
wall-time limit can censor unfinished work. It ran all 12 episodes: both
groundwave wanted cases, disturbed-HF skywave and both AWGN duration cases
completed; the EME wanted case at 10 dB-Hz had no physical completion, and
VLF wanted plus EME outage reached
time limits. This is failure-finding evidence, not a success-probability
estimate. The smoke, AWGN trend, weak pilot and paired CUDA campaigns used the
previous three-quiet-opportunity appraisal horizon. Later diagnostics record
the corrected two-quiet-slot horizon separately; the production receiver was
unchanged.

The corrected VLF reference has a **5,760.4 s DATA bit** and five-slot
**64,806,069-sample complete horizon** at 1,920 samples/s. It represents more
than 9 hours of calendar time and fits the pilot's 80-million-sample cap.
CPU and CUDA one-slot attempts at C/N0=20 dB-Hz accepted 12,966,069 samples but
timed out before scoring any slot; the CUDA run executed real FFT work but also
remained incomplete. These time limits are not sensitivity failures or a
measured VLF floor. A single VLF slot or compressed physical time cannot
substitute for full-message completion. See [validation](validation.md).

`--backend cuda|vulkan|gpu` is a request, not evidence. The local appraisal
build disables CUDA and Vulkan, but the separately compiled RunPod campaign
verified actual CUDA work on a full RTX PRO 6000 Blackwell Server Edition.
CUDA was slower than CPU in all three short paired episodes and utilization
sampling was too sparse to estimate throughput. Neither result qualifies GPU
speedup or complete-receiver capacity. CPU worker, owner, memory, sample, and
wall-time limits remain explicit.

For another authorized cloud run, `cloud-bundle.py` creates a manifest-checked
source closure (about 2 MB in this run), excluding credentials, builds and
captures. After transferring and extracting it into a fresh directory, use
`cloud-build.sh --cache-dir /workspace/appraisal-cache --preflight-only`, then
the same command without `--preflight-only`. The script's `--help` describes
the trusted CUDA development image, full-device checks and persistent-cache
requirements. Run the same native executable and `cuda-paired.json` campaign;
the scripts do not provision resources or authorize spending. This run used no
persistent volume, and its temporary pod was removed after result retrieval.

## What the evidence can support

The protocol separates implementation checks, independent channel validation,
native reception behavior, and computational capacity. Deterministic tests
cover the declared renderer, equations, scenario metadata, short native
message flow, physical quiet handling, the bounded runner, and explicit VLF
horizon-budget reporting. The `integration-smoke` campaign exercises short
geometry. Reference-pilot, EME one-slot and VLF timeout findings are separately
bounded and classified in [validation](validation.md).
The completed exploratory AWGN duration/level sweep shows a coarse transition
between its sampled low and high levels, but only two fixed seeds per receiver
cell; it does not establish a smooth sensitivity curve or confidence bound.
The [validation record](validation.md) explains why its equal-prior binary
oracle is not the receiver's fixed-false-alarm signal-detection limit and
preserves the complete-message limits.

All receiver cases assume the configured public calendar and keyed waveform
ordinal. They do not qualify unknown epoch, ordinal, timing, or address
acquisition. A theoretical AWGN curve cannot guarantee smooth results for
fading, physical outage, finite block decoding, or loss of identifiability.
Compare only with a reference whose channel, observation time, energy and
receiver knowledge match the claim. See the protocol for outcomes and stopping
rules.

Keep binaries, raw receipts, and generated traces as temporary evidence under
the ignored `.build/` and `local-evidence/` paths. Retain each campaign output
only while its stated investigation remains open; then preserve concise results
and unresolved failures in the maintained appraisal documents and remove the
redundant raw campaign. Do not copy reference papers, whole solver source trees,
or obsolete implementations into this subtree.
