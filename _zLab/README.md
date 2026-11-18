# Standalone RunPod GUI for Robust modem simulations

**Handoff for the next implementation chat, 18 November 2026.** Build an optional
developer application in `_zLab/` that runs the canonical Robust receiver on a
RunPod GPU or CPU. A local GUI should configure one simulation or one finite
Live/Duplex transmission, show the received data and plots, and release the
remote resources. FLTK is a suitable single backend. The user reserved
implementation for the next chat. The existing runnable tools are the native
receiver probe, the shared GUI and its Linux pipe worker.

Keep the new application independently built and packaged. All RunPod API calls,
SSH, transfers and credential handling belong to the lab tool. Data Pump must
remain usable offline with its existing dependencies. Its modem, GUI, CLI and
pipe worker must acquire no networking capability through this work. Source
paths below were checked at revision `c2f0c172`.

## Start from a known experiment

| Question | Result to carry forward |
| --- | --- |
| What can be reproduced? | At 2 mW, 180 dB loss and −164 dBm/Hz background, the 3.6 Hz unspread, nonhopping waveform at Target −10 dB-Hz decoded the complete `e` alarm twice on a stable path. Each bit lasted 962.4 s on a 990 s cadence. At 36 Hz, 10× interleaved and hopping, stable reception of both bit values took 1,797.6 s at Target −12.99. |
| What remains open? | Every scored severe 2 mW case erased. At 100 mW, 36 Hz/10× hopping and Target −21, a 12,069.6 s bit decoded on the same realization under both severe path laws. Other realizations erased. A site-specific atmospheric noise distribution, reliable severe alarm delivery and sustained real-time CPU backlog service remain unqualified. |
| How is that different from an SNR threshold? | Target selects a whole-tile duration. Power, path loss and noise determine the physical C/N₀, which was −12.99 dB-Hz at 2 mW and +4 dB-Hz at 100 mW. At fixed 256.8 s the measured stable and severe passing powers differed by 18 dB. At 100 mW, lengthening a bit could increase DATA presence while still leaving its value ambiguous. |
| Which comparison with FT8 was actually made? | In one stable matched-energy test, Robust delivered `e` in 4,964.55 s of input. Standard FT8 repeated the same alarm 228 times with no decoded message in 330 slots. The two decoders ran offline. Severe-channel FT8 superiority and live delivery latency remain open. |

