# Standalone RunPod GUI for Robust modem simulations

**Implementation status:** the developer tool integrates RunPod management,
simulation and finite Live/Duplex controls with the native worker and local
audio companion. The shared GUI runs through an SSH relay to a separately
deployed worker. A short RunPod simulation decoded `e` on both a full RTX PRO
6000 Blackwell Server Edition and a full GeForce RTX 5090. Both used Vulkan for
coarse search and the native CPU receiver for fine processing. Local tests
exercised fake-device audio and one physical analog loopback.
**Finite Live/Duplex with physical audio is still experimental and not
qualified.** The live profile uses the native audio and transport components;
the simulation bridge rejects audio frames. A fake-device transport test has
covered capture stopping before a delayed drain, but delayed drain through the
complete receiver, other physical routes and remote transport remain unqualified.
A locally enforced deadline requests termination while the controller can
contact RunPod. The controller resumes pending termination checks at startup.

All RunPod API calls, SSH and credential handling belong to the separate lab
tool. This work does not add networking to Data Pump's modem, GUI, CLI or
socket-free pipe worker. The local lab currently runs from this source tree:
it imports `tools/preview.py` and serves shared `tools/` and `web/` assets, so
copying `_zLab/` alone does not produce an independently distributable package.

## Local controller

On Linux, run the controller from this checkout with Python 3 and its Tk module,
an available graphical display, a web browser and OpenSSH:

On Debian or Ubuntu, install the Tk module for the system Python if needed:

```sh
sudo apt install python3-tk
```

For a custom Python interpreter, install the Tk support matching that
interpreter. Then launch the controller:

```sh
python3 _zLab/app.py
```

Enter the RunPod API key in the masked field. The lab saves it in
`runpod-api-key` at the repository root. The file is gitignored, has
owner-only permissions (`0600`), and contains a single token. The lab loads
the key and catalog at startup and migrates previous lab key files.

Select compute and click **Start simulation**. After one billing confirmation,
the lab creates the pod, waits for SSH, deploys and checks the native worker,
then opens the shared simulation in the browser. Setup status shows the current
stage and elapsed time. Selecting an existing owned pod lets you continue its
setup. **Advanced** contains optional settings.

Use **End simulation** during setup or while the simulation is open. Closing
the simulation browser tab also ends its pod. Reloading starts a fresh
simulation with default settings on the same pod. Watch the selected row
change to **Terminating** and the status confirm termination. If termination
fails, select the affected pod and retry with **End simulation**.

In the simulation, choose a **Carrier** preset or enter a frequency with
units, such as `1850 Hz`. Presets are calculated for the current **Shift**.
The receiver uses Carrier minus Shift as its audio frequency.

The pod list shows active and pending work. The lab removes confirmed
closed rows. When a pod is absent from the current account and its deletion
is unconfirmed, the lab keeps the underlying ownership record and resumes
termination if the exact pod reappears. Closing
the lab waits briefly for cleanup. If a pod or connection is still pending,
the window offers to close while keeping the saved cleanup record.

Secure Cloud is the default. The dropdown shows current stock and compute list
price for one RTX PRO 6000, RTX 5090 or RTX 4090, or a Secure Cloud CPU with
32 vCPUs and at least 128 GB of RAM. CPU prices cover all 32 vCPUs. Refresh
the catalog to see changes in stock or price. Options with **none** stock are
hidden. If the selected option disappears on refresh, choose another option.