The [58 editable experiment entries](../_tests-RobustModem-HF-simulations-2026-11-18.md)
include losses, timeouts, controls, bit-pattern and slot durations. Start with
the [measured operating points](../docs/indexes/robust.md#low-power-hf-alarm-results)
and the [native replay command](../docs/validation.md#reproducing-the-stable-alarm-and-severe-bit-tests).
Use `plan` for a cheap geometry check, `admission` for one scored bit or silence,
and `message` for the probe's fixed complete-message fixture. Preserve the
distinction between a physical erasure, a resource refusal and an interrupted job.

## Picking compute and running a pod

One pod with one native receiver is sufficient for one admitted job. The GUI
can run locally without renting a pod during development. Additional pods can
shorten a sweep of independent seeds, models or power levels. Each holds its
own receiver and memory. An individual ordered stream uses the resources on
its pod. Reuse an active compatible pod across a bounded batch.

- Offer the full **RTX PRO 6000 Blackwell**, **RTX 5090**, **RTX 4090** and a
  **32 vCPU / 128 GB RAM** CPU choice. Populate or validate the choices from
  RunPod's current catalog. The full Blackwell Server Edition was the measured
  development device. Record what the pod actually exposes, including full
  device or partition, VRAM, host memory limit, effective CPU allocation and
  backend. A matching model name in the dropdown is only a request.
- Preflight the complete receiver workspace and the capture spool. In one
  17,205.6 s hopping trial, 27 effective logical CPUs gave 54 software workers
  and a 204.57 GB host workspace quote. The 188 GB host could admit the matched
  13-CPU, 26-worker experiment at 144.37 GB. Even the smaller quote exceeds
  128 GB. Preserve the automatic twice-effective-CPU worker budget for capacity
  measurements. A stated affinity limit for a sensitivity comparison is a
  different experiment.
- Check the executed stage counters. Eligible matrix contractions can use FP16
  HMMA or INT8 IMMA. The long severe 100 mW success used **1,092,999,648 FP32
  segmented inverse FFTs** and zero credited HMMA/IMMA calls. Moving eligible
  segmented search from CPU to GPU accelerated two complete slots by 2.38× and
  3.74×. The tested Tensor Core inverse took 1.87–4.01 times the FP32 cuFFT
  time including packing and reordering. On CPU-only hosts, time one
  representative capture and slot before distributing trials.
  [Source, dispatch conditions and counters](../docs/triad-gpu-optimization.md#which-arithmetic-ran-in-a-remote-simulation)
  and [measured CPU/GPU choices](../docs/triad-performance.md#choosing-compute-for-hf-sensitivity-trials).

Follow the [RunPod deployment recipe](../docs/triad-performance.md#run-a-native-simulation-on-runpod)
for selection, upload, native build, execution, retrieval and teardown. The
campaign uploaded about 2 MB of allowlisted source and built on the pod. The
[bundle builder](../tools/triad_gpu_bundle.py) packages the probe closure.
A lab worker, launcher or extra GUI dependencies need their own verified
build closure. Exclude local keys, payloads, recordings, build trees and
account records from source uploads. Cache large compatible dependencies on a
network volume when that saves meaningful transfer/build time. Record who owns
that volume and when it can be released.

For the GUI, provide a masked, editable RunPod API-key field. Session-only
storage is a suitable default. The API key authorizes pod management. SSH
authenticates the job connection, and the modem key selects the on-air waveform.
Keep them separate. The pod executing an encrypted test needs the selected
test's inputs and modem key material. Use an explicit lab key or test fixture
and show what will be sent. Keep the API token and SSH private key local and
out of command lines, logs, presets and archives.

The current REST API uses bearer authorization at `https://api.runpod.io/v2`.
The catalog and pod calls use the `catalog:read`, `pods:read` and `pods:write`
scopes. Resolve hardware in `/catalog/gpus` or `/catalog/cpus`. Use
`POST /pods` with a template/image and either `gpu: {id, count: 1}` or
`cpu: {id, vcpuCount: 32}`. For the CPU choice, select a type whose range
includes 32 and whose RAM per vCPU yields 128 GB. For a GPU, distinguish
catalog VRAM from host RAM placement. Poll the individual `GET /pods/{id}`
until running and read `ssh.direct` for the host, username and mapped port.
Full SSH requires the image to run an SSH server, accept the public key and
expose TCP 22. The basic SSH proxy cannot carry SCP/SFTP. Retrieve results
before `DELETE /pods/{id}`.
[API migration](https://docs.runpod.io/api-reference-v2/migrate-from-v1),
[pod creation schema](https://docs.runpod.io/api-reference-v2/pods/create-a-pod),
[pod connection details](https://docs.runpod.io/api-reference-v2/pods/get-a-pod)
and [SSH modes](https://docs.runpod.io/pods/configuration/use-ssh).

Have the launcher persist its unique job name and returned pod ID in private
user state. If creation times out, reconcile that name before retrying. After
a crash, cancellation or transport loss, reopen the owned job, recover output
when possible, and confirm termination. Enforce a job deadline and reconcile
outstanding allocations on restart. The v2 creation schema does not supply an
automatic termination deadline. Report any still-billable resource
and separately owned cache volume. Read current spending authorization before
launch. The previous campaign's private closeout is under
`${XDG_STATE_HOME:-$HOME/.local/state}/pumpModem/runpod/2026-11-18-hf/` on the
original host. It records how many instances ran, elapsed and summed compute
time, estimated charges, peak concurrent rate and deletion confirmations.

## What the local GUI must expose

Bind controls to the production option IDs and validation. This includes
editable numerical inputs and custom values as well as fixed dropdowns.
The shared [console declaration](../src/gui/screen_console.cpp),
[settings controller](../src/gui/controller.cpp) and [GUI contract](../docs/gui-contract.md#modems-and-current-availability)
are the inventory for current choices and defaults.
The [launch settings parser and formatter](../src/gui/launch_command.hpp)
already handles many preset fields. Keep the draft, local audio choice and
provider job settings separately typed. Send user-entered values as data.

| Controls or view | Implementation detail that matters |
| --- | --- |
| Encryption and key selection, Helper Carrier Callsign, Helper and DATA transmit power, path loss, noise | Preserve key and Helper policies, units and the Helper-off case. Separate simulated noise in dBm/Hz from the live input reference in dBFS/Hz. |
| Clock sync, Region, Offset, audio error, base and shift oscillator, Doppler/delay spread, additional frequency-selective modulation, Margin | Preserve the public calendar, initial keyed ordinal and selected receiver search. Support stable simulation with the severe receiver search unchanged so the comparison is repeatable. |
| Added Symbols, Spreading, FHSS, Symbol_Burst, Rate, Carrier, Shift, Target SNR | Added Symbols Off resolves to one native lane. Interleave selects the spreading factor. The GUI subtracts Shift from Carrier to obtain the DATA IF. For the HF fixture, Carrier is 30.001725 MHz and Shift is 30 MHz, yielding 1,725 Hz. Fake FCC is a baseband dwell schedule. Keep planned duration and slot cadence visible. |
| Message and Binary | Reuse the shared editor for partial-byte raw bits, escaped text and the 16-byte editable binary prefix. Exact raw bits and text follow different native encode paths. Carry the actual draft through the job. [Binary editor](../src/gui/binary_editor.hpp) |
| Signal Browser, Spectrum, Live waveform, Receiver input I/Q, Pattern evidence and Pattern steps | Render bounded observations and ordered score/quiet events from the running receiver. Preserve source, sample coordinates, gaps and admission outcomes. Use existing received-text restrictions in all outputs. [Session snapshots](../include/datapump/live.hpp) and [receive presentation](../docs/security.md#received-content-presentation) |
| Simulation or finite Live/Duplex | Only live device I/O uses the selected audio device, left mono, right mono or stereo, TX volume and exclusive mode. Apply routing and final volume on the local physical device. Show negotiated format, drain and any discontinuity. |

## Reuse the native session and keep networking in the lab

For the first simulation, the existing Linux `datapump-worker --simulation`
already hosts the shared C++ application, settings, drafts and UI snapshots.
A pod-side lab launcher can start it with local anonymous pipes and relay the
framed events over authenticated SSH. The worker rejects a terminal, named FIFO
or socket-backed stdio and enforces a no-socket sandbox. Keep that boundary
when qualifying CUDA. If the driver cannot operate in that composition, use a
lab-only native runner linked to the same modem and GPU libraries. Keep the
standard worker policy intact. The native probe is the reference for physical
outcomes and backend counters. Its fixed `message` fixture cannot replace
arbitrary GUI drafts, audio capture or diagnostic views.
[Worker build and tests](../docs/building.md#browser-application-and-pipe-worker),
[DPW1 framing](../docs/web-protocol.md) and [shared session API](../include/datapump/live.hpp).

Start on Linux with the commands below from the repository root. The protocol
test launches a local simulation worker, reads its GUI snapshot and exercises
the pipes and shutdown behavior. This gives the lab launcher a working local
peer before any RunPod integration.

```sh
./build.sh --cli --web-worker
build/dev-cli-web/datapump-worker --self-check
python3 tests/test_web_worker.py build/dev-cli-web/datapump-worker
```

For reproducible research jobs record source and binary identities, resolved
settings, seed, channel law, calendar/ordinal, time and memory limits, and
actual backend. The normal GUI simulation generates fresh noise. The probe
supplies explicit seeds. Return bit duration, slot cadence, received or erased
bits, quiet-slot completion, wall time by stage, sample counts and peak
workspace. A dry run can show planning estimates before any paid execution.
[Profile preparation](../include/datapump/robust_profile.hpp) and
[native receiver probe](../tools/triad-receiver-probe.cpp).

### Finite Live/Duplex with physical audio

The pod should generate the transmit waveform and process the captured input.
The local machine plays it through the selected physical output and captures
the physical input. Prebuffer enough playback to meet the device schedule.
Start capture early enough for the public window and filter support. Keep
it running through the two actual quiet slots required for completion. Bound
the pending audio to that one transmitted episode, using the known local TX
plan and explicit sample/byte limits. A remote processing backlog may drain
after capture ends. Report TX drain, capture cutoff and receiver commit as
separate events. Only receiver decisions can complete the message.

Preserve local frame number, sample rate, UTC date and uncertainty, timing
quality and discontinuities alongside each range of PCM. Network arrival time
cannot date a sample. The current DPW1 capture frame contains stream position
and PCM but lacks the provider UTC information required by Robust live RX.
The native audio tool's `mapped-v1` also describes sample coordinates.
Implement and verify the dated capture boundary in the lab adapter. Reject
gaps or represent them as missing according to native receiver semantics.
Use the existing native real-PCM projection, filter delay and validity
tracking. An EOF, timeout or short capture must leave the result incomplete.
[Native audio API](../include/datapump/audio.hpp), [dated live receiver](../src/live.cpp),
[PCM conversion](../include/datapump/robust_audio_frontend.hpp) and
[bounded device tool](../tools/triad-audio-stream.cpp).

Budget audio separately from the source archive. At 48 kHz one float32 mono
stream uses 192,000 bytes/s, about 953 MB for the 4,964.55 s alarm, per
direction before overhead. The experiment's 6 kHz complex modem rate is a
different quantity. The current DPW1 frame limit is 8 MiB. Keep audio flowing
when plots refresh, spool in bounded chunks and surface overruns. Verify
actual local device behavior separately from simulated or offline replay.
[Audio timeline and flow control](../docs/web-protocol.md#audio-timeline).

## First implementation and acceptance checks

1. Build the standalone GUI independently. Compare its resolved settings and
   enabled choices with the production controller, including custom values and
   partial-byte binary drafts. Build the ordinary Data Pump targets with the
   lab absent. Check the link/package dependencies and the existing worker
   no-socket tests.
2. Exercise the local GUI and pod launcher against a local child plus a fake
   provider. Run a short native simulation end to end. Inspect all six views,
   cancel during setup and processing, simulate a lost create response and
   recover a job after restart. Compare the returned samples/settings and
   decisions to a matched native reference. Reserve the long HF replay for
   a question a short fixture cannot answer.
3. Use one admitted GPU pod for a bounded native smoke test. Retrieve the
   hardware identity and evidence of executed GPU work, then verify teardown.
   Test the CPU choice as a CPU job. Keep the RTX 4090/5090 entries labelled
   by the coverage actually achieved. [Accelerator checks](../docs/gpu-precision-controls.md)
   and [representative receiver hardware](../docs/triad-performance.md#representative-receiver-hardware).
4. For Live/Duplex, qualify local PCM and timestamps first, then capture real
   playback on a loopback device or physical audio path and replay it through
   the remote native receiver. Compare with local decoding of that *same*
   capture. Test missing audio, delayed processing, early EOF and device
   cancellation. Show both quiet slots in order before reporting delivery.
   [Native session tests](../tests/test_gui_robust_live.cpp),
   [worker protocol tests](../tests/test_web_worker.py) and
   [host audio checks](../tests/test_host_audio.cpp).