**Start simulation** checks the selected hardware and tier again when creating a pod. If stock
is **none**, the app stops before confirmation. Otherwise, review the fresh
stock, price, image and deadline in the confirmation dialog. Stock can change
before RunPod places the request. A recognized capacity rejection prompts a
catalog refresh. Billing and field-validation rejections report the provider's
reason when recognized. The final rate can include storage. For full SSH,
the image must support the official RunPod `/start.sh` startup script and a
directly reachable SSH port. The default image supplies this startup.
The GPU and CPU catalogs load independently.
[GPU catalog](https://docs.runpod.io/api-reference-v2/catalog/list-gpu-types)
and [CPU catalog](https://docs.runpod.io/api-reference-v2/catalog/list-cpu-types).

The interface reports a catalog
HTTP 403 with a prompt to check `catalog:read` and account permissions rather
than treating it as an empty catalog. During one read-only check, a key with
pod read/write permissions but Catalog Read disabled received 403; a new key
with the catalog scope successfully read both catalogs and listed zero pods
and queued requests. No pod was created by that check.

A subsequent live RunPod smoke used a user-created key with Catalog Read and
Pods Read/Write. It created and deleted two test pods; individual lookups then
returned HTTP 404 for both and the inventory showed neither. The first harness
waited for SSH details in a list response that did not include them. After the
test harness fetched the individual pod details, it observed a pod as RUNNING.
Its command preflight received HTTP 403 from `POST /v2/pods/{id}/exec` before
GPU inspection or deployment. The cause of that denial remains unknown. The
GUI uses SSH for both deployment and simulation.

The controller records its owned pod and queue requests under
`${XDG_STATE_HOME:-$HOME/.local/state}/pumpModem/runpod-lab/jobs.json`.
The controller reconciles requests after a lost response or restart. Each
creation requires a deadline. While open, the lab checks for expiration and
requests termination or queue cancellation. RunPod confirms termination
before the lab removes the row. Pending requests are saved locally
so the controller can check them again after a restart.

The simulation action expects the lab relay at
`/workspace/pump-lab/_zLab/worker_relay.py` and the native worker at
`/workspace/pump-lab/build/lab/datapump-worker` on the selected running pod.
Select the `simulation` worker profile and click **Start simulation**.
New pods receive a unique host key generated locally before creation through
the authenticated RunPod request. The lab also creates a client identity for
that pod and installs its public key. It checks the offered host key against
the saved fingerprint and uses strict SSH verification. Leave the optional
SSH fields blank for this automatic setup. The lab removes its managed SSH
credentials after verified termination.

For older pods, the lab reuses an existing verified host pin or obtains the
fingerprint from the authenticated startup log.
[RunPod pod logs API](https://docs.runpod.io/api-reference-v2/pods/stream-pod-logs)

For an older pod whose log is incomplete, wait for SSH startup and retry. If the API denies
log access, select the same pod in the RunPod console, open **Logs → Container**
and find the entry for `/etc/ssh/ssh_host_ed25519_key.pub`. Paste its
`SHA256:…` fingerprint into **SSH trust (optional)** after selecting that pod
in the lab. The controller checks the offered key against this fingerprint.
Changing the selected pod clears the pasted fingerprint. You can also supply
an independently verified `known_hosts` file for the exact endpoint. A key
mismatch stops the connection. Verify the pod, current endpoint and current
key independently before using a replacement file. The shared GUI sends the
remote draft and test modem key material to the pod. Use a designated lab key.

For an already running pod, [deploy.py](deploy.py) can upload an allowlisted
source bundle and build the worker. It needs Python, CMake, a C++ toolchain,
OpenSSL development headers and the relevant native build dependencies on the
pod; CUDA additionally needs a compatible toolkit, driver and C++11-ABI
CUDA-enabled LibTorch. The direct SSH port must be accessible and the account
public key must be installed on the pod. Confirm the exact host key through an
independent trusted channel before putting it in `known_hosts`; scanning a key
over the connection alone does not authenticate it. With the verified host and
port substituted below, and an SSH identity supplied explicitly or available
through the SSH agent, the CPU build is requested by:

```sh
python3 _zLab/deploy.py --host POD_HOST --port POD_PORT --user root \
  --known-hosts "$HOME/.ssh/known_hosts" --mode cpu
```

For a GPU job, the GUI requests `--mode gpu`. The deployer checks whether
CUDA can discover the device inside the worker's socket restrictions. With
compatible CUDA and LibTorch prerequisites and a passing probe it builds the
CUDA worker. If CUDA is unavailable there, it builds the native Vulkan worker
and configures a private NVIDIA EGL driver manifest for headless operation.
On the supported Ubuntu 24.04 amd64 image the deployer installs missing
Vulkan build dependencies from signed Ubuntu packages. The standalone CLI
defaults to `--mode cpu`. Use `--mode gpu` for automatic GPU selection or
`--mode cuda` to require a CUDA build.
GPU sessions enforce `--require-gpu`. Check the receiver's backend, scored
rows and stage counters to see where GPU work ran. Reopening a managed deployment
checks the source, build settings and worker hash before reuse. An interrupted
matching build can resume. A different source or an unmanaged directory requires
a fresh pod. A timeout or lost SSH connection may leave remote work active.
The remote deployment lock prevents two attempts from building concurrently.
The helper does not stop the pod. The source archive remains on the pod for deployment
verification; remove it after the build is verified and it is no longer needed
for diagnosis, or terminate the pod when the job ends.

When using the command-line deployment helper, enter the same verified
known-hosts file and, if needed, the same identity path in the GUI to open a
simulation with those credentials. The SSH invocations bypass SSH configuration and
proxies; supply a directly reachable endpoint and either an explicit identity
or an SSH agent. Known-hosts and identity paths cannot contain whitespace,
quotes, backslashes, `%` or `$`.

After deployment, select the owned pod in the controller and open its
simulation. This starts the shared GUI through the lab's loopback browser host
and the pod's anonymous-pipe worker. The short RunPod fixture covers one
simulation configuration. On a full RTX PRO 6000 Blackwell Server Edition,
automatic deployment installed the missing build dependencies and selected
Vulkan. The remote worker performed 780 GPU coarse-search rows and decoded
`e` through the shared GUI protocol. The run used the selected pod's
console-verified SSH key. A Secure Cloud RTX 5090 with the default image also
completed this fixture with 780 Vulkan search rows, DATA 0, 0, 1 and two
scored QUIET slots. [RTX 5090 validation](../docs/validation.md#rtx-5090-pod-creation-and-simulation--19-november-2026)

The experimental `live` profile deploys a separate lab worker under
`/workspace/pump-lab-live`; it can coexist with a simulation deployment. The
equivalent command-line deployment uses `--profile live` and an appropriate
`--mode`. The lab's live audio settings under **Advanced** ask for a locally
built native companion, a physical device, explicit capture and playback
limits, and independently established clock uncertainty and drift bounds. Those
values must cover the entire episode, including quiet slots and filter tails;
an exhausted bound fails capture. These components and controls do not by
themselves establish a qualified physical or remote live path.

Build the local companion on Linux from the repository root, with CMake 3.21+,
a C++20 toolchain, OpenSSL 3 development files and the project dependencies
described in the [build guide](../docs/building.md). These commands configure a
separate CPU Release build and compile only the companion target:

```sh
./build.sh configure --cli --web-worker --build-dir build/lab-local -- \
  -DCMAKE_PROJECT_DataPump_INCLUDE="$PWD/_zLab/audio/inject.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DDATAPUMP_PORTABLE=OFF \
  -DOPENSSL_USE_STATIC_LIBS=OFF
cmake --build build/lab-local --target datapump-lab-audio --parallel 2
```

Enter the absolute path to `build/lab-local/datapump-lab-audio` in **Local
companion executable**. Use **Refresh devices** in the controller, or run
`build/lab-local/datapump-lab-audio --list-devices`, to enumerate local devices.
Linux live audio requires the ALSA runtime `libasound.so.2` and a working
device or audio server route. Enumeration does not prove the selected device's
timestamp accuracy, duplex behavior or successful physical transmission.

For local, non-billable component checks:

```sh
python3 -m unittest discover -s _zLab -p 'test_runpod.py' -v
LAB_ISOLATED_X11=1 xvfb-run -a python3 -m unittest discover -s _zLab -p 'test_gui_integration.py' -v
python3 -m unittest discover -s _zLab -p 'test_host_identity.py' -v
python3 -m unittest discover -s _zLab -p 'test_worker_relay.py' -v
python3 -m unittest discover -s _zLab -p 'test_browser_bridge.py' -v
python3 -m unittest discover -s _zLab -p 'test_app.py' -v
python3 -m unittest discover -s _zLab -p 'test_bundle.py' -v
python3 -m unittest discover -s _zLab -p 'test_deploy.py' -v
```

The browser bridge checks require permission to bind a loopback listener. The
native-worker smoke check uses `build/dev-cli-web/datapump-worker` if available;
build it as described below for that coverage. The fake provider and SSH tests
do not contact RunPod or establish remote execution.

The controller requires Python Tk. Its dark-themed window rendered locally
under Xvfb using temporary distribution Tk libraries; nine nested-X11 GUI tests
covered masked key entry, status clipboard and catalog error/partial-load
handling, and a real Firefox view was checked. The ordinary Python environment
used for development did not have Tk installed. These GUI checks do not qualify
a physical audio device or a RunPod session. The source bundle is distinct
from the local controller and its browser assets.

One local CPU test exercised the relay and native worker with a strong, simple
link and draft `e`: it observed three DATA decisions, two scored QUIET slots,
and the decoded message. Run this bounded, opt-in fixture with
`LAB_RUN_SIMULATION=1 python3 -m unittest discover -s _zLab -p test_simulation.py -v`.
It is a single functional case with a four-logical-thread affinity cap, not
remote, GPU, sensitivity or capacity qualification. The provider, SSH and
browser tests likewise do not establish actual RunPod hardware identity or
GPU work.

An earlier CPU build from the extracted worker bundle passed its no-socket
self-check and the complete `tests/test_web_worker.py` suite in Release mode.
An unoptimized build of that closure had timed out after 15 seconds while
waiting for the large escaped-message acknowledgement; the Release result
does not erase that failure. A later refreshed bundle, with manifest SHA-256
`559459ed07cdd3a4ba28a7b56beba01135e8f6af0bca272dde3cd9fbd0820275`, built
the CPU Release lab worker and audio companion. The worker self-check passed
and the companion enumerated local software devices. This is not a source-tree
hash, GPU execution result or physical-device qualification. The temporary
build was removed; test-fixture changes after that build are separate. The
current `CCACHE_DISABLE=1 ./build.sh test gui` run passed 32 of 32 tests in
191.18 seconds; it does not qualify a physical device or a RunPod GPU.
The triad group failed one of 45 tests: `triad_receiver_probe` did not find a
`coarse_prepare` phase before its 32.01-second failure. A serial rerun passed;
the cause of the original failure is unconfirmed. The
[validation record](../docs/validation.md#native-finite-duplex-and-runpod-lab--18-november-2026)
retains the original group failure and rerun separately.

For the finite-audio boundary, the native GUI live test's
`--capture-backpressure`, `--buffered-live` and `--finite-duplex` targeted
checks passed locally. The first finite-duplex fixture had intermittently raced
its synthetic source ahead of scheduling and reported changed quiet rearm;
the targeted test passed after the fixture was corrected. The first integrated
synthetic live run failed after 132 seconds because its zero audio-error
assumption conflicted with nonzero timestamp uncertainty. A second attempt
failed with missing frame type 229; the relay was a strong candidate, but
suppressed child stderr left its cause unconfirmed. A separate older worker hit
a 30-second playback-credit timeout before a future scheduled start; a targeted
repair and a greater-than-30-second lead check followed.

A later real-time, prehistory-free synthetic run passed in 424.725 seconds on
CPU: the native session, relay, transport and fake device companion decoded
`e`, with DATA decisions `001` and two scored QUIET rows after the last DATA
row. The fixture did not check that their slot indices were adjacent, and the
raw rows were not retained, so this observation does not establish consecutive
quiet slots or independently verify physical completion. The worker and
fake-companion SHA-256 values for that run were respectively
`eecbba2d939e14edeb50d16b28571c4783d9f09294006519e0abed6a61d5fd80` and
`5d978283405ea816ec8d4e62a518b416d5221e4ba906957fcef73493313ad7b5`. That run
predates the repair of a false GUI "input ended" status.

A subsequent stricter real-time fake-device CPU holdout passed in 380.327
seconds without injected prehistory. It observed the native GUI decode `e`,
consecutive DATA `0`, `0`, `1` decisions followed by consecutive QUIET,
QUIET decisions, and the session's finite physical-completion drain status.
The worker binary SHA-256 was
`ca77684a472a7281843762c9053de812db86192e212170a6ab654c861c9a9495`; the
fake-companion binary SHA-256 was
`5d978283405ea816ec8d4e62a518b416d5221e4ba906957fcef73493313ad7b5`. These
hashes identify those binaries, not an independent source-tree hash. The test
did not intentionally slow the receiver or establish delayed backlog drain;
the backpressure checks above are separate targeted tests. This one
fake-loopback case does not qualify a physical device, the real network path,
RunPod GPU execution or receiver capacity.

A separate fake-device transport composition withheld capture credit until
capture stopped and then verified the preserved samples and dates during
drain, including early EOF and responsive GUI clock traffic. Three normal/EOF
pairs passed, followed by an independent two-test pass. An earlier timeout had
a likely buffered test-harness read race; the revised fixture passed these
repeats. This is transport-level evidence, not a complete-receiver or physical
backlog-capacity result.

One subsequent local physical test used the Realtek ALC257 analog `plughw`
route and the user-connected headphone-to-microphone loopback cable. A
preflight confirmed available ports and a physically correlated 997 Hz tone.
With 5% transmit volume, the crystal oscillator model, 48 kHz hardware and
6 kHz logical audio, one strict finite-duplex run decoded `e` with contiguous
DATA `0`, `0`, `1`, QUIET, QUIET and finite completion. The roughly 380-second
run recorded actual physical playback drain at 257.092 seconds and capture end
at 378.623 seconds in the transport trace. It recorded 2,220 playback PCM
packets, 187,381 capture packets and empty transport queues at the end. The
relative-clock bound was 50 ms and the local UTC uncertainty was a provisional
200 ms, not a certified clock measurement. The trace output's middle was
truncated: the successful harness exit establishes its checked predicate, and
the retained output showed the stated bit decisions; raw capture was not kept.
The tested worker and native companion SHA-256 values were respectively
`5b1945a37020040219b77a6622f42ef24a5156a245a397465d2f27865b723024` and
`4cfa9fa2310dc13c710783135371886e9a4d3e031147c9e24ae5605f00565195`.

An earlier default/Pulse route reported a discontinuity after contradictory
timing observations. Before a clock-message priority change, an attempt with a
5 ms relative bound reported that the pod/local clock difference exceeded its
declared bound; another with a 50 ms bound failed after 209.9 seconds. The
successful run used the same 50 ms relative bound and a 200 ms UTC bound after
the priority change, but the earlier per-probe round trips were not retained,
so its causal effect remains unconfirmed. One later run was interrupted with
exit 143, which is not a success or a failed reception decision. These findings
apply only to the tested local setup and do not establish physical-route
reliability, sensitivity, receiver capacity, RunPod execution or GPU work.

The remainder of this guide preserves the operating requirements, research
findings and unfinished acceptance checks for the requested complete tool.

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

- The controller matches **RTX PRO 6000**, **RTX 5090**, **RTX 4090** and
  **32 vCPU / at least 128 GB RAM** CPU types against RunPod's catalog. Verify
  the exact GPU edition and full-device allocation. The full Blackwell Server Edition was the measured
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
  segmented inverse FFTs** and zero credited HMMA/IMMA calls through the
  instrumented lag-matrix paths. Other library kernels' internal Tensor Core
  use was unmeasured. Moving eligible segmented search from CPU to GPU
  accelerated two complete slots by 2.38× and
  3.74×. The tested Tensor Core inverse took 1.87–4.01 times the FP32 cuFFT
  time including packing and reordering on the tested batches. That prototype
  demonstrates Tensor Core compatibility for those inverses. Other
  factorizations, fused kernels, layouts and precision choices remain open
  performance questions. Current CPU routing also leaves bulk numerical
  suboperations available for investigation. On CPU-only hosts, time one
  representative capture and slot before distributing trials.
  [Source, dispatch conditions and counters](../docs/triad-gpu-optimization.md#which-arithmetic-ran-in-a-remote-simulation)
  and [measured CPU/GPU choices](../docs/triad-performance.md#choosing-compute-for-hf-sensitivity-trials).

Future Tensor Core speed and utilization remain research questions. Candidate
techniques suggest experiments, with their feasibility and gains to be assessed
for the proposed implementation. Keep measured results, conditional bounds
and untested possibilities explicit when planning further work.
[How to interpret future performance](../docs/triad-gpu-optimization.md#interpreting-future-performance-and-utilization).

Follow the [RunPod deployment recipe](../docs/triad-performance.md#run-a-native-simulation-on-runpod)
for selection, upload, native build, execution, retrieval and teardown. The
campaign uploaded about 2 MB of allowlisted source and built on the pod. The
[bundle builder](../tools/triad_gpu_bundle.py) packages the probe closure.
The separate [lab bundle builder](bundle.py) adds the relay and the worker's
native build closure; it is not the local GUI package. Exclude local keys,
payloads, recordings, build trees and
account records from source uploads. Cache large compatible dependencies on a
network volume when that saves meaningful transfer/build time. Record who owns
that volume and when it can be released.

The GUI provides a masked, editable RunPod API-key field with private local
file storage. The API key authorizes pod management. SSH
authenticates the job connection, and the modem key selects the on-air waveform.
Keep them separate. The pod executing an encrypted test needs the selected
test's inputs and modem key material. Use an explicit lab key or test fixture
and show what will be sent. The API token and client SSH private key stay local.
The unique server host key travels only in the authenticated creation request
and is removed from the container startup environment before ordinary startup.

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

The controller persists a unique job name and returned pod or queue ID in
private user state. If creation times out, reconcile that name before retrying.
After a crash, cancellation or transport loss, reopen the owned job, recover
output when possible, and confirm termination. The deadline is enforced only
while the local controller runs with an API key, and automatic restart
reconciliation remains unimplemented. The v2 creation schema does not supply
an automatic termination deadline. Report any still-billable resource
and separately owned cache volume. Read current spending authorization before
launch. The previous campaign's private closeout is under
`${XDG_STATE_HOME:-$HOME/.local/state}/pumpModem/runpod/2026-11-18-hf/` on the
original host. It records how many instances ran, elapsed and summed compute
time, estimated charges, peak concurrent rate and deletion confirmations.

## Shared GUI controls and remaining requirements

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

For simulation, the existing Linux `datapump-worker --simulation` hosts the
shared C++ application, settings, drafts and UI snapshots. The live profile
uses a lab-only native worker linked to the same modem and GPU libraries, with
its dated-audio provider. The lab relay starts each with local anonymous pipes
and forwards framed events over SSH. The workers reject terminal, named FIFO
or socket-backed stdio and enforce a no-socket sandbox. Preserve that boundary
when qualifying CUDA. The native probe is the reference for physical
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
The lab's separate dated-audio boundary carries the required timing fields;
its fake-device integration has been exercised, while physical timing and
remote execution remain unqualified. Reject gaps or represent them as missing
according to native receiver semantics.
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

## Remaining acceptance checks

1. The local window, fake provider, native-worker simulation and component
   boundaries have the scoped checks recorded above. Complete the comparison
   of resolved settings and enabled choices with the production controller,
   including custom values and partial-byte drafts. Verify independent packaging
   and ordinary Data Pump targets with the lab absent.
2. Beyond the short native `e` simulation already exercised, inspect all six
   views, cancellation during setup and processing, and restart recovery in an
   integrated GUI/provider session. Compare returned samples, settings and
   decisions to a matched native reference. Reserve the long HF replay for a
   question a short fixture cannot answer.
3. The bounded native GPU simulation covers one full Blackwell Server Edition
   and a full RTX 5090. Exercise the CPU choice as a CPU job and run the same
   fixture on an RTX 4090. [Accelerator checks](../docs/gpu-precision-controls.md)
   and [representative receiver hardware](../docs/triad-performance.md#representative-receiver-hardware).
4. For Live/Duplex, the fake-device holdout and bounded transport backlog and
   EOF checks have passed within the scopes above. Still qualify local PCM and
   timestamps on a real device, then capture real playback on a loopback device
   or physical audio path and replay it through the remote native receiver.
   Compare with local decoding of that *same* capture. Qualify missing audio,
   deliberately delayed complete-receiver processing, early EOF and device
   cancellation on the applicable physical and network path. Show both quiet
   slots in order before reporting delivery.
   [Native session tests](../tests/test_gui_robust_live.cpp),
   [worker protocol tests](../tests/test_web_worker.py) and
   [host audio checks](../tests/test_host_audio.cpp).
