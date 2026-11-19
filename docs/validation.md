# Validation status

The native CPU automatic policy uses qualified Q15 spectral products with FP32
FFTs on its supported AVX2 one-ray geometry, and FP32 or FP64 on other paths.
An earlier bounded FP32-versus-FP64 comparison improved a challenging five-slot
workload by **1.196×**; that historical comparison is not a current Q15 result.
The local Radeon Vulkan backend uses qualified packed-FP16 direct work and passes
actual-device correctness checks. Its earlier **1.269× complete coarse-phase
speedup** and narrow-case **39.6% slowdown** compare against the historical FP64
CPU path. Cold compilation remains a separate cost below.
Earlier Ryzen CPU and RTX 4090 CUDA results retain their original source and
hardware scope. No complete receiver-capacity or Tensor Core utilization claim
follows.

Updated 18 November 2026. The [low-power HF study](#bounded-2-mw-hf-fhss-investigation--18-november-2026)
covers the severe-channel 2 mW misses, the conditional 100 mW success and
the two complete stable-path alarm replays. The prior native history, matrix/preparation and
wide-search follow-through remains complete with its scoped checks below,
including the fine-tail repair. Current overlap follow-up has focused functional
and paired service evidence below; complete-receiver capacity and refreshed CPU
forecasts remain unresolved.
The earlier
bounded native throughput/precision investigation
from `6bc9b0e3dd0ca91b3b8e3f4851301e6cf1b1371d` is complete. It supports scoped
GPU phase improvements, three component forecasts and an accepted CPU latency
tradeoff; complete-receiver capacity and nominal/conservative GPU-assisted CPU
forecasts remain unresolved. The earlier migration checkpoint remains the
baseline. Historical measurements retain their original source/runtime.
None is release certification.

The historical cleanup checkpoint was based on
`357a50970a0c6214eeaade7c5704bbc39abd4b79` and committed as
`419e6bae8c4fe3b0f59be23a6dfb3b69fbaed80e`. Its concise outcomes and limitations
remain below; they are not release certification or validation of changed code.

The native CPU receiver's serial steps now overlap parallel work within a slot,
and its transform visits the same butterflies in a cache-blocked order. Matched
replays are 1.4–2.0× shorter with identical receipts on the one measured host.
Earlier local CPU phase costs no longer describe this work; see the
[follow-through](#receiver-overlap-and-transform-order-follow-through).
Helper acquisition now uses the slot's lanes and, where the resource shape
allows, can run beside coarse preparation. Its workspace bound follows the
public calendar, so it no longer sets a Helper slot's lease; see the
[Helper follow-through](#helper-acquisition-overlap-follow-through). The exact
one-arrival coarse profile disables Helper; its bank and observation filter
already overlap, so Helper overlap is inapplicable to that profile.

## Native finite-duplex and RunPod lab — 18 November 2026

This checkpoint uses working-tree changes over baseline
`d25df5e3bdd956c8f1eccba76b090a27bb37848d`; that commit alone does not identify
the changed sources. The optional one-message finite-duplex seam, dated bounded
capture provider and lab worker are integrated with the shared native session.
They retain scored quiet completion and require a provider-accepted physical capture endpoint;
see the [current architecture](robust-current.md#configured-calendar-reception)
and [lab operation and detailed test record](../_zLab/README.md). The local
`CCACHE_DISABLE=1 ./build.sh test gui` passed 32 of 32 tests in 191.18 seconds.
The targeted `test_gui_robust_live` `--capture-backpressure`, `--buffered-live`
and `--finite-duplex` checks passed, including accepted/refused endpoint,
quiet-rearm invalidation, early EOF and cancellation cases. An earlier synthetic
fixture raced its quiet state before scheduling; its targeted check passed
after correction. `CCACHE_DISABLE=1 ./build.sh test triad` did not pass: 44 of
45 tests passed, and `triad_receiver_probe` failed after 32.01 seconds with
`StopIteration` while seeking a `coarse_prepare` phase in
`tests/test_triad_receiver_probe.py:366`. An isolated serial rerun passed 1 of
1 in 27.20 seconds (27.54 seconds independently observed). Parallel deadline
contention is plausible but the original receipt was not printed, so the
cause is unconfirmed. The isolated pass does not turn the original group into
a pass.

One strict real-time CPU fake-device holdout, without injected prehistory,
decoded `e` through the native session, relay, transport and fake companion in
380.327 seconds. Its checked slots were consecutive DATA `0`, `0`, `1`, QUIET,
QUIET, followed by finite physical-completion drain status. The worker and fake
companion binary SHA-256 values were respectively
`ca77684a472a7281843762c9053de812db86192e212170a6ab654c861c9a9495` and
`5d978283405ea816ec8d4e62a518b416d5221e4ba906957fcef73493313ad7b5`;
these identify the tested binaries, not an independently hashed source tree.
The test did not intentionally slow reception or qualify backlog drain. A
separate bounded fake-device transport composition held capture credit until
the physical-capture fixture stopped, then checked exact PCM, frame coordinates,
absolute dates, timing quality and transport-added uncertainty while the GUI
clock and heartbeat remained responsive. It also checked early EOF remains
incomplete. Three repeated normal/EOF pairs passed 6 of 6, and an independent
rerun passed 2 of 2 in 10.351 seconds. An earlier composition check timed out;
a buffered `select`/read race in the test harness was a likely cause, and the
revised harness passed these repeats. This composition does not exercise the
complete native receiver or establish real-device or network backlog capacity.
An earlier fake-device attempt failed because its zero audio-error assumption
conflicted with nonzero timestamp uncertainty; another failed with missing frame
type 229, with cause unconfirmed. A separate older worker's future-start
30-second credit timeout had a targeted repair and follow-up check. The
preceding successful fake-device run did not check consecutive quiet indices.

The refreshed bundle's manifest SHA-256
`559459ed07cdd3a4ba28a7b56beba01135e8f6af0bca272dde3cd9fbd0820275`
identified a CPU Release archive that built the lab worker and local companion;
the no-socket self-check passed and the companion enumerated software devices.
An earlier unoptimized worker protocol run timed out on a large-message
acknowledgement; its Release counterpart passed. These local checks do not
qualify physical audio, network or RunPod operation, actual GPU work, receiver
capacity, communication probability or unknown-calendar acquisition. No cloud
pod was started for this validation. Earlier source-bound evidence remains
subject to its stated limits.

## Local physical finite-duplex loopback — 19 November 2026

On the Realtek ALC257 analog `plughw` route with the user-connected
headphone-to-microphone loopback cable, preflight confirmed available ports and
a physically correlated 997 Hz tone. One bounded CPU run used a crystal
oscillator model, 5% transmit volume, 48 kHz hardware and 6 kHz logical
audio. The strict native session/relay/local-companion harness exited zero and
checked decoded `e` with contiguous DATA `0`, `0`, `1`, QUIET, QUIET and finite
completion. The transport trace recorded physical playback drain at 257.092
seconds and capture end at 378.623 seconds, with 2,220 playback PCM packets,
187,381 capture packets and empty transport queues at the end. The run lasted
about 380 seconds. The retained terminal output was truncated in the middle:
the harness exit is evidence of its predicate, and visible output showed the
bit decisions, but the full trace and raw audio were not retained.

The worker and local native companion binary SHA-256 values were respectively
`5b1945a37020040219b77a6622f42ef24a5156a245a397465d2f27865b723024` and
`4cfa9fa2310dc13c710783135371886e9a4d3e031147c9e24ae5605f00565195`.
The test used working-tree changes over baseline
`e7c8b815ffe9cbcd7433ed56ef6a65848a83d5ae`; neither the baseline commit nor
those binary hashes establish an independently hashed complete source tree.
An instrumented Python transport copy printed numeric clock samples. The
successful setup declared a 50 ms relative-clock bound and a provisional
200 ms local UTC uncertainty; neither is a certified clock measurement.

An earlier default/Pulse route reported a discontinuity after contradictory
timing observations. Before a clock-message priority change, a 5 ms relative
bound reported that pod/local clock difference exceeded its declared bound,
and another 50 ms-bound run failed after 209.9 seconds. The successful run used
the same 50 ms relative bound after that change, but prior per-probe round-trip
times were not retained; the change's causal effect is unconfirmed. One later
run was interrupted with exit 143, not a reception decision. This single
physical-path pass does not qualify other devices, route reliability,
sensitivity, receiver capacity, unknown-calendar acquisition, real network,
RunPod or GPU work.

Separately, a read-only RunPod check found HTTP 403 on a key with pod read/write
permissions and Catalog Read disabled. A new key with catalog scope successfully
read both GPU and CPU catalogs and listed zero pods and queued requests; no
paid pod was created. Nine nested-X11 GUI checks covered masked key entry,
clipboard, dark presentation and catalog error/partial-load behavior; the real
Firefox dark view was also inspected. These results do not qualify a RunPod
simulation or GPU backend.

A subsequent RunPod lifecycle smoke with a user-created key granting Catalog
Read and Pods Read/Write listed both catalogs, created two test pods and deleted
both. Individual lookups returned HTTP 404 for each after deletion, and neither
appeared in inventory. The first harness waited for SSH details in a pod-list
response that omitted them. The corrected harness fetched individual details
and confirmed RUNNING. Its command preflight received HTTP 403 from
`POST /v2/pods/{id}/exec` before GPU identity inspection or worker deployment.
The denial's cause remains unknown.

The lab now verifies the selected pod's SSH host key using its container
startup log. A local integration test combined simulated pod-detail and log
endpoints with a real OpenSSH server and temporary keys. An incorrect log
fingerprint stopped enrollment before a key file was written. A matching
fingerprint allowed the GUI connection helper to enroll the key and OpenSSH
to execute `true` with strict host checking. After the server restarted with a
different key on the same port, OpenSSH refused the connection (exit 255).

On a fresh RunPod GPU pod the read-only container-log endpoint also returned
HTTP 403. The signed-in console displayed the Ed25519 host fingerprint under
**Logs → Container**. The lab accepts this fingerprint for the selected pod
and compares it to the SSH key offered at its current direct endpoint.

The verified pod exposed one full RTX PRO 6000 Blackwell Server Edition
with 97,887 MiB visible. With CUDA 12.8 and driver 595.112.07, device
discovery reported one GPU outside the worker. Under the worker's existing
socket restrictions, `cudaGetDeviceCount` returned error 304. A trace of
successful discovery showed use of an abstract local UVM socket. The lab
preserves the socket restriction and supports headless NVIDIA Vulkan.
In a bounded remote simulation the native receiver reported 780 Vulkan
coarse-search rows. Fine processing and tracking ran on the CPU.

On 19 November 2026 a repeat on the same stock-template pod used the GUI's
automatic deployment function, the verified pod details and the previously
matched SSH host key. It started with a fresh deployment directory and four
missing development packages. The deployer installed those packages from the
signed Ubuntu 24.04 repositories, selected Vulkan and built worker SHA-256
`ed25eff954c3636ba9eb97aba35fb6fcd44246810536d822712255874282b2d9`.
The test sent the short `e` draft through the local HTTP GUI bridge and real
SSH relay. The receiver again reported 780 Vulkan search rows and decoded
`e` with indexed DATA decisions 0, 0, 1 followed by two scored QUIET slots.
After termination the exact pod lookup returned HTTP 404 and both pod and
queue inventories were empty. Subsequent adjustments to GUI and deployment
timeouts and package-index downloads received local unit coverage. The live
test exercised the GUI protocol programmatically. Remote physical Live/Duplex,
receiver capacity and sensitivity remain open.

### RTX 5090 pod creation and simulation — 19 November 2026

The authenticated, one-GPU pod catalog initially reported no RTX 5090 stock
in either tier. When Secure Cloud changed to low stock, the Tk controller
showed its billable confirmation there. Community Cloud still showed no
stock and the same button stopped before confirmation. In an isolated X11
test, a simulated provider returned two positive stock reads and rejected
the real REST client's single create POST with HTTP 400 for capacity.
The controller displayed the refresh guidance and
removed the rejected request from its ownership records. The original
screenshot preserved only the HTTP status, so its exact cause is unknown.

A Secure Cloud request using the lab's ordinary creation and deployment
functions placed one full NVIDIA GeForce RTX 5090 with 32,607 MiB visible.
It used the selected `runpod/pytorch:1.0.2-cu1281-torch280-ubuntu2404`
image and a console-verified SSH host key. The shared GUI protocol returned
decoded `e` and consecutive DATA 0, 0, 1, QUIET, QUIET. The first test
parser expected the GPU name at the end of the diagnostic and missed the
trailing modeled-duration and calendar fields. Its GPU row count was lost.
The original pod was deleted and its absence verified.

The corrected checker matched the complete device field ahead of the timing
suffix. A fresh stock check initially blocked the repeat. When stock returned,
the same creation path placed a second full RTX 5090 with 32,607 MiB visible
and NVIDIA driver 595.112.07. The default image built the native Vulkan worker
with SHA-256 `ed25eff954c3636ba9eb97aba35fb6fcd44246810536d822712255874282b2d9`.
The receiver reported 780 Vulkan coarse-search rows on that device and native
fine processing. It decoded `e` with DATA 0, 0, 1 and two scored QUIET slots.
Teardown was confirmed through the individual lookup and the pod and queue
lists. This short fixture covers simulation through the real SSH GUI bridge.

The standalone suite passed all 130 tests with the opt-in native simulation
and an isolated X display, including 17 Tk tests. At the 920×680 default
size the widened compute selector showed the full model, stock and price.
A preceding suite run exposed a test race when a descendant exited between
opening and reading its Linux process status file. The corrected check
accepts either kernel indication that the process disappeared and still
fails if it stays alive.

### Automated RunPod setup — 19 November 2026

The standalone controller now reloads a private local API-key file and loads
the catalog on startup, including after pending deadline checks. The compute
dropdown excludes unavailable stock. Its primary Start simulation action
creates or continues an owned pod, waits for SSH, deploys the native worker
and opens the shared simulation GUI.

New pods install a unique locally generated SSH host key through the
authenticated creation request. The saved fingerprint authenticates the direct
endpoint. A separate per-pod client identity allows access without editing
account SSH settings. The startup shell removes the host-key variable before
the official image exports its environment. The GUI limits displayed pod
details to fields that exclude the bootstrap secrets.

Deployment retries bind the private directory to the source digest, build
script, profile and requested backend. A completed matching worker must pass
its checksum and self-check before reuse. An interrupted matching build can
resume. A remote lock prevents overlapping builds. A small-pipe fixture found
a deadlock in synchronous script delivery before output draining. Passing
the script as a private temporary file resolved it, with a bounded regression
covering 4 KiB pipes and 64 KiB of early output.

All 147 standalone tests passed with the isolated X display and short native
simulation enabled. The tests cover key persistence and clearing, hidden stock,
automatic setup with log access denied, same-pod retry and startup deadline
checks. The default-size window remains covered.

The real Tk primary action loaded the saved key, created one full NVIDIA RTX
PRO 6000 Blackwell Server Edition with 97,887 MiB visible and driver
595.112.07, verified its generated host key and deployed the worker. The test
made log and remote-command endpoint calls fail immediately if attempted.
The native worker decoded `e` through the HTTP GUI bridge and SSH relay with
780 Vulkan search rows, consecutive DATA 0, 0, 1 and two scored QUIET slots.
Worker SHA-256 was
`ed25eff954c3636ba9eb97aba35fb6fcd44246810536d822712255874282b2d9`.
Closing the session and invoking Start simulation again reused the same
verified worker and reached a ready native session. There was one creation
request and one billing confirmation across both opens. The test allocation
was deleted and its absence verified. This covers the new simulation setup
workflow. Physical remote Live/Duplex retains its separate qualification work.

### RunPod browser and termination lifecycle — 19 November 2026

The baseline Firefox reload sent a close beacon with `Origin: null`. The
local origin check rejected it with HTTP 403 and the reloaded page received
HTTP 409 because the previous page still owned the worker. A separate
startup reproduction returned HTTP 200 before a failed worker exited, then
showed a generic disconnect. The lab now waits for native readiness and
lets the owning tab reconnect to the same worker after a reload.

A real Firefox run through the compact Tk controller created one full RTX
PRO 6000 Blackwell Server Edition with 97,887 MiB visible and NVIDIA driver
595.112.07. It rendered the simulation, reloaded with a fresh browser lease
on the same worker and decoded `e` with 780 Vulkan coarse-search rows,
DATA 0, 0, 1 and two QUIET slots. The fixture observed no browser errors.
After the tab and isolated browser process closed, the controller changed
the exact pod row to **Terminating** and then **Terminated**. RunPod returned
HTTP 404 for that pod and the inventory showed its absence. A separate
local Firefox test left the browser open after closing the tab and observed
session end after about eight seconds.

The existing API key migrated to the ignored checkout file
`_zLab/runpod-api-key` with mode `0600`. The new file authenticated to RunPod,
the old copy was removed and the native source bundle excluded the key.
The standalone suite passed 154 tests, including 21 isolated Tk cases.
The shared preview client and its host and native-worker tests also passed.

A later close regression involved an older **Terminating** row with an exact
pod ID and no saved delete receipt. RunPod returned HTTP 404 for that ID and
listed no matching pod or queued request. The lab now preserves the record as
**No longer visible** and resumes its saved termination intent if the exact pod
returns. Repeating Close keeps the original six-second deadline. When provider
or local cleanup exceeds that deadline, the app offers to close with the
pending record saved.

Using a temporary copy of the affected record and read-only RunPod requests,
the prior window failed to close within ten seconds. The corrected window
closed in 1.41 seconds, with no prompt or callback error. The lab's normal
ownership check then repaired the actual row under the controller lock. The
verification sent no cloud mutations. The final standalone suite passed
162 tests, including 26 isolated Tk cases.

### RunPod setup, Carrier presets and reset — 19 November 2026

The old End action returned while another provider operation was in progress.
The lab now saves End under the exact request name even during creation.
The setup thread stops opening new sessions after End. The isolated Tk tests
exercise late create acceptance, cancellation during deployment and exact
provider removal. Confirmed closed rows and their SSH files are pruned.
Unconfirmed invisible requests remain recoverable outside the active list.

The root `runpod-api-key` file replaces the earlier lab-directory file. The
current key migrated with mode `0600` and authenticated to RunPod. Git ignores
the file, and the actual native source bundle excluded it. The two old local
rows were removed after fresh individual HTTP 404 checks and empty pod and
queue inventories.

The earlier browser could retain an invalid Carrier notice after correcting
the Carrier and Shift pair. The current native controller offers four
editable Carrier presets relative to Shift and clears the error when the
pair is valid. A local Firefox walkthrough changed the preset and typed
`1850 Hz`, decoded `e`, then verified reload restored the Carrier, Shift,
Rate, draft and result defaults. Reload restarts the native simulation
worker on the existing pod and rejects requests from the old browser page.

Start now shows the setup stage and elapsed time. An actual RunPod test
identified Ubuntu package-index downloads as the slowest part reached on
that pod. End was invoked in the standalone window during this stage.
The lab terminated the exact test pod before any simulation page opened.
The individual lookup returned 404 and the pod and queue lists were empty.

A second cold start used the stock RunPod image on one full RTX PRO 6000
Blackwell Server Edition. Setup took about nine minutes. Package-index and
dependency installation took 7:26 of that, including a 19.3 MB Ubuntu
`noble/universe` index on this host. In Firefox, the test selected a Carrier
preset, entered `1850 Hz`, and decoded `e` with 780 Vulkan search rows and two
scored quiet slots. Reload reset the settings, draft and results on the same
pod. The browser reported no script errors. End in the standalone window
showed **Terminating**, removed the row, and left the pod and queue inventories
empty. An individual lookup returned HTTP 404. Each test used one pod and no
persistent volume. These simulations do not qualify physical Live/Duplex.

Vulkan setup now tries five signed amd64 indexes and fetches the base Universe
index only if APT needs it to resolve the install. With the verified Ubuntu
Base 24.04.5 amd64 package status and the current signed indexes, APT resolved
all four packages and their dependencies without the base Universe index or
any removals. Shell tests cover the narrower path, full fallback, and a
failed update on either path. The revised cold-start time on RunPod remains
unmeasured.

The final standalone suite passed 177 tests, including 29 isolated Tk
cases. The named native GUI suite passed 32 tests. The focused native GUI
policy, worker and renderer tests and shared preview-client checks passed.

### Computed Robust carriers and simulation replay — 19 November 2026

The previous browser worker produced 62 updated Spectrum bitmaps with valid
frequency coordinates and completely black pixels for the short simulation.
Absolute simulated link attenuation had been applied against a fixed
display reference. The revised plot references the modeled mean Hann-bin
noise and retains the activity frames after replay.

Carrier recommendations use the native occupied and gated DATA band, the
declared oscillator and channel allowances, and the Helper placement guard.
The worker fixture covers the half-rate preset at 1 Hz, a custom carrier
translated by Shift, Doppler and spreading exclusions, effective Helper
power and a 5 Hz upward rounding case. The Link Planner uses the same
band for its display. The native profile still validates the prepared
waveform's actual band before processing.

Finite replay records a bounded sample slice alongside full receiver
processing. The slice uses actual post-channel samples and begins at the
first DATA activity, or the first Helper activity for Helper-only sends.
The receiver processes the entire input and both quiet slots before
the three-second display replay. Replay cancellation leaves a completed
reception available.

Local Firefox used the real lab bridge and CPU worker. It displayed the
1 Hz presets, preserved a custom 1840 Hz baseband carrier across a 100 kHz
Shift and back, and decoded the test message `e`. The retained default
waterfall showed 12,943 lit pixels out of 18,260. A controlled stronger
link took 3.02 seconds in replay and had a palette peak of 208 in the
occupied band versus 135 off-band. Both browser runs had no script errors.
The separate lab-relay fixture also checked three DATA decisions, two
scored quiet slots, the decoded message and retained waterfall contrast.

The final native groups passed 44 Robust tests and 32 desktop GUI tests.
The standalone suite passed 177 tests with its isolated X11 and native
simulation cases enabled. All 12 web checks outside the separate Fast
audio timing case passed. The Fast browser-audio check initially observed
a 48 kHz capture overrun and a playback underrun in separate runs. When run
by itself it passed both 48 and 44.1 kHz in 224.01 seconds. The final
carrier grid adjustment was rebuilt and verified in the inherited-pipe
web worker and native GUI policy tests.

### RunPod compute and simulation diagnostics — 19 November 2026

The authenticated catalog reported stock for two full RTX PRO 6000 Blackwell
Server GPUs, and the startup ordering selected that offer. A bounded run with
the stock image verified two distinct full-device UUIDs through pinned SSH.
The deployer selected 81 parallel jobs from the 81.6-core quota and 351.1 GiB
container allocation. The native worker linked and started with Vulkan. This is
one observed build and device configuration.

The managed browser bridge delivered sampled CPU and RAM charts and live
NVIDIA utilization and VRAM charts for both devices. The receiver used GPU 0.
One chart sample showed 2% and 0% GPU utilization and 0.7 and 0.6 GiB of
95.6 GiB VRAM in use, respectively. The test decoded `e` with 780 Vulkan
search rows, three DATA decisions and both quiet slots. RunPod returned an
individual HTTP 404 after deletion and empty pod and queue inventories.

In local Firefox, clicking Pattern evidence left the painted plot unchanged.
After clearing the waterfall, manual replay restored the signal pixels in
2.937 seconds and kept the same decoded result. The actual lab relay test
restored Audio error **Default** after reloading and after starting a new
local server. Settings loading preserves Simulation in the lab and preserves
an explicit 70 ms audio allowance. The recorded native settings display
`3.6 Hz` for the loaded Rate. On that configuration the Helper tests
accepted 4 W, explained why 15 kW had no preset at 6 kHz, and recovered the
editable suggestions at 1 W.

All 183 standalone tests, 32 desktop GUI tests, and 12 web checks passed.
The standalone suite used a private X11 display and loopback listeners for
the Tk and native relay cases. It checked exact-count stock with an omitted
catalog ceiling and closing the window with a pending refresh. The web
timing-only Fast audio stress case retains its previous validation.

### Strong-signal raw-bit simulation at 3.6 Hz — 19 November 2026

The native CPU probe reproduced the report that both transmitted bit values can
appear as 1. The checkout was `e0e48f33`. The probe reports configure-time
commit `d3482db2` and source manifest `063c4a4065f7404d…`. It used +3 dBm DATA, 90 dB path loss,
−164 dBm/Hz noise (+77 dB-Hz actual), Target 32 dB-Hz, 3.6 Hz without
spreading, 30 MHz Shift, 1725 Hz IF, two GPSDO-XO models, margin 3,
HF Worst and Each 65%, with Helper and FHSS disabled. The configured
calendar used anchor 1793850000, pattern epoch 1793850013 and ordinal
7000037. One full 1,215,321-sample active public slot was evaluated per
condition, at seed 950101 and on 24 native software workers. Each used the
Current decision and AVX2 FP32 coarse FFTs.

| Channel | Sent bit | Received bit | Both bit log-evidences | Native scoring |
| --- | ---: | ---: | --- | ---: |
| Selected HF Worst and Each 65% | 0 | 1 | 2.9223e8, 3.2003e8 | 13.57 s |
| Same selected channel | 1 | 1 | 3.3601e8, 3.5392e8 | 13.53 s |
| Channel-severity-zero control | 0 | 0 | 8.0347e9, 6.8015e9 | 13.37 s |

All used the fixed-affine model. With 0.6 fine chips/s, two cells require
3.33 s, outside the 0.25 s and 1 s short-block families. The drift
extension also exceeded the preserved owner lease. The severity-zero
control retained the same receiver, timing, noise and bit while removing
the selected changing-channel effects from the signal. These three
observations establish a channel-dependent native polarity error for this
configuration and seed, with complete-message reliability and GPU parity
unqualified.

Follow-up probes on source manifest `23737d863d468c99…` held the seed,
RF settings, and 160.4 s duration fixed. The combined profile again
decoded zero as one with explicit FP64 coarse FFTs (11.90 s native
scoring). Leaving HF Worst enabled and turning selective modulation Off
decoded zero correctly with FP32 (11.44 s). Leaving Each 65% enabled and
turning spread off timed out at the 90 s probe limit without a decision.
That preset change lowered the actual modulation rate from 9 Hz to 0.01 Hz
and shortened the window by 126 samples through the channel tail. The
controlled comparisons cover GUI presets.

A one-off diagnostic with native source manifest `f15e6fe72e3214a4e…`
then kept the receiver declarations fixed and set only the injected
slow and fast modulation depths to zero after profile preparation. The
streaming filter was absent, while the receiver still expected Each 65%
and its 90 Hz allowance. The 21 ms/90 Hz spread, common wander with
one-second knots, seed, calendar, and 1,215,321-sample window were preserved.
The FP32 probe decoded zero correctly in 10.40 s, with the same fixed-affine
bit evidence as preset Off. This isolates a contribution from the injected
modulation at this seed. Recovery with modulation enabled remains unresolved.
The three original slot receipts are in ignored
`local-evidence/runpod-strong-bit-2026-11-19/` for exact replay comparison.
Remove them when a later qualification supersedes this failure record.

Finite raw-bit and short-text simulations compare the public native
reception with their local input after physical completion. They report
match, mismatch, or incomplete reception. Pattern Steps and the Signal
Browser retain the actual score and received content, including bits
available for copying. The local reference is separate from native
scoring and the two-quiet-slot completion rule.

### Complete low-rate raw-bit replay — 19 November 2026

An exact-bit diagnostic using the production native stream receiver extended
the strong-signal fixture to `010`. It held the calendar, 6 kHz sample rate,
seed 950101, 160.4 s DATA duration and +77 dB-Hz nominal input fixed. On
configured native source `f15e6fe72e3214a4e…`, Current scored `111` followed
by two quiet slots and returned a complete three-bit reception without
erasures. The five windows used 6,255,321 samples and 24 software workers.
Scoring took 53.31 s. Evaluating Guarded on the same evidence rejected all
three DATA decisions. In the GUI, Guarded hides this result row while Pattern
evidence now marks each rejection alongside the retained native likelihoods.
Audio error Default and explicit 70 ms resolve to the same arrival allowance.
Two fresh simulations need the same seed, calendar and ordinal for comparison.

Two native contrasts used the original severe zero's identical
`b4d8fa5a…8bf8` capture. The baseline nominees used pattern epochs −1 and
−6 and gave wrong-bit evidence 320.03 million nats. A diagnostic using an
evaluator-selected public atom at the actual arrival and oscillator offset
correctly preferred zero at 313.82 million nats. Adding that particular
atom alone would still lose to the baseline wrong fit. A separate variant
extended the fixed-affine projection to include the declared atmospheric
and selective allowances and spectral skirts. Its wrong decision persisted
at 321.55 million nats. This experiment leaves the original receiver route
in place. The raw-stream receipt joins the prior failure receipts under
`local-evidence/runpod-strong-bit-2026-11-19/` until superseded.

Two private affine contrasts on that same single-zero capture used a
zero-relative-Doppler second tap at 0.444 s or 0.889 s from the public
selective model. Both preferred zero on the eight ordinary coarse candidates.
The 0.889 s contrast preferred the wrong bit when tested at the evaluator's
known timing. A candidate implementation retained the original family and
mixed it equally with the 0.444 s family. On the identical five-slot `010`
capture it decoded `001` with both quiet slots. This repaired the first
zero and changed the previously correct middle bit to zero. Guarded still
rejected all three DATA slots. Receiver service took 73.46 s in this run,
compared with 53.31 s on the earlier baseline. The candidate was removed.
The larger broad varying-coarse plan quoted 2.60 GB and about 98 billion
segmented inverse FFT output rows on the single-slot case. That plan was
inspected without executing its large segmented search.

A separate private proposal used the existing differential coarse search
with the public atmospheric allowance and without segmented timing. It fit
16 signal-derived coordinates on its own 500 Hz projected grid using the
native affine scorer. Each branch kept its full public prior, and the ordinary
and broad log e-values received equal fixed weight. On the same verified
`7f453535…901c4` stream the native controller committed
`0,1,0,QUIET,QUIET` and reported physical completion without erasures.
The original bit evidence was retained. Guarded still rejected all three DATA
decisions. Receiver service was 128.00 s at 24 shared executor workers and
6.41 GB peak application reservation, versus 53.31 s on the original route.
A fresh Helper-off active window, bit `1`, seed 950102 and ordinal 7000047,
also preferred the wrong bit in the original scorer. The fixed mixture
preferred the correct bit with a 45.61-million-nat gap. Extra coarse and fine
work took 6.74 s and 8.72 s in this single-window check. The measured 2.40×
complete service cost and these limited cases leave the proposal unqualified
for automatic operation. The retained receipts and exact input settings
are in `local-evidence/runpod-strong-bit-2026-11-19/`. Remove them when an
independent receiver qualification supersedes these checks.

At 100 W requested Helper power, the plan accepts the full 100 W and the
Target 32 profile extends slot stride from 210 to 240 s. With Target −6 it
uses 481.2 s DATA and 570 s slot stride. Physical DATA C/N0 remains 77 dB-Hz
through these planning changes.

The same proposal preserved native `0,QUIET,QUIET` at 100 W Helper and
Target 32 on seed 950103, ordinal 7000057, with a blank Helper callsign.
At Target −6, one independently seeded complete DATA window falsified
preservation. For seed 950104 and ordinal 7000067, the original scorer
correctly decoded `0` on the verified `11f3bb66…d3911e` input. The
mixture decoded `1`, with an 87.58-million-nat wrong-bit advantage.
This 3,410,122-sample observation met the full-prior transfer, valid-input,
noise calibration and workspace checks. The 2.37 GB expanded slot quote
ran in 79.28 s versus 29.55 s for the original slot. This test covers the
active window. The broader proposal is unsuitable for Helper-enabled
reception on this evidence.

A corresponding Helper-off test used the same seed, ordinal, C/N0 and
481.2 s DATA duration. Removing Helper also changed the first public window
from 3,410,122 to 3,140,121 retained samples and moved its keyed pattern
epoch. The ordinary scorer returned `1`, and the mixture returned the
correct `0` on the same Helper-off observation. The mixture's gap was
37.65 million nats. This is one Helper-off DATA observation and does not
isolate the cause of the Helper-on result.

The Helper-off production-path candidate skipped the alternative on a
slot already scored physically quiet. An exact native replay of
`7f453535…901c4` dispatched three broad searches with 16 fine
coordinates each. It committed `010` and two scored quiet slots without
erasures. Service took 96.78 s (1,061.84 process CPU seconds) with the shared
24-worker budget and a 6.41 GB peak application reservation. The original
stream took 53.31 s. The candidate preserved the admitted ready group and
a full-width next-window refill. With only the ordinary workspace lease,
the optional route reported that it could not be admitted. Its reserved
half prior contributed no evidence. The deterministic threshold-edge
check verified erasure without a flipped bit or a new physical quiet slot.

Dry quotes showed no additional route with Helper enabled, selective
modulation off, or the 36 Hz fast-cadence contrast. In an independent
foreign-key input trial at +14 dB-Hz, the original and mixed scores both
returned ambiguous. Wanted points at +9 and +18 dB-Hz also remained
ambiguous, below the admission threshold. These checks do not establish
near-threshold sensitivity, a false-acceptance rate or sustained receiver
capacity. Remove the scoped receipts once later qualification supersedes them.

### RunPod startup, build stages, and simulation presentation — 19 November 2026

The live catalog selected an available full RTX PRO 6000 Blackwell Server
Edition pair from the saved key before any Tk interaction. The nested X11
regression also verified that an old pod record can be reconciled after the
compute selection appears.

The first new one-device test returned RUNNING without a direct SSH
endpoint and was deleted. A second test verified two Blackwell Server Edition
devices using pinned SSH. The stock image had GNU 13.3 and Unix Makefiles
without Ninja or ccache. Deployment reported 81 jobs against 81.6 cgroup
cores and 351.1 GiB RAM. The build took 50.61 s with 256.49 child CPU seconds
(5.07 core equivalents on average). Configure took 9.91 s. The build time
includes linking and excludes setup and dependency installation. Those
figures describe one build on the old generator.

That device test used the later-withdrawn selective-affine candidate. Both
complete 3.6 Hz `010` episodes executed 19,755,450 Vulkan search rows, with
two quiet slots apiece. The selected device allocated a peak Vulkan lease
of 2,031 MiB without Helper and 4,244 MiB with 100 W Helper. CPU fine work
per slot took longer than coarse search in both runs. The raw results were
`110` without Helper and `001` at 100 W. The new presentation showed the
actual bits and updated spectrum, waveform, I/Q and Pattern evidence during
simulation. Manual replay advanced the retained Pattern evidence. The exact
pod was deleted and absence was confirmed individually and in both inventories.

A third fresh pod used the signed Ubuntu Ninja package with GNU 13.3 and
the same full two-device model and resource quota. Configure took 7.50 s.
The 81-job build compiled and linked in 20.60 s, spending 299.93 child CPU
seconds. The Ninja log recorded 154 objects, 81 simultaneous object jobs
at peak and 14.89 on average. The longest object was
`triad_vulkan_backend.cpp` at 19.90 s. Link took 0.37 s. The worker's setup
and self-check took about 39 s after preparation started. These are
individual cold builds on two equivalent allocations.

With the production scorer on driver 595.112.07, the third pod ran the
same +77 dB-Hz link with 100 W Helper and Target −6. Raw `0` came back as
`1` with two scored quiet slots and an unfiltered 1/1 DATA decision in
30.57 s. This reproduces the reported mismatch on the longer 481.2 s
DATA window. The worker executed 30,028,284 Vulkan search rows on GPU 0
and used CPU tracking. Its peak workspace lease was 4,312 MiB. GPU 1
remained outside this session's receiver. The two quiet slots each
spent more time in CPU fine tracking than in GPU coarse search. During
transmission the native relay delivered 158 spectrum, 162 waveform,
162 I/Q and four Pattern evidence revisions. Manual replay advanced
Pattern evidence by four revisions. Pod deletion was verified against
both inventories.

A subsequent 5090 test reached SSH and failed to connect to the Ubuntu
archive while downloading signed package indices. The pod was deleted
and verified absent. Deployment now bounds the initial index attempt
and can retry with the signed Ubuntu security archive. The package test
exercises the primary, failover and both-unavailable paths.

The last receiver comparison ran the Helper-off candidate on one full
RTX PRO 6000 Blackwell Server Edition. Driver 595.112.07 used Vulkan for
44,451,810 search rows, with native CPU tracking and a peak Vulkan lease
of 4,064 MiB. The pod supplied 13.6 cgroup cores and the receiver used
26 software workers. Its fresh Ninja build took 15.12 s with 13 jobs.
A fresh GUI-generated 3.6 Hz, Target 32, +77 dB-Hz
`010` input finished after two scored quiet slots in 34.86 s. All three
active slots used the broader evidence and the receiver still returned
`110`. The third bit corrected an original affine preference for `1`;
the first wrong decision persisted. This independent failure disqualified
the candidate for production. The native scorer change was removed and
the pod was verified absent from both inventories.

The final standalone X11 suite passed 184 tests with the native worker
and local loopback. The eight focused Robust, clock, bitmap and GUI
policy checks passed on the final source. A concurrent native GUI suite
run reported one timed audio-capture backlog overrun. That case passed
on its own in 7.29 s and again in the final sequential run.

### Receive previews and clean low-rate admission — 19 November 2026

On base `d408fd3a` with these changes, a native bounded CPU fixture checked
the accepted post-channel receive I/Q against the plotted real waveform at
its actual sample phase. It processed 273.758 modeled seconds in 0.814 s,
decoded `001`, and scored two quiet slots before natural-rate replay. The
separate 120 Hz Firefox test used the real managed bridge and pipe worker
with four logical CPUs and eight admitted receiver workers. Its first RX
preview painted 0.165 s after Transmit. It processed 251.7 modeled seconds
in 5.2 s, decoded `e`, and kept the full 2,048-sample waveform visible in
preview and replay. Pattern evidence survived a click and viewport resize,
and 3.147 s of manual replay revealed all five steps without changing the
received message. The test exercised page switching and End simulation.
The cases cover a narrow clean channel with Helper Off. Broad-search
throughput and physical audio performance remain separate checks.

At 3.6 Hz, +77 dB-Hz nominal DATA, Target 32, seed 960101, margin 3,
GPSDO XO on both oscillators and static channel controls Off, the baseline
used a single DC mode per quarter-second block. The correct `0` passed
Unfiltered and failed every stronger policy. Its log-evidence gap was
about 4,776, versus 743 million required by Guarded. On the same 1,215,195
receive samples (SHA-256 `03af4301…c4f3c`), one-fine-cell blocks recovered
about 4.066 billion of gap. All policies retained `0`. Changing only the
receiver key gave about 116 million of gap against 227 million required by
Guarded. Every stronger policy rejected it. The local one-slot probe spent
40.87 s versus 34.06 s on the older short projection. The RunPod build
reproduced the wanted result with native coarse and fine work in 3.08 s.
Its strict Vulkan receipt correctly rejected the CPU-only slot as GPU
qualification. The test covers one fixed observation and an independent
wrong receiver key. False-acceptance rates and sensitivity remain unmeasured.
For the newly eligible geometry, the 3.33 s fine segment exceeds the
fixed 2 s nominal capacity checkpoint. Such checkpoints remain incomplete
under the current capacity procedure, which must be satisfied before making
a local throughput claim.

On identical 6 kHz post-channel I/Q, eight native DC blocks with 100 W
Helper and about 2 mW DATA lost 46.86 dB of measured Helper leakage after a
unit-norm raised-cosine edge taper. The taper retained 94.32% of wanted
projected energy. Residual Helper-only energy was about 139 noise-variance
units versus eight expected for noise alone. The eight blocks had unchanged
public extent and noise degrees of freedom. Helper Off and a dwell-gated
control kept their rectangular projection result, and the 1,500-sample
weight vector was charged to the native workspace. The first synchronized
3.6 Hz, 100 W Helper run used a tapered rank-one projection. It recovered `0`
in Unfiltered in 3.63 s, and every stronger policy erased it. The rectangular
one-cell follow-up retained the wanted `0` and rejected the independent
receiver key at every stronger tier on identical I/Q (`5df4d009…1665`).
The production orthonormal tapered one-cell projection recovered `0` with
all six settings from the same samples in 3.34 s on RunPod. Its log-evidence
gap was 3.956 billion. On identical I/Q the same production projection with
an independent receiver key yielded about 15.44 million of gap against
238.02 million required by Guarded, and every stronger setting rejected it.
A separately generated physical quiet slot in the
same configuration remained quiet with all six settings, with 579.32 energy
against 630.66 in 555 modes. These are synchronized one-slot controls.
Full messages, broad timing in the Helper-on case, varying channels, gated
pilots and Tensor fine processing remain outside this evidence.

One full RTX PRO 6000 Blackwell Server Edition on driver 595.112.07 ran a
same-input 36 Hz Vulkan pair with +77 dB-Hz DATA, Helper Off, Target 22 and
an explicit 6 GiB per-owner device limit. Both runs recovered `0` from the
202,393-sample slot (SHA-256 `72f47b82…a3f9`) and scored all 98,982 rows in
four Vulkan submissions with native fine tracking. FP32 and FP16-storage
took 0.434 and 0.425 s for the complete slots. Device command intervals
totaled 59 and 56 µs. Coarse wall time was 0.390 and 0.383 s, and fine
tracking took 0.044 and 0.042 s. The cold FP32 FFT setup and shader pipeline
creation were 0.220 and 0.085 s. FP16 storage reduced peak allocated bytes
from 8,287,008 to 6,050,592 with FP32 products and FFT arithmetic. These
cold easy-slot results support actual device work and the stage split.
Default storage-policy selection, Tensor Core dispatch and sustained speed
require separate qualification. A subsequent same-process, four-look FP32 trial
scored the same input with identical work identity and decision in every
look, totaling 395,928 Vulkan rows. Four cold GLSL compilations and four
cold FFT plan-cache misses were followed by 12 shader-binary hits and 12
FFT cache hits. The cold slot took 0.426 s, and the warm slots took
0.129–0.136 s. Both caches contributed to this warm/cold difference. The
shader cache is bounded to 128 KiB of payload and 8 KiB of metadata per
backend and it keeps descriptor sets and driver pipelines owner-local.

A plan-only contrast held transmit Rate at 3.6 Hz, with both GPSDO XO
oscillators, margin 3, Target 32, Helper Off and broad timing. The clean
case projected to 3 Hz, 43 arrival points and 50,310 score rows. HF Worst
and Each 65% projected to 500 Hz, 7,036 arrivals, two rays and 8,232,120
score rows. Both used 78 workspaces and 15 frequencies, and the latter
was eligible for GPU search because its projected rate was outside the
tight-clock guard. Prior physical Blackwell results above demonstrate
accelerated coarse work on impaired 3.6 Hz slots. Current Vulkan fine
tracking still runs on the CPU. The native precision fallback now compares
the exact native search routes using their full cost and workspace quotes.

The matched precision comparison kept the public grid and input identical
on the full PRO 6000. A private build bypassed only the Vulkan precision
veto and ran actual FP32 frequency-tile work. The 3 Hz projected clean
DATA and quiet slots used 50,310 rows each. They retained the same outcomes
under all six settings, with CPU totals of 3.83 and 3.89 s against Vulkan
3.64 and 3.66 s. With Target −6 and 1.5 Hz Doppler, the same 3.6 Hz Rate
needed an 8 Hz projection and 6,132 × 113 × 78 hypotheses. Native exact
FP64 chirp-Z took 8.69 s for the complete slot. The unguarded FP32 Vulkan
trial on identical I/Q (`c76109b1…898d`) scored all 54,047,448 rows in
577 submissions and took 21.01 s. Summed device command intervals were
1.77 s. Both runs recovered `0` in Unfiltered and Guarded, and the stronger
settings erased it. The measured geometry favors the exact native route.
The private Vulkan experiment was discarded with the pod. These measurements
cover the selected device, runtime, shape and samples. An automatic
precision decision on other large narrow projections still needs matched
qualification.

A native capability probe used the same socket filter and headless Vulkan
ICD as the standalone worker on a full PRO 6000 Blackwell Server Edition
with driver 595.112.07. It created a logical device with cooperative-matrix,
flexible matrix and cooperative-vector features enabled. The reported subgroup
matrix shapes included FP16 with FP32 accumulation at 16×8×16 and 16×16×16,
BF16 with FP32 accumulation at 16×16×16, FP8 E4M3 and E5M2 with FP32 accumulation
at 16×16×32, and signed INT8 with INT32 accumulation at 16×8×32 and 16×16×32.
The flexible variants included 32- to 256-invocation workgroups. The vector
limit was 1,024 components. The reported extensions and operation tables
contained no FP4 or microscaling operation. The image's glslang 15.1.0
compiled a matching FP16 cooperative-matrix shader and SPIR-V validation
passed. This verifies runtime queries, device enablement and shader tooling.
Receiver packing, dispatch, numerical behavior and complete-service gains
still need implementation and measurement. The post-isolation fine DATA
correlations and Gram terms are the relevant contractions for evaluating
these shapes. See [NVIDIA cooperative-matrix processing](https://developer.nvidia.com/blog/machine-learning-acceleration-vulkan-cooperative-matrices/)
and [Khronos microscaling types](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_shader_ocp_microscaling_types.html).

A private instrumentation replay used the severe HF Worst plus Each 65%
capture `b4d8fa5a…8bf8` with its original 1,215,321 samples and 24 workers.
It returned the original wrong bit `1` for `0`. Fine work took 4.52 s wall
time with eight candidates, 810 blocks and 350,012,448 mapped basis rows.
Summed worker intervals were 25.93 s for template mapping, 62.79 s for
template projection, 0.25 s for conjugate and Gram products, and 1.06 s for
Gaussian updates. These intervals overlap across workers. Projection took
about 70% of their measured sum, and the small post-projection contractions
took about 0.3%. This geometry favors batched template projection with
efficient mapping and data movement for the next accelerator comparison.
Offloading only the final conjugate and Gram products would address a small
share of this workload.

A projection-only FP32 trial reused the same severe input, native mapping,
FP64 likelihood and state, and pruned Bluestein inverse. The fine stage
took 2.87 s and the complete slot 8.47 s, compared with 4.52 and 10.13 s.
The same wrong bit and admission dispositions persisted. Three paired local
controls kept each pair's receive samples identical. The stable wanted
control kept `0` in Unfiltered and Guarded, the noise control stayed quiet
at every setting, and the independent +14 dB-Hz foreign-only control stayed
ambiguous. Complete slots went from 9.96 to 8.25 s, 10.00 to 8.40 s, and
10.25 to 8.44 s respectively. These four paired slots support the bounded
AVX2 FP32 path for the 1,500-row, 47-mode no-Helper geometry. Its production
implementation admits and owns the float buffers and retains the FP64 plan
for amplitudes outside its safe range. Replaying the severe and stable inputs
through the rebuilt production code took 2.99 s and 2.97 s in fine processing
respectively and reproduced the trial's mismatched and correct bit decisions.
An initial per-worker scratch layout exceeded the live test's workspace limit.
Allocating the FP32 scratch at the scorer and charging it only for applicable
slots resolved that failure. The rebuilt live, fine and worker relay checks
passed. Sensitivity at the admission boundary and the correct-bit model under
the severe channel remain open.

At 27.20 provider CPU-time cores, the worker admitted
54 software workers. The two captured Ninja compile stages took 15.07 s
with 27 jobs and 14.64 s with 13 jobs, excluding dependency setup. All
temporary test pods were verified absent from the pod and queue inventories.

The first 45-check triad group passed 44 checks. The probe test ran during
other local compilation and its bounded Helper contrast returned without
coarse preparation. The unchanged test passed in isolation in 27.16 s on
the rebuilt probe. Before integrating the AVX2 FP32 path, all 32 desktop GUI
checks and all 184 standalone checks passed with Xvfb and the rebuilt native
relay. The browser renderer regression and focused native coarse and fine
checks also passed. Compact matched
reception receipts remain in `local-evidence/wrong-key-clean-20261119/` for
future projection qualification. Remove them when a newer matched qualification
supersedes these inputs and results.

## Bounded 2 mW HF-FHSS investigation — 18 November 2026

The [HF operating results](indexes/robust.md#low-power-hf-alarm-results)
summarize the power, airtime and CPU tradeoffs. Use the
[editable simulation list](../_tests-RobustModem-HF-simulations-2026-11-18.md)
for individual settings and outcomes, or the [native replay](#reproducing-the-stable-alarm-and-severe-bit-tests)
to reproduce a measured case.

The investigation used the row-6 native receiver with the configured
public calendar and keyed ordinal, GUI HF Worst and Each 65% selective
modulation at full 3× margin. At 2 mW transmit power, 180 dB path loss and the
test's assumed −164 dBm/Hz noise density, actual C/N0 is −12.9897 dB-Hz.
Source manifest `f0160701…` identifies the initial GUI receiver build.
The results cover single-bit decisions, two independently seeded complete
replays of the stable `e` alarm, and a paired silent five-slot control.
Geographic outage probability and acquisition with
an unknown waveform calendar remain unmeasured. The receiver noise in these
runs is white Gaussian at −164 dBm/Hz. Each 65% modulates the signal.
Lightning impulses and local interference were outside these realizations.
Estimating seasonal atmospheric noise requires the actual receiving site,
RF band, month and time. An installation estimate also requires the antenna
and receiver noise characteristics. The 30 MHz shift sets the oscillator
translation reference and Fake FCC applies a dwell envelope in baseband.
[ITU-R P.372 radio-noise model](https://www.itu.int/rec/R-REC-P.372-18-202609-I/en)
and [oscillator settings](oscillator-models.md).

**Physical link margin at fixed airtime.** The original 36 Hz / 10× Fake FCC
waveform used Target +4 dB-Hz and 256.8 s throughout this comparison. Only
the physical transmit power and stable/severe propagation changed. Source
bit 0, seed 940600, loss, noise density and public search were held fixed.

| Path | Highest tested miss | Lowest tested decode |
| --- | --- | --- |
| Stable | −6 dB-Hz, 10.00 mW | −4 dB-Hz, 15.85 mW |
| GUI HF Worst, 3×, Each 65% | +11 dB-Hz, 501.19 mW | +14 dB-Hz, 1.00 W |

If each path has one local transition between its tested endpoints, the
extra received C/N₀ lies between about 15 and 20 dB. The two observed
passing powers differ by 18 dB. This is a one-realization physical margin;
it does not specify a GUI Target offset. The normalized simulator changes
sample noise variance to represent the change in physical power at a fixed
−164 dBm/Hz link noise density. At severe +11, DATA was detected (44.136
against 14.982), but the gap of 2.322 fell below the required 5.293. At
stable −4 the gap was 6.807. A prior Target 32 simulation supplied the +14
decision: the new Target +4 physical-power preflight produced identical
noisy samples, normalized receiver settings and public grid. The independent
CPU-only power sweeps ran on a 16-vCPU/32 GB EPYC 9575F with 32 workers.
[Physical-power equivalence](../local-evidence/hf-airtime-penalty-20261118/anchor-comparison.json),
[stable −6](../local-evidence/hf-airtime-penalty-20261118/stable-m6.json),
[stable −4](../local-evidence/hf-airtime-penalty-20261118/stable-m4.json),
[severe +11](../local-evidence/hf-airtime-penalty-20261118/severe-11.json)
and [severe +14](../local-evidence/sensitivity-row6-fixed-severe-14-900s.json).
Freezing the two passing endpoints before the bit-1 check gave two further
correct decisions at seed 940601: stable −4 dB-Hz (gap 7.894) and severe
+14 dB-Hz (gap 40.679). These outcomes confirm the two passing settings on
a second realization. Only seed 940600 brackets the transition at both paths.
The CPU pod scored the holdouts in 140.80 and 141.03 s, respectively.
[Paired endpoint results, units and receipts](../local-evidence/hf-airtime-penalty-20261118/summary.json).

At the original 4 W, nominal C/N₀ is +20.02 dB-Hz, 6.02 dB above the
observed 1 W passing endpoint. The original Target 32 trial received the
same 256.8 s bit. At weaker physical levels the same duration can yield less
evidence for the transmitted value or too little evidence of any signal.
[Original 4 W receiver result](#tiny-gpsdo-midpoint-bound-repair--17-november-2026).

The native planner's 36 Hz/10× waveform at Target −12.99 dB-Hz is 1,797.6 s
per bit and requires approximately 19.2 GiB of slot workspace, exceeding the
approximately 9 GiB automatic memory limit in this local invocation. A
72 Hz/5× plan is similar. The 18 Hz/10× and 36 Hz/5× alternatives reject the
configured FHSS dwell geometry. A 30 Hz/12× plan at the same target is 1,848 s
with approximately 19.36 GiB workspace. These are planning and resource
admission results, not failed reception or sensitivity thresholds.

At Target −6.97 dB-Hz the 36 Hz/10× plan is 513.6 s with approximately
7.03 GiB slot workspace. The original 2 mW actual link C/N0 remains −12.9897
dB-Hz; selecting a different planning target does not increase received power.
The original native trial of this candidate (seed 940600, bit 0) ended
incomplete at 1,200.12 seconds without a bit decision. Profile preparation took
0.43 seconds and simulation took 14.85 seconds; no completed receiver-scoring
phase was reported. The timeout is not a reception failure or sensitivity
threshold. The bounded stage diagnostic below does not by itself establish a
population reliability or complete-receiver service claim.

A separate candidate with Target +4 dB-Hz retains 36 Hz/10×, giving a
256.8-second pattern and 270-second cadence while the actual C/N0 remains
−12.9897 dB-Hz. With the same seed and bit 0, the complete public-slot Current
policy returned QUIET, an erasure despite the transmitted bit. This is one
simulated bit on the configured calendar, not a complete-message result or a
population threshold. On this H=12, W=24 CPU run, receiver scoring took
662.717 seconds wall and 6,854.272 CPU seconds. Coarse took 616.188 seconds,
including 589.700 seconds reported for varying-channel coarse work; fine took
46.523 seconds. The varying work is included in coarse, not additional time.
The input hash was `8062109a…`, with source manifest `f0160701…`.
The separate nine-look fixed-affine diagnostic estimated 0.00540 receiver
wall-processing seconds per represented baseband second, but its relative
interval precision was 53.8%, missing the declared 15% goal. It omitted varying
and acquisition work and is not a whole-receiver rate; that estimate remains
unresolved.

A 72 Hz/5× candidate at Target +4 dB-Hz and the same 2 mW link, seed and bit
uses a 128.8-second pattern and 150-second cadence. Its complete public-slot
Current decision was also QUIET/erasure. The slot workspace bound was
3,041,578,471 bytes; receiver scoring took 346.971 seconds wall and 3,601.606
CPU seconds. Coarse took 323.272 seconds, including 310.795 seconds of varying
coarse work, and fine took 23.696 seconds. Its input hash was `af364b67…`;
the source manifest remained `f0160701…`. The respective single-slot scoring
time divided by cadence is 2.455 for the 36 Hz/10× case and 2.313 for this
candidate. These illustrative single-slot ratios do not establish complete
receiver capacity or a population communication result. The 72 Hz/5× candidate
at Target −5 dB-Hz used a 386.4-second pattern and 390-second cadence; with the
same seed, bit and 2 mW impaired link it also completed the public slot with a
Current QUIET/erasure. Receiver scoring took 1,182.062 seconds wall and
12,385.272 CPU seconds; simulation took 10.462 seconds and total elapsed time
was 1,192.838 seconds. Coarse took 1,103.098 seconds, including 1,054.254
seconds of varying work; fine took 78.953 seconds. The workspace bound was
7,102,681,229 bytes, the input hash `522b8d33…` and source manifest
`f0160701…`. These are individual simulated-bit results, not a success
threshold or complete-receiver capacity inference.

A 30 Hz/12× candidate at Target +4 dB-Hz and the same seed, bit and 2 mW
link used a 308-second pattern and 330-second cadence. The Current policy
completed its public slot with QUIET/erasure. Receiver scoring took 977.053
seconds wall and 10,302.894 CPU seconds, with 917.815 seconds coarse including
886.645 seconds varying coarse work, and 59.230 seconds fine. The workspace
bound was 6,079,223,078 bytes, input hash `da406b73…`, and source manifest
`f0160701…`. Its single-slot scoring/cadence ratio was 2.96077, about 20.6%
higher than the 36 Hz/10× case's 2.45451. The waveforms differ and these are
single observations, not sustained capacity or population communication
results.

An independently reviewed native genie comparison supplies a conditional
ideal-observer bound for the same 2 mW seeded channel. For each waveform,
both equally likely bit patterns use the same path; the ideal
observer knows the exact signals, channel, timing and bit presence and faces
complex Gaussian noise on all valid samples, without a frontend model. The
reported percentages are optimal average errors if it must choose 0 or 1;
for a receiver that can also erase, the corresponding error-or-erasure
probability is at least that bound. They are not ordinary receiver results,
independent channel trials or complete-message reliability estimates.

| Rate / spreading | Target C/N0 | Pattern | Ideal binary error | Genie and matching native capture |
| --- | --- | ---: | ---: | --- |
| 72 Hz / 5× | +4 dB-Hz | 128.8 s | 27.88% | [genie](../local-evidence/2mw-oracle-72hz5x-target-p4-seed940600.json), [native](../local-evidence/sensitivity-2mw-row6-rate72-spread5-target-p4-seed940600-bit0.json), input `af364b67…` |
| 72 Hz / 5× | −5 dB-Hz | 386.4 s | 15.42% | [genie](../local-evidence/2mw-oracle-72hz5x-target-m5-seed940600.json), [native](../local-evidence/sensitivity-2mw-row6-rate72-spread5-target-m5-seed940600-bit0.json), input `522b8d33…` |
| 72 Hz / 5× | −12.9897 dB-Hz | 1,803.2 s | 1.3378% | [genie](../local-evidence/2mw-oracle-72hz5x-target-m12p9897.json), [native preflight](../local-evidence/2mw-oracle-72hz5x-target-m12p9897-native-preflight.json), input `d0cea61d…` |
| 36 Hz / 10× | −6.97 dB-Hz | 513.6 s | 11.8767% | [genie](../local-evidence/2mw-oracle-36hz10x-target-m6p97-seed940600.json), [native timeout](../local-evidence/sensitivity-2mw-row6-target-m6p97-seed940600-bit0.json), input `810c9f78…` |

For the 513.6-second case, squared signal distance was 333,255.8471 and complex
noise variance 119,432.1512; the matching ordinary receiver timed out without
a bit decision as recorded above. The wrapper used for those GUI genie results
had source SHA-256 `36cac…`. Its subsequently updated source (`48973cf4…`) and build/run
instructions are retained in ignored `local-evidence/2mw-oracle-tool/` for this
unresolved investigation. This helper contains no copied receiver source or
binaries; it is to be removed when its results are superseded. The source
manifest of the original native probe was `f0160701…`. In the longest case, a deliberate
1 GiB memory preflight generated the matching capture but did not score a bit:
the full quoted slot workspace was approximately 19.18 GiB. That 1 GiB
explicit limit was not the automatic memory budget.

The native probe now also has opt-in `--channel-model itu-disturbed`, a
channel-only fixed-grid reference with two independent proper-complex Gaussian
taps at 0 and 7 ms and 30 Hz full 2σ Doppler width, plus the selected synthetic
Each 65% modulation. It does not add the GUI bounded-ray/common-wander channel.
The receiver's public hypotheses remain unchanged, including ±90 Hz relative
and common Doppler and 21 ms delay: this is an intentional channel/receiver
model mismatch. The adapter and focused tests in `tools/triad-receiver-probe.cpp`
and `tests/test_triad_receiver_probe.py` were independently reviewed; focused
`triad_receiver_probe` and `triad_v3_channel` checks passed 2/2, and
`high_fidelity_scenarios` passed 1/1. The updated source manifest is `7b8bc203…`.

At 72 Hz/5×, Target +4 dB-Hz, 2 mW and seed 940600, explicit 1 GiB preflights
generated both captures but ended incomplete without scoring a bit. The GUI
input hash remained `af364b67…`; the ITU-model input hash was `f87f760f…`.
The updated genie helper uses the same native channel adapter: for the ITU
128.8-second capture its hash matched the preflight, and the paired-noise
residual and variance checks were recorded. Its conditional equal-prior ideal
binary error is 28.5742%; independent review of this scoped genie result
passed. This is neither a receiver success rate nor a universal ITU-channel
claim. The corresponding ordinary native receiver trial completed its public
slot for bit 0 with the configured calendar and unchanged receiver priors.
Every admission policy scored QUIET/erasure, despite the bit being present.
Receiver scoring took 341.801 seconds and simulation 3.489 seconds, for the
128.8-second pattern and 150-second cadence. The input hash `f87f760f…`
matches the preflight and the source manifest is `7b8bc203…`. The
[receiver receipt](../local-evidence/sensitivity-2mw-row6-rate72-spread5-target-p4-itu-disturbed-seed940600-bit0.json)
is one simulated-bit outcome in a physical-channel stratum distinct from the
GUI baseline; it is not complete receiver service, message reliability or a
general climate claim.
Retain the distinct [GUI preflight](../local-evidence/2mw-adapter-gui-preflight-128.json),
[ITU preflight](../local-evidence/2mw-adapter-itu-preflight-128.json) and
[genie result](../local-evidence/2mw-oracle-itu-72hz5x-target-p4-seed940600.json)
until the unresolved comparison is resolved or superseded.

The initial full `./build.sh test triad` run passed 42/45: tests 116
(`triad_native_receiver_late-helper-delay`) and 120
(`triad_native_receiver_long-calm-polarity`) timed out, and test 204
(`triad_receiver_probe`) failed on a missing phase. An isolated rerun of those
same three tests passed 3/3 under unchanged deadlines; the original failure
remains distinct from this follow-up, and the other 42 checks retain their
original passing outcomes.

Temporary `local-evidence/sensitivity-2mw-*` and `local-evidence/2mw-oracle-*`
receipts serve the current 2 mW feasibility and ideal-observer investigation.
Retain the incomplete original receipt, matched captures and diagnostic
outcomes until their purpose is resolved or superseded, then distill applicable
findings here and remove redundant outputs.

### Lower rates at the same 2 mW link

The following runs used the configured calendar, broad timing search, seed
940600, bit 0, 3× margin, GPSDOs, no Helper, GUI HF Worst and Each 65%.
Transmit power was 2 mW and actual C/N₀ was −12.9897 dB-Hz throughout.
Changing Target changed the transmitted waveform duration. The native CPU
receiver used 24 software workers on a Ryzen 5 PRO 5650U with 12 logical CPUs.

| Hopping | Rate / added symbols | Target (dB-Hz) | Pattern / cadence (s) | Slot workspace bound (GiB) | Active-slot scoring (s) | Decision |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Fake FCC | 36 Hz / 10× | +4 | 256.8 / 270 | 4.26 | 662.72 | Quiet, missed bit |
| Off | 3.6 Hz / 1× | +4 | 160.4 / 210 | 0.69 | 14.75 | Quiet, missed bit |
| Off | 3.6 Hz / 1× | −12.9897 | 1,764.4 / 1,800 | 6.09 | 218.14 | Quiet, missed bit |

For the longest nonhopping test, propagation and noise generation took a
separate 57.97 s. The receiver scored 10,839,321 samples. At 3.6 Hz / 1× the
fine-chip rate is 0.6 Hz. The short-block channel tracker requires two distinct
fine cells per fitted block and the drift extension exceeded its slot allowance.
The receiver used its affine signal models and quiet check. Turning off hopping,
reducing rate and removing added symbols changed both the bit pattern and the
available receiver evidence. A single active-slot time gives no sustained
service or message-completion result. Source manifest `7b8bc203…`.
[Short trial](../local-evidence/lowrate-2mw-native/admission-off-3.6hz-target4.json)
and [long trial](../local-evidence/lowrate-2mw-native/admission-off-3.6hz-target-12.9897.json).

On the same 1,764.4 s keyed bit, the 2 mW stable-path control received zero.
Severity zero removed the changing paths and selected modulation while retaining
the oscillator, noise, link level and receiver's broad search. The bit separation
was 52.918 log units under stable propagation and 0.637 under severe propagation,
against a minimum of 5.293. The stable slot took 182.45 s to score. This paired
result isolates a channel effect at fixed duration. It measures one channel and
noise seed and supplies no calibrated dB allowance.
[Stable control](../local-evidence/lowrate-2mw-native/matched_target_stable_2mw.json).
The stable 160.4 s control returned quiet. At 4 W the same short severe-channel
pattern produced an ambiguous decision. That stronger signal raised the actual
C/N₀ to +20.02 dB-Hz. Both controls used the same public waveform and timing.
[Short stable control](../local-evidence/lowrate-2mw-native/short_target_stable_2mw.json)
and [short high-power severe control](../local-evidence/lowrate-2mw-native/lowrate_strong_target4.json).

The stable-path search then varied the number of 160.4 s tiles. At Target
−9 dB-Hz, five tiles (802.0 s) erased bit 0. At −10 dB-Hz, six tiles
(962.4 s with a 990 s cadence) received it. A fresh seed, 940601, also
received bit 1 at six tiles. These are adjacent waveform lengths, with two
observed successes at the longer length. The two successful slots took 85.48
and 92.93 s to score. The bit-0 source used 1.928 modeled joules. The
[stable trial table](../local-evidence/lowrate-2mw-native/stable-threshold-results.md)
records the other targets, the planner's step boundary and which trial was
held out. A separate fresh, silent slot at six tiles scored quiet after all
6,027,321 samples were processed.
[Quiet control](../local-evidence/lowrate-2mw-native/stable_6tiles_seed940602_noise.json).

After a serial Triad run passed 45/45, a bounded capacity calibration selected
the +4, 3.6 Hz / 1× severe nonhopping row. The active and noise continuation
samples reached their 90 s deadlines. The calibration left complete capacity
unresolved. A separate W=24 native replay processed ten consecutive public
slots on one input stream. All six transmitted DATA slots and four genuine
quiet slots scored quiet. There were no delivered messages. Receiver processing
took 147.206 s for 2,092.554 s of input, plus 0.774 s of audio projection.
The observed whole-replay ratio was 0.0707 processing seconds per input
second, including startup, fill and drain. Peak reservation was 8.34 GiB.
The predeclared steady-state interval had three commits with six prior
uncommitted slots at its start, so it could not establish steady-state service
independently of startup. Retain both measurements for the open local capacity
question. [Bounded calibration](../local-evidence/2mw-capacity-lowrate-p4.json)
and [ten-slot replay](../local-evidence/2mw-capacity-lowrate-p4-backlog.json).

At the successful 2 mW stable operating point (3.6 Hz / 1×, hopping off,
Target −10), the local native complete receiver delivered the short message
`e`. Its three DATA decisions were 0, 0 and 1. It then scored both genuine
quiet slots as quiet and confirmed physical completion; an EOF-only negative
control also passed. Each transmitted pattern was 962.4 s, on a 990 s cadence.
From first possible transmitted pulse to the end of the fifth full receiver
observation was 4,964.55 s (1 h 22 min 45 s). Offline processing of those
samples took 519.00 s for the receiver and 1.92 s for audio projection,
or 0.1049 processing seconds per input second. Separate channel simulation
took 66.14 s. Peak application reservation was 8.50 GiB, with up to two
owners sharing 24 workers. This directly measures correct complete-message
reception and favorable compute headroom for one stable stream. Sustained
backlog service and severe-channel delivery remain separate qualifications.
[Complete local stable message](../local-evidence/2mw-capacity-lowrate-stable-m10-message.json).

The independent stable holdout at seed 950101 also received 0, 0, 1,
classified both quiet slots correctly, delivered `e` and passed the EOF
control. The same Ryzen and 24 workers used 560.96 s for the receiver and
2.44 s for audio over 4,964.55 s of input (0.11348 s/s). Peak application
reservation was 9,663,365,848 bytes against a 9 GiB limit. The prospective
backlog interval from commit 0 through commit 4 required all 29,787,321
samples to be ready at commit 0. Only 11,968,512 were ready. The receiver
therefore completed this second replay faster than real time, while the
separate sustained-backlog criterion remains unqualified.
[Holdout and measurements](../local-evidence/2mw-stable-m10-message-holdout-950101.json)
and [predeclared interval](../local-evidence/2mw-stable-m10-message-holdout-950101.policy.json).

With the first trial's seed, noise calibration, five-slot calendar and
receiver retained, suppressing all emissions produced five quiet decisions
and zero decoded messages. The 29,787,321 complex input samples had mean
power within 0.009% of the configured noise variance. Integrating all three
transmitted bits of the wanted `e` message gave 5.782267477 J. The portion
from the end of the emission to the fifth-slot deadline added zero energy.
One silent episode measures the observed false alarms over that episode.
[Full silent control](../local-evidence/lowrate-2mw-native/stable-e-five-slot-silence.json)
and [full message energy](../local-evidence/lowrate-2mw-native/stable-e-full-source-energy.json).

Integrating every sample of the helper-free transmitted complex waveform
reproduced the native emission counter for bit 0. Under the simulator's
2 mW DATA reference, 3.6 Hz / 1× with hopping off used 0.321 J for the
160.4 s pattern and 3.532 J for 1,764.4 s. With Fake FCC hopping, 36 Hz / 10×
used 0.376 J for 256.8 s and 2.571 J for 1,797.6 s. The hopping power reference
excludes the quiet parts of dwells. These figures include transmitted cover and
shaping. Physical transmitter input power and antenna efficiency require
hardware calibration. [Full waveform energy](../local-evidence/lowrate-2mw-native/emitted-energy.txt).

With Fake FCC hopping, the public 0.4 s dwell must contain a data-bearing cell
with its pulse support, quiet time, ramp and margin. Rate × added symbols must
exceed about 343 Hz for even one protected cell under these guards. Actual
eligibility also depends on placement. The native planner rejected 36, 12, 6,
3.6, 1, 0.1 and 0.01 Hz at 1×. With hopping off, the 0.01 Hz / 1× plan at
Target −12.9897 selected 57,600.4 s per bit, 405,209,986 capture samples and
219.68 GiB of slot workspace. The declared atmospheric motion still requires
a 500 Hz projected sample rate. The many hours of capture explain why workspace
increases again at this end of the rate range.
[Hopping geometry check](../local-evidence/lowrate-2mw-native/plan-on-0.01hz-target-12.9897.json)
and [nonhopping plan](../local-evidence/lowrate-2mw-native/plan-off-0.01hz-target-12.9897.json).

Retain the named local receipts while assessing low-rate communication and CPU
service. Remove superseded planning and measurement outputs when that work closes.

### Blackwell receiver trials at 2 mW

The production C++ probe permits the optional disturbed-HF Gaussian channel on
explicit CUDA as well as CPU. The ordinary GUI channel remains the default.
Remote Release source manifest `3144c4e6…` includes the channel adapter. The
native SM 12.0 build ran on a full RTX PRO 6000 Blackwell Server Edition with
97,887 MiB device memory and MIG off. An approximately 2 MB source upload took
9.5 s. The 72 Hz / 5×, +4 dB-Hz GUI control used the original seed and bit.
Cloud and earlier Ryzen runs produced the same input SHA-256 `af364b67…`,
searched 78 workspaces × 11 coarse frequencies × 10,553 arrival positions,
and both missed the bit. The cloud receiver scored the slot in 53.079 s
(37.041 s coarse, 16.028 s fine), compared with 346.971 s on the Ryzen.
The cloud host had 27 effective logical CPUs and 54 software workers. These
system times include different host processors and worker allocations. The
CUDA receipt counted 6,810 FFTs and 9,054,474 score rows. Peak reserved GPU
allocator memory was 800 MiB, with zero credited HMMA/IMMA calls through the
instrumented lag-matrix paths. On this initial
build the segmented changing-channel search ran on the host. [Cloud trial](../local-evidence/hf-2mw-gpu-20261118/base72.json)
and [device smoke test](../local-evidence/hf-2mw-gpu-20261118/smoke.json).

At the original 36 Hz / 10× hopping settings and Target −12.9897, the full
1,797.6 s active bit was scored quiet. The native receiver processed 10,874,839
samples in 1,022.19 s (787.25 s coarse including 678.39 s varying coarse,
234.85 s fine). Simulation took a separate 44.26 s. The two operational bit
evidences were −9.032 and −9.212, separated by 0.181 against a required
5.293. The device completed 117,377 FFTs and
107,830,554 score rows. The segmented part of coarse search included 202
blocks and 3,935 frequencies. This is a completed erasure at the calculated
link budget on a host that admitted the full search.
[Long hopping trial](../local-evidence/hf-2mw-gpu-20261118/long36.json).

For the Gaussian 30 Hz / 7 ms channel plus Each 65%, the 72 Hz / 5×, +4
control reproduced the earlier CPU input hash `f87f760f…` and erasure. The
cloud slot took 53.371 s. This cross-backend check exercises the physical
channel adapter and records executed device work under the same broad receiver
priors. [Gaussian-channel CUDA control](../local-evidence/hf-2mw-gpu-20261118/base72itu.json).

The nonhopping 60 Hz / 1× trial retains 10 fine chips per second and therefore
used the short-block Doppler tracker. At Target −12.9897, it scored the
1,752 s bit as quiet. Bit separation was 4.514 against a required 5.293,
and log evidence for DATA was −8.070 against a 13.102 presence threshold.
On a second full Blackwell with H=13 and W=26, processing took 1,018.675 s,
including 852.767 s of varying coarse work.
[Nonhopping trial](../local-evidence/lowrate-2mw-runpod/60hz1x-target-m12p9897-seed940600-bit0.json).

An isolated C++/CUDA revision moved the eligible segmented inverse FFTs,
frequency tracking and rank accumulation onto the device. It used FP32 cuFFT
and recorded zero credited HMMA or IMMA calls through the instrumented
lag-matrix paths. Tests on the same H=13, W=26 pod
included a direct rank and peak-ordering check. The 72 Hz / 5× hopping control
improved from 64.734 s to 27.251 s per scored slot, with the varying coarse
phase improving from 48.427 s to 8.798 s. At 60 Hz / 1×, a 1,752 s bit
improved from 1,018.675 s to 272.615 s. Varying coarse improved from 852.767
s to 115.660 s, including 143,365,638 device inverse FFTs. Input hash,
decision, public grid, and candidate count matched in each pair. The operational
bit evidence at 60 Hz / 1× matched exactly. These speedups compare full scored
slots of the same receiver on the same host. The faster negative decisions still
count as misses. The paired receipts document the backend comparison. The
integrated automatic route and its separate timing follow below.
[Short baseline](../local-evidence/lowrate-2mw-runpod/72hz5x-target-p4-baseline-pod2.json),
[short device trial](../local-evidence/lowrate-2mw-runpod/72hz5x-target-p4-segmented-pod2.json)
and [long device trial](../local-evidence/lowrate-2mw-runpod/60hz1x-target-m12p9897-segmented.json).

The integrated source manifest `4ec7a895…` also passed direct CUDA
operator and two-owner allocation-pressure tests on SM120. In a matched
GUI Automatic comparison (same H=27, W=54, input and ordinary half-FFT
selection), the 72 Hz / 5× slot took 53.008 s before and 34.403 s after
the change. Bit log evidence differed by less than 10⁻¹² and both arms scored
quiet. Cross-comparing the older explicitly pinned FP32 run to the new GUI
Automatic run had produced a 0.222 log-evidence difference; the two modes used
different affine arithmetic and fine banks. The same-mode comparison preserves
that original selection. [Earlier Automatic](../local-evidence/hf-2mw-gpu-20261118/legacyauto72.json)
and [integrated Automatic](../local-evidence/hf-2mw-gpu-20261118/canonical/auto72.json).

After integrating the CUDA path, the local serial Triad group passed 43/45.
`triad_receiver_probe` reached an internal 12 s deadline while preparing
coarse work, and `long-calm-polarity` reached CTest's 60 s deadline. In an
unchanged isolated rerun the probe passed and the latter reached the deadline
again. Its exact test executable and arguments subsequently passed in 59.60 s
in a separate diagnostic. The broad probe deadline was also reproducible
with the pre-merge CPU binary on the same host. An exact-source build on the
CPU-only EPYC 4565P passed both original CTests under their original deadlines
in 37.97 s total.
[Original group and focused receipts](../local-evidence/2mw-segmented-validation/)
and [independent CTest results](../local-evidence/2mw-segmented-validation/epyc-4565p/ctest.log).

Probe manifest `bcb25b10…` enables a pure stable-path channel contrast on the
explicit CUDA and automatic GPU admission paths. The GUI HF Worst priors stay
active and the receiver receives no path information. The focused probe test
passed. Three native CPU preflights reproduced the original noisy IQ and
validity hashes at 100 mW and 2 mW (256.8 s) and 2 mW (513.6 s). Each stopped
after capture generation at its explicit 1 GiB memory limit. On the same
manifest the local Ryzen passed all 45 Triad tests in 412.00 s with unchanged
deadlines. [Final serial test log](../local-evidence/lowrate-2mw-native/triad-bcb25b10-serial.log).

For long cloud admission tests, manifest `82bdd9f6…` accepts an explicitly
requested wall deadline of up to 90 minutes on the single-slot `admission`
command. The default remains 30 s and the other commands retain a 60-minute
limit. Five option checks passed. Reversing just the console deadline check
and help text reproduced the earlier 551-file compiled-source manifest.
The local serial group on this revision passed 43/45 in 440.52 s. The
long-calm polarity test reached its 60 s limit, and the broad probe fixture
missed the expected coarse preparation phase. Both recurred under unchanged
deadlines on the focused retest.
[Suite results](../local-evidence/lowrate-2mw-native/triad-82bdd9f6-serial.log)
and [focused retest](../local-evidence/lowrate-2mw-native/triad-82bdd9f6-retest.log).
On the separate EPYC 7713P host with the same source and both GPU backends
disabled, the long-calm polarity test passed in 27.20 s and the probe reached
CTest's unchanged 50 s limit. [EPYC CPU-only retest](../local-evidence/lowrate-2mw-runpod/ctest-cap.log).
Isolating the broad probe's exact `slot` call on the Ryzen recovered its
native reason. It reached its original 12 s measurement deadline during
coarse preparation (12.008 s wall, 11.998 CPU s) after the expected
full-prior Helper bypass and before completing a score tile. The same plan
completed in 0.140 s and retained all 91 frequencies, 78 workspaces, 3,518
arrivals and both rays. The receiver library predated both suite runs.
[Isolated slot](../local-evidence/lowrate-2mw-native/broad-82bdd9f6-isolated-slot.json)
and [matching plan](../local-evidence/lowrate-2mw-native/broad-82bdd9f6-isolated-plan.json).

Manifest `a9cdf6d0…` permits the same Gaussian disturbed-HF probe to select
the automatic GPU backend for a matched propagation comparison. Six option
checks passed. Its CPU preflight reproduced all 862,039 input rows and the
historical Gaussian noisy-IQ and validity hash. The integration test passed
the new checks, then reproduced the Ryzen broad-search deadline above.
[Gaussian automatic-backend checks](../local-evidence/lowrate-2mw-native/triad-probe-a9cdf6d0-first.log).

Longer 2 mW patterns remained difficult. At 36 Hz / 10× with hopping, Target
−16 gave 3,595.2 s and the receiver scored zero as quiet. At 60 Hz / 1×
without hopping, Target −19 gave 7,296 s and also scored quiet, despite active
short-block tracking. The latter ran 637,664,898 segmented inverse FFTs,
scored in 1,351.62 s and used a 49.68 GiB host slot allowance. Its bit
separation was 0.019 against 5.293. [3,595.2 s hopping trial](../local-evidence/hf-2mw-gpu-20261118/long36m16.json)
and [7,296 s nonhopping trial](../local-evidence/lowrate-2mw-runpod/60hz1x-target-m19-segmented-seed940600-bit0.json).

A third nonhopping comparison kept 1× spreading and raised the rate to
360 Hz, increasing the subcarrier spacing from 12.5 Hz at 60 Hz to 75 Hz.
At Target −12.9897, it missed the 1,751.2 s bit. It scored 16 varying-channel
candidates in a 414.05 s slot computation. Log evidence was −7.518 against
13.102 required, with a bit separation of 1.561 against 5.293.
[Wider-spaced nonhopping trial](../local-evidence/lowrate-2mw-runpod/2mw-360hz1x-nohop-target-m12p9897-seed940600-bit0.json).

At 100 mW the same loss and noise density give actual C/N₀ of +4.000 dB-Hz.
Under the original 36 Hz / 10× hopping GUI stress, both Target +4
(256.8 s) and Target −12.9897 (1,797.6 s) erased the transmitted zero.
Log evidence improved from −12.478 to −4.989, while the corresponding
thresholds were 14.982 and 13.102. Bit separation improved from 0.007
to 1.652 and remained below 5.293.
[100 mW short trial](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-p4-seed940600-bit0.json)
and [100 mW long trial](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m12p9897-seed940600-bit0.json).

At the same 100 mW, Target +4, seed and original 256.8 s hopping waveform,
the stable-path control correctly decoded bit 0. DATA presence was 372.302
against 14.982 required and bit separation was 48.331 against 5.293. The
local W=24 receiver took 787.88 s to score all 1,630,039 samples with a
4,569,331,207-byte slot allowance. The matched severe outcome above used the
same full public search on a different host and build. The two decisions show
how propagation changes reception even when the planner selects identical
airtime and nominal received C/N₀.
[100 mW stable hopping control](../local-evidence/lowrate-2mw-native/original_stable_100mw_p4_admission.json).

Lowering the stable-path transmitter to 2 mW at the same Target +4 and
256.8 s duration changed the decision to quiet. The same CPU binary and seed
gave DATA evidence −8.882 against 14.982 required and bit separation 0.669
against 5.293. Scoring took 796.55 s.
[2 mW stable hopping control](../local-evidence/lowrate-2mw-native/original_stable_2mw_p4_admission.json).
At Target −6.97 the 513.6 s stable bit reached the 1,500 s local deadline
without a decision. Its slot allowance was 7.03 GiB; the next 770.4 s tile
needed 13.04 GiB, exceeding the local 9 GiB cap. The 513.6 s result remains
incomplete. [Longer stable control](../local-evidence/lowrate-2mw-native/original_stable_2mw_m6p97_admission.json).
The 513.6 s control later completed on the RTX PRO 4500 with identical noisy
input and full receiver priors. It returned quiet after 216.85 s of scoring.
DATA evidence was −5.684 against 14.292 and bit separation was 2.320 against
5.293, favoring the wrong bit. [Completed stable control](../local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m6p97-admission-seed940600-bit0.json).

At 2 mW, Target −12.9897 and 1,797.6 s on the original 36 Hz / 10× hopping
waveform, the stable control decoded bit 0. Its DATA score was 25.821 against
13.102 required, with a correct-bit gap of 8.181 against 5.293. The same
bit, seed, waveform duration and nominal power produced a GUI-severe erasure
with DATA score −9.118 and bit gap 0.181. The stable slot took 622.35 s of
scoring on the RTX PRO 4500 with W=54 and 31.44 GiB of host slot workspace.
[2 mW stable original-waveform success](../local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940600-bit0.json).

The shorter stable 36 Hz / 10× bit-0 controls at Target −10 (1,027.2 s),
−11 (1,284.0 s) and −12 (1,540.8 s) each scored quiet. On the last one, DATA
presence was 7.628 against 13.236 and the bit gap was 2.683 against 5.293.
The next available duration, 1,797.6 s, also decoded bit 1 at a fresh seed,
940601 (gap 5.942). The planner switched from six to seven 256.8 s tiles
between −12.45 and −12.46 dB-Hz at hundredth-dB resolution. The scored
seven-tile test used Target −12.9897. The modeled bit-0 source energy was
2.571 J at seven tiles, compared with 1.928 J at the shortest demonstrated
stable 3.6 Hz / 1× nonhopping point. For these two stable modes, reducing
rate and removing spreading and hopping saved about 25% of modeled energy.
[Six-tile miss](../local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12-admission-seed940600-bit0.json),
[seven-tile bit-1 holdout](../local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940601-bit1.json),
[planner transitions](../local-evidence/lowrate-2mw-runpod/2mw-original-stable-tile-thresholds.json)
and [sample-integrated energy](../local-evidence/lowrate-2mw-native/fhss-stable-bracket-source-energy.json).
A fresh noise-only slot at seven tiles (seed 940602) correctly scored quiet,
with DATA evidence −7.081 against 13.102 and bit separation 3.440. These
three independent one-slot controls leave continuous alarm completion unmeasured.
[Original waveform quiet control](../local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940602-bit0-noise.json).
On the severe GUI realization, the fresh seven-tile bit 1 returned quiet.
Its DATA score was −9.438 against 13.102, and the bit gap of 1.085 favored
zero. The same 2 mW bit, seed and airtime decoded on the stable path with
a DATA score of 32.897 and a 5.942 gap. Thus both seven-tile bit values
passed on their stable realizations and erased on their matched severe paths.
[Original 2 mW severe bit-1 holdout](../local-evidence/hf-100mw-extended-20261118/2mw-admission-active-940601-1-target--12.98970004336.json).

At 100 mW, Target −18 and 5,906.4 s, the receiver detected DATA presence
(38.048 versus 11.918 required). It favored the transmitted zero by 2.645
log units. The required bit separation was 5.293, so the result was an
ambiguous erasure. All 427 frequencies, 10,553 arrival positions, 78 banks
and both rays were included. Scoring took 1,296.75 s on the H=13 Blackwell
pod. [100 mW −18 decision](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m18-auto-seed940600-bit0.json).

At the same 100 mW, seed and bit, Target −20 selected 9,501.6 s. DATA
presence rose to 80.965 against 11.513 required. The correct bit led by
4.273 against 5.293 required, so the receiver again returned an ambiguous
erasure. On the H=13, W=26 Blackwell host, simulating 57,098,839 samples took
221.39 s and scoring them took 1,976.18 s. The fixed 64 GiB CUDA path
completed the full public grid. This increase in correct-bit evidence was
the criterion for testing one longer 100 mW pattern.
[100 mW −20 decision](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m20-explicit64-seed940600-bit0.json).

Target −21 selected 12,069.6 s (3 h 21 min 9.6 s) at 100 mW. All 72,506,839
samples were scored. DATA presence reached 107.177 against 11.290 required,
but the transmitted zero led by only 4.747 against 5.293 required. The
Current policy again returned an ambiguous erasure. This was 47 times the
stable 256.8 s passing duration with the original hopping setup and seed.
The complete trial took 52.9 minutes on a full Blackwell Server Edition with
13 effective logical CPUs and 26 workers. Simulation took 263.47 s and scoring
2,911.73 s. The native host slot allowance was 119.69 GiB. The device completed
1,092,999,648 segmented inverse FFTs; the ordinary affine FFT length increased
to 16,777,216. None of those three durations decoded seed 940600.
[100 mW −21 decision](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m21-admission-seed940600-bit0.json).

On a fresh realization at the **same 100 mW, Target −21 and 12,069.6 s per
bit**, the receiver correctly decoded bit 1 (seed 940601). DATA presence was
114.834 against 11.290 required and the bit gap was 6.353 against 5.293.
The native trial on the full PRO 6000 with H=13 and W=26 took 288.22 s for
simulation and 2,052.09 s for scoring. This establishes reception at the
specified power on one severe GUI realization. The seed-940600 erasure at
the same setting remains part of the result.
[100 mW severe bit-1 success](../local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target--21.json).

A further independent GUI holdout (seed 940603, bit 0) produced an
ambiguous erasure at exactly this setting. Presence was 103.110 against
11.290 required, but bit 1 led by just 0.281 against 5.293 required.
The three completed GUI trials at −21 therefore yielded one correct one
and two erased zeros. The sample does not isolate bit polarity from
channel realization or establish a delivery probability.
[GUI bit-0 holdout](../local-evidence/hf-100mw-extended-20261118/admission-active-940603-0-target--21.json).

Holding the successful seed 940601 and bit 1 fixed, the original 256.8 s
pattern at Target +4 scored quiet. At Target −20 and 9,501.6 s the signal
was clearly present (86.966 against 11.513), but the correct-bit gap was
4.061 against 5.293 required. At −21 and 12,069.6 s it reached 6.353 and
decoded. These consecutive tested whole-dB settings bracket the observed
change for this bit. The planner also permits intervening durations.
[Short control](../local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target-4.json)
and [9,501.6 s control](../local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target--20.json).

The same 100 mW bit 1 and seed also decoded at Target −21 after replacing
the GUI propagation with two independent Gaussian fading taps at 0 and 7 ms,
each with 30 Hz full 2σ Doppler width. The original receiver priors, clock
settings and Each 65% modulation were retained. DATA presence was 101.730
against 11.290 required and the correct-bit gap was 5.616 against 5.293.
The stronger admission policies erased it. The second full RTX PRO 6000
Blackwell Server Edition scored all 72,506,839 samples in 1,878.21 s,
following 248.16 s of channel simulation. This paired result tests transfer
of a known GUI success to a different fading law.
[Matched Gaussian bit 1](../local-evidence/hf-100mw-gaussian-transfer-20261118/results/admission-active-940601-1-target--21.json)
and [ITU-R F.1487 channel conventions](https://www.itu.int/rec/R-REC-F.1487/en).

On the Gaussian holdout at seed 940603 the receiver detected DATA (82.051
against 11.290 required) and favored the transmitted zero, but the 4.949
bit gap fell short of 5.293. It returned an ambiguous erasure. The two
paired seeds thus yielded one success in two Gaussian trials, versus one
in three total GUI trials. The same seeds across the two models are paired
comparisons and do not provide five independent observations. All 72,506,839
samples were scored, with 265.20 s for simulation and 2,552.80 s for
receiver scoring. [Independent Gaussian bit-0 holdout](../local-evidence/hf-100mw-gaussian-transfer-20261118/results/admission-active-940603-0-target--21.json).

At Target −22.5 (17,205.6 s per bit), the original bit 0 at seed 940600 again
produced an ambiguous erasure. DATA presence reached 152.025 versus 11.002
required. Bit-0 and bit-1 scores were 152.701 and 148.633; the 4.068 gap fell
below both the −21 gap of 4.747 and the required 5.293. This 67-tile bit was
67 times as long as the original stable 100 mW success on the same seed. The
full 103,322,839-sample trial took 403.93 s to simulate and 3,706.77 s to
score. The host's default H=27, W=54 required a 204.57 GB slot allowance,
exceeding its 188 GB cgroup. Explicit H=13 affinity with the default 26 workers
matched the previous 100 mW trial's allocation, fit the 144.37 GB slot quote
and kept all 1,240 frequencies, 10,553 arrivals and 78 template workspaces.
Observed peak process RSS was 74.12 GiB. CUDA ran 1.305 billion segmented
inverse FFTs with no fallback. The reversal in the operational bit score
ended same-seed duration extensions.
[100 mW −22.5 decision](../local-evidence/hf-100mw-extended-20261118/admission-active-940600-0-target--22.5.json).
At the same seed and bit, the 1 W, 256.8 s severe transmission that decoded
used 188.21 J of modeled DATA energy. The 100 mW, 17,205.6 s transmission
that erased used 1,245.61 J. Reducing instantaneous transmit power tenfold
while lengthening this pattern increased modeled energy 6.62-fold. The native
energy counter agreed with an independent sum of every emitted sample.
These are modeled signal joules. Battery, amplifier and radiated energies
require hardware measurements. [Energy comparison](../local-evidence/lowrate-2mw-native/alarm-pattern-energy.json).

The first −18 test at 2 mW with a fixed 64 GiB device allowance on H=27
completed the segmented transforms and then exceeded the available device
workspace during ordinary affine search. It produced no bit decision. Its
32-frequency tile would have needed about 130.80 GB. On H=13 the initial
100 mW trial was stopped when its fixed width of 26 proved too large for the
same limit. The GUI Automatic reruns selected 23-wide tiles on the full
device. The corrected explicit path, source manifest `1f70e467…`, sizes
the affine tile before expensive segmented scoring. At 64 GiB it selected
15 frequencies per tile and covered all 427 frequencies in 29 tiles, including
the partial last tile.
The native CUDA tests on SM120 compared all scores and selected peaks on a
two-ray fixture at 128 MiB, checked the large real geometry and covered
unavailable one-frequency and opt-out cases.
[Original explicit resource stop](../local-evidence/hf-2mw-gpu-20261118/canonical/long36m18.json),
[corrected −18 plan](../local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m18-explicit64-plan.json)
and [device tests](../local-evidence/lowrate-2mw-runpod/explicit64-device-tests.log).
On this final source the local Ryzen ran the full serial Triad group in
392.85 s and passed all 45 tests with unchanged deadlines, including
`long-calm-polarity` in 54.92 s and `triad_receiver_probe` in 31.85 s.
[Final complete test run](../local-evidence/2mw-segmented-validation/final-capfix-triad.log).

The completed automatic rerun at 2 mW and Target −18 processed all 35,527,639
samples of the 5,906.4 s original hopping bit. It returned quiet. DATA log
evidence was −11.326 and bit separation was 0.455. Propagation took 149.53 s
and receiver scoring 1,882.69 s, including 928.81 s coarse and 953.75 s fine.
The device completed all 351,478,218 ordinary score rows as well as the
segmented search. At the same Target and link assumptions, raising power from
2 mW to 100 mW changed the decision from quiet to ambiguous; both erased the
bit. The 2 mW plan at −20 had a 9,501.6 s bit and 105.91 GB host slot
allowance. Its run was stopped during setup because lengthening the preceding
two 2 mW patterns had produced weak evidence for both bits. The separate
2 mW endpoint at −20 remains unscored.
[2 mW −18 decision](../local-evidence/hf-2mw-gpu-20261118/canonical/autolong36m18.json)
and [2 mW −20 plan](../local-evidence/hf-2mw-gpu-20261118/canonical/pauto36m20.json).

### Channel-history diagnostics at 2 mW

One native diagnostic replayed the 2 mW −18 capture to distinguish timing
selection from weak bit discrimination. It reproduced the full noisy input
hash and both scores of an originally selected arrival bin (1871) to within
1.8×10⁻¹⁵ log units. The evaluator then supplied the nearest public bin to the
simulated direct arrival (3101 of 10,553), retaining the original search
penalties. At that bin the signed-lag gain model scored bit 0 at 15.2844 and
bit 1 at 15.2532. Even with this timing information its preference for the
correct bit was only 0.0312, against the ordinary 5.293 separation requirement.
The ordinary receiver had no access to the simulated arrival. This test isolates
one model and seed. The local replay took 246.34 s with 618 MB peak RSS. Retain
the compact native wrapper and receipt until the changing-channel statistic is
superseded or the replay is no longer needed for diagnosis.
[Known-arrival diagnostic](../local-evidence/hf-2mw-gpu-20261118/known-lag-evaluator.json).

A second evaluator used the same 5,906.4 s, 2 mW capture to isolate the
effect of the common time-varying Doppler. Both arms used one public timing
and residual oscillator-frequency bin selected from the known simulation
values. With the native fixed-affine, persistent-gain scorer, the original
samples favored the wrong bit by 0.553 log units. Rotating each noisy sample
by the inverse of the simulator's known common phase history raised the
correct-bit gap to 16.714. The matched noise-only control remained below the
DATA gate. The input hash matched, and the maximum relative sample-power
change was below 8.1×10⁻¹⁶. Selective filtering, relative echoes and noise
remained present. The output rotation imperfectly inverts a phase applied
before the simulated selective filter. This isolates a recoverable bit signal
when a channel history is supplied. An operating receiver must estimate or
average over the unknown history and pay its search cost. The four arms ran
in 368.26 s on the Ryzen with 714 MB peak RSS. Retain the compact native replay
with the known-arrival evidence while investigating the channel estimator.
[Common-phase and noise controls](../local-evidence/hf-2mw-gpu-20261118/common-phase-evaluator.json).

To test whether a shorter gain history retained that evidence, the evaluator
partitioned each phase-corrected capture into publicly anchored 4 s windows
and reset all native per-window gain and local timing state. It reproduced the
whole-slot controls first, then scored all 35,527,639 samples exactly once in
each arm (1,481 windows including the tail). The summed noise-relative scores
for bits 0 and 1 were 4.835 and 5.935 on the wanted input, favoring the wrong
bit by 1.100. On the matched null they were −3.788 and −1.712. Charging the
original global coordinate once gave DATA evidence −14.842 for the wanted
input. Only the 4 s reset model fit the predeclared timing selection. Even
with the known common phase, this particular short-history scorer did not
recover the bit. A general minimum coherence time remains unmeasured.
[Four-second window replay](../local-evidence/hf-2mw-gpu-20261118/window-evaluator/result.json).
In a separately declared 16 s replay (371 windows), the correct-bit gap rose
to 3.180, below 5.293 required. Global-coordinate-weighted DATA evidence was
−3.888 versus 11.918 required. Both whole-window controls and full capture
hashes again matched. This tests the effect of resetting all local gain and
timing state at 16 s, with exact common phase supplied within each interval.
[Sixteen-second window replay](../local-evidence/hf-2mw-gpu-20261118/window-evaluator/result16.json).

### CPU-only cloud trials at 2 mW

The 36 Hz / 10×, 1,797.6 s hopping waveform also erased bit 0 on the separate
Gaussian channel. The two taps were at 0 and 7 ms with 30 Hz full Doppler
width, and Each 65% remained enabled. The unchanged receiver used the wider
GUI priors. Log evidence for DATA was −9.060 against the 13.102 threshold,
and bit separation was 0.186 against 5.293. This independent propagation
model reproduced the GUI trial's negative decision on the original link.
The CPU-only EPYC 4565P (16 cores, 32 logical CPUs, W=64) scored the full
slot in 927.78 s, with 32.73 s for simulation. The CPU-only probe build
required the declaration of its GPU availability dispatcher even when both
optional backends were off.
[Gaussian long receiver receipt](../local-evidence/gaussian-2mw-runpod-cpu/results/original-admission.json).

With the 3.6 Hz / 1× nonhopping setup, the 962.4 s bit that succeeded under
stable propagation scored quiet on the severe GUI channel at the same 2 mW
link. The original attempt on an EPYC 9575F host timed out after 480 s during
simulation. On the EPYC 4565P the same CPU binary finished simulation in
18.63 s and scoring in 11.08 s. At Target −19, the 7,378.4 s bit also scored
quiet in 309.25 s, with 139.24 s for simulation. These timings illustrate
how much actual service can vary between rented 32-vCPU allocations.
[Matched −10 severe trial](../local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m10.json)
and [long −19 trial](../local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m19.json).

The separate 962.4 s bit-1 holdout also changed from a stable correct bit to
a severe erasure. With seed 940601, signal-presence evidence fell from 23.790
to −11.531 against 13.688 required. Bit separation fell from 35.133 to 0.499
against 5.293 required. The two paths retain the same waveform, oscillator
settings, noise density, timing uncertainty and transmitted bit.
[Stable bit 1](../local-evidence/lowrate-2mw-native/stable_6tiles_seed940601_bit1.json)
and [severe bit 1](../local-evidence/lowrate-2mw-native/severe_6tiles_seed940601_bit1.json).

At Target −21.5 the 3.6 Hz / 1× waveform reached 13,473.6 s per bit
(3 h 44 min 33.6 s), fourteen times the 962.4 s stable passing duration.
The severe-channel receiver scored all 81,094,521 samples and returned quiet.
Bit separation was 5.288 against 5.293, while the independent DATA-presence
score was −14.773 against +11.156 required. On the 4565P the slot used a
56,648,067,997-byte inclusive allowance and took 1,029.78 s to score, with
261.44 s for simulation. Changing the bit-separation threshold alone would
leave the presence miss.
[Near-memory-limit −21.5 trial](../local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m21p5.json).

At the same 962.4 s nonhopping settings as the stable 2 mW passing point,
raising the transmitter to 100 mW on the severe GUI channel gave an ambiguous
erasure for bit 0. Its bit separation was 2.925 against 5.293 required.
The Ryzen scored the full slot in 85.03 s, with a separate 31.74 s for channel
generation and a 3,544,373,557-byte slot allowance.
[100 mW low-rate severe control](../local-evidence/lowrate-2mw-native/100mw_severe_6tiles_seed940600_bit0.json).

For the 100 mW, 60 Hz / 1× nonhopping alternative, Target −8 selected a
552 s bit with a 570 s cadence. The full W=24 Ryzen slot fit the local 9 GiB
allowance (6,191,621,961 bytes). The simulation generated all 3,409,505 samples
in 16.58 s. Scoring exceeded the remaining 878 s and the probe stopped without
a bit decision. This is a local processing deadline and an active-slot real-time
failure. [100 mW 60 Hz local timeout](../local-evidence/lowrate-2mw-native/100mw_severe_60hz_1x_m8_seed940600_bit0.json).

An RTX PRO 4500 Blackwell (32,623 MiB, SM 12.0) became available when the
full PRO 6000 allocation was sold out. This smaller GPU and its host's 62 GB
cgroup admitted the 552 s and 1,752 s controls. The 552 s GPU trial reproduced
the local input hash `e8553c7e…` and finished scoring in 208.04 s at W=54.
The Current policy returned an ambiguous erasure. DATA presence was −10.916
against 14.234 required and bit separation was 1.343 against 5.293, favoring
bit 1 when zero was transmitted. The production CUDA path completed 17,201,184
segmented inverse FFTs. The local CPU timing remains censored.
[100 mW 60 Hz GPU decision](../local-evidence/lowrate-2mw-runpod/severe100-gpu-target-m8-admission-seed940600-bit0.json).
On that same GPU, Target −12.9897 lengthened the 100 mW bit to 1,752 s. The
correct bit was favored by 2.246 against 5.293 required, and DATA presence was
−6.568 against 13.102. The full slot again returned an ambiguous erasure.
Simulation took 59.42 s and scoring took 673.45 s. The GPU completed
143,365,638 segmented inverse FFTs.
[100 mW 60 Hz longer GPU decision](../local-evidence/lowrate-2mw-runpod/severe100-gpu-target-m12p98970004336-admission-seed940600-bit0.json).

At the local 9 GiB limit, the Ryzen completed a severe 2 mW slot with Target
−14, 3.6 Hz / 1× and hopping off. It scored the 2,245.6 s bit as quiet in
342.34 s within a 2,280 s cadence. The inclusive slot workspace was
9,397,081,877 bytes (8.75 GiB). The next longer tile selected a 2,406 s bit
whose slot quote already exceeded 9 GiB. [Local memory-bound trial](../local-evidence/lowrate-2mw-native/local_limit_target_m14.json).

The CPU-only hosts supported independent power trials. An RTX PRO 4500
Blackwell handled stable controls, and full RTX PRO 6000 Blackwell devices
ran the long, full-search GUI and Gaussian comparisons. About 2 MB of
source was sufficient to build the native probes on the test hosts.
[Compute selection, memory admission and measured throughput](triad-performance.md#choosing-compute-for-hf-sensitivity-trials).

### Standard FT8 single-frame comparison

WSJT-X 2.7.0 decoded 0/2 complete FT8 messages at **each** of 2 mW and
100 mW on **each** of three channels: stable, Gaussian 30 Hz / 7 ms with
Each 65%, and GUI HF Worst with 3× margin and Each 65%. All twelve weak
15 s slots yielded empty decode lists. The +40 dB-Hz reference
controls decoded 2/2 on the stable channel, and 0/1 on each severe model.
Two noise-only slots produced no messages. The ordinary decoder used no
usable prior-message information. The native channel kept each realization's
physical fading, with noise density independently verified to within 0.033 dB.

A standard FT8 message carries 77 source bits in 12.64 s of transmission.
This consumed 0.02528 J at 2 mW and 1.264 J at 100 mW. The stable paths at
those powers gave nominal −46.97 and −29.98 dB in 2,500 Hz. The FT8 authors
measured the 50% no-AP threshold at −20.8 dB in AWGN and −8.6 dB at 10 Hz
/ 3 ms. Robust's demonstrated stable bits used substantially greater airtime
than one FT8 transmission. The following repeat comparison uses the same
application alarm, transmit-energy ceiling and input cutoff.
The compact reference controls remain while supporting this comparison.
[Native comparison and decoder receipts](../local-evidence/ft8-reference-20261118/reference.md)
and [FT4/FT8 authors' Table 6](https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf).

### Stable alarm comparison at matched energy and deadline

At 2 mW the 3.6 Hz unspread Robust message delivered the byte `e` and
used 5.782267477 J. The comparison sent the same byte as standard FT8 free
text `HEX 65`. One-way transmission every 15 s fit 228 complete FT8 frames
inside that energy ceiling, using 5.76384 J (99.68%). A 229th frame would
exceed the ceiling. Both receivers had 4,964.5535 s of signal observation
from first possible support. Both receivers processed the samples offline.
Live commit latency was unmeasured. WSJT-X decoded zero messages in the 228 occupied slots
and in the following 102 complete listening slots. The remaining 14.5535 s
fell short of a full decoder slot.

The stable propagation, white noise density and oscillator assumptions were
matched. Seed 940600 generated the paired FT8 signal-plus-noise and noise-only
streams continuously at 12 kHz. Robust used its native 6 kHz sample rate.
The measured FT8 SNR was −46.9686 dB in 2,500 Hz against −46.9691 dB
from the link budget, with no clipping. Each chronologically labelled 15 s
audio file went to a fresh WSJT-X 2.7.0 `jt9` process with the flags
`-8 -p 15 -d 3 -w 0 -m 1 -Q 1 -c b -x b -X 0`. The decoder had blank
calls, QSO state 1 and no usable prior-message hints or previous-frame history.
The strong FT8 control recovered the exact alarm. The full
matched noise-only episodes gave zero messages from either receiver.
This establishes a stable-path advantage for the tested alarm and ordinary
independent FT8 decoding under the common budgets. It does not measure a
success probability, a severe-path repeat result or a joint receiver that
combines complex observations across FT8 transmissions.
[Comparison and controls](../local-evidence/ft8-reference-20261118/repeated-alarm.md)
and [per-slot decoder evidence](../local-evidence/ft8-reference-20261118/repeated-alarm.json).

### Reproducing the stable alarm and severe bit tests

The casebook at Git revision `ebbd80f6` contains 58 editor blocks. An
independent census matched all 50 distinct configurations with completed
receiver decisions to the power, Target, channel, waveform, bit length and
cadence in their native records. Eight additional blocks describe planning,
incomplete, diagnostic or matched-reference cases. Individual control bits,
seeds and replays at the same settings share a block. The detailed linked
JSON files are local evidence. The tracked casebook retains the measurements
and their interpretation without those files.

Firefox ESR 153.7.0 opened all 18 fields per block and all 187 commentary
lines. Downloading the unchanged list reproduced its original bytes
(SHA-256 `62a550c4b8ded2629853a2ecda36f162fc9475f6735f259524e933f830a5b766`).
A custom Target edit changed exactly one field and survived a second
open/download cycle. The visual editor preserves historical commentary
when configuration fields change. The separate manual-capacity runner reads
the original 16-field workload format. [Visual editor](../tools-endUser/robust-test-list-editor.html)
and [manual capacity workflow](robust-manual-capacity.md).

The first stable 2 mW complete-message replay used the command below from
the repository root. Its executable is the [native receiver probe](../tools/triad-receiver-probe.cpp).
The compiled defaults for these captures were 6,000 samples/s, a 1,725 Hz
DATA IF, −164 dBm/Hz assumed input noise, calendar epoch 1793850000 and
initial waveform ordinal 7000037. CPU worker allocation was automatic
(12 effective logical CPUs and 24 workers here). This replay used manifest
`82bdd9f6…`. For the independent holdout, seed 950101 and manifest
`a9cdf6d0…` used the same receiver and settings.

```sh
build/dev/triad_receiver_probe message --case hf --encrypt on \
  --helper off --helper-power-w 0 --data-power-w 0.002 --path-loss-db 180 \
  --timing-policy broad --base-oscillator gpsdo-xo --shift-oscillator gpsdo-xo \
  --spread-preset hf-worst --selective-perturbation each-65 --margin 3 \
  --fhss off --burst-wait-tiles 0 --rate 3.6 --rf-hz 30000000 \
  --added-symbols 1 --added-interleave off --target-cn0 -10 --channel-severity 0 \
  --slots 5 --max-samples 32000000 --max-spool-bytes 805306368 \
  --memory-limit-bytes 9663676416 --deadline-ms 1200000 --seed 940600
```

For a cheap geometry check, use the probe’s `plan` command. For single-bit
sensitivity use `admission` and read the Current-policy decision. A stable
control uses `--channel-severity 0` with the severe receiver fields intact.
The severe GUI comparison omits the severity override. The Gaussian comparison
also selects `--channel-model itu-disturbed`. In a fixed-power duration sweep,
vary `--target-cn0` while holding physical power, noise, bit and seed fixed.
For the 100 mW matched pair, use `admission` and retain the HF loss,
oscillators, spread, selective perturbation, timing policy and margin above.
Set `--data-power-w .1 --noise-dbm-hz -164 --fhss on --rate 36
--added-symbols 10 --added-interleave on --bit 1 --seed 940601 --stratum active`.
The severe GUI run erased at `--target-cn0 -20` and decoded at `-21`.
The −21 Gaussian transfer and its bit-0 holdout at seed 940603 used
`--channel-model itu-disturbed`. The full Blackwell GPU ran with
`--backend gpu --require-accelerator on`, 13 effective logical CPUs and
26 workers. The Gaussian jobs allowed 110,000,000 samples and a 5,400,000 ms
deadline. Use the host and device workspace quotes for this larger geometry.
For an independently seeded bit or a noise-only control, record both the
signal-presence and bit-separation tests. A stopped run supplies a timing
or resource limit and leaves the bit decision open.
[Measured operating points](indexes/robust.md#low-power-hf-alarm-results)
and [field-by-field configurations](../_tests-RobustModem-HF-simulations-2026-11-18.md).

## Tiny GPSDO midpoint-bound repair — 17 November 2026

An exact manual row-6 HF-FHSS single-bit simulation at Target 32 dB-Hz,
nominal actual C/N0 20.0206 dB-Hz (4 W, 180 dB loss and assumed noise density
−164 dBm/Hz), seed 940600 and bit 0 reached the native receiver but raised
`Robust midpoint timing mapped outside its public clock bound` before a bit
decision. A separate fixed-reference 32 dB-Hz receiver with a −12 dB signal
contrast reached the same error; that contrast is not the ordinary nominal
noise calibration. A 120-second single-slot attempt and a 170-second complete
message attempt timed out without decisions. These errors and timeouts are not
reception failures or sensitivity measurements.

A focused native regression at the small GPSDO clock bound reproduced the
original error on a legal midpoint lag. The repair accounts for cancellation
roundoff when mapping the admitted arrival interval back to clock rate; the
coupled interval check and exact output clock and arrival bounds remain in
force. The regression now passes for 8,443 legal grid lags and rejects both
adjacent outside-support lags. `CCACHE_DISABLE=1 ./build.sh test triad` passed
45/45. The repaired exact-row rerun of the same 256.8-second bit and seeded
input (`affe39e3…`, source manifest `f0160701…`) completed the public slot and
the Current admission policy received bit 0 correctly. Guarded, Strict,
Very-strict, Extreme and Maximum policies produced erasures from competing
evidence. On the measured CPU, receiver scoring took 449.36 seconds and the
separate simulation took 6.40 seconds. At actual C/N0 8 dB-Hz with the same
seed and waveform target, the impaired-channel Current policy instead returned
ambiguous/erasure, after 587.64 seconds of receiver scoring; its input hash
was `7e79724b…`, reflecting the changed noise level. This brackets two observed
outcomes for one seeded bit, not complete-message delivery, a population
sensitivity threshold or a capacity qualification. The stable-channel 8 dB-Hz
comparison ended incomplete at its deadline (600.112 seconds elapsed) without
a bit decision, so no calm-versus-impaired penalty follows. A severe-channel
14 dB-Hz midpoint test with the same seed also ended incomplete at its
deadline (600.103 seconds elapsed) without a bit decision. Neither timeout is
an erasure or a reception failure. A repeat of the severe 14 dB-Hz case with a
900-second bound completed and received bit 0 correctly under Current after
624.34 seconds of receiver scoring. Its input hash was `fdb57f30…` and source
manifest remained `f0160701…`. This additional outcome narrows the observed
seeded-bit transition to between the tested 8 and 14 dB-Hz levels, without
establishing a general threshold or reliable delivery probability.

Temporary root `local-evidence/sensitivity-row6-*` receipts retain the failed,
incomplete and successful attempts for this investigation. Preserve them until
the unresolved threshold and calm-versus-impaired comparison are resolved or
superseded; then distill applicable findings here and remove redundant outputs.
The [14 dB-Hz completion](../local-evidence/sensitivity-row6-fixed-severe-14-900s.json)
and its earlier [incomplete attempt](../local-evidence/sensitivity-row6-fixed-severe-14.json)
remain distinct receipts.

## Persistent-channel and calendar-age follow-through — 17 November 2026

Final source `718f91c5…` removes demonstrated reception losses from combined
negative Doppler, long coherent-model exclusion, discarded coherent timing and
smaller per-owner worker allocations. The corrected ordinary receiver recovers
the original failed samples, while final severe, delayed-dominant, HF and EME
preservation checks pass. Complete service costs increase materially where the
newly available models must search quiet windows; the measured increases and
memory bounds are retained below. The long-calm continuation delivers each
bit and closes physically with two erasures, not a clean raw message. This is
mechanism and finite service qualification, not a natural outage probability
or an optimal sensitivity bound.

This investigation starts from clean `24929430`, preserving the preceding
ordinary-reception repairs. Its separate questions are older-calendar timing
cost, path-power changes not represented by a permanently present strongest
direct ray, and current applicability of retained noise/cross-path failures.
The tests use the native ordinary receiver and real quiet successors, without
cross-attempt combination, clock resets or simulator truth in acquisition.
Temporary `local-evidence/persistent-availability-20261117/` paths below are
relative to the appraisal directory; `local-evidence/current-persistent-20261117/`
is at the repository root. Compact failed and successful receipts support the
current validation claims and their reproduced counterexamples. Private copied
implementations and redundant captures are removed after their diagnostic use.

The first bounded noise requalification uses the three existing `noise-bursts`
cases: wanted at nominal white C/N0 32 and 8, and noise-only at 32, seed 9301.
Each retains its 8-million-row / 119.5-second native ceiling, with a 180-second
aggregate stop. All three finish in 3.836 seconds on the preceding frozen
native binary. Both wanted cases receive **all three DATA bits correctly**.
At 32, both true quiet successors become false DATA and prevent physical
completion; at 8, the quiet pair and complete message pass. The noise-only
case reports five false DATA decisions. This is a current completion/noise-model
failure, not an observed failure to deliver the raw distress bit.

The nominal level changes white noise and its receiver reference together;
fixed colored and shot terms stay unchanged. The receiver declares variance
1.211438 at 32 and 304.299493 at 8. The other terms add approximately 1.514298
mean power, with temporal correlation and non-Gaussian shot tails. Merely adding
their mean powers does not establish an adequate scalar covariance bound or
a Gaussian likelihood. The scorer's `noise_statistics_valid` flag records
valid completed numerical processing; it does not test the actual noise law.
Noise-only IQ matches the archived capture exactly; wanted IQ changes with the
current carrier-v1 waveform. The wrapper's obsolete stopping-field name caused
the already authorized third control to run after the decisive pair; no fourth
case ran. Exact arguments, identities, original results and per-slot scores are
retained in `local-evidence/current-persistent-20261117/noise/summary.json`.

Two predeclared component ablations then finish in 2.309 seconds total, inside
their 30-second aggregate bound, on unchanged native source and archive. Both
noise-only input hashes match the old captures exactly. White plus AR(1)
produces five AMBIGUOUS slots and no DATA: its quiet projection energy is
14,878–17,076 versus threshold 5,528.85. White plus shot noise produces five
false DATA slots, with presence evidence 429.99–960.39. Thus both mechanisms
can block absence recognition, by different decision paths. These results do
not justify forcing AMBIGUOUS to QUIET or treating filtering as completion.

The next calibration discriminator changes only the receiver variance on the
original wanted/noise captures, retaining the original source and DATA basis
calibration. In this non-varying fixture, quiet projection energy scales
exactly as the reciprocal variance while its threshold stays fixed. Recorded
energy/threshold ratios of 9.27–12.13 prove that the proposed 2.25× mean-power
correction cannot make the affected windows QUIET; that negative result is
source-derived, not a new executed trial. The separately derived stationary
second-moment spectral envelope is
`1 + .25*coth(pi*3/1920) + coth(1/(2*1920*.02)) = 128.734331…` times the white
reference. A rounded-up 128.735× contrast is limited to the original wanted
and noise-only cases. This is a diagnostic reference, not a production noise
constant or a valid Gaussian-tail claim for the Poisson process.

Both 128.735× native contrasts pass in 2.661 seconds total, within their
60-second aggregate / 30-second external per-case limits. Exact input hashes,
sample counts, source/noise parameters and canonical DATA basis gain
`1.0043005406109369` remain unchanged. Receiver variance alone increases from
`1.2114381014019706` to `155.9544839839827`. Wanted decisions become correct
`001, QUIET, QUIET`, completing `e`; the noise-only case becomes five QUIET
decisions. Wanted presence evidence remains 253.41–275.65 versus threshold
16.48. Quiet projection energies are 403.28 and 521.16 versus threshold
5,528.85. This isolates a recoverable calibration mismatch on the constructed
episode. It does not establish an automatic estimator or qualify the existing
live control, which also scales DATA basis gain at fixed Target SNR.
The exact two-run receipt is
`local-evidence/current-persistent-20261117/noise/noise-reference-summary.json`.

The corresponding existing live-control scaling also passes on identical IQ.
At fixed Target SNR, `prepare_profile` multiplies receiver variance by `f`
and its calibrated DATA basis amplitude by `sqrt(f)` when Live input noise
increases by `10*log10(f)` dB. For `f=128.735`, executed basis gain is
`11.394939275827989` and variance remains `155.9544839839827`. Wanted `001`
and both quiet successors pass; noise-only gives five QUIET decisions. The
two executions take 2.679 seconds total. The initial receipt predicate had
incorrectly frozen `receiver_reference_mean_norm`, which is defined as inverse
effective receiver gain squared; correcting its expected value to baseline/f
requires no source or IQ change and no wanted rerun. That predicate failure
is preserved separately. Temporary diagnostic options are removed afterward.

This is native scorer equivalence to an existing live control, not an actual
PCM/device test or a new automatic estimator. The 21.097 dB reference adjustment
is not a measured sensitivity penalty. It is a fixture-specific covariance
envelope, not a recommended universal setting. A real operator needs an
independently justified input-noise reference for the device, gain, bandwidth
and noise regime; mean power alone does not supply it, and the ideal Poisson
model still has no finite Gaussian exponential bound. The useful result is
that this episode's completion failure has a practical finite reference
remedy without discarding samples, changing thresholds or altering the bits.
Receipts: `local-evidence/current-persistent-20261117/noise/live-noise-reference-summary.json`.

The predeclared older-calendar discriminator uses the existing 176.8-second
GUI waveform at Target −3, actual +10 dB-Hz, HF Worst / Each 65% / Margin 3,
oscillator/path/noise seeds 62014 / 62034 / 72014 and selected public slot 48.
The native channel must advance continuously from sample zero, with all
preceding scheduled attempts and actual waveform ordinals preserved. One
selected DATA and its two genuine quiet successors are scored. The existing
26-million-row / 450-second selected-duration limits remain. This is an
age-12 versus age-48 comparison at fixed duration; its absolute clock age is
similar to the earlier 696.8-second slot-12 case, not a days-long extension.
A sealed-spool replay must verify both full-capture and selected-input hashes,
charge receiver construction and spool verification, and use only the selected
unique input span as its service denominator. Three windows do not establish
steady backlog capacity. Original and optimized comparisons must use identical
input and explicit resource limits; incomplete or rejected runs remain failures.

The original three-window preflight rejects slot 48 before DSP: its public tail
bound requires clean quiet slots 50 and 51, rather than 49 and 50. The corrected
selected harness retains and scores **all four windows 48–51**, preserving the
decision in guard window 49. It records actual first-QQ completion independently
of the guaranteed clean pair and distinguishes bit delivery, physical completion
and clean communication; extra DATA or guard erasures cannot disappear into a
success flag. The full continuous history is 17,973,084 rows / 52 public slots,
with 17 scheduled attempts. Selected input is `[16,583,818,17,973,084)` and the
305,542,428-byte spool fits the explicit 442 MB cap. Earlier selected captures
retain their exact extent. Independent source review and 176 runner checks pass
before the frozen original-native replay.

Both frozen original-native replays then pass all 32 acceptance assertions.
Early slot 12 receives bit 1 followed by two QUIET decisions; late slot 48
receives bit 0 followed by three QUIET decisions, with no wrong or false DATA.
Late physical completion uses the actual first quiet pair 49/50; the guaranteed
pair 50/51 is also scored. The late baseline therefore already succeeds: the
subsequent exact-correlation optimization addresses cost, not a newly repaired
late bit. Early full/selected IQ hashes match the archived capture, and the
late capture and accepted-span hashes are frozen for the candidate comparison.

On the Ryzen 5 PRO 5650U (12 effective logical CPUs, automatic 24 workers),
cold selected service is 21.886 seconds for 539.634 seconds of unique early
input and 57.719 seconds for 723.576 seconds of unique late input. These times
include construction and reading/hashing the entire sealed spool; rendering
is separately measured and is not subtracted from overlapping work. Peak
owners are three and four; total reserved bytes are 2,558,159,821 and
3,676,269,823, within the explicit 6,442,450,944-byte limit. These short cold
replays are not sustained-backlog capacity qualification. The original explicit
three-owner invocation was rejected by the selected-service parser before DSP;
both successful comparisons use the same automatic owner limit and full W=24.
Setup, preflight and replay wall time totals 180.707 seconds within the declared
700-second budget. Exact arguments, identities and assertions are retained in
`local-evidence/persistent-availability-20261117/late-baseline/automatic-owners/`.

The optimized native kernel passes three focused checks: coarse search,
signed-gain timing (including complete-cell direct-sum comparisons), and the
channel/path-power controls. Two frozen component comparisons preserve input
hashes, public lag/frequency/block coverage and nominees. Transform row-stages
fall from 650,674,176 to 428,670,976 and from 603,832,320 to 419,558,400
(34.12% and 30.52%). The old fixed-rank scores remain bitwise identical. One
direct score differs by 1.76e−7 at approximately 3.60e6, consistent with the
changed floating-point transform order; all nominees are unchanged. Measured
component time falls from 0.3640 to 0.3276 seconds and from 0.3470 to 0.2963
seconds with two workers. These are component points, not complete-service
speedups. Charged workspace rises by 17,864 and 17,840 bytes. Exact source,
binaries, counts and inputs are retained in
`local-evidence/persistent-availability-20261117/clock-rank-components/`.

Matched candidate cold replays preserve every input hash, public work count,
timing nominee, per-map evidence and decision in both cases. Early geometry
does not select the new tiled kernel: its cold service is 22.299 versus
21.886 seconds (a 1.9% increase in this single comparison). Late slot 48
reduces reported transform row-stages from 38,300,123,136 to 32,042,225,664
(16.34%) and cold service from 57.719 to 54.494 seconds (5.6% shorter in this
single comparison). Its reservation increases by 1,246,080 bytes to
3,677,515,903 bytes, with four owners and all 24 software workers. These are
descriptive finite paired measurements, not a statistically established
speedup or sustained-capacity result. The unchanged successful late baseline
is preserved; no additional reception recovery is attributed to this kernel.

The conditional opposite-polarity late holdout also receives DATA1 and three
QUIET successors, with physical completion at pair 49/50 and cold service
54.752 seconds. All three candidate cases pass 58 acceptance assertions in
282.391 seconds aggregate. The 144-minute history renders 17 transmissions
continuously, but only the selected late bit and its guards are scored; this
is not 17 successful reception trials. The matched comparison and holdout are
bound to native source `194be24f…` and application `a8887934…`, before the
separate combined-frequency-support investigation below. Receipts are in
`local-evidence/persistent-availability-20261117/late-baseline/matched-candidate/`.

The path-power support audit identifies a separate representational gap.
With HF Worst / Margin 3, the simulator independently permits common
atmospheric motion of ±90 Hz and delayed-ray relative motion of ±90 Hz.
Their instantaneous sum can reach ±180 Hz. Projection preserves the wider
support, but the direct timing rank and the aligned independent-gain tracker
use oscillator-plus-common motion only (about ±100.35 Hz in this fixture).
A fixed positive relative-frequency basis does not cover the negative
combined endpoint, and constant block gains cannot exactly absorb arbitrary
residual rotation. Independent source reviews confirm the gap. It is not
yet an executed blackout demonstration: other models or finite leakage may
still recover a particular capture. The declared discriminator uses native
constant-frequency endpoints, then checks delayed-dominant and balanced rays,
before widening the affected support while retaining the simpler families.

The first two ordinary endpoint executions demonstrate the asymmetry. With a
10.4-second bit, fixed +10 dB-Hz reference, pure delayed ray at exactly the
declared 21 ms delay, zero clock/arrival offset and common/relative motion
at their ±90 Hz endpoints, the old receiver receives bit zero at **+180 Hz**
(presence evidence 51.629 versus threshold 16.483), followed by two QUIET slots
and physical completion. At **−180 Hz**, it calls the active transmission
QUIET (evidence −2.910), then correctly scores the two genuinely quiet slots;
the wanted bit and message are lost. This is a complete-receiver counterexample
at unchanged signal/noise energy, not merely a symbolic model limitation.

The two native runs take 2.035 and 2.084 seconds, use all 24 software workers
and process 252,515 rows each. The first setup rejected a self-imposed
100,000-row cap before DSP because the public quiet spacing requires the longer
capture; the cap was prospectively corrected to 300,000 without changing
geometry. The predeclared total permits these two old receiver calls plus eight
candidate endpoint calls, with a 45-second per-call / 120-second aggregate
limit and 1 GiB workspace. Their source is a native canonical three-carrier
waveform and fixed Gaussian noise seed 83041. Constant corners isolate model
support; they do not establish the occurrence or duration of these extremes
on a natural path. Exact input and executable identities are retained under
`local-evidence/persistent-availability-20261117/doppler-support/`.

Widening the independent-gain frequency support alone is insufficient at this
short weak operating point. Across eight candidate corners, both positive
pure-echo bits pass, both negative pure-echo bits miss, both negative
delayed-dominant (`p≈0.233`) bits miss, and both negative balanced (`p=0.5`)
bits pass; genuine quiet successors remain QUIET throughout. The original two
and these eight calls take approximately 22.81 seconds. Wider states retain
useful negative-bit evidence, but short-block nuisance gains do not exploit
the same coherence as the successful positive case. No threshold is lowered.

One additional unchanged positive active-window diagnostic identifies the
successful existing **drift model**, with bit evidence 54.599 before outer
mixture, versus less than seven in the short tracking/independent alternatives.
Mirroring the existing short tracker would therefore target the wrong cause.
An existing drift-score witness with negative relative Doppler, evaluated at
the same public timing coordinate and with full timing/frequency/slope/power
priors, produces correct/wrong evidence **66.713 / −10.583** on the failed
negative IQ. The corresponding positive witness gives 54.114 / −10.742.
Each takes approximately 0.096 seconds. This identifies information already
usable by the canonical coherent inference when the relative-Doppler sign is
represented. The proposed repair adds that sign symmetrically inside the
existing drift family; it does not add a separate two-frequency grid or a
historical channel search.

The mirrored-drift production correction then passes **all nine complete
ordinary endpoint tests at the original +10 dB-Hz**, without the contemplated
stronger-signal retry. Both bit values pass for pure delayed rays at either
frequency sign, and for negative-endpoint delayed-dominant and balanced rays.
A fresh-noise holdout with common −90 Hz, relative −45 Hz and `p≈0.233`
also passes. Every case scores both actual quiet successors and completes
physically. Negative pure-echo evidence is 63.535 / 47.666 for bits zero/one,
versus the widened-only misses at 7.760 / 3.296. Positive evidence becomes
50.936 / 80.490, reflecting the additional sign's normalized prior. The
intermediate holdout gives 62.079. This is recovery by the ordinary production
receiver on the unchanged negative input, without a supplied timing/frequency
winner or a channel-specific operating mode.

The nine-run wrapper totals 27.20 seconds. Matched complete native costs rise
from 2.035 to 2.726 seconds for the negative case and from 2.084 to 2.798
seconds for the positive case (approximately 34% on this small geometry).
Charged peak memory is approximately 705 MB with W=24. These costs belong to
the complete three-window endpoint tests; the existing longer weak histories
need their separate comparison. Three focused native checks pass in 42.99
seconds, including a new signed-Doppler regression for both signs/bits, noise,
more than 65 independent states, zero-Doppler equivalence and exact prior
normalization for overlapping/different/missing timing-atom counts. Independent
source reviews pass for widened support, explicit unavailable-extension
reporting and mirrored-drift priors/workspace. At this checkpoint the Triad
group contains 43 checks; the final group results appear below.

One private short-fine diagnostic's coordinate label was corrected in a
separate provenance note: its actual frequency coordinate was ±90 Hz, not the
zero printed by the old diagnostic label. Frozen arguments and source retain
the executed coordinate; samples and scores are unchanged. The decisive
drift witness and final ordinary receiver results are unaffected. Failed
original and widened-only results remain alongside the successful mirrored
receipts, rather than being replaced.

The frequency-support/sign checkpoint requalification passes **all three original
captures and 64 acceptance checks**, with every full/DATA/accepted-span IQ
hash unchanged: early slot-12 bit one and late slot-48 bits zero and one all
arrive, every guard remains QUIET, and physical completion is preserved.
Direct/independent tracking has 79 frequency states; the fixed-rank subset
remains 43. Cold service is 23.757 seconds early and 60.602 / 56.730 seconds
for the two late polarities. Against the original baseline, the matched early
and late-zero increases are 8.55% and 4.99% in these single comparisons.
Peak reserved memory remains below 3.685 GB with four owners and W=24.
The 299.132-second wrapper aggregate fits its 700-second budget; no case is
skipped or timed out.

These broad Each 65% histories qualify the widened direct/independent search,
not the mirrored drift model: its existing reduced-proposal bandwidth guard
declines these geometries. The drift sign qualification is the separate small
endpoint cohort above. The earlier overlap-save-only transform reduction is
likewise a checkpoint result, not a net final-work reduction after expanding
frequency support. These replays bind application `b040af6d…` and native
source `4d7b6986…`; full identities and comparisons are retained in
`local-evidence/persistent-availability-20261117/late-baseline/final-support/`.

Four subsequent path-power checks also pass at this checkpoint. Both bit
values arrive with direct-power fraction `p=0.23276941178344046`, derived from
the measured 5.18 dB echo contrast, and with the conditional synthetic
near-null `p=0.01`. The seeded excess delay remains 18.108 ms, not the measured
0.317 ms. Each 176.8-second ordinary bit uses Target −3, actual +10 dB-Hz,
slot 12 and the same HF Worst / Each 65% / Margin 3 history. All genuine quiet
successors and physical completion pass. Full-history combined atmospheric
motion ranges from −178.530 to +176.424 Hz; this includes silence and is not a
per-bit or Doppler-spread estimate. The four wrappers total 200.001 seconds;
cold service is 23.573–27.159 seconds. Automatic W=24 and three owners are
preserved, with peak reservation 2,564,056,845 bytes.

The path-power launcher accidentally omitted the predeclared explicit 6 GiB
limit. Its actual automatic limits were 10.8–11.31 GB. Observed reservation is
below 6 GiB, but these runs **do not qualify enforcement of that limit**.
The first correct native raw-bit result was also rejected by a Python check
expecting zero where the raw mode correctly emits JSON null. An independently
reviewed exact-null correction revalidates the unchanged output; the first
native case is not rerun. The original failure and the resumed three-case
budget are retained. This is a harness correction, not a receiver recovery.

### Long coherent observations and the FFT planning boundary

A further source audit finds a different avoidable exclusion: the drift
model's preferred 262,144-point FFT requires decimation stride three for the
176.8-second broad profile, while its frequency-fidelity bound permits only
stride two. The branch therefore disappears as duration increases. Faithful
524,288-point processing fits inside the already quoted complete receiver
workspace. At 696.8 seconds the corresponding size is 2,097,152. An internal
preferred transform size must not silently become a sensitivity boundary.

The predeclared discriminator uses one new static, pure delayed ray at 21 ms,
common/relative Doppler −90/−90 Hz, actual clock zero and arrival −0.5 ms,
while retaining the conservative receiver profile. The 176.8-second bit has
actual C/N0 −2.304489 dB-Hz, giving essentially the same received energy/noise
as the successful **10.4-second / +10 dB-Hz** short endpoint: measured ratios
103.98755 versus 103.96479, differing by 0.000951 dB. This is a separately
constructed calm history; it does not reuse a moving atmospheric trajectory.

Ordinary active-slot acquisition calls that bit QUIET, with evidence −5.286
versus threshold 15.386. A private larger-FFT drift witness on the identical
344,690-row aperture gives correct-bit evidence 95.743 after full public
timing, sign, family and no-line mixture penalties. The original diagnostic
summary omitted the no-line factor; its separately corrected value subtracts
log(2), without changing input or rerunning DSP. Ordinary scoring and witness
take 11.072 and 1.515 seconds; aggregate setup/execution is 13.713 seconds
inside the declared 180-second bound. Active IQ SHA-256 is
`57285f4f3bad38f5bdb885c249efe74f174f09a536a782bca24583cee550f437`.
The witness identifies a usable model, but by itself is not complete reception.

The production planner now retries this bandwidth/FFT exclusion with a larger
faithful transform, retaining stride one when reduction is unsuitable. The
ordinary and retained-line extensions are admitted independently only when
their complete peaks fit **both original full-W sequential and overlapped
workspace quotes**. All dependent coarse/projection work is included. A
rejected optional extension leaves the original families and priors intact and
reports a preserved-lease limit, rather than claiming exhausted physical RAM.
Admission precedes received-IQ inspection. At least three slope proposals
preserve zero plus a best proposal for each bit, with the full slope prior.

The 176.8- and 696.8-second W=24 plans retain their original complete quotes
of 693,098,478 and 2,669,814,011 bytes. These are owner planning bounds, not
measured stream reservations. Focused planning tests cover faithful FFTs,
independent ordinary/line admission, unchanged leases and rollback. Default
W=24 and W=1 admit the tested zero-clock extension; an explicit eight-lane
planning shape rejects it under its smaller preserved lease. That conditional
policy result is not an observed clock-related failure or proof that every
worker allocation has equal sensitivity. The native long-calm active-slot
regression passes in 18.31 seconds; short-drift and signed-Doppler checks also
pass. Complete ordinary acquisition and quiet completion are qualified below.

Independent review also corrects optional error handling and retained-line
telemetry. An optional larger-model shape failure restores the valid baseline;
mandatory baseline errors, allocation failure and cancellation still propagate.
A native planning-only regression exercises these distinctions in 0.14 seconds
without DSP. Executed line status now comes from its frozen plan. This
checkpoint build binds native source `612b7839…`, application `4a893189…`
and receiver archive `97878133…`. The preceding application build passes all
six appraisal checks, including 177 runner tests; after these narrow changes,
the incremental build and pre-DSP retired-option rejection checks pass without
rerunning that unchanged suite. Full hashes and scopes are in
`local-evidence/persistent-availability-20261117/late-harness/reviewed-status-identity.json`.

The first complete-receiver pair on this source **does not pass**. Bit zero
is recovered on the frozen active IQ (ordinary evidence 86.26), but its quiet
successors are QUIET / AMBIGUOUS, preventing physical completion. The fresh
bit-one control is missed (evidence −5.724) and has the identical quiet outcomes.
Both quiet apertures have exactly zero generated signal energy. Every source,
input, calendar and resource assertion passes, with explicit 6 GiB, W=24 and
peak reservation below 2.41 GB. The pair takes 87.261 seconds within its
300-second aggregate bound. Thus the active-zero regression and private
witness are insufficient evidence for a complete long-calm repair; the original
failure receipts remain under `drift-limit/complete-long-calm-execution.json`
and motivate the following matched-sample diagnosis.

The quiet failure is isolated in a 0.24-second component check on the exact
quiet-14 samples. The unchanged legacy absence statistic is 49,827.014643
against threshold 49,815.795779 over 49,128 proper-complex modes. Its declared
upper-tail probability is 0.001, so a Gaussian noise window can legitimately
remain ambiguous. The expanded quiet statistic passes (113,824.276751 versus
114,250.602308), no interference line is retained, and all validity checks pass.
Both bit cases share this identical noise window; they are not two independent
absence failures. No threshold or noise reference is changed. The appropriate
continuation is additional genuine quiet observations, retaining the ambiguous
decision, rather than forcing this window quiet or attributing it to drift.
Under the unchanged completion contract, if slots 15/16 provide the final
quiet pair, slots 13/14 remain interior erasures. Such a continuation can
demonstrate delivery of the wanted bit and physical closure, but cannot be
reported as a complete erasure-free raw message. An initial follow-up fixture
expectation of zero erasures was corrected by source review before execution.

The bit-one failure is a separate acquisition defect. Instrumented canonical
drift scoring reproduces its miss. A public timing cell at epoch offset zero,
arrival −0.5 ms and clock zero receives the correct bit with full-prior
presence evidence 45.643, but that cell is absent from the initial timing union.
Crucially, an **already present** nearby cell at arrival −2.5 ms also receives
it, with evidence 36.217 versus threshold 15.386. Its correct/wrong bit evidence
is 36.910 / −20.789 after the full 702-cell timing prior, sign and outer mixture
penalties. The negative-relative-Doppler segmented ridge rank discards this
usable cell (rank 8,122 versus retained values about 8,318–8,519). Thus a wider
timing bank or another channel model is not justified by this failure: the
existing candidate must survive a ranking that respects coherent information.
These instrumented component calls are diagnostic evidence, not production
service measurements; the unchanged complete-pair failures remain recorded.

A separate allocation contrast exposes another consequence of the preserved-
lease policy. The same zero-clock waveform calendar permits cropping the frozen
bit-zero input to `[4,147,195,4,489,293)`, with SHA-256
`23fae84145f0ca594898ec1f11225b3dcc01f8a05f782738d7df695ef7c40cbb`.
With eight per-owner workers, the 276,033,982-byte baseline lease rejects the
expanded drift and the bit is missed (presence about −4.147). With 24 workers,
the 453,996,094-byte baseline admits it and receives DATA0 (about 96.220).
Both use the identical 342,098-row crop and explicit 6 GiB DSP allowance; native
children separately enforce a 6 GiB process address-space limit. Peak RSS is
187,211,776 / 501,420,032 bytes. Component scoring takes about 3.576 / 9.189
seconds; the pair totals 13.175 seconds within its 180-second bound.

This is a component comparison, not complete-stream admission or a capacity
measurement. Nevertheless, automatic W=24 scheduling can assign eight lanes to
each owner when that admits a wider ready prefix. The demonstrated sensitivity
change therefore warrants a generic remedy: fit the new coherent stage's
parallel width inside the existing owner lease instead of discarding that
model. Global workers, timing/frequency hypotheses and the original complete
lease need not change. The tested 300-ppm broad profile already admits the
extension with eight workers; that different planning result would not have
resolved this zero-clock counterexample.

The combined correction retains all eight prior ridge nominees and adds at
most one coherent zero-slope nominee per bit, separately ranked and deduplicated
inside the same timing union. A bounded zero-slope peak-frontier cache avoids
repeating those FFTs during final scoring; its storage fits the existing 64 KiB
metadata reservation, without retaining a spectrum/template for every timing.
The extra prepass is real work: the source bound is 4.622 billion FFT row-stages
for both signs at the 58-timing maximum (3.188 billion for the observed 40-point
case), partly offset by cached final zero-slope work. Complete cost must be
measured rather than inferred from the cache size.

Expanded stages now choose the largest fitting parallel width. On the exact
zero-clock planning shapes, an eight-worker owner uses five ordinary drift
lanes and four retained-line lanes; the 24-worker owner uses eight/eight. Their
original 276,033,982 / 453,996,094-byte complete leases remain unchanged.
A focused native regression receives the formerly missed broad-clock bit one,
then receives bit one at both narrowed worker allocations with operational
evidence equal within 1e−9. These three active-slot checks take 37.51 seconds.
Planning/exception checks pass in 0.244 seconds. This is useful opposite-bit
coverage for the allocation correction; the exact archived bit-zero crop and
complete ordinary continuation still require the separate final proof below.

The final compiled application binds native source `718f91c5…`, application
`32a41213…` and receiver archive `a19bf3c3…`. Independent source review passes;
cached versus recomputed zero-slope scoring differs by exactly zero in the
9.735-second native control. All six appraisal checks pass on this build,
including 177 runner tests. The exact identities and build/check logs are in
`late-harness/timing-lanes-identity.json` under the evidence root above.

The original bit-zero allocation counterexample then passes at **both** worker
counts on the identical archived crop. Operational evidence is exactly
96.21986483596918 in both receipts, versus threshold 15.386207642081944;
the original 276,033,982 / 453,996,094-byte leases and public 14,708-frequency /
four-arrival geometry remain unchanged. Eighteen signed timing atoms are used.
W8 scoring is 11.162 seconds, W24 10.517 seconds; wrappers total 22.071 seconds.
Peak RSS is 297,291,776 / 501,772,288 bytes, below the enforced 6 GiB process
address-space ceiling. This private driver reports owner workers rather than
per-stage widths; the five/four and eight/eight stage-width statements above
come from the separately executed native planning/regression checks.

Both ordinary long-calm continuations then deliver the wanted bit on their
unchanged original active and three-window-prefix samples. Bit-zero presence
is 86.261443 and bit-one presence is 36.216898 against threshold 15.386208;
the formerly missed bit one is now received. Both sequences are
`DATA, QUIET, AMBIGUOUS, QUIET, QUIET`, with physical closure from slots 15/16.
They retain the two interior erasures at slots 13/14, report one observed bit
and `raw_bits_complete=false`, and have zero false DATA. **This is bit delivery
and physical closure, not clean raw-message completion.** All four quiet
apertures contain exactly zero generated signal energy; quiet 14 retains its
original energy and ambiguity. The five-window span is
`[4,145,950,5,873,454)`, 1,727,504 rows. Original active and three-window input
hashes remain exact for both values.

The two receiver intervals are 76.336 / 76.522 seconds; wrappers take
77.030 / 77.234 seconds, totaling 154.436 seconds within the unchanged
300-second aggregate bound. Before execution, a source-derived 90–100-second
nominal estimate motivated raising the proposed per-case cap from 120 to 180
seconds while preserving that aggregate limit and all inputs/criteria. Peak
reservation is 3,868,289,282 / 3,868,256,514 bytes with five owners and W=24,
inside the explicit 6,442,450,944-byte receiver limit. These constructed
five-window intervals are not sustained-backlog capacity measurements.

Final-source preservation also passes the original severe slot-12 bit-one
history and a delayed-dominant bit-zero history with `p=0.01`: **41/41 checks**,
unchanged full/DATA/admitted IQ, correct bits, both genuine QUIET successors,
physical completion and zero false/wrong DATA. Cold complete service increases
from 23.757 to 40.620 seconds for the default-power case and from 26.766 to
41.491 seconds for the delayed-dominant case (71.0% and 55.0% in these finite
comparisons). Wanted DATA already has sufficient evidence before drift; the
two quiet windows now execute the previously unavailable drift family,
including 76/80 slope scores across 109,611 frequency bins. That is meaningful
additional work required to preserve sensitivity to possible weak coherent
signals; a low-energy quiet statistic alone cannot justify skipping it.

Both cases retain W=24 and three owners, with approximately 2.56 GB reserved
inside 6 GiB. They each cover approximately 539.6 seconds of unique selected
input, so these cold intervals remain well below their input duration, but
are not an updated universal CPU ceiling or sustained-capacity qualification.
The delayed comparison's previous automatic cap was 11.085 GB; the new case
enforces 6 GiB. Its earlier cap omission remains recorded, rather than being
retroactively treated as a matched resource-limit test. Only this one of the
four earlier power cases is rerun on final source. See
`late-baseline/final-drift-holdout/combined-final-preservation-summary.json`.

Four final-source cross-path preservation cases also pass on their exact old
IQ and sample counts: fresh HF `A` receives 13 DATA bits plus two QUIET slots;
HF `e` receives three plus two; the matching HF noise control gives 15 QUIET
slots; EME disk `e` receives three plus two. All wanted messages complete
physically without wrong bits, false DATA or erasures. Aggregate execution is
49.866 seconds, W=24 throughout, with charged reservation 0.780–1.340 GB.
These preserve earlier repairs rather than constituting new physical-model
recoveries. Receipts are at repository-root
`local-evidence/current-persistent-20261117/crosspath/summary.json`.

Final named validation passes **Triad 45/45** (190.53 seconds) and **GUI 32/32**
(168.83 seconds). All 671 tracked source/build-scope files match their pre-run
hashes and `git diff --check` passes. The development-profile application,
probe and receiver archive have hashes `517d45fa…`, `6feafb6b…` and `072cd19a…`;
these are distinct binaries from the appraisal-profile `32a41213…` / `a19bf3c3…`
used for the matched campaigns. Exact manifests and logs are retained under
repository-root `local-evidence/current-resilience/final-*-support-rank-drift.*`.

### Matched marginal two-hop preservation on final source

The earlier ordinary −3 dB-Hz, 266.8-second two-hop bit-one recovery had only
0.116 log units of detection margin. Its compact historical receipt did not
establish whether the changed drift selection was used, so it could not be
declared unaffected. One fixed current-native reconstruction is predeclared,
without a restored product option or duration sweep: original seeds 73613/93613,
requested 256 seconds, reference/RRC, 1,550,364 captured samples, W=24, native
720-second / wrapper 750-second ceilings and a 3.5-million-row limit.

The cheap preflight exactly reproduces 266.8 active seconds, nominal first-data
capture time 0.384375 seconds and source energy 512,612.05783348344 before any
field construction. The private fixture uses current canonical profile, source,
channel and `CompleteStreamReceiver` APIs; no channel truth is supplied to
acquisition. Separate source/launcher reviews enforce the one-shot budget,
immutable bindings, retained setup failures and distinct reception-success
checks. An initial missing-brace compile error precedes every native assertion
and field operation; it is retained as setup evidence.

The single execution **passes on final source `718f91c5…`**. Before reception,
it verifies the exact old physical-table SHA-256
`ff2b3f4e40bf6fde12fa89fc733240f08038de0020f497f09adba1652f0008aa`
and whole IQ/validity SHA-256
`408461b1dab0e016aa864f1171a0130f9027dcfd2d775742821b0f3d3bc33a68`.
Post-channel energy is exactly 243,736.20476785346 and received energy/noise is
63.62368448042231. Current ordinary reception gives **DATA1, QUIET, QUIET**,
valid physical raw-bit completion and no erasures. Presence evidence is
15.097510449175903 against threshold 14.9817814951062; its difference from
the historical evidence is only +3.08e−11. This is preservation of the earlier
recovery, not a newly measured physical-channel sensitivity improvement.

Drift actually executes in all three windows: eight stage workers, 9/10/9
timing atoms and 288/320/288 scored slopes. Thus the final check resolves the
missing dispatch evidence rather than merely bypassing the changed model.
The preferred 65,536-point FFT is sufficient here, with 32 retained slopes
from a public 5,343-slope prior. This physical profile has a 5 Hz common-motion
allowance and zero declared relative spread/clock; it is distinct from the
HF Worst / Each 65% statistical and bounded-motion tests above.

Field/preparation takes 507.383 seconds and complete native reception takes
43.356 seconds. The wrapper totals 552.857 seconds within the fixed limit.
Peak receiver reservation is 1,414,671,553 bytes, with W=24 and both native
and process limits of 6 GiB. The physical simulator's preparation cost is not
operating receiver work. Appraisal application/archive/source bindings remain
`32a41213…` / `a19bf3c3…` / `718f91c5…` throughout. This one marginal polarity
does not requalify every old physical capture or establish natural outage rates.

Evidence is in `drift-limit/marginal-two-hop-execution.json` and its native
stdout/bindings. The exactly matched 26,356,188-byte capture is independently
rehashed, then removed after final receipt review: the investigation has no
remaining question or current release action requiring those raw samples.
The small current-API reproduction recipe, hashes and compact results remain
for the current validation claim. `marginal-two-hop-cleanup.json` records the
completed purpose and removal; speculative future reuse does not justify
keeping the capture. The compiled private fixture, copied diagnostic
implementations and obsolete staging trees are also removed. The three resolved
calm/quiet raw-IQ buffers (16,551,744 bytes) are verified and removed as recorded
in `diagnostic-cleanup.json`. Duplicate clean/noise arrays and an extra field
table are not retained. Compatible shared
build caches remain. No RunPod instances were needed or rented for this work.

## Ordinary reception with conservative settings — 17 November 2026

This follow-through starts from `4a50df52` and removes the dedicated repeated-bit
product feature at the user's request. Ordinary raw drafts, matching waveform
settings, global ordinals and two-scored-quiet completion remain. The
[retirement record](#dedicated-repeated-bit-feature-retirement--17-november-2026)
keeps the useful historical grouped recovery distinct from current reception.

The principal new sensitivity repair is full-current-bit timing nomination.
It recovers both polarities of the weak combined GUI channel at **176.8
seconds**, and corrects the previously wrong 696.8-second zero on identical
samples at both +10 and +13 dB-Hz. A fresh weak history also passes. The final
likelihood and admission thresholds are unchanged. The experiments below
separate this acquisition repair, duration/energy recovery and computational
admission improvements; none establishes a natural-channel outage probability.

### Fixed receiver model, stronger actual signals

The native probe now supports a bounded `--signal-gain-db` / `--channel-severity`
contrast for CPU, Helper-Off `admission`, `message` and `plan`. Signal gain is
applied before unchanged independently generated thermal noise; severity scales
only realized spread, common wander and selective-AM depth. It does not change
the source, receiver reference amplitude/noise, search bounds or calendar.
Plan checks verify those invariants and reject unsupported combinations and
out-of-range values. This avoids the confound in earlier simulated C/N0 sweeps
that recalibrated the receiver noise model for each link level.
The stated C/N0 levels are injected reference levels before realized fading,
not measurements of instantaneous received power. For a fixed history the
signal-only 30 dB increment scales the channel output by the same amount while
the noise and receiver model stay fixed.

Predeclared first tier: eight single-aperture outcomes, at most 30 seconds each,
120,000 rows and 1 GiB per call, fixed Target/reference 16 dB-Hz, HF Worst /
Each 65%, 1,500 samples/s, 120 Hz inner rate, 497 Hz IF, keyed waveform and full
automatic `W=24`. Requested eight-second active duration rounds to 10.4 seconds.
Actual signal-only increments are 0/10/20/30 dB. Clean and full-stress bit-one
cases pass at actual 16/46 dB-Hz; half-stress bit zero passes at 26/36; full
stress bit zero passes at 46; the noise-only control remains QUIET. All eight
complete with 17,648 rows and 0.101–0.350 seconds of native slot service.

A separately declared nine-call holdout uses fresh channel/noise seed 74219
and public slot 1 (keyed ordinal 7000038). Both polarities pass clean and full
stress at actual 16/46 dB-Hz; the noise control remains QUIET. These calls
overlapped an appraisal build, so their times are not performance evidence.
These small seeded cohorts establish their outcomes, not a success probability
or universal ordering of channel difficulty.

A further predeclared nine-call duration stratum uses requested 128 seconds,
rounded to **135.2 seconds** for this waveform, with Target/reference 16 held
fixed. Both polarities pass clean/full stress at actual 16/46 dB-Hz; the
severe-configured noise control stays QUIET. Each complete slot consumes
204,848 valid rows, with a 250,218,448-byte DSP quote and `W=24`. Native service
is 1.098–2.352 seconds; all nine calls take 22.651 seconds including rendering.
The stronger wanted signals activate the coherent-line nuisance alternative
and still receive correctly. This specifically checks long-aperture gain
tolerance, with the same narrow stationary frequency/clock prior as the short
probe. It does not cover the cost of a broad oscillator search or the archived
late-calendar weak Each 65% case.
The long seed visits nearly ±30 Hz common/relative motion but draws only
0.39746 ms excess delay; the independent shorter stream below visits 6.21 ms.
The configured 7 ms allowance is not a claim that every run visits its edge.

A separate five-call joint stratum reuses seed 74219 for its already known
6.564946 ms realized delay, with slot 1 and the same 135.2-second waveform.
Both polarities receive at actual 16/46 dB-Hz and noise stays QUIET. The
continuous rendered prefix/aperture visits −29.813 to +29.995 Hz echo motion,
−29.896 to +29.780 Hz common motion and an echo/direct ratio up to 0.996885;
these extrema are not measured separately within each active bit. Each call
renders 429,847 rows and scores 204,851, with a 250,218,547-byte quote and
`W=24`. This reused-history, near-delay-edge check executes the later
multi-path-chirp binary. It is correctness-only because it overlaps field
preparation. Its five-call / 45-second aggregate / 15-second-call limit and
binary identity are in `fixed-link/long-near-delay/` below.
Both 135.2-second probe strata declare zero clock uncertainty, so the later
signed-lag repair is ineligible (`clock_axis_collapsed`). Their original
source-bound outcomes remain applicable to that unchanged path; they are not
reruns or tests of the new timing nomination.

A fresh continuous full-stress seed 74022 receives encrypted `e` (`001`) and
both quiet successors at actual 46 dB-Hz with the same Target/reference 16.
It processes 377,648 rows, with 1.1477314 seconds complete service and
1,069,308,356 bytes peak application reserve. Realized excess delay is
6.208745 ms, relative echo motion −29.807 to +29.859 Hz and common motion
−29.982 to +29.908 Hz. These are sampled GUI dynamics, not calibrated ionospheric
statistics or a processor capacity boundary.

The actual native live `Session` / dated fake-provider / `AudioInput` PCM route
also receives ordinary `e/001` plus two true QUIET windows at both actual
16 and 46 dB-Hz, with Target 16, HF Worst / Each 65% bounds, independent noise
and waveform fixed. It uses a clean realized channel and normal planned
10.4-second bits. Peak absolute PCM is 0.0139185 / 0.0647858; the two calls take
6.793 / 6.812 seconds. Both polarities occur in each message. This is software
PCM execution, not an actual-device or severe-PCM channel measurement. The new
GUI regressions retain the fixed-target and stronger-than-target comparison.

The first probe-only clean setup incorrectly zeroed an inactive common-wander
knot period and was rejected before scoring. The test control was corrected
to leave the required positive cadence intact; no receiver failure or
sensitivity result came from that rejected setup. The PCM pair above used the
pre-cap-repair receiver; the IQ cohort used the subsequent cap-repair binary.
Binary hashes and exact arguments remain in the compact receipts.

Evidence is in ignored `local-evidence/current-resilience/fixed-link/`:
`matrix-summary.json`, `holdout-summary.json`, `stream-severe-30-seed74022.json`,
PCM logs, binary digests and the preserved setup rejection. Retain these small
receipts for the current fixed-reference validation and unresolved weak-duration
comparison; remove superseded logs after the final evidence is distilled. No
raw audio or IQ capture is retained. `long/{manifest,plan,summary}.json` records
the separate duration stratum and its 120-second aggregate / 20-second-call
stop rule. No upper-level rejection was observed within the tested 30 dB span;
these tests do not define hardware input range.

### Ordinary duration and weak-channel follow-through

The final ordinary-receiver outcomes are:

| Capture and actual C/N0 | Before full-bit direct timing nomination | Current result |
| --- | --- | --- |
| 41.6 s, +10 dB-Hz, bit one | AMBIGUOUS | AMBIGUOUS on identical IQ |
| 176.8 s, +10 dB-Hz | Bit one AMBIGUOUS; opposite bit not previously run | Correct DATA for both polarities |
| 696.8 s, +10 dB-Hz, bit zero | Wrong DATA1 | Correct DATA0 on identical IQ |
| 696.8 s, +10 dB-Hz, bit one | Correct DATA1 | Correct DATA1 on identical IQ |
| 696.8 s, +13 dB-Hz, bit zero | Wrong DATA1 with stronger separation | Correct DATA0 on identical IQ |
| Fresh 696.8 s, +10 dB-Hz, bit zero | Independent history, no earlier comparison | Correct DATA0 |

Every row preserves both real QUIET successors; every successful DATA row has
one physical completion. All use HF Worst / Each 65% / Margin 3, matching
ordinary waveform settings and `W=24`. Target +4 / −3 / −9 selects the
41.6 / 176.8 / 696.8-second waveforms. There is no repetition mode or
cross-attempt soft combination. The intermediate opposite-bit control changes
only polarity, not key, profile, channel/noise seeds or ordinal. Its raw input
appropriately differs, so only the existing bit-one input has predecessor-IQ
parity. The fresh case was fixed before inspection at oscillator/path/noise
seeds 62015 / 62035 / 72015; key, clock-edge settings, source geometry and
public bounds stay fixed.

At 176.8 seconds, bit one's correct separation rises from 0.7843 to **38.179**,
with presence 78.260 versus 15.386; zero has separation **57.725** and presence
95.730. Native complete times are 41.436 / 41.645 seconds, versus the original
42.025 seconds for one, with approximately 2.845 / 2.842 GB peak charged
reserve. Direct nomination scores 1,776 of 1,791 available blocks with common
support and chooses the useful −142 cell. At the long matched +10 zero / +10
one / +13 zero points, correct separations are **132.033 / 134.410 / 873.989**.
The fresh zero has separation **178.460**, presence 281.518 and 219.399-second
complete time. The 41.6-second miss improves to presence 6.7511 versus 16.4834
and separation 5.1946 versus 5.2933; it remains a miss, not a qualified weaker
floor. No additional duration/SNR ladder follows the successful intermediate
pair.

The following chronology preserves the original failures and distinguishes
the computational correction from the later timing repair.

The actual Target SNR planner selects 41.6 / 93.6 / 176.8 seconds for targets
+4 / 0 / −3 dB-Hz in the archived GUI HF Worst / Each 65% / Margin 3 geometry,
with actual C/N0 fixed at +10 dB-Hz. The +4 control exactly reproduces the
archived 18-slot input hash `9bd27a5e…03744803`; selected ordinal 12 remains
AMBIGUOUS, with evidence 1.947534 versus threshold 16.483433 and wrong-side bit
separation only 0.698434. Both real quiet successors stay QUIET. This is an
ordinary receiver miss, with no wrong DATA admission, after the repeat feature
has been removed.

The first Target 0 attempt reaches its predeclared 199.5-second native deadline
without committing a slot. All 4,100,378 capture rows were rendered and 645,006
selected receiver rows accepted; three owners share `W=24` and peak application
reserve 1.505 GB. Rendering takes 42.18 seconds and receiver control/wait 156.85
seconds. The 93.6-second DATA aperture itself has 184,068 rows. This is an
execution censor, not evidence that the longer bit is undecodable. The Target
−3 trial was not run at this checkpoint under the stopping rule. A quote of
the censored geometry identifies 7,818 stationary frequency points, 78
workspaces, two rays, four arrivals and a 65,536-point frequency-route FFT.
This motivates evaluating the existing chirp-Z mathematics across the same
complete multi-path grid, not reducing its hypotheses or increasing the test
deadline without diagnosis.

The GUI duration points preserve oscillator/path/noise seeds and render
continuous history. Changing duration moves ordinal 12 to a later physical
time; it is not a same-time isolated energy multiplier. The normalized
simulation reference gain is fixed with actual C/N0, whereas changing the
live target also changes its reference gain. The independent fixed-gain tests
above address that distinct requirement.

The matched current chirp replay preserves the exact +4-target input and
operational scores while reducing sampled complete wall time from 42.545 to
12.03 seconds (3.54×); receiver control/wait changes from 28.25 to 2.43 seconds.
The previously censored Target 0 capture now completes in 31.484 seconds, with
the same complete/selected input hashes and full 7,818-frequency grid. It still
misses the active bit (evidence 2.1475 below 15.791; wrong-side separation
0.418), while both quiet successors remain QUIET. This separates the repaired
compute limitation from sensitivity; 199.5/31.484 is a lower bound against the
unfinished original, not a measured original completion-time speed ratio.

The conditional Target −3 / 176.8-second case completes in 42.025 seconds,
covering all 14,708 frequencies with a 692,416,638-byte DSP quote. Presence is
now strong (40.9917 versus 15.3862), but correct-bit separation is only 0.7843
against the required 5.2933, so ordinary admission correctly remains AMBIGUOUS.
Both quiet successors remain QUIET.

The predeclared actual +10 dB-Hz, true Target −9 / **696.8-second** interval
receives bit one correctly, but its conditional opposite-polarity check
**transmits zero and admits one**. Both quiet successors remain QUIET for each
polarity. The bit-one run has operational evidence 113.7601 against 14.0061
and correct-bit separation 9.928364; the zero run has bit scores
90.03575 / 98.28239 and wrong-bit separation 8.24664. All observations and
noise-model checks are valid. This is a wanted-bit discrimination failure,
not wrong-key interference or successful generic weak-channel recovery.

Both runs use the same key, oscillator/path/noise seeds, public ordinal 12,
source binary and geometry. The complete 24,848,747-row history evolves for
12,942.05 seconds; 4,116,485 rows cover the selected DATA and two quiet slots.
Changing the transmitted bit appropriately changes the source and input
hashes, while quiet energies and channel realization agree. All 57,766
stationary frequencies, four arrivals and 78 workspaces execute with `W=24`,
a 2,684,951,615-byte DSP quote and approximately 10.2 GB peak application
reserve. Native service is 189.295 / 190.406 seconds. These are bounded
single-episode costs, not a sustained capacity qualification.

The nominal independent-gain moving-centroid model favors one for both
waveforms; lower-power alternatives also favor one on the zero capture.
Fixed-affine and short fixed-gain alternatives weakly favor the correct zero
but cannot meet admission. Aggregate receipts do not isolate the contributions
of the two executed signed-lag maps. This warrants a matched strength and
timing/model investigation, not tuning weights or reducing the bit threshold.
The matched discriminator keeps the failed zero waveform, history and
Target −9 fixed and uses actual +13 dB-Hz, under the same finite
26-million-sample / 450-second cap. It again admits the wrong one, with a
larger separation of **12.89069** and presence 366.70647 versus 14.00613.
Both quiet successors remain QUIET, and all completeness/validity checks
pass. Source, propagated energy, path realization and noise innovations are
unchanged; the declared variance falls from 192 to 96.22795. Native service
is 189.933 seconds with approximately 10.294 GB peak reserve. This appraisal
control changes noise and its declared variance; it is separate from the
fixed-reference gain experiment above. Increasing energy alone has not repaired
this failure, so no further signal-level or duration sweep follows automatically.

Source review identifies a concrete proposal/scorer mismatch. Signed-lag
nomination sums block evidence for each fixed frequency and then marginalizes
frequency. Its final independent-gain scorer also permits changing frequency.
The bounded candidate preserves those old nominations and adds at most one
per bit using frequency marginalization within each sampled block before
summing blocks. It reuses the same correlations and public lag support,
deduplicates their union, and must charge all four possible maps without
renormalizing the selected public mass.

On identical +10 zero input this candidate changes wrong DATA1 to AMBIGUOUS:
the correct-bit margin rises from −8.24664 to +0.78260, still below 5.29330.
Both quiet successors remain QUIET. The unchanged fixed nominees are
`[1623,1625]`; moving-frequency nominees add `[-867,163]`. The −867 map
contributes bit evidence 119.47796 / 116.74552 before the outer public prior,
but their combined margin is inadequate. Nomination samples 493 of 7,032
available blocks, scores 489 with common support, and uses the same
4.25425-billion FFT stages. Its measured stage time is 0.89749 seconds and
workspace quote 70.91 MB. Complete native time is 190.545 seconds versus
190.406 for the failed original; this one render-overlapped comparison is not
a sustained performance bound. Full input/selected hashes, grid and `W=24`
are preserved. The conditional bit-one and +13 runs are not executed because
the predeclared correct-zero gate fails.

This is a useful partial diagnosis, not an accepted generic recovery. None of
the nominees is near the evaluator-derived nearest public direct-path lag
−572. A bounded private witness scores that existing public coordinate
(99.7173418 ppm, arrival zero, epoch zero) with the canonical fine likelihood
on identical, all-valid +10 zero IQ. Its bit scores are 247.54480 / 99.41908
before the outer public prior, or **224.28937 / 76.16365** after the full prior,
gain-family, outer-mixture and absent-line weights. Correct-zero separation
is **148.12572**. This is substantial same-model information omitted by
operational timing nomination, not a favorable detector supplied with the
exact distorted waveform. It does use evaluator timing and therefore remains
a private component witness, not an ordinary receiver pass.

The witness regenerates the identical 17,934,673-row continuous prefix through
the selected aperture, scores 1,350,855 rows at `W=24`, and takes 63.968 seconds
including preparation / 1.836 seconds native fine work. It executes no ordinary
receiver or quiet completion. The temporary entry point is removed after the
result; the compact `known-lag-witness.*` receipt and command preserve its
purpose and provenance.

Before increasing sampling work, a second nomination/scorer mismatch needs
testing: the original coarse echo has the configured +90 Hz relative Doppler,
while this successful independent-gain likelihood uses delay-only columns.
The private rank comparison holds the 493 sampled block indices fixed and
compares canonically prepared zero-relative-Doppler and direct-only moving
proposals against the existing controls. Equal per-ray power, original control
winners and input hashes are preserved. Among 3,455 lags, the useful −572 cell
ranks 2,928th with fixed-frequency scoring, 1,787th with the first moving rank,
1,114th with zero-relative echo and 91st with direct-only moving scoring.
The respective gaps to each correct-bit winner are 47.485 / 22.425 / 27.771 /
6.606 nats. None nominates useful timing support. The zero-echo winner is −867;
direct-only is −868. Thus matching the two-ray geometry alone is not the fix.

This diagnostic completes in 74.740 seconds including 2.886 seconds native
preparation/ranking, 6,369,361,920 rank FFT stages and 571,637,760 preparation
stages, within its 120-second / 26-million-row cap. It runs no ordinary decode.
Its temporary appraisal entry point is removed; `gain-rank-witness-summary.json`
retains the failed alternatives and exact provenance.

The single full-aperture direct-only comparison reproduces its 493-block
control and then succeeds at nomination: its correct-bit winner is **−573**,
one projected sample from the qualified −572 cell. The latter improves from
rank 91 to rank 2, with a gap of only 0.84560 nats. Best correct/wrong scores
are 859.74612 / 746.49557. All 7,032 public blocks are visited, of which 6,977
have the required common support for every lag. The full 43-frequency,
3,455-lag dictionary and original per-ray power are retained. No truth enters
selection and no final decode is executed in this diagnostic.

Native preparation/ranking takes **7.361 seconds**, with 30.521 billion
executed rank FFT stages against a 30.7617-billion bound. The complete prefix
run takes 70.391 seconds within the same 120-second bound. Actual private
preparation is 0.245 billion stages; the normal full bank would quote 14.364
billion. These are distinct scopes. A single preparation-work allowance would
fit only 3,283 direct blocks, whose result has not been tested. Retain the
compact `direct-rank-witness-summary.json`; remove its temporary entry point.

The retained production implementation shares old sampled fixed-proposal work
with full-current-aperture direct ranking, retaining at most two old and two
new timing maps and the original public prior. It uses exact workspace/work
arithmetic, `W` and cancellation bounds rather than a new arbitrary sampling
cutoff. It does not rescore previous transmissions. Independent review also
removed a needless single-frequency exclusion and restored the original
endpoint-alignment regression. A plan with zero legacy sampled blocks retains
the full direct path. Focused checks cover complete work/memory quotes, a
one-byte-below rejection, fixed-rank parity and the actual timing endpoints.

The complete long matched zero / one / stronger-zero replays above take
**215.305 / 216.755 / 215.722 seconds**, versus the original
190.406 / 189.295 / 189.933: about **13–15% more** complete wall time, including
overlapped continuous channel rendering. Primary rank work is 7.410–7.881
seconds, but quiet-slot rank phases also execute and can take longer under
the shared scheduler. Use complete elapsed service, not a single rank phase,
for this tradeoff. Peak charged reserves for the three matched and fresh long
rows are 10.152 / 10.052 / 9.977 / 10.049 GB, each within its reported automatic
live limit, with three owners and `W=24`. All 57,766 stationary frequencies
remain. The active rank executes 32.636 billion FFT row-stages within its
32.894-billion bound, scoring 6,977 of 7,032 blocks; quiet rows score 6,976.
The original fixed zero nominees `[1623,1625]` remain unchanged, with direct
nominees `[-573,1308]` at +10 and a useful −572 at +13.

These are source-bound finite CPU measurements on AMD Ryzen 5 PRO 5650U,
12 visible/affinity logical CPUs, 15,677,412 KiB reported system RAM,
x86-64 Linux `7.2.6+deb13-amd64`, GCC 14.2.0 Release builds and native CPU
binary64 ranking. No GPU execution or sustained capacity claim follows.
Plan-only public slots 1 / 12 / 48 retain full direct histories but quote
8.500 / 32.894 / 148.838 billion combined FFT row-stages; older clock-age
service is unmeasured. Growth with clock age remains a capacity question,
rather than a justification to silently omit information used by this repair.

`direct-lag-candidate-summary.json` and `direct-lag-intermediate-summary.json`
bind the final wanted/quiet decisions, frozen arguments, original IQ controls,
new-history identities, work and memory under the evidence directory below.
The compiled source also removes all private known-timing and alternate-rank
entry points. Retain the compact failed and successful comparisons for this
current acquisition claim. Final lifecycle cleanup removes 82 redundant
launch/build/progress outputs (769,487 bytes), retaining 107 compact evidence
files (1,017,240 bytes) for the failure, recovery, source and final-check claims.
Useful build caches and other investigations are unchanged.

### Ordinary weak two-hop duration recovery

The separate physical trial uses the same qualified 2,400-second two-hop
field (`ff2b3f4e…0008aa`), −3 dB-Hz input reference, source/noise seeds 73613 /
93613, first DATA time 0.384375 seconds and native ordinary per-slot receiver.
Each comparison contains one raw bit and two real quiet successors; the field
is continuous and supplies no receiver truth. The shorter and longer waveforms
share the field and start, but increasing duration includes later channel
evolution. These fixed native durations are distinct from the GUI planner
targets above.

| Ordinary DATA duration | Bit zero | Bit one | Real quiet successors |
| --- | --- | --- | --- |
| 139.2 s | QUIET, evidence 5.757925 | QUIET, evidence 1.88807 | Both QUIET for each polarity |
| 266.8 s | Correct DATA0, evidence 15.76870 | Correct DATA1, evidence 15.09751 | Both QUIET for each polarity |

All six short-window scores match the archived first-attempt comparison.
Each longer row has one actual physical completion and no wrong/false DATA.
The longer calendar's ordinary daily-opportunity threshold is 14.98178 versus
15.56818 for the short interval: bit one depends partly on this valid reduced
opportunity count and would not pass the short-calendar threshold. Do not
attribute its recovery solely to integration gain or change thresholds after
seeing the result. Received energy/N0 rises from about 36.13 to 63.63
(+2.46 dB); duration rises 1.917×. Complete longer row times are 35.168 / 33.283
seconds, with 329,254,196-byte DSP quotes versus 188,411,612 bytes for the
short rows. This is a constructed first-attempt time/energy bracket, not a
rerun of all five late alarm attempts or a natural outage probability.

The finite bracket stops after both polarities recover at the first longer
point; the 522-second guard is not executed. Total wall is 628.929 seconds,
including 527.43 seconds of immutable physical preparation that overlapped
correctness checks/compilation. Receiver scoring itself had no competing
timed DSP. The physical run used the cap-repair binary before the multi-path
chirp extension; its single stationary frequency remains on the unchanged
singleton dispatch. Its exact source identity is retained, not relabeled as
the later binary. The temporary physical bracket hook was removed after
recording this result; compact plans, input/field hashes and
`physical-duration-summary.json` remain in the directory below.

Compact inputs, exact arguments, censored and completed receipts are in ignored
`_appraisal/highFidelity-triple_Robust_Modem/local-evidence/ordinary-duration-20261117/`.
They serve the ordinary-duration comparison and subsequent matched sensitivity
and service repairs. The completed comparison retains the original
censor, exact replay arguments and input hashes; redundant intermediate build
logs and launch scripts have been removed. Retire these compact receipts when
their source-specific validation claims are superseded.

### Long-duration frequency-grid admission

A separate source audit reproduced an artificial 65,536-point coarse-grid
limit. With the same 1,500 samples/s, 120 Hz rate, 497 Hz IF, 7 MHz shift,
crystal audio / IC-7100 RF references and HF Worst / Each 65% bounds,
Target −10 selects 863.2 seconds and 48,732 stationary frequency points.
Target −12 selects 1,372.8 seconds / 77,473 points and −13 selects 1,736.8
seconds / 98,003 points. The −13 rejection was executed on the old probe;
−12's rejection follows from the old code and its 77,473-point geometry.
Both exceed that fixed point count. This stationary oscillator grid is distinct from the
declared atmospheric-motion alternatives.

The corrected preflight computes interval/group shapes without allocating a
frequency vector. Exact-index and checked byte arithmetic replace the fixed
point cap; full frequency values and retained peaks are materialized only
after complete workspace admission. Helper narrowing still pays for its full
fallback. The selected plans quote 1,611,647,625 / 2,880,740,014 /
3,203,272,836 bytes respectively with `W=24`; these storage quotes alone
establish neither elapsed service nor reception.

Removing the cap alone would still leave avoidable compute growth. The native
CPU automatic path now admits the existing exact chirp-Z scorer for multiple
rays and arrivals when its complete transform-work estimate has the required
fourfold margin and its entire workspace fits the original frequency-route
lease. Every frequency, arrival, bit, epoch and clock alternative remains.
Explicit arithmetic overrides, GPU eligibility and singleton selection retain
their previous scope. This is a different evaluation of the same correlations,
not an added channel model or dropped search dimension.

[Rabiner, Schafer and Rader (1969), §§II–III](https://web.ece.ucsb.edu/Faculty/Rabiner/ece259/Reprints/015_czt.pdf)
derive the convolution evaluation of uniformly spaced transform points. In
this receiver, the omitted per-arrival output phase is common to both ray
correlations and cancels in their Hermitian quadratic score; that final
statement is a code-specific mathematical deduction. Native regressions compare
full candidate coordinates, ranks and scores with the FP64 frequency route for
ordinary and nearly rank-one pairs, and exercise complete searches above
65,536 points with one and two rays. They also check spacing/endpoints, rejection
one byte below the required workspace and touching Helper intervals.

The censored weak-HF geometry now selects the chirp route on all three windows:
7,818 frequencies, four arrivals, two rays, 78 workspaces and `W=24`, with
131,072-point convolutions and the same complete quota. Its full receiver replay
is recorded separately from these mathematical and component checks. Keep
compact cap/control plans and executed regression receipts in ignored
`local-evidence/current-resilience/coarse-cap/` for this current admission and
service claim, and retire superseded build logs and unused launch scripts once
the final comparisons are distilled.

An executed ordinary encrypted `e/001` check crosses the old limit in every
window: Target −5 dB-Hz, actual reference +15 via a signal-only +20 dB increment,
262.4-second bits, 600 samples/s, Rate 36, IF 100 Hz, 12 MHz shift, Margin 3,
GPSDO-OCXO audio and IC-7100 RF declarations. Each of five windows searches all
76,343 frequencies and 78 workspaces with one arrival. The actual channel is
clean. All three bits are correct, both physical quiet successors are QUIET,
the message completes, and the separate EOF control succeeds. Complete native
replay service is 5.344071803 seconds (including replay reads/verification),
8.473 seconds for the whole command, over 807,436 rows. `W=24`, an explicit
2 GiB limit and four admitted owners produce peak reserve 2,133,081,630 bytes.
The predeclared limit was one message, 1.3 million rows and 60 seconds; no
isolated long-slot fallback was needed. This is actual above-limit complete
service for that geometry, not the broad severe-channel capacity inferred from
the plan-only rows.

### Functional checkpoints and identity

The retirement checkpoint passes triad 42/42 and GUI 30/30. After the chirp and
fixed-reference changes, `./build.sh test triad` passes 42/42 (111.28 seconds)
and `./build.sh test gui` passes 32/32 (168.07 seconds), including both new live
PCM regressions. The later rank-one/rank-two assertion addition was rebuilt and
the native coarse suite passed again. Initial compilation's read-only default
ccache failure occurred before assertions; successful commands use the existing
workspace `build/ccache`. Functional times are not capacity measurements.

These checks precede the subsequent signed-gain nomination candidate. The
chirp checkpoint's application SHA-256 is
`f85937381d1260eccc012f78ebd7317ef9bc2e346dd1c5ca6ccba11ed6482f10`,
native probe
`aa9644b37aea9f1cfc4f85261d739e75b1993db77e4ce64853a20afc052c612b`,
and receiver archive
`cd625188a6e655cc29fc555f09144f04ab9ebd83fba442eda92815f91e9949ad`.
These bind the code checks; later test-harness-only changes retain their own
appraisal identities.

After the full-current-aperture timing repair and removal of all private
diagnostic entry points, the frozen source passes **triad 42/42** in 103.09
seconds and **GUI 32/32** in 168.04 seconds. The two fixed-reference native
PCM regressions pass again (6.70 / 6.76 seconds). The independently rebuilt
appraisal application and three native test executables pass all **six named
appraisal groups** in 48.71 seconds; the separate Python orchestration runner
passes **175/175 assertions** in 1.111 seconds. `git diff --check` passes.
These are functional checks, not additional independent channel trials.

Final development application SHA-256 is
`509bf637fdd82004e4e5a005d69e222cddd14d85e54d6982107338334cc4bf3e`,
native probe
`603b9aad559458f99dbfbd819bb77bd8a7153bdbdc73c58eabdcb28a8e5d6055`,
and receiver archive
`3ca504cbd236ceec65e195e031c456e5ec7cd84a5356f629dba0fd845338dc79`.
The final appraisal application
`b1c384899c77776a6d8cf0161a465fad9fc62fd03ec38ed0ce8c8ea90c7fdbd4`
matches the long and intermediate repaired replays; its application/archive
hashes are verified before and after the final checks. The replay's canonical
native-source identity is
`e5c0f10be4bf903a770547840a1856509397c424c11b91ee03d601549b8652b6`.
Build-profile differences account for separate development and appraisal binary
identities. Logs, exact hash manifests and final source closure remain in
the repository's `local-evidence/current-resilience/final-*-full-direct.*` and
the appraisal evidence directory's
`ordinary-duration-20261117/final-direct-appraisal-*`.
No RunPod device or spending, physical radio measurement,
natural availability probability or release certification is claimed.

## Long VLF gain-calibration discriminator — 17 November 2026

The short independent-gain power mixture repaired one HF live-level mismatch,
but does not execute at the 60-second fine cadence of the long VLF slow-gain
family. To test that exposure, predeclare two native first-slot captures of
the 9489 phase-wander geometry: wanted DATA and a separate genuine noise-only
control. For each, score the same IQ with the normal receiver reference and
with ten times the receiver DATA basis amplitude (20 dB power overestimate).
Neither transmission, noise, calendar, key nor channel changes. Require the
archived wanted input digest `9ad02c3b…fca2b89`, current correct DATA 0 and
actual quiet/noise control before interpreting the matched contrast. Record
validity, noise statistics, both evidence/decisions, applicable line handling,
workspace and elapsed work. Use automatic `W=24`, a 14-million-row limit per
capture and 150 seconds per case / 300 aggregate. A failure or resource
rejection is a result; it does not authorize weakening the memory or wall
limits. This tests one first aperture and a separate absence, not later ordinal
quiet or a full-message/natural-availability claim. One-off appraisal capture
code is retired after disposition, while compact source-bound receipts remain
until their validation purpose is superseded.

The two comparisons finish in 134.817 seconds. The wanted capture matches
the archived input digest, and both scores receive DATA 0: evidence falls from
**57.09468 to 38.11114** under the 20 dB receiver-reference overestimate,
against the same **11.77529** calendar threshold. Correct-bit separation
falls from 68.339 to 54.765. The independent noise-only capture remains QUIET
at **−10.20344 / −11.39123**. All four scores are valid and H0/noise-verified;
the 364.756/1459.025-second slow-gain alternatives execute while the short
power mixture is ineligible. DSP quote stays 952,574,517 bytes at `W=24`.
Wanted elapsed time is 27.670 seconds; noise takes 107.126 seconds, including
51.850 seconds of baseline receiver wait and 50.137 seconds of gain contrast.
The evidence loss is material but does not demonstrate a missed bit at this
operating point or justify a new model by itself. App binary `ca19dba9…1afb8f`
and frozen plan `1e369e30…e68b8` bind this private comparison.

## Weak groundwave energy boundary — 17 November 2026

The current native receiver was tested on the retained weak 1 MHz land path,
using a predeclared two-level, same-seed comparison. The static 100 km
single-ray construction, `e` message, five public slots, seed 9202, original
source and noise process, geometry and 576.4-second DATA duration are fixed.
Only the noise variance changes: 76,436.58 at −16 dB-Hz and 19,200 at
−10 dB-Hz. Two genuine quiet successors are scored. These are two correlated
operating points, not independent reliability trials.

At −16 dB-Hz all three active bits remain QUIET; their evidence is
8.959598, 6.270915 and 3.291646 against the current calendar threshold
14.046622 (`N=126`). The full input hash matches the historical weak capture.
At −10 dB-Hz the correct `0,0,1` bits and both QUIET successors are scored,
and `e` completes correctly. The active evidence is 54.026981, 48.449716 and
41.183807; there are no wrong or false DATA bits in either row. All ten slot
scores are complete and valid with valid noise statistics. Source energy is
3,319,051.822 in both rows; changed additive noise gives different full IQ
hashes. Automatic CPU `W=24` on the Ryzen 5 PRO 5650U uses a maximum DSP quote
of 184,207,780 bytes. Wall times are 46.441/46.042 seconds and receiver wait
37.601/37.002 seconds. This is a selected 6 dB energy recovery bracket, not
an optimal-receiver penalty, a general groundwave sensitivity floor, or a
natural outage distribution. Frozen commands and source-bound receipts are in
appraisal `local-evidence/groundwave-energy-boundary/` until superseded by
equivalent current validation.

## Differential echo-motion investigation — 17 November 2026

This follow-through starts from clean `a27ead7cc275d30e187b49491040d8a568f47c20`.
The retained weak HF Worst / Each 65% / Margin 3 observation still misses at
+10 dB-Hz after the independent-gain power mixture; Helper is Off, so the
subsequent Helper repair cannot alter it. Its weak bit separation also prevents
a presence-threshold adjustment from resolving the demonstrated loss. The
earlier bright timing-cell transfer improves evidence without recovering DATA;
neither that experiment nor the physical quarter-power comparison is repeated.

The source audit identifies another model mismatch worth isolating. The GUI
echo has a changing relative frequency, while the useful nominal/signed-lag
short independent-gain atoms assume zero relative shift. Their common-frequency
tracker does not independently follow the echo. This is a possible cause of
lost information, not proof that the channel is practically decodable or common.

Predeclare one native +10 dB-Hz render using the retained selected-slot manifest,
including the full continuous source/channel history. Require its archived
full-IQ digest `9bd27a5e…`, ordinary slot-12 evidence 1.947534 and complete valid
quiet successors 13–14 before interpreting a contrast. Retain only the active
aperture. Compare the ordinary score with one additional complete canonical
slot score whose private alternative supplies the realized relative echo phase
to the two nominal and at most two signed-lag full-delay atoms. Recompute the
ordinary proposals from the same IQ; do not supply alternative timing cells.
Keep the declared 21 ms delay, common-frequency grid, four power levels,
projection/noise treatment and public prior charges. No stronger-capture timing,
true delay, true gain or threshold change is supplied.
The actual echo delay is 18.10756 ms, leaving 2.89244 ms mismatch to that fixed
21 ms model. The fixture has 25 Hz carrier spacing and 75 Hz unspread occupied
bandwidth: that residual delay spans about 0.217 phase cycles across the latter,
in addition to selective-filter mismatch. Retain these limits when interpreting
a negative result. Apply
the phase to raw echo templates before projection, re-form their statistics,
and use the same stateful physical phase integration as the original channel.
If the alternative crosses admission gates, ordinary conditional-line processing
must run before interpreting its decision.
The extra score repeats complete active-slot work; a changed decision may avoid
the ordinary drift fallback or trigger a line-conditioned score, so identical
executed branch counts are not an acceptance requirement.

This is an **evaluator-informed mechanism test**, not operational recovery:
the receiver does not know that phase. A positive result would motivate a
bounded generic relative-frequency model and independent reception/cost checks;
a negative result would limit this explanation at these coordinates, without
excluding delay or selective-filter mismatch. Allow one render and comparison,
at most 120 seconds, 2.4 million rendered samples, 90,000 retained active rows
and automatic `W=24`. Charge the extra phase storage as well as ordinary DSP,
resident input and stack bounds. No phase, delay, level or seed sweep follows
automatically. Keep frozen commands, source identities and compact results in
appraisal `local-evidence/differential-echo-motion/` while the mechanism remains
under investigation. A fresh `active-slot12.iq` in that directory may retain
only the verified active aperture using the existing native capture format,
with exact write/read-back digest validation. This supports a later separately
justified mechanism comparison without keeping the full continuous render;
retire the private comparison code and capture at disposition.

The audit also identifies a separate unresolved common-motion assumption. The
short tracker gives 0.72 transition mass to staying, 0.12 to each adjacent bin
and 0.04 to a uniform reset. This kernel does not scale with block time or the
declared motion. Here its 43 states are 5 Hz apart and ordinary blocks are
100 ms. The GUI's 1-second quintic knots within ±90 Hz permit instantaneous
rates up to 337.5 Hz/s; those bounds do not imply the particular capture reaches
that maximum. Nonadjacent moves remain supported through the reset, but receive
much less prior mass. Within-block common chirp is another possible mismatch.
The echo-only contrast leaves both effects unchanged, so a failure cannot rule
them out. This source-derived exposure is not a measured sensitivity loss.

The echo-phase invocation completes in **51.392 seconds** (51.204 native),
with exact full-stream and retained-aperture hash parity. Ordinary slots 12–14
retain evidence **1.947534, −5.798362, −5.783023**: one AMBIGUOUS miss and two
valid QUIET successors. The phase-informed active score remains AMBIGUOUS,
with evidence **−1.554711**. Its signed correct-bit margin worsens from
**−0.698434 to −3.143424**; the larger absolute separation favors the wrong bit.
Unchanged affine, short-fixed, ordinary frequency-tracking and drift components,
public masses, candidate counts and 43-state/four-power geometry match exactly.
The changing independent-gain components decline. This is negative evidence for
this phase substitution at the retained fixed-delay/filter model, not a complete
known-channel detector or proof that relative echo motion never matters.

The phase recurrence takes 0.116 seconds for 1,465,348 updates; the extra
complete score takes 11.899 seconds. Its DSP quote is 259,827,608 bytes, with
565,795,456 other reserved bytes and `W=24`. The verified native capture has
83,368 rows / 1,417,256 bytes, IQ digest `abef33aa…c98a0`. Frozen plan
`9198b830…3bdca9`, binary `abd88e94…5bc8`, native source `987667df…e251` and
appraisal source `24430579…0540` bind the result. Compact `comparison.json`
has digest `c9223433…7f355`; the corrected signed-margin interpretation retains
the original raw evidence. Six no-signal preflights precede this single run;
source-review corrections to the harness occur before any signal processing.

The independent common-motion finding justifies a separate bounded test on
that retained active IQ. Predeclare one ordinary baseline and one alternative
complete score, with no channel render, simulator phase or other truth. Reuse
the same independent-gain emissions and public hypotheses. In a second moving
frequency state, retain 0.72 staying and 0.04 uniform reset, but distribute each
0.12 directional mass uniformly over steps 1 through 7; return blocked steps
to self. Seven bins cover the previously stated GUI knot-rate envelope for
this geometry, not a universal natural-motion bound. Mix narrow/broad moving
likelihoods only after the whole aperture, with fixed weights 0.75/0.25, and
preserve the separate fixed-centroid contribution. The original moving
contribution therefore retains three quarters of its mass without repeatedly
paying that factor per block.

Require deterministic transition normalization and old-branch retention checks,
archived ordinary parity and unchanged public configuration before interpreting
the comparison. Limit the pair to 60 seconds, 90,000 input rows, `W=24` and
3 GiB total admitted process/DSP/stack storage. No extra FFT, source template,
frequency cell or timing nomination is added; account for additional state and
transition work. A positive diagnostic needs fresh wanted/absence and affected
service checks before promotion. A failure does not authorize tuning transition
weights or running a grid. Retire the superseded private echo-phase route while
retaining its source identities, original result and this capture for the
new declared comparison.

The broader-transition pair completes in **24.376 seconds**, with no channel
render and exact baseline/input parity. Both observations are complete, valid
and H0-verified. Evidence increases only **1.947534 → 2.359400**, below 21.49338;
the correct-bit signed margin worsens **−0.698434 → −0.736937**. Both remain
AMBIGUOUS. Fixed-varying, ordinary frequency-tracking, drift and public-prior
components match; the outer short-stage aggregate changes through the intended
independent-gain mixture. The candidate executes 597,184 added state cells,
with radius seven and broad weight 0.25. Complete score times are 11.894/12.318
seconds, about 3.6% more in this one ordered pair, not sustained-capacity evidence.
Both peak DSP quotes remain 259,827,608 bytes, with 209,739,872 other reserved
bytes and `W=24`.

The focused fine-session test passes in 4.37 seconds, including normalization
over 1/3/16/43/65 states, every source-state row and repeated unit emissions.
Six no-score preflights pass. Plan `r7-plan.json` has digest `dd7baa63…91dd6`,
binary `46eafdd1…68422`, native source `0950426d…c7619` and appraisal source
`9317e038…dae55`. Compact `r7-comparison.json` has digest `26d2d3b7…ce7aae`.
Neither alternative recovers the bit, so neither is promoted or tuned further.
Both private implementations and their obsolete tests/hooks, plus the verified
active capture, are retired after disposition. Retain these compact negative
findings and source-bound receipts until equivalent evidence supersedes the
model-selection claim. The +10/+16 recovery bracket and natural-availability
limits remain unchanged by these tests.

### Joint echo and selective delay discriminator

The GUI's frequency-selective filter runs after the echo and common Doppler
stages. Its three nominal delay branches on both paths motivate the six-column
span `{0, De, d, 2d, De+d, De+2d}`, where `De` is the echo delay and
`d=1/(3*carrier_spacing)` is the selective filter's first nonzero delay. The
previous echo-phase diagnostic omitted the selective branches and used the
maximum declared echo delay. The negative three-delay comparison on earlier
short bits compared different spans at other timing points and conditions.
Neither test makes a six-column Gaussian fit an operating receiver model.

Predeclare one exact +10 dB-Hz selected-slot replay from the relative-echo
plan, with its retired options replaced by `--hf-gui-joint-delay-shadow`.
Render the entire native 2,041,521-row continuous profile once. Require the
archived full IQ and slot-12 digest, native slot-12 AMBIGUOUS evidence and slots
13–14 QUIET evidence, exact ordinal geometry and complete valid scoring. The
private comparison runs only after closing the ordinary stream receiver, on
the retained 83,368-row DATA aperture. Repeat canonical coarse proposals on
those rows with the ordinary public config and require the same two nominal
arrival endpoints and signed-lag nominations `[48,29]`. Compare two columns
`{0,De}` with the six-column span above. Both arms use the same actual echo
delay of about 18.108 ms. No simulator frequency, phase, gain, bit, or timing
truth participates in these private scores. Delay geometry is evaluator
information and cannot enter admission.

The comparison preserves the 434 short blocks, projected DATA subspaces,
43 common frequency states, fixed and Markov centroid priors, four aperture
power states, and the full public nominal/signed-lag timing weights. It shares
the projected spectra and Gram statistics between the two arms, dividing a
given total gain power equally over that arm's columns. This compares both
spans and the associated redistribution of their gain prior. The positive
identity in the covariance factor retains finite evidence for dependent
columns. Check the zero-observation and coincident-column identities and a
non-collinear complex embedding against the native two-column likelihood before
shadow search. Report full prior-weighted per-bit evidence and signed wanted
separation, shadow-only times and workspace, and underlying template and Gram
work counts. A substantial improvement would warrant a public finite-delay
proposal and matched wanted/absence and cost checks. Neither exact delay
support nor a larger fit from both bit templates establishes public recovery.
A negative result limits this independent-gain basis at the selected timing
and common-frequency approximation. The sampled fractional-delay filter and
within-block phase of each path can still cause mismatch.

Use at most one invocation, 120 seconds, 2.4 million render rows,
90,000 shadow rows, and automatic `W=24`. The shadow admits at most four
concurrent maps, 500 blocks, 512 MiB stage workspace and 3 GiB total including
resident memory and worker stacks. Require no-DSP parser and sample-budget
preflights. There is no automatic additional level, timing, or filter sweep.
Store compact plans and receipts in appraisal `local-evidence/joint-delay-shadow/`.
Retire the temporary hook after its decision, and retain no new IQ file.

The single invocation completes in **50.030 seconds** (49.862 native).
Full capture `9bd27a5e…4803` and selected aperture `abef33aa…c98a0` match
the archive. All three ordinary windows are complete, valid and H0-verified,
with the original AMBIGUOUS, QUIET, QUIET decisions and exact log evidence
**1.947534, −5.798362, −5.783023**. The current calendar threshold is 16.483433.
Deterministic normalization checks and all requested map/block/state parity
checks complete before the private comparison is reported.

The timing-weighted two-column bit scores are **5.30233 / 6.36888**;
six-column scores are **8.99785 / 9.79530**. Equal-bit presence increases
**5.97152 → 9.47404**, while wanted-bit separation decreases
**1.06655 → 0.79745**. Both arms fail the presence and 5.29330-nat separation
requirements even before charging an outer model mixture. The broader
delay span and redistributed gain prior improve both bit fits on this
observation, with less discrimination between them. This rejects promoting
this independent-gain extension on the tested aperture. The exact delays
are still evaluator inputs. This comparison leaves timing, inter-block
frequency tracking, and within-block path and filter variation unresolved.

The shadow uses `W=24`, 434 blocks and 43 frequency states, with a
192,168,020-byte workspace quote. It executes 4,001,664 template rows and
203,875,728 Gram cells, taking 10.208 seconds for independent coarse parity
and 0.306 seconds for additional fine work. These shadow stage timings
do not measure sustained receiver capacity. Plan `8b6e2ec7…4b16d81`, binary
`ca19dba9…1afb8f`, native source `68af5d4d…c63e1`, appraisal source
`e660c165…80511` and compact comparison `eafb1e15…ccf0e5` bind the result.
Six argument/sample preflights and the three relevant native tests pass.
An initial compile failed in the workspace quote before tests and was
corrected by including the declaration header. No IQ file was written.

### Within-block common-frequency curvature discriminator

The fixed-delay comparison leaves a different mechanism open. At the declared
337.5 Hz/s slope envelope, a linear frequency ramp over 100 ms can change
phase by about 2.65 radians relative to a constant-frequency model at the
block edges. This is a worst-case calculation for a centered time reference,
not the actual residual of the selected channel. The one bounded
`--hf-gui-common-curvature-shadow` replay keeps the exact +10 dB-Hz input,
ordinary three scores, and the previously matched nominal/signed-lag maps,
four powers and two/six delay priors. It requires full and selected IQ digest
parity and numerical parity with both previous unwarped wanted scores.

At the input and each nominal retarded selective-filter time, reconstruct the
stateless common Doppler curve from the native channel's physical-time seed.
Integrate frequency within each native block with the trapezoidal rule and
remove the straight line through its two phase endpoints. The residual is
independent of the accumulated phase prefix and omits the true local linear
frequency term. Apply the residual unit phase to the templates before
projection. The delay before common rotation shares the direct branch's phase;
the post-rotation selective delays have separate retarded residuals. This
does not account for the filter's multiple integer samples used to realize
each fractional lag, relative echo phase or changing gain inside a block.
Separate chords also remove potentially different linear frequencies at those
retarded taps. The six-column tracker still fits one shared frequency
hypothesis. The two-column arm shares the same chord in both columns.
All timing and frequency selection remains in the original native path.

Compare two versus six columns and unwarped versus curved phase on the same
wanted aperture. Repeat the private calculations on actual quiet slot 13,
with a complete valid thermal absence window and no source scheduled. The
quiet slot selects its own ordinary maps from its samples. Record both
conditional bit scores, presence and separation, phase RMS/max, and actual
coarse/fine work and memory. Deterministic checks include identity for
constant frequency, agreement with the analytic phase residual of a linear
frequency ramp, unit magnitudes, and the existing independent Gaussian
normalization checks. Correctly separated, above-threshold private evidence
would motivate a finite public model and fresh controlled comparisons. An
improvement below either requirement still diagnoses only that contribution.
The simulator phase residual and true delay geometry never enter admission.

Allow one 120-second invocation and one 2.4-million-row continuous render.
Retain only the two ≤90,000-row private apertures in memory (83,368 and
83,437 actual planned rows), with four maps, ≤500 blocks, ≤512 MiB stage
workspace and ≤3 GiB total private workspace plus resident/stack accounting
per sequential contrast, using automatic `W=24`. No level or seed is changed.
Store compact evidence under appraisal `local-evidence/common-curvature-shadow/`.
Retire both private delay and phase routes after disposition. No IQ file
is written.

The one invocation completes in **62.853 seconds** (62.668 native), with
the full/input IQ and all three ordinary outcomes identical to the prior
joint-delay result. The unwarped two/six aperture score pairs repeat exactly.
The measured wanted residual is **0.465 radians RMS, 2.521 radians peak**, so
this diagnostic actually applies a consequential phase correction. The
two-column equal-bit presence changes **5.97152 → 6.00883** and wanted-bit
separation **1.06655 → 2.52822**. The six-column presence declines
**9.47404 → 8.72210** and its separation **0.79745 → 0.66504**. Every arm
remains below both the current fixed-calendar threshold (16.48343) and the
bit separation requirement (5.29330), before any further mixture charge.
The private known-curvature residual helps two-column discrimination in this
one case without recovering presence. It offers no basis to promote a new
template bank. This negative result has the stated shared-frequency and
nominal-delay limits, and leaves relative path motion unresolved.

The real quiet aperture has digest `287d4a91…9f4238`. Its four private
presence values range from **−6.92211 to −6.91534**, and both original quiet
successors remain QUIET. Each of the two private apertures executes 434
blocks and 43 frequency states, with `W=24`. Coarse times are 11.270 and
10.885 seconds; paired fine times are 0.521 and 0.536 seconds on this host.
Their maximum admitted DSP workspace is 192,593,428 bytes. They render
4,001,664 and 4,004,976 native template rows and execute 407,751,456 and
408,098,208 Gram cells, respectively. These are evaluator stage costs.
Plan `027a663b…15a5fb`, binary `ea26696b…b97790`, native source
`8af97f38…c0aa94`, appraisal source `4e5520e6…a797449`, and comparison
`bdd0990f…ab3f9b6` bind the results. Six no-DSP preflights and the focused
fine-session test pass. Restore the earlier native/appraisal source after
retiring the temporary comparison hooks and shared FFT extraction; the
source-bound compact receipts retain the observed limits.

## Public-calendar admission investigation — 17 November 2026

A separate source audit finds that the fixed operational threshold charges
216,001 opportunities per day, based on a 0.4-second interval. A fixed native
public calendar instead spaces whole-bit supports at least 60 seconds apart;
Helper-enabled supports have a still larger minimum. Every ordinal, including
quiet/missing transmissions, is advanced only once, and codec rearming preserves
that sequence. This is a deterministic scheduling property, not an estimate
from successful bits or executed work.

For valid conditional noise-null e-values, at most `N` predetermined
opportunities give the union bound `P(any false DATA) <= N*exp(-T)`, without
requiring independent windows. A calendar-derived `T=log(N/1e-4)` can preserve
the same stated budget for **one fixed public calendar**, rather than paying
for impossible opportunities. Fixed planned ready timestamps, clock-bound
geometry and sample rounding must determine the count. Multiple calendars,
keys or arbitrary reconfigurations do not thereby acquire one shared
application-wide guarantee. This remains a conditional noise-model bound,
not a measured interference, wrong-key or natural-availability rate.

The conservative 60-second count with a one-sample guard gives `N=1442` and
`T=16.48413`. Reclassifying archived final scores predicts correct DATA for the
Helper bit-zero miss (16.59397, correct separation 25.23040) and the physical
last-bit miss (16.60373, separation 23.94147); their actual quiet controls remain
QUIET. These are **counterfactual classifications**, not new receiver successes.
The +10 combined-channel miss still fails both presence and bit separation.

The implemented native correction carries an explicit locally derived calendar
context. `make_receiver_slot_plan` mints it from `PublicProfile`; standalone
scoring of that plan and continuous reception both use it. A bare plan or an
explicitly cleared context retains the reference gate. The bound uses one
endpoint roundoff allowance across the entire day, rather than subtracting
rounding from every gap. Minimum 60/90/150-second recurrences normally give
`N=1441/961/577`, respectively. An unsupported numerical scale falls back to
the reference. The larger `N=1442` above remains the preliminary conservative
counterfactual, not the final implemented count.

Duration planning and intermediate model/fallback selection retain 21.49338;
earlier weak evidence cannot skip useful later models merely by passing the
calendar gate. Newly eligible windows enter native coherent-line nomination
and weighted acceptance at their scoped threshold before final classification.
Previously eligible windows preserve their original line/fallback path. Bit
separation, input/H0 validity and physical quiet completion remain required;
retained unexplained lines remain AMBIGUOUS, not falsely quiet. Wrong-key
filtering keeps its original reference in the competing-score margin.

The focused native calendar test passes, together with stream and Helper-burst
tests. It covers Helper/Off recurrences, fixed/random waits, the random sequence
boundary, large ordinals, clock-expanded ready times, rounding and unsupported
numeric fallback. Native controls preserve thermal quiet, invalid samples,
bit separation, pure-line rejection and DATA with a line. A real on-grid line
between the two thresholds, combined with a test-only raw DATA supplement,
executes the newly eligible native nuisance path and is vetoed; this verifies
the execution path rather than merely changing a saved decision.

Predeclare a single four-score Helper comparison: generate the retained stale
6 dB-Hz bit-zero aperture and its actual zero-DATA Helper/noise control once
each, then score identical IQ with cleared and factory-bound contexts. Preserve
the quiet hash, legacy evidence/decisions, full aperture and `W=24`; limit the
whole process to 60 seconds. A separately frozen physical comparison regenerates
the original +3 dB-Hz two-hop field/history once, verifies the archived table
and 4,300,572-row IQ hashes, and retains only the 844,697-row envelope of slots
12–14. Score those three windows with unbound context first, require archived
evidence and QUIET decisions, then score all three with their factory context.
Use the original 139.2-second bits, epoch/key/ordinals, seeds 73613/93613, noise
variance and 5-Hz allowance; cap preparation, render and six scores at 720
seconds. This tests late-bit admission and two physical quiet windows, not a
full message, five-attempt episode or completion horizon. Neither comparison
supplies receiver channel truth. Frozen plans, compact receipts and source
identities belong in appraisal `local-evidence/calendar-admission/`; remove
the one-off physical comparison hook after disposition and retain no new IQ
file. No endpoint repair is claimed until those native paths run.

The Helper comparison completes in **26.019 seconds**, with four complete,
all-valid, H0- and noise-statistics-verified scores on 314,643-row apertures.
The old-context bit-zero miss remains QUIET at **16.59397259 / 21.49337869**;
the factory context receives **DATA 0** with identical evidence and
**25.23039664** correct-bit separation, against **16.07831478** (`N=961`).
The actual Helper-plus-noise zero-DATA observation stays QUIET in both contexts,
with evidence **−7.73723225**. Wanted IQ digest `cbfa0093…cff67ba` and absence
`4af2f6ae…a25ac6fb8` match within each pair; the latter also matches its archive.
No channel truth or newly narrowed search is supplied. The preceding Helper
repair's full-frequency-prior skip remains active and charges zero Helper-fit
work.

Wanted service is **5.249 / 5.463 seconds** (unbound/bound), absence service
**5.587 / 5.741 seconds**. These single ordered pairs include the applicable
native line search; no line is retained. Workspace remains **359,228,115 bytes**
and `W=24`, within the original 768 MiB lease cap. Both contexts retain equal
coarse/fine, varying and drift geometry/work counters and likelihoods.
This is a matched weak-bit/absence repair, not a sustained-capacity measurement
or a repeated-alarm availability estimate. Helper plan digest `6833ea6d…cd301e3`,
fixture source `ec0f81d4…708861`, binary `50977dff…11635b` and native archive
`d09184d3…d59c8` bind the result. An initial thin-fixture link omitted the
existing static liblzma; that setup failure is retained, corrected before the
single signal-processing run, and is not a failed reception trial.

The physical comparison also passes, in **539.36 seconds** externally
(539.268 native). The field table matches `ff2b3f4e…f0008aa`; the complete
4,300,572-row stream matches `fed418cd…d3963`. All three unbound scores match
the archived evidence and QUIET decisions before any bound score runs. All
six observations are complete, all-valid, H0- and noise-statistics-verified.

| Same physical +3 dB-Hz aperture | Unbound reference | Calendar-bound (`N=577`) |
| --- | --- | --- |
| Slot 12, wanted bit 1 | QUIET, missed bit | **DATA 1** |
| Slot 13, actual absence | QUIET | QUIET |
| Slot 14, actual absence | QUIET | QUIET |

The wanted evidence remains **16.603733824**, with correct-bit separation
**23.941471890**. Its threshold changes **21.493378688 → 15.568182638**.
Quiet evidence remains **−6.858888619 / −6.775606922**. Native line selection
runs where newly eligible; none is retained. No received signal energy,
waveform, public hypothesis, channel model or bit-separation requirement changes.
This is a demonstrated repair of the selected weak physical endpoint, without
the previously needed 1 dB increase. It does not re-run the full `E` message,
five attempts or the earlier −3 dB-Hz failures.

The field preparation takes 485.700 seconds, rendering 4.937 and all six
receiver scores 48.498. Ordered unbound/bound service is **9.532/9.453 seconds**
for DATA and **6.989/7.013**, **7.767/7.739** for the quiet windows. These are
single-pair observations, not a speedup or capacity qualification. Maximum DSP
workspace remains **188,928,842 bytes**, with **249,952,656 bytes** reserved
outside it and automatic `W=24` on the Ryzen 5 PRO 5650U (`H=12`). Field and
signal preparation are test costs, not deployed receiver work. Neither local
comparison uses RunPod or GPU execution. The physical receipt reports native
automatic CPU arithmetic with Q15 spectral products and FP32 coarse FFTs;
this does not label all fine/state arithmetic as low precision.

Physical plan `2f51968c…29fe24`, appraisal binary `a6a622dd…4b0c`, native source
`9282f778…79825081`, appraisal source `46895090…7aab021` and app-main source
`c6f71e52…14b592` bind the six-score result. The compact/raw receipts preserve
the original command and conditional scope. The new in-memory tail is released
at process exit; no IQ file is retained. The temporary comparison hook is
retired after this result, retaining ordinary calendar count/scope reporting.
The reusable Helper fixture and compact receipts serve reproduction of these
specific weak endpoints; retire redundant logs/binaries when that validation
purpose is superseded. The failed echo/R7 alternatives remain negative findings,
and weak Each 65% reception and hours- or days-long natural availability remain
unqualified.

Final checks pass **41/41 triad** and **28/28 GUI** tests. The first affected
appraisal group passes five of six checks; `high_fidelity_integration` is
censored by its `hf-transition` child's 25-second limit. Preserve that original
failure: the complete 1,658,432-row input is already accepted, but only slots
0–11 commit, the last at 24.887 seconds. All those scores lie outside the newly
enabled presence band, so they run no added calendar line/model work.

One separately declared exact-case replay uses the same unmodified binary,
40 seconds natively and 45 externally. It completes all 15 slots in **31.902
seconds** (31.914 external), with exact input hash `cab687…` and identical
non-timing results for the first twelve. Slot 12 is DATA 1 at evidence 1966.919;
slots 13–14 are QUIET at −5.0434/−5.27395 and finish the physical episode.
No slot enters the newly enabled band or retains a line; stage selection is
unchanged by this repair. The run retains eight resumed DATA slots, one
reacquisition, zero wrong/false/premature decisions and one physical completion.
The known interior fade still prevents correct text completion, as in the
original fixture contract. This resolves a test deadline, not a new successful
message or receiver-capacity claim. That integration subprocess's limit
changes to 40 seconds with a 45-second launcher timeout; all assertions,
production limits, effects and workers remain intact. Raw diagnostic stdout
digest is `f7d1133c…8b8e71`, with the original failure and new plan/results in
`local-evidence/calendar-admission/final-validation/`.

The isolated integration rerun clears that first transition, then reaches the
adjacent `hf-mode-transition` fixture's unchanged 25-second limit. It also
commits only twelve slots. Retain this second censor separately. The base
`a27ead7c` test already requires one premature completion, two eventual physical
completions and no correct full text: its constructed mode loss leaves a mode
40 dB weaker while the original gates are closed. That deliberate fault
control is not a newly demonstrated atmospheric-model failure.

One exact-case 45-second-native/50-second-external discriminator completes in
**32.961 / 32.974 seconds**, preserving the full 1,658,439-row IQ digest
`dbe5b7cd…a24` and all non-timing fields of the censored prefix. All fifteen
scores are complete, valid and outside the newly enabled presence band, with
no retained line. The final DATA 1 has evidence 993.3232; the last quiet windows
have −5.4221/−5.2946 and run the existing coherent/drift work. Two physical
completions, one known premature completion, seven resumed DATA slots and zero
wrong/false bits exactly satisfy the original modal-fade behavior; correct
full-text completion remains false. Raw stdout digest is `c3e66b2b…62d025`.
This second integration child alone receives the same measured 40/45-second
headroom; later cases are planning/guard checks. Both original 25-second failures
remain recorded. Neither correction changes a production resource limit or
supplies capacity qualification.

The final isolated integration check passes **1/1 in 111.56 seconds**. Together
with the five unchanged appraisal passes, all six selected appraisal checks
are covered; triad 41/41 and GUI 28/28 remain applicable. Neither whole group
is repeated after the test-only deadline corrections. All original failures
remain in separate logs. The final summary has digest `721f99f8…08eb848`.
The adopted app is `2ad4a9a3…a5ffed`, probe `fab0e593…96641e`, native source
`c67cdb55…22ab3d` and appraisal source `e3dfb587…e4467`. The native receiver
archive remains byte-identical to `d09184d3…d59c8` used in both scientific
comparisons; cleanup changes only temporary appraisal plumbing, receipt scope
wording and test limits. The one-off Helper binary is removed after checking
its exact digest. All 557 local documentation targets resolve and whitespace
checks pass. These checks are not release certification or broader hardware,
capacity or natural-availability qualification.

## Weak Helper follow-through — 17 November 2026

This investigation starts from `6571cadadf504974a55f6d58cb44c16fcd674f3f`.
The source audit finds no persistent rejected Helper state and no Helper-only
veto on DATA: absent/weak acquisition restores the full constant-frequency
search. Accepted Helper intervals narrow the coarse constant-frequency bank,
while varying and drift alternatives retain wider public support. The retained
9 dB-Hz common-frequency example succeeds with stale assistance, but loses
about 6.18 nats and takes 5.36 rather than 1.15 seconds. That leaves a useful
weak-signal discriminator, rather than evidence of universal fallback sensitivity.

Predeclare one matched **6 dB-Hz DATA / unchanged Helper** pair using the
retained native fixture. Keep its 6 kHz sample rate, 10.4-second bit one,
public epoch 1793850000, ordinal 15, thermal seed 37, 0.18-second actual arrival,
3 Hz atmospheric allowance, 7 ms declared delay and original key. Scale only
DATA amplitude to `10^(-6/20)` against the existing 12 dB-Hz noise buffer.
Compare aligned +1.10 Hz against the continuous common-frequency transition
from −0.95 to +1.10 Hz during source seconds 58.6–59.7, before DATA begins.
Require exact post-transition IQ equality, complete valid observations and
verified zero-hypothesis statistics. Record actual Helper intervals, bit/null
evidence, branch work and elapsed service. Preserve automatic `W=24`, at most
360,000 aperture samples / 1.1 million channel-prefix samples, the existing
768 MiB workspace limit, and 45 seconds for the two ordered scores.

Aligned DATA with a transition miss demonstrates an assistance-related loss;
two passes or two misses do not demonstrate that gap at this point. No extra
level, seed or duration follows automatically. Fixture setup/censoring remains
distinct from reception failure. The thin fixture and compact frozen evidence
belong in `local-evidence/weak-helper-followthrough/`; retain the original
Helper receipts. Any successful generic repair needs its own bounded controls
and affected native checks before promotion.

The single pair demonstrates an actual assistance-related miss. Aligned
Helper gives interval `[1.00778,1.18667]` Hz, containing the +1.10 Hz DATA
frequency, and receives correct DATA one at log evidence **24.67816**.
The transition yields the earlier interval `[-1.02,-0.868889]` Hz and scores
the same DATA as **QUIET**, at **17.36909**, below 21.49338. Both observations
are complete, valid and zero-hypothesis verified. The entire post-transition
suffix from sample 370200 matches SHA-256
`3eb62f27729669e9abca71508f6fb0e0b2bdbe85d262318d5cd4500b32221de1`;
DATA and noise vectors also match. The correct-bit separation remains ample
in both arms; missing presence evidence is the immediate failure.

Aligned and stale arms take 1.097 and 4.721 seconds respectively. Both retain
`W=24`, the 359,228,115-byte quote, selected fine prior mass
`1.27619672955e-6` and 15,102,864 fine basis rows. The aligned affine wanted
score is 41.02237; stale assistance reduces it to 1.73851. The wider short,
coherent-gain and drift alternatives execute but do not recover the bit.
The full invocation takes 8.192 seconds, with no extra level or seed. Summary
`local-evidence/weak-helper-followthrough/summary.json` (`0be7e2ac…`) binds
fixture `6121468d…`, native source `7ec93490…` and receiver archive `a725a26b…`.
Build-identity and ccache setup corrections occurred before DSP; their original
logs are retained. This is a constructed in-bound frequency history, not a
measurement of its natural incidence or persistent alarm outage duration.

The first repair candidate changes the transferred interval, not the pilot
fit or DATA threshold. If the declared common atmospheric excursion is
`[-A,A]` at both Helper and DATA times, its change can reach `2*A`. Expand
accepted Helper proposals by that amount after pilot consistency checks,
clip to the existing coherent DATA prior, and retain the existing broad/empty
interval fallback. No new historical search, second coarse pass or finer
frequency grid is introduced. This may forgo Helper's speedup when the allowed
motion is large relative to that prior, so measure both matched arms and a
quiet control before accepting it. Reusing existing zero-slope drift peaks for
a few extra coherent candidates is a conditional lower-cost alternative;
it does not itself guarantee timing support or apply where drift is unavailable.
First require focused transfer-bound checks, then repeat the frozen 6 dB-Hz
pair once on unchanged captures within the same 45-second cap. Further tests
need separate bounded declarations.
For an enabled changing-channel profile, `A` is the maximum of its common
Doppler and atmospheric excursion declarations, rather than their sum: the
GUI's common allowance already includes atmospheric motion and explicit Doppler.
The two existing fields are not independent powers or additional bandwidths.
Native physical motion is sampled in receiver time, so the nominal-frequency
guard also divides by `1 - endpoint_clock_bound_ppm*1e-6`, using the slowest
allowed receiver clock. Clamping the one-sided excursion to the existing
frequency bound before doubling avoids unnecessary range growth; the resulting
intervals still clip to that unchanged public prior. Focused tests include
nonzero endpoint-clock conversion and unsaturated intervals.

The candidate passes the exact pair: both aligned and stale captures now
receive correct DATA one at log evidence **25.11317**, correct-bit separation
34.58601 and the unchanged 21.49338 threshold. Each full capture matches its
own baseline hash; both retain the identical DATA/noise suffix. Broad transfer
uses the existing full-search fallback, with 136 coarse frequencies instead
of 10 aligned / 8 stale. Stale affine wanted evidence rises from 1.73851 to
41.45739, making the later medium/drift fallback unnecessary. All observations
are complete, valid and zero-hypothesis verified, with `W=24` and the same
359,228,115-byte quote.

Matched core slot time changes **1.097 → 1.162 seconds** aligned (about 6%
more) and **4.721 → 1.155 seconds** stale (about 76% less). Total ordered
acquisition/slot scoring changes 6.110 → 2.629 seconds; the candidate process
takes 4.269 seconds. These are one paired local observation, not sustained
capacity measurements. Candidate fixture binary `c0c98342…` links receiver
archive `620d424f…`; the unchanged fixture source is `7436e971…`.

After that positive gate, predeclare two further candidate-only observations
within one 45-second invocation and the same sample/workspace limits: the
stale-transition bit zero at the same 6 dB-Hz DATA level, then actual Helper
plus the unchanged thermal noise with zero transmitted DATA. Preserve the
same channel history, key, ordinal and public bounds; the opposite polarity
is a source-waveform holdout, not an independent natural history. Verify the
quiet capture against its retained exact Helper/noise digest and require valid
physical absence scoring. Record any opposite-bit miss without an automatic
level/seed extension. Then run the applicable native triad and GUI groups on
the finalized source.

Both controls complete within the cap, but the opposite bit remains a miss:
bit zero is QUIET at log evidence **16.59397**, despite correct-bit separation
25.23040. Its affine wanted evidence is 32.93060. Actual Helper-plus-noise is
correctly QUIET at −7.73723, with full IQ matching the historical
`4af2f6ae…ac6fb8` absence digest. Both are complete, valid, zero-hypothesis and
noise-statistics verified, with the same `W=24` and workspace. This does not
claim both polarities receive at 6 dB-Hz. The summary is
`candidate-controls/summary.json` (`7ad258b1…`) under the Helper evidence
directory. Control binary `44258224…` executes exactly one two-score invocation.

The bit-zero slot takes 5.786 seconds and absence takes 15.376 seconds, despite
equal reported coarse/fine/varying counts. The latter is not a conditional-line
rescore; its cause cannot be attributed from those counters alone. Predeclare
one cost comparison using the preserved current pre-repair binary `6121468d…`
and its existing `lower-data-common-frequency-controls` command, capped at
45 seconds with unchanged sample/workspace bounds. Its two existing observations
are 9 dB-Hz bit zero and the exact same Helper/noise absence; the first is an
ancillary old control, not a new sensitivity point. Compare the latter's exact
capture and complete service with the candidate. No automatic repetitions
follow, and historical pre-power-mixture timing is not a matched baseline.

The current pre-repair binary scores the exact absence input in 5.343 seconds
(plus 0.154 seconds of separate Helper measurement), against the candidate's
15.376 seconds. The ancillary 9 dB-Hz bit-zero control remains correct DATA
zero. The single command completes in 15.442 seconds; its summary is
`pre-helper-baseline/summary.json` (`32e7bbb9…`). This preserves the observed
2.88-fold quiet timing difference rather than dismissing it as an old-version
comparison.

However, the candidate quiet run's standalone Helper measurement also takes
1.022 seconds, against 0.146 for the preceding candidate bit-zero observation
and 0.154 for baseline absence. Its pilot-fit values are unchanged. Wider DATA
frequency search cannot explain that earlier slowdown. Predeclare one further
candidate-only quiet timing observation, capped at 45 seconds, on the exact
same absence input. Print existing native phase times and drift-work counters,
plus process CPU/fault/context-switch deltas around standalone Helper and full
slot scoring, with passive host/cgroup CPU-pressure snapshots. This adds no
receiver instrumentation or new communication history. Overlapping phase times
are not additive. A return to ordinary timing would support a transient
explanation; a repeated slowdown requires phase-level diagnosis. No capacity
claim follows from either result, and no automatic repetition is authorized.

That one timing observation completes in 7.952 seconds and returns the same
valid QUIET decision and exact input. Core receiver time is **5.971 seconds**,
with 43.684 user / 0.727 system CPU seconds and zero major faults; standalone
Helper time returns to **0.148 seconds**. Existing fields report 0.240 seconds
coarse, 5.729 fine and 4.393 drift, with their nested/overlapping scope retained.
No cgroup throttling is recorded; cgroup totals can include other activity and
are not receiver CPU counters. Summary `quiet-timing/summary.json` (`3b11eff2…`)
binds the reviewed instrumentation and unchanged guarded receiver. The earlier
15.376-second outlier is not reproduced; the observations support variability,
not an established threefold algorithmic cost or sustained-capacity pass.

The source audit separately proves avoidable work: when the effective common
excursion covers the coherent frequency bound, every valid transferred Helper
interval spans that full prior, while every failed fit already falls back to
it. A shared planning/execution predicate can therefore omit acquisition in
the production driver and quote zero Helper scratch in that case, preserving
standalone Helper diagnostics, full input validation and all DATA hypotheses.
This is a separate optimization, not an explanation for the timing outlier.
Its candidate must preserve exact matched DATA/quiet scores and public work,
resource admission and the full worker budget before final acceptance.

The production skip passes focused Helper/receiver checks and a four-window
matched replay: aligned and stale bit one remain DATA at 25.11317; opposite
bit zero remains the recorded QUIET miss at 16.59397; absence remains valid
QUIET at −7.73723. Every shared evidence/prior/work field and input/noise hash
matches the transfer-guard-only version. All windows keep `W=24` and the
359,228,115-byte peak quote. The explicit production status is
`common_motion_requires_full_data_frequency_prior`; standalone Helper
measurement remains available. Bit-one core times are 0.995 / 1.008 seconds,
with 2.294 seconds for both standalone acquisitions plus native scores;
the two control scores/acquisitions total 10.435 seconds. These are scoped
measurements, not a complete capacity qualification.

The final matched parity receipt is `final-skip-gate/parity.json`
(`836aaac8…`); its file inventory excludes its own digest. Raw results are
unchanged by that metadata correction. The native source then passes
**40/40 triad** checks in 108.48 seconds and **28/28 GUI** checks in 154.73
seconds. A focused-test compile initially used an unqualified internal type;
the test-only namespace correction precedes the passing runs and is retained
in the build log.

A measurement audit also found that the staged probe's `acquire()` still
charged standalone Helper when production omitted it. It now uses the same
shared predicate and retains a named Helper phase with zero time/work and the
explicit skip reason. The GPU-to-CPU forecast accepts that deterministic zero
only with the exact reason, matching geometry and every required zero counter;
other zero-time stages, missing fields and stale acquisition calibration remain
rejected. Direct standalone Helper diagnostics remain available.

The actual probe CLI check passes **1/1** in 23.06 seconds, covering saturated
motion with the full 91-center frequency grid, stationary acquisition and
unsaturated motion. Each slot deliberately executes one coarse tile and remains
incomplete; this is bounded interface validation, not capacity evidence. The
test initially looked for a plan-only field in a slot receipt; a zero-DSP plan
of identical options corrects that test lookup, and the original failure log is
retained. Capacity/workload/forecast contracts pass **39/39**. These changes do
not remove the staged changing-channel path's other incomplete-work gates or
refresh any manual capacity calibration.

The rebuilt appraisal application passes all **six** scoped channel, HF GUI
profile, correlated-gain, normalized-null, integration and runner checks in
50.10 seconds. Production DSP is unchanged after the 40/40 triad and 28/28 GUI
runs. The final receiver archive is `79bb9417…e8f2`, probe binary
`5850ec0c…9aaf7`, probe source manifest `4cbd6c6d…54c6` and appraisal binary
`a5444c63…4035`. Full identities and log digests are recorded in
`_appraisal/highFidelity-triple_Robust_Modem/local-evidence/weak-combined-modulation/final-validation/final-identities-and-checks.json`
(`52daf24e…`). The appraisal rebuild retains an existing-source compiler warning
about the optional CPU timing value in `ServiceTiming::add`; no appraisal source
change or failed check is hidden by that passing result.

Keep the thin native fixture `local-evidence/current-helper/discriminator.cpp`
(`ccc3e637…ed3f`) and the compact frozen commands, hashes, raw decisions and
setup/failure/check logs for the current matched Helper regression claims.
The ten obsolete task-owned fixture executables/objects are removed after
verification, with identities retained in `cleanup.json`; compatible build
caches remain. Retire the fixture and redundant receipts once
an equivalent maintained replay supersedes these claims. No cloud resource or
new complete-receiver capacity calibration was used. Opposite-bit sensitivity,
weaker physical failures and hours- or days-long natural availability remain
outside the successful qualification.

## Weak physical gain-level discriminator — 17 November 2026

The remaining zero-delay, one-template physical two-hop rows cannot execute
the recently added independent short-gain power mixture. An earlier 8/32-second
non-additive coherence extension already failed to recover the +3 dB-Hz last
bit; the additive .5/2-second alternative improves it but remains below
threshold. Repeating that coherence sweep is not a new discriminator. A future
matched physical preparation must test a distinct supported mechanism, rather
than merely relabel an old miss with a new source identity.

Predeclare one native physical preparation for the original +3 dB-Hz `E`
history: `hf-vector-two-hop`, requested 128-second / actual 139.2-second DATA,
reference/RRC, oscillator seed 73613, noise seed 93613, Helper/FEC Off and
unchanged zero declared spread/clock with the 5 Hz atmospheric allowance.
The complete 4,300,572-row render must reproduce the archived table `ff2b3…`
and full IQ `fed418cd…`. Retain only the contiguous 844,697-row tail spanning
DATA slot 12 and physical quiet slots 13–14. Require ordinary evidence parity
with 16.603733824, −6.858888619 and −6.775606922 before interpreting the contrast.

For each unchanged aperture and its ordinary fine coordinates, use existing
native `score_fine` with half the DATA basis amplitude, equivalent to one fixed
quarter-power gain covariance. Combine it using the existing supplement
interface with fixed mass 1/8, retaining 7/8 for the ordinary receiver and all
public coordinate/noise/line accounting. This is a private causal diagnostic,
not a promoted receiver model or channel-truth input. One preparation and one
render are allowed, at most 720 seconds aggregate, 4.35 million full samples,
0.9 million retained tail rows, automatic `W=24` and the exact ordinary workspace
admission. Plan/sample preflights precede field computation. Keep the bounded
capture until disposition so a positive result can be evaluated without another
field solve. No variance, coherence, energy or seed sweep follows automatically.

The recorded-voltage Gannon variance-only failures in
[the appraisal](../_appraisal/highFidelity-triple_Robust_Modem/hf-recorded-diagnosis.md#frozen-selected-cell-variance-check)
remain contrary evidence: their unpartitioned and independently partitioned
banks do not qualify this different correlated stationary-plus-OU physical
aperture, and this experiment cannot overturn those failures by implication.

The single physical invocation completes in **568.234 seconds**: 491.151 for
field preparation, 5.735 for rendering, and 71.195 for receiver/verification
work. Full table/IQ hashes match exactly, as do all three archived ordinary
log-evidence values. The verified retained suffix contains 844,697 samples,
14,359,849 bytes, digest `afe6a7f4…517184`. All six ordinary/alternative
observations are complete, valid, zero-hypothesis and noise-statistics verified,
with `W=24` and no retained line.

| Public slot | Ordinary log evidence | Quarter-power mixture | Decision in both |
| --- | ---: | ---: | --- |
| 12, expected DATA 1 | 16.603734 | 20.824883 | QUIET, missed bit |
| 13, physical absence | −6.858889 | −6.712395 | QUIET |
| 14, physical absence | −6.775607 | −6.866212 | QUIET |

The alternative improves presence evidence by **4.22115 nats**, but remains
0.66850 below the unchanged 21.49338 threshold. Its raw affine wanted score
improves 24.77449 → 29.67585 with identical public selected mass 0.0045248869.
This is useful mechanistic evidence, not a demonstrated communication repair;
changing the mixture weight from this near miss alone would not provide fresh
validation. Actual signal energy is zero in both quiet successors. The active
aperture has about 107.03 clean energy/noise units, which does not remove
unknown-channel or public-search costs.

The compact receipt is
`weak-physical-gain/physical-quarter-summary.json` (`5bd258fa…`), and raw
stdout has digest `c9719eb1…dc06`. The generic top-level
`outcomes.quiet_active_slots=0` is an unused stream aggregate in this
`component_only` command; direct ordinary and alternative slot decisions above
are authoritative and show the active miss. Preserve this reporting correction
and the original bytes; no rerun is needed for that metadata defect. The
existing +4 dB-Hz physical recovery remains a separate earlier result. No
additional covariance, coherence or energy trial has been run.
That separately validated +4 recovery uses the same `E` physical history:
the selected +3 miss lies within the existing **1 dB energy-recovery bracket**,
not a demonstrated high-energy blind spot. It is not an outage-duration claim.
Keep the positive model-fit finding and reconsider a reusable gain-level
alternative when independent evidence shows an incremental reception benefit
and its complete-service cost is qualified. Do not promote a heavier weight
from this near miss alone. The one-off comparison code and 14,359,849-byte
bounded capture are retired after disposition; retain the frozen commands,
hashes, valid failures and
compact receipts until equivalent source-bound evidence supersedes this claim.

### Fixed quarter-power comparison on the weaker repeated alarms

The archived final −3 dB-Hz matrix has five misses for each bit value. All
ten active evidence scores remain below the current 150-second calendar
threshold of 15.56818. Four bit-zero attempts and the first four bit-one
attempts favor the correct bit with sufficient separation. One bit-zero
attempt has insufficient separation, and the final bit-one attempt slightly
favors the wrong bit. The calendar change alone therefore does not predict
recovery. The zero-delay receiver still uses the one-spatial-template
stationary-plus-OU affine path, outside the fast independent-gain power bank.
This makes the previous fixed quarter-power alternative an independently
testable model for this lower-signal pair, with its original weight and no
adjustment based on the results of either polarity.

Predeclare exactly two −3 dB-Hz, 139.2-second DATA histories, with five
separate attempts each in slots 0, 3, 6, 9 and 12. Ten actual quiet slots
remain in each 15-slot native receive episode. Keep the existing two-hop
profile, continuous channel history within each episode, unchanged oscillator
and noise seeds 73613/93613, zero declared spread/clock, 5 Hz atmospheric
allowance, Helper and FEC Off, advancing ordinals, and configured key. Share
only one immutable qualified field preparation across the two independent
polarity renders. Before their matched replays, verify 4,300,572 rendered rows
per polarity against archived full IQ hashes `3eb49bf2…bc3c1b3` and
`03bb384b…058d333`, and table `ff2b3f4e…0008aa`. Both arms for one polarity
read the same sealed temporary observation. Its backing and buffers are
charged through the existing native resource admission, and the observation
is released before the next polarity's render.

Run each ordinary arm first and require all fifteen archived evidence values
and QUIET decisions, current calendar count 577, valid noise statistics and
full ordered H0-verified apertures. Then execute the same native stream with
the fixed 1/8 supplement. Its selected affine coordinates and complete
public prior come from ordinary acquisition. It reuses native fixed-affine
scoring at half the reference basis amplitude, hence quarter-power Gaussian
gain covariance. The other 7/8 remains with the whole ordinary receiver.
Canonical fallback order, conditional line selection and residual handling,
calendar classification, bit separation and actual quiet scoring apply to both
arms. Retained coherent lines discard the unqualified supplemental evidence
without restoring its reserved mass. No channel truth enters reception.

Use one native invocation, at most 8.7 million rendered rows and 17.4 million
replayed rows across four receivers, at most one 73,950,000-byte spool, and
automatic local `W=24`. Bound each replay by 180 seconds and everything
including preparation by 1050 native / 1060 launcher seconds. A cancellation,
resource limit or input/ordinary score mismatch prevents interpreting that
comparison. Do not extend the wall budget or repeat an arm after seeing its
outcome. Compare complete ordered decisions, first success and longest miss
run, quiet pairs, false and wrong DATA, supplemental and original work, actual
receiver service time and workspace. At least one recovered DATA with no new
wrong/false DATA or lost quiet pair merits a review of efficient integration
and distinct holdouts. Neither a preliminary score increase nor a private
alternative that misses qualifies a production change. Both polarities share
the same physical realization, so their retries do not give independent
samples of natural outage duration. The temporary comparison hook will be
removed after disposition, retaining the compact matched evidence and plans.

The single invocation completes in **812.525 seconds** (812.431 native).
Both full IQ digests and the immutable field digest match. The first field
preparation takes 486.433 seconds and the second binds the same table in
0.000326 seconds. Both baseline arms reproduce all archived evidence and QUIET
decisions. All 60 observations pass the ordered validity, noise-statistics,
H0 and replay-digest checks. The 30 alternative slots satisfy the exact
normalized mixture identity with the unchanged public prior and reserved
coherent-line mass. None skips the alternative or retains a line.

| −3 dB-Hz history | Correct attempts, ordinary → quarter power | Longest miss run, both | Actual quiet pairs, both | Complete replay service, ordinary → quarter power |
| --- | --- | --- | --- | --- |
| Bit 0 | 0/5 → 0/5 | 5 | 5/5 | 72.352 → 85.837 seconds |
| Bit 1 | 0/5 → 0/5 | 5 | 5/5 | 73.171 → 85.876 seconds |

All arms have zero wrong bits and zero false DATA in the quiet slots. The
nearest bit-zero miss, slot 12, changes **13.580601 → 13.581812** against
15.56818. The largest improvement in either polarity is 0.824 nats for
bit zero at slot 6, where the correct separation also stays below 5.2933.
The greatest bit-one evidence falls from **6.500194 → 6.376121**, and its
last attempt still slightly favors the opposite bit under either model. The
−3 dB-Hz all-missed intervals therefore reproduce under the current calendar
and the fixed alternative. The observed −3/+3 recovery bracket remains
specific to this two-hop trajectory and operating profile.

The maximum slot DSP workspace stays **188,928,842 bytes** at `W=24`. Each
candidate arm charges 96,727,944 more affine basis rows. Peak receiver and
outside reservations together stay below 3.45 GB, including the spool charge.
The measured 17–19% service increase is specific to these paired replays
and includes the diagnostic's deliberate second affine projection. It does
not qualify CPU capacity. Neither the +3 improvement in this mechanism nor
the fixed quarter-power observations at −3 support promoting the alternative
or selecting a new weight based on these histories.

The [compact comparison](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/weak-physical-alarms-quarter/comparison.json)
has SHA-256 `3035d885…1e092`; full stdout is `3b5fd797…f5ab9` and the frozen
invocation `f8bef5e3…fa38d`. The one passing plan and six bounded parser
rejections execute no DSP. Appraisal binary `ca19dba9…fb8f` uses native source
`68af5d4d…63e1` and appraisal source `e660c165…0511`. Retire the temporary
scoring and CLI hook. Both spools were temporary and released at exit. Retain
the original raw and compact result with the recipe for this negative finding
until equivalent evidence supersedes it.

For the explicitly repeated-alarm use case, a separate receipt calculation
uses the native operational bit log evidence before individual DATA decisions.
It confirms disjoint full observation apertures, complete validity/H0/noise
statistics, and the original public priors for all 60 slots. On the fixed
15-slot transaction schedule, use slots 0, 3, 6, 9 and 12 as the same-bit
attempts, with two scheduled looks after attempts four and five. Sum each
bit's evidence separately and then take the equal bit mixture, with half
of the group budget per look. Keep the current 15.56818 threshold and bit
separation requirement. This schedule is an exploratory fixed arithmetic
contrast on the retained histories, so it needs independent qualification.

The ordinary bit-zero group gets log evidence **8.22522 / 22.49897** at
attempts four/five, and bit one **16.20422 / 9.45183**. Both have a correct
crossing with more than 42 nats of bit separation, at **32.33 / 24.83 minutes**
from capture origin for bit zero/one (ready-time clock). Both offset quiet
controls use five actual zero-signal windows, with complete-group log evidence
−31.76858 and −32.26031. The controls share the same modeled thermal history
across polarities. For a fixed schedule with a valid conditional mean-at-most-one
null per window, the product is bounded under the null even when actual wanted
signals are correlated. Disjoint apertures and the known white-noise model
support that assumption here. Structured noise, unknown repeat groups and
other model deviations require separate checks. This [receipt calculation](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/weak-physical-alarms-quarter/fixed-repeat-evidence-analysis.json)
(`947f7162…094bc`) establishes neither a native grouped receiver decision nor
physical or codec completion. Any grouped event would need explicitly agreed
repeat semantics and fresh wanted, absence and interference controls.

### Native soft evidence and coherent-line probe for configured repetition

The receipt-only comparison motivates a separate native diagnostic for an
explicitly repeated bit. The ordinary slot score keeps its original gate.
Each eligible repeat attempt copies its final per-bit evidence to a separate
result. If the ordinary scorer retained a coherent line, the repeat result
reuses that conditioned score. Otherwise the same complete aperture may run
one extra native line probe at the fixed calendar threshold divided by five.
A retained line invokes the existing normalized line-plus-DATA alternative
with the same selected public coordinates. Keep the line complement likelihood
when separable DATA survives it. The extra pass uses the original sequential
DSP lease, one bounded line FFT and refinement, and the already quoted residual
stages only when a line is retained. Require complete, all-valid, H0 and
noise-statistics checks, Helper Off, CPU scoring and a factory calendar.

One deterministic boundary check uses a noiseless DC tone with weighted log
evidence halfway to the ordinary calendar threshold. The repeat probe must
retain it without changing any ordinary evidence, decision or workspace.
Replaying with an erased sample must return no repeat factor. One additional
native cohort uses eight fixed five-attempt cases on the same 497-Hz, 120-Hz
inner-rate, tapered-RRC profile, with configured epoch 1793850000 and initial
keyed ordinal 7000037. Active indices are 0, 3, 6, 9 and 12. The thermal input
is seeded proper complex white noise with variance .25 (seed 9480037).
Use the same thermal realization across matched cases and safely disjoint
apertures within every case. Both wanted waveforms use matched received
`E/N0=22` per aperture and a fixed per-attempt phase rotation. A continuous
tone at DC or 8.12345 Hz has coherent `E/N0=35` in the first quiet aperture.
The cases are repeated 0, repeated 1, noise alone, DC tone alone, off-grid tone
alone, repeated 1 plus that off-grid tone, mixed `00100`, and repeated 1 with
a fixed foreign key. Only native keyed basis generation supplies signal
samples. The receiver receives no truth about bit, key mismatch, phase or tone.

The diagnostic examines four- and five-attempt prefixes at fixed half mass
per look, equal bit mass, the original conservative calendar threshold and
log(199) bit separation. All declared active factors are included regardless
of ordinary DATA, QUIET or AMBIGUOUS. Report each ordinary and conditioned
group and actual line probe, bit decisions, sample geometry and workspace.
The mixed-word case measures the consequence of violating the repeated-bit
contract. A prefix cannot reject an undecided future change of bit. The
production codec never consumes group scores and no new app alarm event is
enabled. The cohort covers eight fixed conditional episodes, with a maximum
of 40 scores, 6,000 IQ samples per score, 256 MiB native DSP lease per score,
automatic `W=2*H` and a 60-second launcher bound. No additional level, key or
seed is chosen after seeing the outcome. The deterministic boundary check
uses two workers and a separate short check budget. Preserve misses and
false group crossings alongside their exact source and runtime identities.

The product argument assumes conditional per-bit null means at most one.
The fixed line dictionary keeps its full frequency prior and the residual
uses observations orthogonal to the nominated line under proper white noise.
Retaining a subset and dividing by a factor at least one do not increase the
upper bound provided by the full normalized mixture. Disjoint full input
dependencies and fixed calibration make the slot factors conditionally
valid under the specified white null. `zero_h0_verified` indicates a scored
null model. It does not measure whiteness or validate a live noise model.
With real PCM ingress, check dependency ranges expanded by both FIR halos or
establish an applicable joint noise argument. Wrong-key traffic, persistent
interference and other noise departures require explicit mechanism controls.
These controls do not define a universal interference or field false-alarm
rate. Keep only compact evidence after the diagnostic is disposed.

The deterministic DC boundary passes in 0.572 seconds. It retains the
subthreshold tone in the separate soft evidence, with exact ordinary
evidence, decision, reason and workspace parity. An erased sample produces
no repeat factor. The initial launch used unavailable `/usr/bin/time` and
performed no checks. The corrected launcher preserves that receipt.

All 40 predeclared cohort apertures complete and are valid with the configured
noise model. The first invocation rejects its geometry before scoring because
the fixture omitted the declared Added Symbols = 1. The corrected invocation
completes 35 slots, then its foreign source rejects the AES-only test key before
scoring. The final five foreign-key slots execute once as a separate invocation
with the same intended AES prefix and two complete synthetic test roots. The
first seven cases are never rescored. Both original setup errors and the 35-slot
result remain in the retained receipts.

| Fixed five-attempt case | Correct ordinary DATA among five | Line-aware log evidence at four / five | First group crossing |
| --- | --- | --- | --- |
| Repeated 0 | 1 | 35.63959 / 38.50111 | bit 0 at four |
| Repeated 1 | 0 | 36.28843 / 49.42591 | bit 1 at four |
| Noise alone | 0 | −14.08501 / −17.37496 | none |
| DC tone alone | 0 | −14.16321 / −17.52469 | none |
| Off-grid tone alone | 0 | −12.71069 / −16.16251 | none |
| Repeated 1 with the off-grid tone | 0 | 32.95194 / 45.23277 | bit 1 at four |
| Mixed `00100` | separate data | 28.90488 / 31.76641 | bit 0 at four |
| Repeated 1 using the foreign test key | 0 | −12.97062 / −16.10863 | none |

The group threshold is 16.48343 and the extra line probe threshold is 3.29669.
The two tone-only cases have no unconditioned group crossing either. Their
line-aware results use three and five retained lines respectively. All five
off-grid tone-plus-DATA apertures retain the line, and the desired group
still crosses. The mixed word falsely passes a homogeneous group hypothesis.
It rules out applying the result to arbitrary DATA, including distinct
packets using the same key in the declared attempt slots. A dedicated send
schedule and a distinct repeat result are necessary for deployment. The
prototype publishes evidence only in the test executable and does not qualify
an application alarm or a live noise assumption.

The two scored invocations take 4.185 and 0.465 seconds elapsed. Scoring
accounts for 4.476 seconds, including 0.935 seconds in the extra line probe
and retained-line scoring. In the wanted plus line case, the extra work is
0.348 of its 0.803 score seconds. In the two wanted-only cases together it is
0.00768 of 0.850 seconds. The maximum DSP workspace remains 165,509,725 bytes
at automatic `W=24` / `H=12`. These short-aperture observations do not
calibrate longer receiver service. The paired log files, source hashes and
two scoring plans are bound by [the compact receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/repeat-evidence-native/summary.json),
SHA-256 `16b60fa0…d39362`. The test binaries for the first seven cases and
the remaining foreign-key case are `adf58556…cfa9` and `fd4d781a…acc12`.
The native archive is the same `28fc0ffb…2523` for both.
The temporary characterization and foreign-only test modes are retired after
their evidence is captured. The deterministic line boundary and complete
repeat stream remain as native regressions.

The integrated candidate adds a dedicated five-attempt schedule to the native
source and stream receiver. It retains the counted two quiet slots after
every attempt, the original scoring and physical completion, and a separately
queued logical group result. The opt-in stream expects calibrated white
complex IQ, Helper Off, and native CPU execution. Default stream options keep
ordinary reception. The simulator uses this mode only for its explicit repeat
action. It inserts thermal noise after the modeled propagation and clock
effects and requires no receiver frontend transformation for this action.
Live audio input is outside this change.

Before interpreting the integrated implementation, predeclare a bounded
native `repeat-stream` check. At Fs 497, inner rate 120, Added Symbols 1 and
requested .001 seconds with the canonical pulse duration floor, render one
15-slot group each for bit 0, bit 1 and a genuinely quiet source. Use epoch
1793855000, initial keyed ordinal 7037343, 8 Hz receive atmospheric allowance,
tapered RRC, continuous native channel, variance .25 and seed 373133. Keep the
same nominal signal gain `sqrt(22*.25/source_samples)` across both polarities.
The actual received energy depends on the emitted waveform. Use automatic
`W`, a 1 GiB receiver reservation, at most 450,000 rendered samples per case
and 30 seconds aggregate.
Check all 45 native slot extents, validity and advancing ordinals. Require one
separate correct group event for each wanted history, none for noise, and
the ordinary quiet decisions in all genuinely quiet windows. At least one
wanted group must succeed with zero ordinary DATA admissions. Check the
existing grouped-cohort mixed and interference results independently rather
than infer their outcomes from this integration. Report costs and source-bound
receipts. This clean continuous channel within a wider declared allowance
checks the operating data flow. It does not reexecute the earlier physical
two-hop field or model a natural outage distribution.

The first integration launch reaches the emitted-source TX band guard before
scoring. The default .02-second ramp declares a ±287.5 Hz source with its
spectral skirt at a 248.5 Hz Nyquist frequency. A .1-second ramp gives
±247.5 Hz and preserves the duration floor and all other planned inputs.
The setup failure remains recorded. The corrected fixture is scored once.

The corrected native integration completes in **3.815 seconds**. Each
continuous capture consumes 428,327 samples and visits all 15 public slots
with the fixed key and advancing ordinals. Bit 0 produces one group event at
attempt four, log evidence **54.4956**, one ordinary DATA and ten actual
quiet decisions. Bit 1 produces a correct group event at attempt four, log
evidence **25.7875**, despite **zero ordinary DATA** across five attempts.
All ten actual quiet slots still score QUIET. The absence history gives
fifteen QUIET slots and no group event. The local CPU uses `H=12`, `W=24`,
with peak reservation 582,433,653 / 590,584,693 / 588,094,325 bytes for the
three cases. Extra line probes total .00460 / .00395 / .00460 seconds per
case. The extra probe can run even when individual DATA evidence is near
ordinary noise. For an eligible long window it may use a bounded wide FFT,
full-window line refinement and the residual DATA stages. The short results
do not qualify this service for long windows or broad capacity limits. The
private aperture-only interference results above remain separate.
Test binary `e4a43c77…9562f` uses receiver archive `2b30b516…c6a6`.
The focused compile and native/GUI check receipts remain with the scoped
source-bound repeat evidence under `local-evidence/repeat-evidence-native/`.
The separate `gui_robust_repeated_bit` check passes in **3.51 seconds**. It
verifies the 15-slot simulated transaction, the five-attempt source trace,
one separate result, the ordinary quiet slots, expected input guards and the
following ordinary raw-bit transmission starting 15 keyed ordinals later.
The new GUI action is separate from standard transmit and unavailable for
live input, GPU execution or Helper use. The initial CMake configure failure
for the new test name is recorded separately. Declaring its build prerequisite
resolved setup before an executable was run.

### Production repeat receiver on the frozen weak two-hop bit-one history

Predeclare one native production-stream replay of the archived −3 dB-Hz
`hf-vector-two-hop` bit-one row. Keep the original reference RRC waveform,
128-second request and 139.2-second actual DATA, Helper/FEC Off, configured
calendar and key, and oscillator/thermal seeds 73613/93613. Five attempts
retain ordinals 0, 3, 6, 9 and 12, each followed by two quiet slots through
one continuous field and thermal history. Prepare the field once, render
4,300,572 rows once and use one sealed temporary observation. Require the
full capture digest `03bb384b…058d333` and field table `ff2b3f4e…0008aa` before
the receiver starts. Retain original public search bounds and all fifteen
full and quiet aperture extents. The declared physical FIR padding is 128
samples. The renderer adds proper complex white noise after that filter and
applies no receiver frontend filter, ADC, clipping, AGC or extra additive
noise component. Explicit unfiltered-IQ padding provenance binds this exact
existing geometry to the native fixed repeat receiver without modifying the
input samples or asserting a PCM covariance model.

Use one invocation, at most 4.35 million render and replay rows and one
73,950,000-byte temporary spool, `W=24` on the local `H=12` host, native resource
admission, and 720 native / 730 launcher seconds aggregate. A no-DSP plan must
bind geometry, source options, native/app identities and workspace. Parser
rejections check the fixed controls and budget before signal execution.
Stop after this one execution, including if it is censored. The receiver
must run through all fifteen slots and EOF regardless of any group decision.
Record the complete per-slot repeat factors, extra line work, runtime, raw
decisions and independently drained native group events. Require the ordinary
scores and QUIET decisions to reproduce all archived slots, with valid noise
stats and full H0-verified input. A real bit-one group event, if emitted, must
come from the canonical context with the configured four/five-attempt stopping
rule and matching soft-evidence sums. A missing or incorrect group result is
a communications outcome to preserve. A successful native group here would
be stronger than earlier receipt arithmetic, with the actual extra line
analysis included. It would still cover one constructed channel episode and
proper white IQ, separate from live PCM or natural outage qualification.

The single invocation passes in **567.360 seconds** (567.292 native). Field
preparation takes 485.205 seconds and rendering 4.086 seconds. The exact field
and all 4,300,572 IQ rows reproduce; ordinary per-bit evidence, log evidence,
quiet energies, both aperture extents, clock and keyed ordinal agree in all
fifteen slots. All slots are complete and H0-, validity- and noise-verified.
Each of the five ordinary attempts remains QUIET and every genuine quiet pair
does too, with zero false DATA and zero physical completions. No physical or
codec decision uses group evidence.

The canonical stream emits **one distinct correct bit-one event after the
fourth attempt**, at public slot and ordinal 9. Its log evidence is
**16.20422133** against **15.56818264**, with separation **42.76859511**.
The slot is ready **1,489.88125 seconds** (24.83 minutes) from capture origin.
All fifteen slots still commit. The fifth-attempt arithmetic would give
9.45183352, and the receiver emits no duplicate event. The actual additional
line probes take 0.580731 seconds summed over the five attempts and preserve
their exact ordinary bit evidence as the repeat factors.

Complete local replay service is **77.669 seconds**, against the archived
73.171-second ordinary replay on these same samples and worker budget. This
ordered comparison includes the new line work and runtime variation. It is
not a capacity qualification or an isolated overhead measurement. Maximum
slot workspace remains **188,928,842 bytes**, peak application reservation
3,275,764,666 bytes, with `H=12` and `W=24`. The one 73,109,724-byte spool is
released after complete replay and no capture is kept. The passing plan,
six no-DSP rejection checks and focused native stream test precede the run.
Appraisal binary `56fc5983…a522f` uses native `0adccc40…bc167`, receiver archive
`8b773aa2…6cfe9` and appraisal source `f8b9497f…104dc`. Initial ordinary evidence
comes from the both-polarity archive `3b5fd797…f5ab9`. The frozen invocation
digest is `ffb3e8e4…65c7d`, raw grouped stdout `33fbb59e…a917e`, and the
[compact result](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/weak-physical-grouped-replay/comparison.json)
is `dcbe2102…a394df`. The temporary app hook is retired. Keep the receipts
only to support this matched physical result.

### Long VLF repeat line probe cost

The extra line proposal below the ordinary per-slot threshold may be costly
on long apertures. Before executing this cost check, fix one `vlf-phase-wander`
reference tapered-RRC first-slot noise control, seed 9489, Helper/FEC Off,
nominal C/N0 −18.4299502 dB-Hz and calibrated complex white variance
133750.75808487268. Reproduce the archived 12,966,069-row input exactly, hash
`f500b914…81ee6e68`, including its binary validity. Full aperture is
`[0,12966069)` and quiet aperture `[448,12904463)`. Require epoch 1793405280,
ordinal and public index zero, the same matched native workspace quote
952,574,517 bytes and automatic `H=12`, `W=24`. No colored noise, tone,
impulse, converter, receiver frontend, channel truth or group admission is
supplied. The old synthetic public profile includes positional halo padding;
the renderer supplies raw complex samples without a front-end filter. This
is a direct slot comparison, outside the public repeat profile's halo guard.

Run one native invocation with the exact archived noise command, replacing
the temporary gain option with `--vlf-repeat-line-cost`. Limit the entire
invocation to 150 seconds and cap rendered samples at 14,000,000. Exit the
positive sample-cap preflight before rendering, with a separate parser
negative. Hash the assembled worker input before any score, using the
canonical sequence of IQ bytes and validity. Verify the archived complete
ordinary QUIET result and log evidence −10.203439308846256 at the fixed
calendar threshold (`N=13`). In the same normal score invocation, append the
existing repeat line check at threshold `T/5`; the ordinary receipt is sealed
before this extra work. Time the complete slot, the original portion and
the additional proposal/residual, with input verification outside scorer
timings. Count FFT rows, normalized coarse peak, searched bins and executed
prefix/full correlated rows. No duplicate full score, extra capture or
additional worker pool is needed. If the raw noise fails to nominate a line,
record actual early rejection; it does not measure the expensive conditional
tail. If refinement executes, retain the complete observed path and cost
without changing signal or threshold. Preserve any cancellation, resource
rejection or parity failure and perform no extra run. Retire the temporary
instrumentation and app hook after disposition.

The cap-1 preflight returned resource limit before rendering, and the
altered-seed command failed the frozen input guard. The single authorized
replay completed in 52.673 seconds end to end, with 293.487 process CPU seconds.
It matched the complete IQ and validity hash, the ordinary QUIET decision
and log evidence, and the 952,574,517-byte slot workspace. It used all 24
executor workers. Whole-stream peak reservation was 1,396,237,621 bytes.

The normalized FFT peak of 3.60695 exceeded the 2.35506 proposal gate. The
262,144-row FFT searched nine retained bins. The refinement used 24 prefix
dots over 3,145,728 correlated rows and 24 full dots over 309,696,360 rows.
It retained no line and returned the same per-bit evidence for repetition.
The extra probe added 0.93107 seconds to the 45.65487-second ordinary score
(2.04%). The 46.58594-second instrumented score excludes 0.21885 seconds of
input verification. This captures a noise nomination that reaches the full
refinement, with no retained-line DATA residual. It gives no retained-tone,
broad-channel, long-duration backlog or natural availability measurement.
If full dots repeatedly affect sustained service, reuse a center correlation
only across identical atom and sample supports so both selection and line
evidence remain unchanged.

The temporary run binary hash is `bc97b1e6…b0c1f`; the run receipt is
`985ed629…f1c0fe`. Keep the exact invocation, preflights and receipt under
`local-evidence/repeat-vlf-line-cost/` for the current cost finding. Remove
those temporary outputs when a maintained measurement supersedes it.

### Calibrated live PCM repeated-bit check

Final current-source correctness groups pass **46/46 triad**, **30/30 GUI**
and **6/6 selected appraisal** checks after retiring the temporary physical
replay hook. The final native receiver archive `8b773aa2…c36cfe9` is identical
to the one used in the physical grouped replay. Final app `13a5adba…6f48d`,
probe `38e95294…5134d044` and the individual log/source identities are recorded
in `local-evidence/live-repeat-final-validation/validation-summary.json`
(`63c8d539…e08b48`). These are correctness checks, not capacity or field
certification.

The native audio test renders one 15-slot group through the canonical
emitter, white real PCM, the centered `AudioInput` FIR and the continuous
receiver. It uses 1,024 Hz sampling, 256 Hz DATA IF, 48 Hz inner rate,
12.8-second bits, −61.0721 dBFS/Hz declared one-sided input density, 256-sample
raw FIR halos and seed 74613. It requires all 15 complete valid score
windows. It verifies the separate fixed-repeat result and separately
compares ordinary physical completion to received DATA followed by two
scored quiet slots. RAW receive dependencies of each contributing attempt
are disjoint after expanding the scored I/Q support by both FIR halos.

The initial test stopped before scoring because the fixture expected its
first observation at zero. The canonical geometry begins the observation at
sample 9,984, after the capture starts. The corrected test kept the same
input, including complete FIR prehistory and tail. The first scored run
completed all 75 windows. Each strong bit crossed after four attempts and
both controls produced no group. The weak bit was missed, with all five
ordinary attempts quiet. That fixture had set the receiver basis gain to the
unscaled emitter normalization, about 59.35 dB above the weak waveform gain.
The native live path instead uses the declared target C/N0 and noise
normalization to set the absolute receive basis.

A subsequent one-group contrast retained the same seed, signal, ordinal,
raw PCM projector and public windows. It used the existing 32 dB-Hz live
receiver target, which put the fixture basis about 30.28 dB above the weak
waveform gain. A separately rendered reference consumed no DSP score. The
two fresh renders matched raw PCM SHA-256 `93385f8e…92d91` and projected
I/Q-plus-validity `a317fefc…a5867`, with per-chunk comparison before receiver
ingress. The original unscaled run did not record those hashes. Across the
five active slots, nominal emitted `E/N0=22` gave mean modeled clean
received `E/N0=19.04786` before the real PCM projection and 19.00066 after
it, about 0.011 dB of loss. At the calibrated gain, bit 1 crossed at the
fourth attempt with log evidence 37.24694 against 16.48343 and 52.50 nats
of bit separation. All 15 ordinary slots were quiet and there was no
physical completion. The paired check finished in 4.44 seconds, with one
scored group, 24 workers and peak reservation 430,274,440 bytes.

The permanent `robust_audio_repeated_bit` CTest uses that calibration for
all five frozen cases. It passed 75/75 scored windows in 12.49 seconds on
24 workers, with peak reservation 630,360,819 bytes. Strong 0 and 1 each
gave one correct repeat event at attempt four, five ordinary DATA decisions
and five real physical completions. The modeled thermal control and
continuous coherent-line control gave zero repeat events, zero DATA and
zero physical completions. The line control exercised line conditioning.
The weak bit 1 repeated result again arrived at attempt four with zero DATA
and zero physical completions. This uses one shared seeded thermal history
for matched float PCM comparisons. It exercises the conditional receiver
contract and the live gain setting without establishing that device noise
obeys the model, or establishing a natural-channel availability rate.

The failed fixture receipt is `dedf71bd…d672`, the calibrated matched-input
contrast `09509e66…ac5c` and the durable test receipt `2dd51f4c…6c65` in
`local-evidence/repeat-pcm-native/`. Retain the compact findings after the
temporary diagnostic and build scratch are retired.

## Weak combined-modulation follow-through — 17 November 2026

This investigation starts from `c1f1f0f`, following the committed direct-gain
repair below. The next discriminator targets the previously bright GUI
HF Worst / Each 65% / Margin 3 history, rather than treating the standard
Gaussian tests as equivalent coverage. Keep the 41.6-second DATA profile,
bit one, public calendar/key/ordinals, oscillator/arrival seed 62014,
atmosphere seed 62034 and thermal seed 72014. Changing actual C/N0 changes
receiver noise variance against the fixed normalized DATA reference, not the
transmitted waveform or channel history.

The retained weak two-hop receipts already select `stationary_plus_ou` with
one spatial template: their declared delay is zero. The preceding repair
changes the two-template case, so it does not alter that one-template
likelihood path. The old −3 dB-Hz misses are complete, valid observations with
ordinary coarse/fine, varying-channel and drift work, not absent search or a
timeout. The appraisal's `public_search_executed=false` denotes that its
separate `hf-search` diagnostic was not requested; it does not mean the native
receiver skipped search. The +3 dB-Hz final-bit log evidence of 16.60 remains
below 21.49. Those source-bound failures remain relevant; another expensive
field-table regeneration without a new discriminator would add little.

Predeclared first stage: render the continuous six-attempt source and channel,
but score only late public DATA slot 12 and its real quiet successors 13–14
through the canonical native slot scorer, with full `W=24` and the original
public bounds. This measures three apertures, not six-attempt completion or
continuous receiver throughput. First compare a 34 dB-Hz control with the
saved full-stream input hash and the same three decision receipts, excluding
orchestration/timing counters. Then score +4 dB-Hz; score +10 only if the
active bit is missed, or 0 only if it is correctly received. Preserve any
wrong bit or ambiguous/false quiet observation separately. The maximum is
three executions, 2.4 million samples and 120 seconds per execution, 360
seconds aggregate. Execution censoring is not a communication failure and
does not authorize an automatic extension. Stop after the selected pair and
inspect per-branch evidence, candidate availability and physical energy
before choosing any production change or additional test.

The selected-aperture harness passes independent review, 175 runner checks and
native no-DSP sample-budget preflights. It uses `CompleteStreamReceiver` with
the original global first slot and sample offsets. Full channel rendering
still covers all 2,041,521 samples; the receiver accepts only
`[1381980,1695817)`, 313,837 samples. The 34 dB-Hz control exactly matches
the saved full-IQ hash `5cdac17c…` and every common non-timing decision/work
field for slots 12–14. All three invocations bind native `e9087e04…`,
appraisal `82cda018…`, and probe `4dbe76cd…`.

| Actual reference C/N0 | Slot 12 decision | Operational log evidence | Elapsed |
| --- | --- | ---: | ---: |
| 34 dB-Hz, control | Correct DATA 1 | 87943.2260 | 40.099 s |
| +4 dB-Hz | QUIET, missed DATA | −5.6689 | 41.211 s |
| +10 dB-Hz, conditional | AMBIGUOUS, missed DATA | 2.2350 | 40.386 s |

The threshold stays 21.4934. Both real quiet successors are QUIET at every
level; all nine observations are complete and valid, with no wrong or false
DATA. The source and non-noise channel are identical, including source DATA
energy 479,725.3732 sample units. Noise variance changes as declared; full IQ
hashes therefore differ. Aggregate elapsed time is 121.696 seconds. The
conditional 0 dB-Hz branch is not run. The comparison receipt is
`weak-combined-modulation/three-point-selected-slot12-comparison.json`
(`3de77282…`). The +10 case still lacks bit separation, even before the full
public prior penalty; lowering that penalty would not demonstrate a repair.
These two weak misses are not yet evidence of an avoidable model deficiency
or, by themselves, a measured sensitivity boundary. The later +16 endpoint
below establishes a finite recovery interval for this history.

A separate source audit identifies a live-calibration distinction. Simulation
changes additive noise against a fixed DATA basis; live mode sets its expected
basis amplitude from Target C/N0 and declared input-noise density. Original
affine/additive and nominal independent-gain branches use fixed normalized
gain power, while the ordinary short-frequency bank already includes weaker
power scales. The next predeclared causal contrast reuses the successful
standard-Gaussian `hf-disturbed` first aperture, seed 9202, +12 dB-Hz,
139.2-second DATA, bit zero from `e`, Helper/FEC off. On the same retained IQ
and noise covariance, compare the ordinary receiver with only its DATA basis
gain multiplied by ten: the live-amplitude equivalent of Target 32 against
actual +12 dB-Hz, with public geometry unchanged. One wanted and one noise-only
case are allowed, at most 270,000 samples and 120 seconds each / 240 seconds
aggregate. Require the wanted baseline's prior input digest and correct DATA
before attributing any change; inspect candidates and likelihood components
separately. This is a receiver-setting contrast, not an atmosphere change or
a production power-prior modification.

That contrast demonstrates a real calibration loss. Wanted capture
`4cb05606981b401acad364c6d37fcbe1a7b7ce74c3e627ac81ef64eec7b810c0`
matches the archived input and scores correct DATA zero at log evidence
51.65795. Increasing only the receiver basis gain by ten makes the same
observation AMBIGUOUS: log evidence is 22.91656, but the bit scores
21.12119 / 23.52302 weakly favor the wrong value and fail the unchanged
separation requirement. Both use all 268,361 valid samples, `W=24`, identical
candidate counts and a 629,000,553-byte DSP quote. The nominal independent-gain
wanted score collapses from 70.21734 to −15805.95878 under the oversized fixed
covariance. Existing weaker ordinary-short scales do not recover the wanted
bit. The matched noise capture remains QUIET in both arms. These are
16.010 / 17.011-second wrapper invocations, with probe `44757f0b…`, appraisal
`991e48cd…` and unchanged production native `e9087e04…`. The compact receipt
is `weak-combined-modulation/live-gain-contrast/live-gain-contrast-summary.json`
(`cf400d69…`). This isolates the expected-amplitude setting; it is not a
live-device noise-estimator or duration-planner qualification.

The candidate therefore adds a whole-aperture power mixture to the nominal
and signed-lag independent short-gain branches: total powers
`1, 1/16, 1/256, 1/4096`, with weights `3/4, 1/12, 1/12, 1/12`.
The weaker scales reuse established ordinary-short support and reach about
36 dB below the configured reference; they are not fitted to the contrast's
particular 20 dB offset. Original unit-power contribution retains three
quarters of its conditional mass. This bounds only that contribution's
log-evidence reduction by `log(4/3)`, not sensitivity or bit-error loss.
Use the same templates, frequency/timing coordinates, observations and
thresholds, and charge the additional small likelihood/Markov state. Focused
normalization, erased/rank-zero and workspace checks precede any candidate
receive run. The first candidate gate repeats the exact two captured-case
configurations above, with the same finite sample/time budget and original
failure receipts preserved. There is no assumption that this also repairs
the weak combined-GUI misses.

After that gate passes, one further candidate preservation run reuses the
exact +10 dB-Hz selected GUI manifest and its 120-second / 2.4-million-sample
bounds. Require the original full IQ hash, three complete valid observations,
no wrong/false DATA and both real quiet successors. Record the wanted decision
whether recovered or still missed; do not extend the original three-run
baseline stage or alter its results. Then run the affected named correctness
groups. Primary emission counts are not complete-service counters: a separate
line-conditioned rescore is excluded, while measured end-to-end time includes
all executed work.

The candidate passes the frozen calibration contrast. On identical wanted IQ,
the matched-reference arm remains correct DATA zero at log evidence 51.37026;
the oversized-reference arm changes from AMBIGUOUS to correct DATA zero at
70.81670 (bit scores 71.50984 / 23.52307). The original unit-power raw scores
are exactly retained; the predeclared `1/256` level supplies the useful
alternative in the oversized-reference arm. Both noise arms remain QUIET on
the same noise IQ hash `4f33a070…`. All four scores are complete and valid,
with verified zero hypothesis, `W=24`, unchanged candidate/basis/frequency
geometry and the same dominating 629,000,553-byte DSP quote. Primary gain
emission cells increase fourfold to 872,144 per score; the additional state
is charged even though it does not increase that maximum quote.

Candidate probe `9c9c40c7…` binds native
`104db8fa51a68de1f28199fa446afc664d02dd1e49512c8547b1dfaccdb56124`
and appraisal `b880e6b4…`. Complete paired native service changes about
15.997 → 17.614 seconds for wanted input and 17.000 → 19.209 seconds for
noise (about 10% / 13% more on this host). These are bounded serial
observations, not sustained CPU-capacity or GPU measurements. Old unit-only
short-score timings do not calibrate this version. Rendering,
projection and Gram/correlation work already share the four powers. A further
determinant/eigen simplification could reintroduce near-rank-one cancellation;
no such unqualified numerical shortcut is adopted. Minor scalar hoisting has
no demonstrated complete-service benefit here.

The +10 dB-Hz GUI preservation keeps exact full IQ `9bd27a5e…`, unchanged
source energy and public work, with slot 12 still AMBIGUOUS and both quiet
successors QUIET. Its active evidence changes 2.23501 → 1.94753; it does not
recover the missing bit. All three windows remain complete and valid without
wrong or false DATA, and the 39.067-second wrapper / 38.871-second native
timing is a single overlapping-stream observation. The compact comparison
receipts are `candidate-live-gain/candidate-live-gain-comparison.json`
(`9ceac09e…`) and
`candidate-each65-plus10/candidate-plus10-preservation-comparison.json`
(`599c8647…`) under the current evidence directory.

The repaired native source passes **40/40 triad** (103.42 seconds), **28/28
GUI** (154.46 seconds), and **6/6 appraisal** checks (49.24 seconds), including
the clarified Target C/N0 tooltip. Runner checks remain 175/175. These are
correctness checks; no manual-capacity refresh, actual-GPU run or RunPod
resource was used. Their command/output summary is
`weak-combined-modulation/final-validation/` (`c18febb0…`).

Independent completion review requires one further discriminator for the
unresolved +10 GUI miss. Reconstruct at most two public signed-lag cells
nominated by the successful +34 same-history observation, and compare them
with +10 nominations. If different, evaluate those cells on unchanged +10 IQ
using the existing short independent-gain scorer and full `13*L` public
normalization. Keep the ordinary weak result and all physical/source settings.
This is a private proposal-support diagnostic: a rescue would establish useful
missing timing support, not prove that the weak receiver can discover it.
No additional SNR, seed, duration or covariance sweep follows automatically;
the frozen native command is `weak-combined-modulation/signed-lag-contrast-plan.json`
(`a9e0769d…`). Its single execution allows 120 seconds total, two continuous
2,041,521-sample renders, two retained 83,368-sample DATA apertures, full
automatic `W=24`, and a diagnostic 3 GiB workspace ceiling with resident and
stack charges. The native sample-budget preflight must stop before rendering.
If transferred support crosses the admission threshold, retain ordinary
conditional-line processing or label the result as a pre-line diagnostic.

The timing contrast completes in 36.483 seconds with exact input and weak
ordinary-score parity. Bright nominations are `[-100,-58]`; weak nominations
are `[48,29]`. Transferring the two cells raises the correct-bit-one margin
from −0.69843 to 2.58266 nats and operational-scale pre-line evidence from
1.94753 to 4.40405. Both remain below their respective 5.29330 and 21.49338
requirements, so no conditional-line stage is triggered. Other affine, drift,
fixed and ordinary tracking components are unchanged. A union of these two
sets cannot close the gap: even adding both complete likelihoods, double-
counting their shared components, gives an upper bound of 4.48631 nats.
This is a demonstrated timing-selection loss but not a complete receiver
repair. The private receipt leaves `affine_selected_prior_mass` at its default
zero; this is a reporting defect, not the calculated mass. Exact component
and posterior parity verifies the original nonzero public weight was used.
Preserve the raw receipt and this correction rather than rerun the experiment.
Its compact summary is `signed-lag-contrast/signed-lag-contrast-comparison.json`
(`8f761625…`); probe `4112b1ce…`, appraisal `8001386e…`, native `7ec93490…`.
The latter native identity includes the tooltip change; DSP matches the
earlier calibrated candidate. This executes native component scoring, not a
complete receiver session or operational acquisition of the transferred cells.

One further fixed recovery endpoint is predeclared after that causal test:
ordinary selected slot 12 and quiet 13–14 at **+16 dB-Hz**, exactly 6 dB above
the +10 miss, with every source/channel/calendar parameter unchanged. Use the
existing 120-second / 2.4-million-sample cap and full W; expected noise variance
is `192 / 10^0.6`. The manifest is
`weak-combined-modulation/each65-cn0-16-slot12-recovery-endpoint.json`.
It uses ordinary nominations, with no transferred cells. No bisection, new
seed, further level or model expansion follows automatically. This addresses
the remaining 24 dB gap to the demonstrated bright success, not an outage
probability or sensitivity curve.

That ordinary +16 dB-Hz run receives **correct DATA one**, followed by two
complete, valid QUIET decisions and one physical quiet pair. Slot 12 has bit
scores 155.14718 / 289.97711, correct-bit separation 134.82993 and operational
log evidence 289.28397 against the unchanged 21.49338 threshold. Clean source
DATA energy 479,725.3732 and propagated signal energy 613,030.1472 sample units
exactly match +10; noise variance is 48.228219485, as predeclared. Full IQ is
`71f68c0158585b56cd9eee092c7703b06f5b8d9cab5f7ddb9c89fa675cb94ce2`.
The original key, six source emissions, channel history, public ordinals,
clock bounds and selected interval remain unchanged. This is ordinary receiver
nomination, without transferred timing support.

The run completes in 37.804 native / 37.992 wrapper seconds, with `W=24`,
434 active blocks, 9 fine / 18 varying candidates and a 259,827,608-byte peak
DSP workspace. It binds the same `4112b1ce…` probe and `7ec93490…` native
source as the timing contrast. Its compact receipt is
`plus16-recovery-run/plus16-recovery-comparison.json` (`b13b8d42…`). An initial
runner output-directory setup error executes no native trial; its separate
receipt is retained. Exactly one actual +16 trial executes.

Thus +10 misses and +16 succeeds for this fixed 41.6-second combined-channel
observation: a 6 dB signal-to-noise increase restores reception. This is not
a measured 6 dB penalty relative to theory, a complete sensitivity curve,
six successful attempts, or an estimate of natural outage duration. The timing
contrast does not justify another production proposal bank. Earlier weak
physical-channel failures and weak Helper remain outside this new result.

Temporary manifests, input/source identities and receipts belong in appraisal
`local-evidence/weak-combined-modulation/` for this discriminator and any
resulting repair. The one-off signed-lag transfer command and capture-only
scaffolding are removed after resolving its question; its exact receipt and
reporting correction remain evidence of why no timing change is promoted.
The reusable selected-attempt and live-gain contrasts remain in the appraisal.
After removal, the appraisal rebuild and all six affected checks pass again
(49.39 seconds). Native source remains `7ec93490…`; the rebuilt appraisal
probe returns to `d31daf40…`. The closure summary is
`closure-validation/closure-summary.json` (`c1c7b30b…`); historical diagnostic
binary identities remain attached to their actual runs. Independent review
confirms that the reusable contrasts remain and the private command is gone.
No large IQ capture or private comparison binary is retained. Original failures,
frozen commands/manifests and compact matched receipts support the current
repair and unresolved weak-channel boundary; retain them until those claims
are superseded by an equivalent source-bound regression or release record,
then distill the conclusions here and remove redundant outputs. No natural
outage distribution, weak Helper qualification or actual-GPU measurement
follows from this stage.

## Persistent-reception investigation — 17 November 2026

This follow-through starts from `f825306e`, reuses the retained weak two-hop
endpoint, and separates receiver support loss from insufficient physical
propagation. The [research reconciliation](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#persistent-deterioration-versus-physical-loss)
adds recent absorption observations while retaining path-specific Doppler,
mode-change and cross-carrier limits. It does not infer an outage rate from
selected storms or successful seeded retries.

The native state audit finds fresh Helper, coarse and fine state for each
public slot; ordinary misses do not retain rejected search coordinates into
the next attempt. Message assembly deliberately waits for two consecutive
scored quiet slots after a rejected/incomplete episode or joining an unknown
suffix. Ambiguous or DATA slots interrupt that wait, but raw slot receipts
continue. Explicit resource/processing failures can terminate a receiver and
are reported separately; they are not ordinary atmospheric acquisition misses.

The live presentation previously skipped rejected episodes because their report
omits the first-slot identity. It could leave a prior browser row displaying
“Receiving Robust content.” The correction marks an abandoned pending row as
rejected before releasing its identity, including when rejection, rearm and a
new episode occur between deliveries. It preserves the old row's own counters,
does not manufacture completed content, and leaves raw bit publication intact.
A deterministic nine-slot sequence starts at public slot 41 and exercises
ambiguity, DATA during recovery, interrupted quiet, rearm, and later one-bit
physical completion. This is an orchestration check, not atmospheric sensitivity
or a direct UI-rendering test.

The physical discriminator predeclares a single +4 dB-Hz `E` capture from the
retained two-hop field, actual 139.2-second DATA, and two operational receivers
whose only model difference is declared delay spread, 0 versus 7 ms. Both use
one common capture aperture and source calendar, 13 DATA slots and two real
quiet successors. A single native field preparation serves both receivers;
the complete IQ/validity digest and historical capture-prefix digest must
match before inference. The fixed envelope is 4.35 million samples, full
`H=12/W=24`, 180 seconds per replay and under 900 seconds aggregate. The new
scoped probe can retain its bounded sealed capture by exclusive creation to
avoid repeating physical preparation during diagnosis. Explicit named outputs
remain caller-owned, including partial failed captures; only a full matching
readback digest marks one verified. Ordinary anonymous spools still clean up
automatically. Neither an unavailable model alone nor a bright successful test is
treated as a demonstrated weak reception failure.

The baseline comparison completes in 615.504 seconds, including 486.896 seconds
of field-table preparation. The common 4,300,585-row capture is the historical
4,300,572-row +4 capture plus 13 tail rows; its historical prefix is verified.
Both full replay digests equal
`ea887215dc1109d78ddc05da605f129d8a72e21c2529c9e631b7429e0a8dcf73`.
Probe `e0dbbb5b…` binds native `985bcaa0…`, appraisal `7eef3e8d…`, GNU 14.2.0
Release and the unchanged production receiver archive `b1eb7336…`.
The host is an AMD Ryzen 5 PRO 5650U, 12 logical threads and 15,677,412 KiB
physical RAM. These are native CPU runs, not Radeon or NVIDIA measurements.
The 7 ms coarse receipt reports FP32 FFT arithmetic; fine likelihood/state
remains double precision. The narrower eligible geometry reports Q15 products
with FP32 FFTs. Automatic memory admission is resolved per run, not fixed to
an old machine-specific limit.

| Declared delay spread, same input | Correct active bits | Message | Final DATA slot log evidence | Complete replay wall time |
| --- | ---: | --- | ---: | ---: |
| 0 ms | 13/13 | Correct `E` | 31.88684 | 35.993 s |
| 7 ms | 12/13 | Truncated/rejected | 15.44955 | 86.891 s |
| 7 ms, direct additive repair | 13/13 | Correct `E` | 31.15931 | 79.605 s |

The threshold is 21.49338. Both actual ending windows remain QUIET, with no
wrong bits or false DATA. The original 7 ms arm calls the final active slot QUIET,
causing premature physical completion when the next real quiet slot arrives.
All 15 windows are complete and valid; there is no deadline or resource censor.
Baseline 0/7 ms peak replay reservations are 3.263/4.706 GB with full `W=24`.
These are complete services for this loaded capture, not a hardware-capacity
qualification or a general speed ratio.

This establishes a receiver-support loss when the operator widens the
allowance while the physical waveform remains unchanged. It is not an isolated
likelihood ablation: changing delay geometry also changes fine candidates
(four versus three at the missed slot). The useful additive family is enabled
only in the original 0 ms arm. The matched **7 ms old/new** replay then
demonstrates the repair: all fine candidate counts, basis rows, public coordinate
mass, calibration and apertures remain identical. At the missed slot these are
three candidates, 9,673,560 basis rows and mass 0.003393665. Wanted affine
evidence increases 23.90798 → 39.61774, while the short fixed-gain scores are
unchanged. Earlier admission skips the previous medium/drift fallback there.
The repaired message has no premature completion, wrong bits or false DATA.

The change retains the existing two-path model at 0.75 affine mass and gives
0.25 to the existing direct-template stationary-plus-OU alternatives. It
reuses the direct correlation and Gram diagonal after the original update;
there is no extra template, timing candidate or channel-history enumeration.
Focused native checks cover two-block packing with nonzero cross terms,
rank-zero/erased blocks, one-/two-tap direct-model evidence parity, normalized
mixture, charged state updates, unchanged basis work, Off and slow-model
behavior, and quiet scoring. The retained-file probe checks exact extent and
digest before DSP, then hashes actual canonical consumption again; a same-sized
incorrect input is rejected before reception.

Candidate probe `5bada1b4…` binds native
`e9087e04f33dbb43c19145cefa81ef571377143efd16e59833e3f0afb3844e70`,
appraisal `e0a01288c35e8d76dd5cb394fb349451235867973d89d517a8213bc9d2ec4e93`
and receiver archive `13958c0d…`. It performs no field generation. Both input
digest checks match the original common capture. Complete replay CPU time
falls 831.166 → 756.913 seconds, with wall time shown above. The largest
single-owner quote stays 275,913,870 bytes because another stage dominates;
additional gain state is still charged. Aggregate peaks vary with live RSS and
owner scheduling, so 4.706 → 4.571 GB is descriptive, not a guaranteed memory
reduction. These measurements do not qualify general receiver capacity.

The longer coupled GUI holdout freezes bit one, requested 32 seconds (actual
41.6), C/N0 34 dB-Hz, six separated attempts, and fresh oscillator/arrival,
atmosphere and thermal seeds 62014/62034/72014. It keeps HF Worst, Each 65%,
Margin 3 and the original full clock-edge bounds. Exact-mode preflight uses
the early sample-budget guard before channel/DSP work: 18 public windows need
2,041,521 samples, within the existing 2.4-million cap. Fourfold DATA duration
and 6 dB lower C/N0 relative to earlier 10.4-second/40-dB rows are only an
approximate source-energy comparison, not matched realized energy or coherence.

The first 80-second invocation is **censored**, not a communication miss or a
six-attempt pass. It renders and accepts all input, but commits only slots
0–4: two correct DATA, three QUIET and one completed quiet pair. The receiver
then cancels at its deadline. It uses all 24 workers, reaches eight running
owners and reserves 5.125 GB against 11.395 GB, without input backpressure or
memory refusal. The retained receipt has no final live-owner/queue snapshot;
the uncommitted prefix cannot be treated as unstarted work. Its DATA evidence
is already very large, so this is a coupled-support check, not a weak Each 65%
sensitivity boundary.

The short-case timing estimate understated the longer search. Coarse frequency
centres grow 929 → 3513, while the public aperture grows about 3.64×. Source
geometry gives FFT lengths 8192 → 32768 and about 17.45× logical transform-row
stages for this path. That is a work calculation, not an elapsed-time forecast
or an isolated attribution to the new gain model. Three completed quiet windows
also execute fallback work. A separately frozen, single **300-second** replay
therefore keeps every physical/source/receiver parameter and the sample bound.
The appraisal exception is limited to this exact case; other repeated-GUI
rows retain 120 seconds. Boundary tests cover the allowed tuple and rejection
of excessive time, samples or a different seed. The 80-second failure remains
in the record; there is no automatic further timeout extension.

The amended invocation completes in **223.343 seconds native / 223.644 seconds
including the runner**, with all **6/6 DATA bits and 6/6 quiet pairs**, no wrong
or ambiguous bits, and all 18 complete, valid windows. Its full input hash
`5cdac17c2551652eae8cf0f6b6d9473be4e474612dcff955dc06750f67efef93`
matches the censored run; the five completed prefix records are identical in
evidence, work and public geometry. The continuous channel realizes common
frequency excursions about −89.988 to +89.955 Hz, echo Doppler about −89.946
to +89.959 Hz, and 18.108 ms excess delay inside the margin-expanded 90 Hz /
21 ms allowances. Selective component depths are 0.65/0.65 at 0.9/9 Hz.
It uses `W=24` and peaks at 5.202 GB reserved. The 17.72-minute capture is one
constructed history, not independent natural retries or a capacity measurement.

The wrapper initially rejects that complete native output because a second
receipt-validation branch still enforces 120 seconds. Input admission and
receipt validation now share the same scoped cap, while all full-hash, sample,
calendar, bit/null and physical-completion checks remain. Validation also binds
the receipt to the actual invoked wall and sample limits. The saved native
bytes (`5d8d12d8…`) are revalidated offline against their original command;
there is no new DSP trial or rewritten successful runner record. The original
invalid-wrapper result and the separate corrected validation remain. This run
binds probe `514e537b…`, unchanged native `e9087e04…`, and appraisal `eef06b08…`;
the later Python-only validation fix has its own source identity. Offline
validation receipt `c59b43c3…` binds the unchanged native output to that command.

Final preservation uses probe
`9836958ad4050f3a7529b5c8356af1ff5f70086e9d7bcb7843f7d085ace1af27`,
appraisal `a641c09b919195bafcec7bec956587561d07006ba4ab20c5408e9a262a6d19ef`
and unchanged native `e9087e04…`. The frozen `hf-disturbed-long-alarm` campaign
retains the standard Gaussian 30 Hz full-2σ / 7 ms model, seed 9202,
139.2-second DATA and 12 dB-Hz, with Helper/FEC off. Both bit values receive
**5/5 correct DATA and 5/5 physical quiet pairs**: all 15 slots per row are
complete and valid, without wrong or ambiguous bits. Each row has 4,300,361
input samples; full IQ hashes are `62b04413…` and `10d69b55…` for zero and one.
The pair takes 168.644 seconds total (83.853/84.661 native), with runner receipt
`a1236d77…`. The separately frozen noise-only seed 9602 produces five complete,
valid QUIET decisions and zero DATA in 28.637 seconds; its 1,420,361-sample IQ
hash is `4b0ca1e6…` and runner receipt `0dc28530…`. These preserve one selected
statistical history and its absence control; they do not estimate a natural
outage probability or qualify weak Helper acquisition.

Software checks finish with **40/40 triad**, **28/28 GUI**, and **6/6 appraisal**
tests passing. The initial triad run passed 39/40: its short-drift test still
required unchanged affine evidence when changing-channel support was enabled.
The corrected test uses a same-input additive-disabled ablation to preserve
that equality, and separately checks the original model's retained 0.75 mass,
new OU receipts, unchanged search work and both-polarity drift recovery. No
production change was needed for that correction. After the appraisal-only
cap/receipt fixes, runner and integration tests pass again, **2/2 in 49.88 s**
(log `a6179786…`); the runner's 174 checks and native boundary preflights also
pass. Original timeout, failed assertion and wrapper-error receipts remain
alongside their dispositions. No local CPU-capacity or actual-GPU qualification
follows from these correctness checks, and no RunPod resource was created.

The coarse implementation has an existing chirp-Z route limited to a single
arrival and ray. A future broader eligibility change would need exact
two-ray/projection statistics, workspace and complete-service evidence, not
fewer public hypotheses. This work makes no such dispatch change and does not
call the observed computational growth a hardware ceiling. See the
[arithmetic contract](search-arithmetic.md).

The repaired physical case retains its constructed moving-layer history and
5 Hz atmospheric allowance; declaring 7 ms does not make it a severe plasma
reconstruction. The earlier −3 dB-Hz two-hop 0/5-per-polarity results are neither
repaired nor rerun here, and the +3 boundary retains its prior limits. These
findings do not establish a weak Each 65% sensitivity floor, all allowed joint
channel histories, or hours-/days-long natural availability. The useful result
is removal of a demonstrated weak reception loss caused by broad receiver
settings, alongside bounded recovery and preservation evidence.

A separate two-row groundwave check replays the retained 30 MHz land seed 9202
on that same baseline binary at 0 and +6 dB-Hz. At 0, all three active bits
remain QUIET; its 506,412-sample IQ hash `d0a63020…` exactly reproduces the
retained weak case. At +6, all three bits and the two quiet successors correctly
complete `e`, without wrong bits or premature completion. Path, seed and clean
signal energy are unchanged; noise energy falls by the specified 6 dB.
The two rows complete in 2.377 seconds total. This closes the missing matched
energy bracket for that static constructed episode, not a threshold estimate,
an atmospheric-drift test, or a real 30 MHz route qualification. The separate
weak 1 MHz case has no such matched bracket here. Earlier successful VLF
phase-wander and EME repairs retain their original scope and source identity.

Temporary evidence is under appraisal `local-evidence/current-two-hop-spread/`
and `local-evidence/current-persistent-reception/`. The matched repair and
preservation question is resolved: the 73,109,945-byte physical capture and
ten superseded build/progress logs are removed (73,140,617 bytes total).
Compact source/configuration/input hashes, commands and outcome receipts remain
for this current validation claim, including the original failed-check receipts
with their dispositions above. Retire those local receipts when a superseding
validation or release record no longer needs them. No private executable/source
copy was retained; compatible shared build caches remain in use.

## HF Worst 30 Hz / 7 ms follow-through — 16 November 2026

This appraisal test is the standard `hf-disturbed` fixed-grid Gaussian model,
not the changing GUI **HF Worst** realization. Each of its independent,
zero-mean proper-complex rays has a 30 Hz full 2σ width at 0/7 ms; GUI HF Worst
uses 7 ms / 30 Hz as pre-margin support and adds common wander, with Margin 3
declaring 21 ms / 90 Hz. Keep the realized spectrum separate from declared
allowance; see [HF profile distinctions](channel-models.md#distinguish-hf-stress-profiles)
and the [appraisal model definition](../_appraisal/highFidelity-triple_Robust_Modem/models.md#hf-standard-and-nonstationary-controls).

At base HEAD `54c9c` / native source `8d36…`, the old short-message binary
`f062…` requalification completes `e` with 3/3 correct DATA at 20 dB-Hz,
seed 9201; seed 9202 at 0 dB-Hz yields three QUIET slots. A separately
predeclared +12 dB-Hz test on seed 9202 still misses all three short slots
(two QUIET, one ambiguous). These different-seed/energy cases are not a paired
sensitivity threshold. A separate same-IQ control uses the same 10.4-second
shaped native synthetic bit-1 fixture (seed 37, 5,974 rows, unchanged noise)
at two fixed pre-noise aperture energy/noise-variance ratios, 128 and 32; each
calm/wide comparison uses the same IQ. At ratio 128, both conditions produce
DATA. At ratio 32, calm evidence is 22.914 (DATA), while wide evidence is 14.941
(QUIET) against 21.493; the noise-only control remains quiet. These levels are
6.0206 dB apart, but this selected aperture contrast is not a sensitivity
threshold or universal energy guarantee, and the ratios are not seconds or
message labels. See the [ratio-128 receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-worst/same-iq-calm-vs-worst-margin1.txt)
and [ratio-32 receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-worst/same-iq-calm-vs-worst-margin1-e32.txt).

The current long-bit raw baseline binds appraisal `c49d4eed…`, appraisal
source `2e501cd…`, and native `8d36…`. At C/N0 12 dB-Hz it sends five
139.2-second attempts of each polarity through one continuous 15-slot channel
and receiver per polarity (starts 0, 3, 6, 9 and 12), fixed key fingerprint
`eb846…ba1de`, seed 9202,
on a Ryzen 5 PRO 5650U CPU with H=12 / full W=24. Both polarities receive 0/5
correct bits; for each polarity, all five active slots are ambiguous and all
five quiet pairs complete. There are no wrong bits or physical message
completions. The original wrapper process was interrupted after bit zero at
130 seconds. Its valid bit-zero receipt took 93.443 seconds; bit one completed
in 94.371 seconds
and both saved receipts passed offline audit. This does not mean the original
campaign wrapper passed. See the
[source-bound reconciliation](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-worst/hf-disturbed-long-alarm-plus12-reconciliation.json)
(`b1fa3de…`) and [offline receipt audit](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-worst/hf-disturbed-long-alarm-plus12-pair-offline-validation.json)
(`09c63bb…`).
The short requalification likewise does not establish behavior over multiple
days, a population outage rate, or complete long-message reception.

Matched public scoring motivates a bounded nominal independent-gain and
common-frequency receive-model extension: at the
first long +12 dB-Hz aperture, the full-prior flat 1/16 and flat-unit models
score 4.295 and 37.514, while the public-endpoint, noise-whitened exact
observed-Gaussian reference scores 93.283 against a 21.493 presence threshold.
This exact reference is diagnostic; it cannot supply hidden phase or
channel-history truth to the receiver. Unit-only half-delay and support-only
low-power attempts remain insufficient. A nine-fixed/two-cell-block
expectation check supports the per-block unit-power normalization used by the
bounded family, but does not bound the whole-window score. Three matched-IQ
aperture checks originally motivated the nominal independent-gain/frequency
extension (mean-zero evidence 51.658, GUI-like
support 26.958 and digitally shifted +30 Hz 50.136), all above the 21.493
presence threshold. These component scores are not complete-reception evidence.
The measured candidate binds binary
`e82e8ab2a1fc36ca75fed0c012a1019dc44828bf2602869b02e8e2349118a0a0`, native
source `3ae114932087707799a4650e7c10385475008a446670b6c5b8e55b364ab7ec67`,
and appraisal source
`282c817afdb2d88f3c615034aaf4237931d3db494964c5ee73b55d27ad3d0f8f`; its
source-bound reconciliation receipt is
[here](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-worst/frequency-gain-candidate-reconciliation.json)
(SHA-256 `38acdc6677fd290721c7e81895e2835eff8fd8f2debe9eb39269c5a8e5258d36`).
These identities are the campaign's exact measured provenance; the later
test-registration update changes the current native-source hash but does not
change this already measured receiver binary or its source binding.
On regenerated continuous captures whose full-capture hashes verify identical
IQ to the seed-9202 baseline failure, both bit polarities improve from 0/5
correct to 5/5 correct. An independent seed-9601 fresh pair also gives 5/5
for each polarity. The seed-9602 noise-only control remains 5/5 QUIET while
the new family is active. These are finite characterizations of seeded
standard-Gaussian histories, not independent natural-channel trials or outage
rates; this cohort does not qualify plasma or GUI-HF-Worst channel laws. The
separate GUI preservation replay below is scoped evidence for its named case.
Candidate matched bit-zero/bit-one wall times are 108.268/110.750 s,
versus 93.443/94.371 s at baseline (+15.9%/+17.4%); reserved memory is 9.79 GB
at baseline and 9.79/9.99 GB for the candidate. A separate preservation replay
retains GUI HF-Worst A40 completion (13 correct DATA plus two quiet slots;
13.0455→13.5578 s, +3.9%) and EME30 `e` completion plus two quiet slots
(17.7581 s on candidate).

Additional same-IQ aperture checks remain component diagnostics: final
bit-one GUI-like evidence is 26.6728 and the +30-Hz case is 52.3978; for
moving bit zero, GUI D=21 scores 25.7585 with actual bounded phase-rate
support −29.83236…+29.84666 Hz. Each exceeds the 21.493 presence threshold,
but none independently proves complete reception. The earlier f0-only
candidate failed preservation and is retired; no additional delay-grid search
is implemented or needed for the observed case. At the prior checkpoint,
`./build.sh test triad` passed 38/38 in the cache-fixed run; the preceding `final-triad.log` attempt failed during
read-only ccache setup and executed zero tests. The refreshed lightweight
runner passes 173/173 in 1.144 s. The positive-delay Helper stratum passed 2/2
at that checkpoint after a combined 55.57 s attempt came too close to its 60 s
harness timeout: the split checks took 16.13 s for the original case and
36.49 s for the new late-helper-delay case. The GUI suite then passed 28/28 in
154.62 s (`final-gui.log`); appraisal tests passed 5/5 in 69.55 s, including
integration in 68.25 s (`final-appraisal-tests.log`; build receipt
`final-appraisal-build.log`). These are prior-checkpoint results, not checks of
the later receiver patch. The prior compiled appraisal binary was
`8305dfda5c783eab6c68ddb73044c195a6392577c55ca0f3d5e2bc5bcf6081c9`, with
native source `b8945c6d6a927f6b37020591f9a1d8bbdf382202b0a0a78f95fe0560352dc87c`
and appraisal source `282c817afdb2d88f3c615034aaf4237931d3db494964c5ee73b55d27ad3d0f8f`.
At that checkpoint its only delta from the measured campaign binary was
test-registration metadata, and production receiver code was unchanged; later
receiver changes supersede that statement as a description of current source.
These scoped prior-checkpoint checks did not qualify broader field sensitivity
or availability. The 38/38 triad result is the base suite; it was not rerun as
a combined expanded inventory after splitting the Helper stratum. Final-source
triad/GUI/appraisal and repeated-bit outcomes are recorded below. The added family uses
nominal epoch/clock and two public arrival endpoints;
arbitrary clock ages, all delay values and weak Helper sensitivity remain
unqualified. Recent path-scale evidence and its measurement limits are
reconciled in the [maintained HF research summary](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#hf-disturbed-scales-are-not-interchangeable).
After recording the conclusions, the scratch diffuse-block-covariance source
and executable and four obsolete preflight directories were removed. Compact
diagnostic receipts and current source pins, failures and results remain as
the source-bound record of the completed checks and unresolved channel limits.

## Final-source repeated long-alarm check — 16 November 2026

The final candidate-only run sends five separate raw one-bit emissions of each
polarity through one continuous seed-9202 standard-Gaussian channel and receiver
per polarity. Each bit is 139.2 s at 12 dB-Hz, maximum public delay 7 ms, with
starts at slots 0, 3, 6, 9 and 12 and two scored quiet opportunities after each
emission. The receiver records 5/5 correct bits for both polarities, 5/5
physical quiet-pair completions for each, and zero wrong bits, ambiguous active
slots or active quiet decisions. Both ordered episodes complete; text/FEC
completion is outside this raw-bit endpoint. This is one fixed channel/noise
history with repeated emissions, not ten independent natural-condition trials
or an outage-probability estimate.

The candidate's complete input hashes match the saved predecessor for both
polarity rows: bit 0
`62b04413a79e81b4168f6ec4cdc58bef3e3787256054ffc0aa700026d98dfe19`, bit 1
`10d69b55e72d9933ef8107860fb9c17d003ae2cca575a82038a6eae909c9d126`; each
contains 4,300,361 samples. Key fingerprint is
`eb846767d659c6bf75cb4847917247cd75cb2935ae86ff3e071cf230d26da1de`. The
saved candidate predecessor (`e82e8ab2…`) also completed 5/5 correct for each
polarity on these identical captures. This preserves its already recovered
outcome; it is not a new 0→5 repair. The pre-extension `c49d4eed…` / native
`8d36…` baseline remains the historical 0/5-correct result for both polarities,
with all active slots ambiguous and quiet pairs complete; it was not rerun.
The campaign changed only its runner budget to 240 s aggregate and
120 s per trial. Candidate runner elapsed times were 74.368/77.569 s
(152.077 s total) versus saved-predecessor runner times 108.327/110.838 s.
Native `timing.wall_seconds` were 74.303/77.500 s versus 108.268/110.746 s;
these timer fields are distinct and must not be substituted for runner elapsed
time. Because the predecessor was not rerun, these elapsed differences are not
a controlled speedup claim.

The [comparison receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-resilience-checks/hf-disturbed-long-alarm-comparison.json)
has SHA-256 `61af885b96160cf649bc85e91f801d7a2b3dc78089cb620896275b3f54ad5a11`;
the candidate campaign JSON has SHA-256
`ba9317a73b0b566f9b61eaa2e00da4b106f78a4bf5a06104ed363883c3102676` and the
runner-validated campaign hash is
`bfa5139b8e57536861b5dfd7981dcaef78664c7879fe987463e43171d1357db6`. The
current source is native `b89b2a973c264a049c83d154fd30f70b282a83fe57a8de6b37589a0c6303644c`,
appraisal `ec9f94f124cf66214d5be941d04103b9693b56e2356696886fb50114567cd93b`,
and probe binary `c7672a55e517ebe83887e5e75c5fa2d7d5acabc6dd1793ee31f87d62afbe51e2`.
The run is configured at HEAD `6ac502a2281ce23923a43915e26ee44ec408c431`
with code-diff SHA-256
`284d4a1cb3a44dad18c884e9d9f305fd4f77ceedc14a8068f532250bb782b23e`.

On the frozen source, `triad` passes 40/40 in 104.23 s, GUI passes 28/28 in
154.57 s, and the five focused appraisal groups pass 5/5 in 45.78 s. The
existing appraisal compile warning about possibly uninitialized
`spool_read_time` in `src/main.cpp:468` remains; no source change was made for
it. Source review found `ServiceTiming::cpu` initializes to optional zero and
`add()` dereferences it only when both optionals are engaged; the file is
unchanged and no uninitialized-use failure was observed. These checks and this
candidate-only repeated-bit result do not qualify complete text reception,
sustained capacity, physical outage duration, a
population success rate, or other channel laws.

Lifecycle cleanup is complete: 26 obsolete scratch files (52,670,292 bytes)
were removed, including 17 generated executables, seven obsolete private
sources/scripts and two full ranking dumps. Four thin fixtures and current
outcome/source-hash receipts remain for the exact claims above until maintained
replay integration or same-scope superseding qualification. The 1,096-row
private rank output was reduced to
[`lag-rank-private-42m.summary.txt`](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/lag-rank-private-42m.summary.txt),
which keeps the plan/work totals, top six cells per bit and strongest separated
competitors while retaining the original source digest.

## Late-clock and interior-delay probes — 16 November 2026

These checks began at base HEAD `6ac502a2281ce23923a43915e26ee44ec408c431`.
On a severe standard `hf-disturbed` 30 Hz / 7 ms episode at +12 dB-Hz, a
139.2-second bit at slot 12 / ordinal 12 reaches about 1,800 seconds of
calendar age under a declared ±150 ppm clock bound. That baseline estimated
41.8 million exact-column timing templates against a hard 30-million limit
and refused before rendering or scoring IQ. This was a conservative proxy
refusal even when the older fixed-affine lane was shared, not a new
double-counting bug. The current working source removes that absolute
30-million hard refusal: 30M remains a heuristic for the mapped route, while
the existing 120M mapped-route limit and full workspace, FIR/grid and
cancellation checks remain. Admission no longer adds a new absolute 42M,
60M or 4.29B cap. The old refusal receipt records its source context in
[`late-clock-baseline.refusal.txt`](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/late-clock-baseline.refusal.txt).

An initial private diagnostic raised only the legacy integer-template
admission limit to 42 million. At the actual 0 ppm capture it decided bit 0
DATA with log evidence 69.69057 (threshold 21.493), but cost 106.65 s at W=24
and used 1.070 GB; coarse work was 1.925 s and adaptive ordinary fixed-affine
fine scoring 104.239 s, including 16.559 s for the short model. A −100 ppm
capture was rendered and coarse-processed, then its aggregate 119.5 s timeout
expired before fine scoring completed. The first diagnostic therefore supplied
no displaced-bit decision.

A subsequent private fixed-only rerun on the same two capture hashes finishes
the pair in 78.04 s. At 0 ppm it preserves exactly the bit-0 DATA decision and
69.69057 evidence while reducing wall time 106.654→36.581 s (−65.7%); coarse
work is 1.956 s, fixed-affine fine work 34.181 s, and peak workspace remains
1.070 GB because coarse search sets the lease. The −100 ppm capture now
completes, but is AMBIGUOUS at operational evidence 7.16867 below 21.493,
taking 41.439 s. Thus omitting adaptive diagnostic work recovers useful
service on the nominal control but does not recover this displaced bit. The
fixed-only run remains a private diagnostic, not production admission or
capacity qualification; its exact revised source identity was not recorded.
Receipts
and fixture are in
[`current-hf-timing`](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/).
The evaluator-known follow-up completed on the shifted capture
`cb3b0189…`. Changing only the gain-map clock to the known −100 ppm takes
44.640 s and raises raw gain evidence from 27.11416 to 87.09030 (+59.97614
nats); the counterfactual operational score would change from 7.16867
AMBIGUOUS to 67.14461 DATA bit 0. This isolates a strong timing-nomination
effect in this one capture, but uses evaluator truth and is not operational
receiver evidence. See
[`exact-clock-private-42m.stdout.txt`](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/exact-clock-private-42m.stdout.txt).
A bounded signal-derived signed-lag proposer is now in the current production
path. Its unmodified operational result is recorded below; evaluator-known
results remain a diagnostic only.

The working source routes the ordinary fine likelihood through fixed-affine
`FineLanes` and skips its adaptive per-block lane; separate varying, drift and
coherent-gain alternatives remain unchanged. The adaptive lane remains
available on an explicit diagnostic route. Capacity and forecast consumers
require the fixed-only receipt shape (`adaptive_diagnostics=false`, adaptive
lane and score fields explicitly null, adaptive work counters zero) before
accepting an operational work receipt. The production clock pair and current
source details follow; this is still not a final merged qualification or a
capacity pass.
Focused fixed-only erasure, cold-256, two-segment and bulk-fault mode checks
pass; the native fine-session check passes in 3.89 s and the `abbreviated42`
suite passes. The updated strict fixed-only capacity/forecast consumer schema
has 38 Python test passes, including the
`fine_component_omits_varying_operational_work` guard that prevents a component
measurement omitting configured varying work from counting as complete
capacity. On the frozen final source, focused `fine_session` (3.63 s) and
`signed_gain_timing` (10.57 s) pass, triad passes 40/40, GUI passes 28/28,
appraisal passes 5/5, and the candidate-only five-attempt-per-polarity replay
passes; the replay's exact input/source comparison is recorded in the
[final-source entry](#final-source-repeated-long-alarm-check--16-november-2026).
This does not qualify complete text reception or capacity. Do not attribute the private
65.7% service reduction to the production
pair, forecast a sustained throughput gain, or treat this as late-clock
reception acceptance.

A production build with the bounded signed-lag proposer scores the original two
full-capture IQ hashes without overrides: 0 ppm (`6d279587…`) is DATA bit 0,
evidence 69.873, in 41.269 s; −100 ppm (`cb3b0189…`) is DATA bit 0, evidence
63.708, in 46.098 s. The previous private fixed-only lane was AMBIGUOUS on that same
−100 ppm capture at 7.169; its original nominal-gain diagnostics remain
bitwise unchanged in the production run. The bounded signal-derived search
uses 548 lag candidates × 13 CFO candidates over 4,185 valid blocks, ranking
in 0.303 / 0.346 s with 2.443 billion logical FFT stages and 5,027,840 bytes
of additional state. Full W=24 DSP workspace is 1,075,115,663 bytes; the two
single-bit runs total 87.384 s. The measured binary is
`d5b4e1eec0e2deb4117f385ccf9bd9974e451a0cac9358b805f8a3475ffe11eb`, linked
receiver archive `b10b1ae47164724ca3f2835525689a49102414fab86373eda600143f09e2a65e`;
the [identity manifest](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/production-clock-build.hash.txt)
and [pair receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/production-clock-pair.stdout.txt)
bind the run. The later frozen source removes the static delay third tier and
has distinct binary `8489f086…`; the earlier clock-pair build is therefore
kept as exact build-bound evidence, not silently rebound to that binary. These
two selected bit decisions do not show message completion,
long-run availability or capacity.

A separate production preflight asked for a 278.4-second bit at the same
approximately 1,800-second calendar age, slot 6 / ordinal 12, with ±150 ppm
support and 24 tiles. The earlier 256-second request produced a 266.8-second
actual interval at 1,620 seconds of age but was rejected during setup before
DSP. The 278.4-second preparation now plans and covers all 156 columns: 55.744M
shared-plus-projected and 83.644M raw rows, 6.648G planned versus 6.615G
actual bank FFT stages, and a 2,113,628,343-byte full DSP lease under the
3-GiB limit. Preparation takes 5.478 s; bounded ranking adds 4.286G logical
FFT stages and 0.542 s. This check ends at preparation/ranking, before IQ
scoring or a bit decision, so it is not a reception or capacity result. See
the [plan](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/production-double-plan.stdout.txt)
and [preparation receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-timing/production-double-preparation.stdout.txt).

**Correction: the earlier interior-delay aperture was invalid and does not
establish an in-bound delay floor.** The fixture omitted the canonical 0.25 s
public preroll: although its metadata reported a 15.75 ms second-path delay,
the capture window began at row zero while the earliest declared direct
arrival was row 480 (0.25 s later); its direct-path start was therefore before
the earliest allowed DATA start and outside the 0–10 ms arrival bounds. Its
reported +12 dB-Hz AMBIGUOUS score
(2.054 < 21.493) and +20 dB-Hz DATA score (536.514) therefore cannot be
interpreted as reception at any exact in-bound delay. Preserve them as
historical observations of that invalid setup only; the 8 dB contrast is not a
sensitivity bracket. The 5.25 ms comparison was absent from that old invalid
campaign; its later corrected result is recorded below. The old [12 dB receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/interior-delay-aperture.stdout.txt)
and [20 dB receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/interior-delay-plus8db.stdout.txt)
remain linked as invalid-setup provenance; the [origin audit](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/corrected-delay-no-interior.stdout.txt)
records original row zero, corrected row 480 and the 480–499.2 allowed range.
The corrected isolated no-third-tier pilot used
the canonical 0.25 s preroll, with direct origin row 480 inside the public
480–499.2 arrival rows, and scored DATA bit 0 at +12 dB-Hz (log-e 27.570) in
8.984 s. The final frozen source completely removes the unsupported static
`D/3, 2D/3` third tier and its tests. Its corrected canonical five-aperture
series now passes: at +12 dB-Hz and 139.2 s per bit, log-e values are 27.570
(15.75 ms, bit 0), 23.644 (5.25 ms, bit 0), 39.907 (10.5 ms, bit 0), and
22.346 (15.75 ms, bit 1), all above 21.493; true absence is QUIET. Every
capture has direct origin row 480 within public arrival rows 480–499.2, uses
the original full-D model, W=24 and a 629,011,988-byte workspace quote. The
[five-aperture receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/corrected-production-five.stdout.txt)
has SHA-256 `927f16329cf5bc2d9b1f3138fc34002d44d508165e9d74a7afb90769f601b218`,
measured binary `8489f08623ff1fb7997a898ed9a7ac6961d93e2872f376eb7351d8aa14eb134f`,
and linked receiver archive `b1eb7336c3242f081398e4ca9161bb74cb5dbb5b3c945adea2137a471a528ac6`.
These are five single-bit / null checks, not complete messages or a sensitivity
curve.

The frozen production build also passes the combined age-1,800-second case:
source ordinal 12, actual −100 ppm clock and 15.75 ms echo under public ±150 ppm
and 21 ms bounds, Gaussian 30 Hz 2σ per-ray Doppler, +12 dB-Hz, DATA bit 0 at
log-e 38.935 in 61.807 s. The matched noise-only capture is QUIET at −6.249 in
59.581 s. Both use 269,482 rows, W=24, a 1,075,126,314-byte quote, two received
maps and the full 548-lag × 13-CFO proposer; no oracle timing or delay-tier
override is used. See the [active](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/joint-production-active.stdout.txt)
and [noise](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/joint-production-noise.stdout.txt)
receipts. The compiled binary is
`8c5a58cdc467a47807b8afdf9587ebad8f4681e613039e9f2e39a74fa7ce4391`, linked
receiver archive `b1eb7336c3242f081398e4ca9161bb74cb5dbb5b3c945adea2137a471a528ac6`.
These are selected bit/null receptions, not complete-message,
population or outage-duration evidence.

The same final-source active IQ also passes a bounded common-frequency motion
replay: `BoundedRandomSpread(0,30,1,70031)` rotates the complete parent capture
with sampled Doppler from −29.83236 to +29.84666 Hz. The verified parent IQ
hash is `e1898144a9410b6eee8cad82802efac3d5361087592368e1ea9ac030dbbbbca9`;
the rotated IQ hash is
`64b2f74ff984cbba1e4d10581cc58d316cfc9ebb72fb1be90552cf3f72c75335`. The
white-noise law and full-capture energy are unchanged (maximum relative row-
energy error `6.66e-16`). At −100 ppm / 15.75 ms the receiver remains DATA bit 0
at log-e 37.8052384 versus 21.493, against 38.9354114 on unrotated IQ. It uses
W=24, a 1,075,126,314-byte quote, all 548 lag × 13 CFO hypotheses and 4,186
common-valid blocks. Receiver scoring takes 45.397 s; total process time is
47.780 s including a 2.363 s independent same-buffer coarse audit. Selected
lags 194/265 are independent same-buffer audit output only; no receiver truth
is supplied. Receipt SHA-256 is
`9c0a5a094e3e27875e56c0e90dfe427cf55acd7794f7ec34b600649fe359b001`, binary
`1b1cd40971ddc4c0f01e4553665c10b9d903925ff09edf532e8f952eface1a5a`, and
linked receiver archive
`b1eb7336c3242f081398e4ca9161bb74cb5dbb5b3c945adea2137a471a528ac6`; see the
[motion receipt](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/joint-production-motion.stdout.txt)
and [command](../_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-hf-delay/joint-production-motion.command.txt).
This is one synthetic motion preservation point, not broad Doppler or
availability qualification.

The same base-source Helper discriminator has three narrower outcomes. A
0.5-voltage Helper at 12 dB-Hz yields no usable narrow interval and restores
full DATA frequency search; the tested bit remains correct. In the stronger common-mode
case, an in-bound physical common-frequency transition from −0.95 to +1.10 Hz
finishes before the DATA portion of each 10.4-second bit. At 9 dB-Hz, both bit
values decode and Helper-plus-noise remains QUIET. The successful Helper
interval is stale at DATA time ([−1.02, −0.869] Hz, excluding the +1.10-Hz
DATA frequency), yet the receiver's broader DATA search recovers the bits. For
bit one, evidence changes 65.60→59.42 (threshold 21.493) and slot wall time
1.15→5.36 s; all arms use 24 workers and the same 359 MB workspace lease.
This demonstrates correct fallback on one constructed trajectory, with extra
work, not a general absence of Helper-induced communication cliffs or sustained
throughput evidence. The deliberate independent-cross-band control at 12 dB-Hz
also decodes both DATA arms, but it assigns distinct Helper and DATA frequency
offsets contrary to this physical fixture's common-frequency assumption; treat
it as a controlled mismatch diagnostic, not field or declared-model evidence.
No production Helper change is justified by these selected checks alone. See
the [weak-Helper receipt](../local-evidence/current-helper/baseline.log),
[independent cross-band control](../local-evidence/current-helper/canonical-helper.log),
[common-frequency pair](../local-evidence/current-helper/common-frequency.log),
and [bit-zero/quiet controls](../local-evidence/current-helper/common-frequency-controls.log).

The production fixed-only route also passed a matched three-slot preservation
replay on the common-frequency Helper fixture: bit one DATA evidence
59.4243753231, bit zero DATA 49.5220157555, and physical absence QUIET
−7.76914589889, unchanged to 12 significant digits from the adaptive
reference on identical full-capture IQ hashes. W=24 and the 359,228,115-byte
lease are unchanged. Slot times were 5.356→5.547, 5.533→5.582, and
5.559→5.590 s; these single drift-dominated slots are slightly slower, so this
is preservation evidence, not a speedup or capacity claim. Reported basis rows
fall from 30,205,728 to 15,102,864, but the former count includes both old
fixed and adaptive lanes and is not a fixed-only lane-work counter. The
[fixed-only replay receipt](../local-evidence/current-helper/fixed-only-common-frequency.log)
and [binary/archive identity](../local-evidence/current-helper/fixed-only-identity.log)
record the comparison. The bounded clock-pair and Helper checks remain finite
preservation evidence, not a general ionospheric coverage guarantee.

These checks complete the named functional groups and candidate-only replay,
but do not qualify receiver capacity. On the frozen source, triad passes 40/40,
GUI passes 28/28 and appraisal passes 5/5; the candidate-only repeated-bit
outcomes are in the [final-source entry](#final-source-repeated-long-alarm-check--16-november-2026).
The corrected delay series spans only the five named aperture/null cases; the
Helper cases likewise cover selected trajectories. Neither set qualifies
arbitrary clock age, all
channel combinations, physical outage duration or five-path field reliability.

For lifecycle, the repeated-message replay is now complete. Retain the four thin
current fixture programs—
`late-clock-production-aperture.cpp`, `corrected-interior-delay.cpp`,
`corrected-joint-clock-delay.cpp` (including motion mode), and
`local-evidence/current-helper/discriminator.cpp`—plus compact outcome and
source-hash receipts until maintained replay integration or same-scope
superseding qualification. The cleanup has removed the 17 generated
executables, seven obsolete private sources/scripts and two full ranking dumps
(26 files, 52,670,292 bytes). The 1,096-row private ranking dump was reduced to
the linked bounded rank summary, which preserves plan/work totals, top six
cells per bit, strongest separated competitors and the original source digest.

## Weak physical-channel alarm investigation — 16 November 2026

The receiver now admits a simultaneous stationary-plus-fading gain, recovering
the matched +4 dB-Hz two-hop message from 12/13 to 13/13 correct DATA and proper
completion. A separate fixed-size covariance-factor repair removes a reproduced
two-path numerical failure. Final software, repeated-alarm preservation and
fresh-noise checks complete below. The −3/+3 misses remain; this is not a
qualification of natural outage duration or all-path sensitivity.

The [frozen native matrix](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-long-alarm-matrix.json)
addresses the remaining historical two-hop counterexample on the current
waveform and receiver. Before execution it declares exactly six rows: the
139.2-seconds-per-bit `E` message at −3/+3 dB-Hz, then five separate raw-zero attempts
at each level and five separate raw-one attempts at each level. Public starts
are slots 0, 3, 6, 9 and 12, with two physical quiet opportunities after each
attempt. Each row retains one continuously evolving channel and receiver.
Only the immutable qualified physical table is shared between rows; noise,
source and receiver state restart for a matched comparison, never between
attempts within a row. The 15-slot horizon stays within the existing 2400-second
field model. The aggregate process cap is 1200 seconds, with 4,350,000 samples
per row; this is one process containing six observations, not one trial of
reliability. Planning, native sharing/schedule checks and strict receipt checks
must pass before receiver execution. Valid communication misses remain results.

The prior two-hop failures used native `dd2c9e…` and the older waveform; the
latest GUI retry results do not requalify them. Current rendering is therefore
a new source-bound comparison through the same physical construction, not an
exact-IQ replay of the historical waveform. This local planar two-hop model
uses retarded sinusoidal F-layer motion (300 m / 540 s), not stochastic storm
FM or a calibrated natural occurrence distribution. Its declared receiver has
5-Hz atmospheric support, zero clock uncertainty, and a finite arrival range;
these are distinct from the HF Worst / Each 65% GUI fixture. Physical FIR tails
must not be relabeled multipath spread. The known-waveform presence reference
remains a diagnostic with unavailable channel knowledge, not an achievable
blind-decoding floor. A persistent current miss will trigger a separately
bounded mechanism comparison and appropriate generic implementation work;
neither broader random sweeps nor threshold changes follow automatically.

### Current two-hop baseline

The six observations finish in 936.326 seconds natively, within the recorded
1199.496-second native deadline. Binary
`2b3d4ffe0bd76ce554a8a1251dcc355652146bc9ac113e0ed28a9dcad7daf137`
uses native `a497b7fa028d48f2adb29892125136fed19557db03dbddafb986750814406052`
and appraisal `c02ed4f8ccbe384b7c8d6e037c0d3f4be562ae460daa713c17e60f8ef324ac44`.
Each row has 4,300,572 samples. The qualified physical table retains its
historical digest
`ff2b3f4e40bf6fde12fa89fc733240f08038de0020f497f09adba1652f0008aa`.
Construction costs 488.038 seconds once; subsequent table bindings take
microseconds, without sharing source, noise or receiver state. Thus this test
does not repeatedly pay the approximately eight-minute unchanged field solve.

| Current source / C/N0 | Correct active bits | Active misses | Physical/codec outcome |
| --- | ---: | --- | --- |
| `E`, −3 dB-Hz | 0/13 | All QUIET | No DATA or message completion |
| `E`, +3 dB-Hz | 12/13 | Slot 12 QUIET | Premature truncated completion; codec rejects |
| Five raw-zero attempts, −3 | 0/5 | All QUIET | All five successor quiet pairs score quiet |
| Five raw-zero attempts, +3 | 5/5 | None | All five successor quiet pairs score quiet |
| Five raw-one attempts, −3 | 0/5 | All QUIET | All five successor quiet pairs score quiet |
| Five raw-one attempts, +3 | 4/5 | Slot 12 QUIET | All five successor quiet pairs score quiet |

There are no wrong DATA bits or ambiguous active windows. Scored quiet pairs
alone do not establish successful alarm reception: at the weaker level no DATA
arrives. At +3 both polarities receive the first attempt. The raw-one and text
slot-12 miss have identical operational evidence 10.76695 against 21.49338,
with correct-bit preference; clean per-window E/N0 is about 107.03. Its raw
fixed-affine wanted evidence is 18.938 before public coordinate/branch priors.
The medium model's approximately 8.6267-nat frequency-prior adjustment must be
removed before interpreting its near-noise contribution. The weaker E/N0
26.8–38.6 windows also pay substantial legitimate inference/search costs;
their oracle comparison does not by itself prove an avoidable sensitivity floor.

The original runner reports `invalid_receipt`: its nested raw child was
incorrectly passed through the text-completion validator. This is a harness
failure, distinct from the native communication misses. The corrected validator
retains all raw per-slot, quiet-pair, source, physical-table and resource checks.
Independent author and coordinator checks validate the same saved stdout,
SHA-256 `626a813393300a533f74ac9bf759849390d53a8f268c80f009634fc0bf8e6d40`,
against the recorded invocation. No native row was regenerated or rescored.
Original runner status, native bytes and the separate validation receipt remain
in `local-evidence/current-physical-alarm/two-hop-long-alarm-baseline-run/`.

A source review identifies a bounded candidate: fast fine-chip cadence currently
excludes the two extra gain-coherence states even for 139.2-second DATA, leaving
0.5 s, 2 s and infinite coherence in the fixed-affine family. A predeclared
one-row discriminator tests an 8/32-second continuation on exactly the +3 text capture,
using the existing statistics, normalized priors and two state slots. It does
not lower the threshold or use the physical reflector's Doppler as a fitted
parameter. A failure does not authorize automatic scale tuning or bank expansion.
Individual scale scores are not exposed by the operating receipt; native unit
checks must reconstruct the mixture, while the physical comparison records its
combined affine evidence and all other public branches.

The single 8/32-second discriminator completes with a strict valid receipt but
**does not repair the failure**. Binary
`1ad10eb3da2c1a0c29390dd4627e6c5a7efc147ed6b02087d372817bdec123ed`
binds native `1bd43f6dc3e4c39cf6314b521ceb28c8a5a24b156b260f53865fcc67952befa4`
and appraisal `923b031d07d5aba54bb0329b8c52c1f6215226846dc0ed9e6c2eefafa4846a82`.
The complete noisy input hash is unchanged:
`fed418cd69f1766c3bf7197984390b55f77ba9ec2e4390595a339aa4d9dd3963`.
All fifteen decisions remain unchanged. Slot 12's fixed-affine evidence moves
18.93770 → 18.86703 and operational evidence 10.76695 → 10.69628; it still
prefers the correct bit but scores quiet. Six other DATA windows gain evidence,
up to 8.49 nats, without a decision change. Thus intermediate memory can help
some windows, but this two-model family is not the missing-bit repair.
Subtracting the retained 0.75 old-family contribution from the normalized
candidate score gives joint new-family evidence 18.6190 at the target, assuming
the verified unchanged candidates/statistics; individual-scale attribution is
unavailable. No scale sweep follows this result.

The candidate takes 539.612 seconds natively: 487.203 field preparation,
7.575 source/channel rendering and 44.649 receiver control/wait. The matched
baseline recovery row takes 52.632 seconds with its table already shared;
subtracting candidate preparation gives 52.410 seconds, a descriptive single
observation rather than a speedup claim. Full W=24 remains, and the total
first-slot workspace quote stays unchanged because another stage sets its
maximum; native unit checks separately charge the additional fine posterior
state and Gaussian work. These results do not qualify production adoption or
complete receiver capacity.

The next [predeclared discriminator](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-additive-gain-discriminator.json)
replaces that unsuccessful fast-cadence extension with an additive persistent
plus OU gain, initially for one spatial basis and public DATA of at least
32 seconds. Its two fixed alternatives use the existing 0.5/2-second scales
and equal 0.5 static/0.5 dynamic prior variances, with the same normalized
0.75 old / 0.125 / 0.125 weights. The physical implementation sums fixed and
moving voltage contributions, motivating simultaneous components; the receiver
does not receive their actual amplitudes, phases or Doppler. Exact dense
covariance, erased-block, prefix and synthetic simultaneous-component checks
must precede another physical score. The original slow-cell VLF models and
unsupported multiple-spatial-basis behavior are preserved. This is a different
covariance family, not a retuned OU timescale or a fitted static subtraction.

Independent review catches a prospective numerical defect before any additive
physical score: the generic two-state information solve loses its small pivot,
then its observable posterior variance, at large finite template energy. The
candidate instead stores the observable sum and dynamic component, `[h=s+d,d]`,
and performs a scalar observation update with a positive factor construction.
This is an exact coordinate change, preserving the same prior and covariance
model. The native fine-session check passes in 2.16 seconds, including dense
proper-complex covariance parity, repeated Gram `1e18` observations, retained
tiny covariance and log determinant, an erased interval whose positive elapsed
time rounds the exponential decay to one, additive-only wanted-bit separation,
normalized mixtures, workspace and prefix/worker parity. Existing update paths
keep their original arithmetic. Physical reception remains a separate gate.

The additive discriminator is valid but **does not meet the +3 repair gate**.
Binary `14515c702562698148132f5d1daf805a2bbd0601cde2e972a324e0a328e6325d`
binds native `99abfcc76035038e861907d1cd053b4661ccdfa2ee067fdd85a8ad988478d75b`
and appraisal `0af60f15cc151f22ef8ff636ea783c17f586a3e17d5e27bea64f98894c19a06a`.
The full IQ/table digests and all public apertures match. Every wanted affine
score rises: the target moves 18.93770 → 24.77449, and operational evidence
10.76695 → 16.60373, still below 21.49338. Other active windows gain up to
15.91 nats. Correct-bit separation at the missed slot rises to 23.94 nats;
the remaining miss concerns presence, not bit ambiguity. All fifteen decisions
stay unchanged, including the premature truncated completion. Both true ending
quiet slots remain quiet. Joint additive-family target evidence, reconstructed
from the normalized mixture, is 26.1591; this does not identify either scale's
individual contribution or establish a sensitivity change in dB.

Native wall is 544.245 seconds, including 486.704 field preparation, 7.923
rendering and 49.421 receiver control/wait. Subtracting preparation gives
57.541 versus 52.632 seconds in the matched baseline recovery row, approximately
9.3% more in one observation. This is a scoped descriptive cost, not a complete
receiver-capacity measurement. At this stage the code remained a candidate
pending endpoint and preservation evidence; no automatic coherence or variance
tuning followed.

The [next bounded characterization](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/two-hop-additive-boundary.json)
freezes a +1 dB change and a matched existing-bank/additive comparison at
+4 dB-Hz. A +3 existing-bank row must first reproduce the saved baseline before
any +4 inference. The same canonical driver selects the original bank before
classification and fallback scheduling; no public receiver control or copied
DSP implementation is introduced. One qualified table serves the three rows,
and the +4 pair uses fresh sequential full-W receivers with a common sealed
IQ/validity capture and independently verified replay digests. All three may
execute within the fixed 900-second aggregate cap; offline parity failure
invalidates comparative inference. This prospectively avoids a historical-score
parser or runtime handshake. Only-additive success would demonstrate endpoint
improvement; both-success would demonstrate energy recovery without measured
separation; neither-success stops this energy extension; only-existing success
is a regression. There is no automatic +5/+6 search, and the failed +3 gate
is not relabeled successful.

The first boundary invocation stops after the +3 calibration row: the runner
mistakenly applies its ordinary 8 MiB file limit to the boundary's internal
73,109,724-byte capture, causing `SIGXFSZ` before either +4 receive. The saved
+3 checkpoint exactly matches the historical full input/table hashes, all
fifteen public apertures, bit evidence, operational scores and decisions.
This is a launch-resource defect, not a new communication result. The original
empty stdout, checkpoint, process status and failed report remain in
`local-evidence/current-physical-alarm/additive-boundary-run/`. The correction
must admit only the already planned bounded capture while retaining the
separate stdout/stderr limits; the frozen native binary and campaign do not
change for its rerun.

The resource-corrected invocation completes in 647.534 seconds with no execution
failure. Binary
`7b0bf07790d1eeba5c64a522accf8f8706c9f41350c9d88303fac8af8db5264c`
binds native `329113ec5c6bfbe73bf159c5a193e51db1bdd81a91bad4d1fc2a98837be0209b`
and appraisal `a790ef0c51fb279ef51003bd57a493c08d720a16e4ea241fc112368fbf334b97`;
the external runner is `91036edc5667fab1f029f5bb00954a7c21b214e95d136e409bfb1c3eff75dcff`.
The new +3 calibration again exactly matches all saved score/threshold,
public-aperture, decision and coordinate-work fields. Both +4 replays verify
the complete common capture
`4c1cfc29c2ccada26914c1c007f63849a58955d73991bc914ba2beff9cf25ef2`.

**At +4 the original model still receives 12/13 DATA bits; the additive model
receives 13/13 and correctly completes `E`.** Original slot 12 remains QUIET,
with operational evidence 19.19167 below 21.49338. Additive evidence is 31.92640,
and the same bit becomes correct DATA. Fixed-affine wanted evidence rises
27.36242 → 40.09716. Both genuine ending slots remain quiet; there are no wrong
bits, false DATA or ambiguous active slots. The additive result has no premature
completion and finishes at quiet slots 13/14. This is a demonstrated endpoint
gain in one fixed adverse history. It does not erase the +3 failure or estimate
a sensitivity threshold or natural outage probability.

The sequential, full-W=24 replay services take 51.191 seconds for the original
model and 44.751 seconds for the additive model, with process CPU 426.266 and
420.538 seconds respectively. Earlier sufficient evidence avoids some existing
medium/drift fallback work. These single observations show why extra Gaussian
states need not increase complete service at every signal level; they are not
a hardware-capacity or general speedup qualification. Peak reserved memory is
3,393,952,485 versus 3,440,462,565 bytes, including the charged common capture;
both admit up to fifteen owners. Shared rendering is charged only once in the
aggregate. The table preparation takes 491.463 seconds once.

The matched endpoint gain activates the predeclared preservation checks: the
already frozen six-row matrix, the changing-frequency raw pair below, and one
[fresh-noise long-window control](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/physical-long-additive-noise.json).
The matrix's repeated +3 text row is a functional replication, not a new
independent success trial. The noise control must score all fifteen slots quiet
with no DATA/message completion while actually enabling the additive family.
These bounded checks do not estimate natural outage or false-admission rates.

Completion review removes an obsolete restriction before the final preservation
run: the 32-second cutoff came from the rejected 8/32-second experiment and is
not a boundary for the retained 0.5/2-second additive dynamics. At that checkpoint,
eligibility depends on fast fine cadence, changing-channel mode and one spatial basis,
without a bit-duration cutoff. Off, two-spatial-tap and original slow VLF
behavior remain separate and unchanged.

The fixed short mechanism fixture requests 16 seconds and prepares **24-second
DATA**. Matched native fine scoring preserves the correct mixed/constant-channel
bit and the noise-only quiet result. Mixed affine evidence is approximately
179909 → 179973; noise evidence is −16.7109 → −16.9986. These deliberately
strong wanted samples demonstrate the mechanism, not short-link sensitivity.
Gaussian updates increase 8910 → 14850, with a conservative additional
106,496-byte bound and unchanged basis rendering, adaptive work and worker
allocation. Same-worker fine spans are 0.04685/0.04607 seconds, descriptive
single observations rather than a performance guarantee.

Two complete native slot scores then use the identical short mixed IQ and
identical 12-Hz common/atmospheric allowance, changing only declared delay
spread from zero to 7 ms. Both are complete, valid and correct DATA, with
operational evidence approximately 178555/178385 against 21.49. Only the
zero-spread case selects the additive family. This bright case finds no
material admission failure; it does not qualify weak two-spatial-tap behavior.
A direct-template additive submodel under wider declared bounds could reuse
existing statistics without four latent gains; it was then an unqualified
alternative, rather than a mathematically impossible one. The later
[weak same-IQ comparison and repair](#persistent-reception-investigation--17-november-2026)
qualifies that direct subset in a previously failing broad-bound case. Full
simultaneous additive dynamics for both spatial templates would require more
latent state and remain separate.
The final focused test passes in 3.34 seconds with independent source review.

Final preservation uses binary
`f062cc9b201661b2af033610ce3ce4ea1fc96dfd8e68449937479eee8068ba5b`,
native `8d36d5e9194a139f53d8cfcc0ec3f182101467142dc128d8c997c1ce6ef5747d`
and appraisal `132dcf1cc5d0602bdc664653d4e27a910eef89f6609af98072b1db8c793f98d3`.
All 38 `triad` checks pass after the duration change (87.36 seconds), including
the affected shared Robust GUI check. All 28 shared GUI checks passed before
that final eligibility change (159.09 seconds); the other 27 GUI checks are
unchanged. Five affected appraisal checks pass (60.27 seconds): channel,
GUI profile, correlated gain, normalized null and native integration. The
runner's 169 checks passed after the file-limit correction. Four strict native
plan invocations admit the six-child matrix, both random-field polarities and
the noise control with automatic H=12/W=24 and their frozen sample/time bounds.
These software/preflight passes are distinct from the physical results.

The final six-row matrix completes in 941.578 seconds externally / 941.273
seconds natively, within its 1200-second aggregate limit. All six full IQ hashes,
the qualified table and public apertures match the original baseline. The
additive family is active, including quiet windows, and the +3 text evidence
exactly reproduces the earlier additive discriminator. All six communication
outcomes remain as in the baseline table: −3 text 0/13 and both raw polarities
0/5; +3 text 12/13, raw zero 5/5 and raw one 4/5. The +3 text still truncates;
every raw successor quiet pair scores quiet. There are no wrong DATA bits,
ambiguous active windows or false DATA. These are preserved results, not a
repair of the weaker points. Row wall times are 572.465 (including 487.588
preparation), 52.535, 85.399, 70.349, 85.068 and 75.389 seconds in the declared
order. The original matrix took 936.326 seconds natively; these single aggregate
observations are not a capacity or performance-equivalence qualification.
The repeated +3 text capture remains a functional replication, not another
independent success trial.

The final fresh-noise control also completes: 65.189 seconds externally /
65.047 seconds natively, below its 180-second cap. All fifteen apertures are
complete, valid, noise-statistics-valid and QUIET while `stationary_plus_ou`
is active. There are no DATA decisions, messages or physical completions.
The independently seeded noise draw (40109) is one control, not an estimated
false-admission probability. The final changing-frequency pair below preserves
both 5/5 raw-alarm outcomes. The bounded implementation and preservation gates
are therefore complete, with the weaker communication misses retained.

Execution is local on an AMD Ryzen 5 PRO 5650U, six physical cores / twelve
logical threads, 14.95 GiB OS-visible RAM, automatic W=24, GNU 14.2.0 Release
and the native CPU backend. The matrix executes Q15 products / FP32 coarse FFTs
with FP64 fine state. No actual GPU or complete-capacity qualification follows.
No RunPod resource or spending was used for this investigation.

Temporary source-bound raw baseline, failed/corrected admission, paired-cost
and final preservation receipts remain in
`local-evidence/current-physical-alarm/` for the current qualification and
unresolved weak points. Retain them until those claims are superseded or their
release-assurance duty ends, then distill and remove them. The current comparison
and final validation binaries remain for reproducibility. Five superseded
prototype/baseline binaries, duplicate plan/manifest/log files and the retired
8/32-model trial outputs were removed after preserving the negative finding
above. Active build caches remain reusable.

### Separate two-path numerical investigation

Review of the additive candidate also identifies an independent defect in the
pre-existing ordinary two-spatial-tap Gaussian update. A direct native test with
prior covariance `0.5 I`, unchanged state transition and rank-one Gram
`2^60 [[1,1],[1,1]]` returns status 6 (`invalid_innovation`), both for zero
correlation and phase-aligned nonzero correlation. The exact innovation remains
positive definite. A one-bin fine projection can produce rank-one geometry
even for distinct physical taps; the native finite-positive noise contract
does not impose a safe upper bound on this normalized Gram.

This is a reproduced numerical weakness, not an observed cause of the weak
one-spatial-basis two-hop failures above. Retaining the tiny posterior variance
across later observations requires more than a positive pivot: a dense FP64
covariance can lose that direction. A bounded persistent square-root
representation now replaces the ordinary two-tap update, with the same four
complex state slots and no extra search. Fixed, adaptive, varying-basis, slow
VLF and medium coherent-gain callers preserve that factor between updates.
Small QR operations propagate process noise and merge the existing adaptive
mixture. Normalized scalar-mode likelihoods avoid cancellation with a nonzero
prior mean. The original one-tap and additive arithmetic is unchanged, and
the obsolete two-tap subtractive solve is removed. The dense compatibility
entry point uses the same solve internally; returning a dense covariance does
not preserve an unrepresentably thin direction between separate wrapper calls.
Operating two-tap states use the factor interface throughout. See the
[research reconciliation](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#expanded-numerical-and-mechanism-checks)
for numerical literature and its applicability limits.

Independent review also identifies rounded correlation leaking into an
unobservable direction of the stateless projected tracker. Its known-rank-zero
case now contributes zero; known-rank-one scoring uses only its measured axis.
The multi-mode path keeps supported weak directions. A phased, one-ULP
perturbation check covers the repaired helper. This is distinct from the native
high-Gram DC-projection check of persistent affine/adaptive/slow states; it is
not a high-Gram stateless physical-channel trial or a universal conditioning
qualification.

Focused `triad_fine_session` passes in 3.03 seconds with analytic repeated
high-Gram, nonzero-mean, erased/tiny-time, full-rank and projection checks;
`triad_native_receiver` passes in 26.30 seconds, including the existing
two-tap weak diffuse-fading medium-model DATA/noise checks. Independent source
review accepts both the persistent and stateless changes. Final native groups
and physical preservation remain separate checks.

Two predeclared matched ordinary HF captures preserve complete input hashes,
decisions, work counts and workspace bounds. Worst/Each65 A40 has identical
operational evidence and correct message completion; complete wall time is
12.634 → 13.704 seconds. Moderate e15 retains two DATA, one active ambiguous
slot and two quiet slots; operational evidence is identical and the largest
bit-score difference is `4.44e-16`. Its wall time is 1.710 → 1.784 seconds.
These observed approximately 8.5%/4.3% increases are not a throughput or capacity
qualification. Summed fine/medium phase spans decrease, but overlap prevents
adding those spans or using them to dismiss the complete-wall observations.
The later stateless rank-zero/one correction is outside the executed paths:
Worst has at least nine projection modes before at most one fitted line is
removed, while Moderate uses raw tracking. Thus those timings apply to the
changed ordinary two-tap work, not the unmeasured stateless edge case.

### Complementary changing-frequency baseline

A separate frozen `hf-ensemble-changing-rate` pair uses the canonical receiver
without the experimental phase supplement or refinement. Each polarity has
five separated raw attempts, 139.2-second active bits, the same key and advancing
ordinals, through one continuously evolving scalar physical field. Source/noise
seeds are 30107/40107, trajectory seed 20107 and physical offset 197 seconds,
at 0 dB-Hz. The law combines analytic eikonal evolution with seeded, bounded
quintic critical-frequency knots; it is a constructed random history, not a
measured ionospheric storm distribution.

Both polarities receive **5/5 correct bits**, with all five successor quiet
pairs scored quiet and no wrong or ambiguous active slot. The two native
observations take 58.024 and 57.366 seconds, with 4,300,338 samples each. Native
source remains `a497b7fa…`; binary
`25b7a587d5d6c6bd6d1b735ae4b9f063970a85ca76828fa1919a63e337351d38`
binds appraisal `16f4092cafabeaec84aec109596c660d7871d9ec82f8fb84acd84fe85e6cb083`.
Clean active-window E/N0 is approximately 139.7, so this is a stronger
received-energy control than even the +3 two-hop capture. Equal input C/N0
labels across different propagation models would not make matched sensitivity
comparisons. These successful retries exclude neither other random laws nor
weaker failure points.

The runner validates both native receipts and completes. Its initial derived
report incorrectly counts these raw receptions as failed text messages and
omits their raw-bit aggregate. The corrected runner passes 163 regression tests;
its separately saved `physical-random-long-alarm-baseline-derived-summary.json`
revalidates both saved receipts, reports the raw counts above, and counts zero
text successes or failures. It binds the original run/report hashes and runner
`2859143005cb08139a7ded7056f59dc1944be1f9085b7625765574e5dbee2d0e`.
Original files remain unchanged, with no regeneration or rescoring. This
reporting error does not change any native bit or quiet decision.

Final-source preservation repeats this frozen pair with the additive family
active. Both polarities again receive **5/5 correct DATA bits**, all five
successor quiet pairs complete, and no wrong/ambiguous active or false-DATA
decision appears. The runner completes both rows in 112.164 seconds; individual
native walls are 55.269/56.633 seconds. The complete IQ and public geometry
remain matched to the baseline. This is a preservation comparison of the same
constructed history, not ten new independent reliability trials or a capacity
qualification.

## Repeated single-bit channel diagnosis — 16 November 2026

The bounded nominal-epoch and midpoint-lag repairs recover **48/48 raw DATA
bits in the original matched cohorts, versus 36/48 before either repair**.
Four fixed fresh retry captures at 40/43 dB-Hz recover **24/24 DATA bits**, versus
20/24 with the nominal-epoch-only candidate. Each fresh capture still has the
same ambiguous internal quiet slot, so only five of its six quiet pairs complete.
These are raw-bit results, not 72 independent trials or completed text messages.
The midpoint family uses at most six received-data nominees, a fixed one-eighth
tracking share, and the full public lag/epoch denominator. It adds no historical
channel-path enumeration. Its existing capture/FFT extent still grows with
unresolved global clock uncertainty; only the expensive nominee count is fixed.
The detailed source identities and distinctions below remain binding, including
unqualified field incidence, outage duration and sustained receiver capacity.

The appraisal now measures raw-bit reception separately from text-codec
acceptance and browser filtering. Its first bounded discriminator emits six
contiguous copies of one bit under a fixed key, advancing the public ordinal
through one continuous channel, then scores two quiet slots. These are six
DATA slots within one emission, not six separate GUI requests with two quiet
slots between requests. The GUI regression separately exercises two individual
raw-bit transmissions and verifies their `initial` / `initial+3` ordinals,
binary display, no decoded text, and physical quiet completion.

Baseline source is HEAD `2f9b8c79`, native
`13e3fca3592d4a3ec89d083e7745c60914a00dd25f5c9452f6f0a7f51941e809`,
appraisal `98c6a9b0b97b16264dcde3e05d4e481ad2381b08aaf0e47daba879dc2ce97cbe`,
executable `644240b2d81e2b64535258d936037700facfee0fa5a4efe8c21936d16506ce06`.
Receiver algorithms are unchanged from bounded short-model competition.
The [six-case manifest](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/repeated-raw-bit-contiguous.json)
holds HF Worst / Each65 / Margin3 receiver support, clock-edge errors, key,
oscillator seed 62013 and noise seed 72013 fixed at 43 dB-Hz. Only actual
atmospheric delay, Doppler and selective effects scale by 0, 0.5 or 1, using
predeclared path seeds 62031/62032/62033. Opposite bits share a physical history.
The requested two-second fixture prepares **10.4-second DATA**, with 60-second
calendar spacing and approximately 431.903 seconds of captured time per row.
The actual-atmosphere-Off rows retain the declared oscillator effects.

| Actual atmosphere | Correct bit-0 slots / 6 | Correct bit-1 slots / 6 | Wrong public ordinals (bit 0; bit 1) |
| --- | ---: | ---: | --- |
| Off | 6 | 6 | none; none |
| Half scale | 5 | 4 | 3; 3, 4 |
| Full scale | 5 | 4 | 5; 4, 5 |
| Full scale, thermal noise removed | 5 | 5 | 5; 5 |

All eight captures execute with complete, valid ordered observations and two
final quiet slots; none has an active-slot erasure or ambiguity. Every primary
row first receives the correct bit at ordinal zero. The longest primary run
without a correct bit is two slots. Neither polarity is permanently missed in
these finite episodes, but both flip at ordinal five even without added thermal
noise. The conditional [thermal-off pair](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/repeated-raw-bit-thermal-off.json)
retains positive receiver variance `0.09622794885643632`; wrong-bit score margins
are 5480.720 and 294.193 nats. Noise removal fixes bit 1's ordinal-four error,
not ordinal five. Model support and signal distinguishability therefore need
further diagnosis; repetition is not a proof that a particular future channel
will become decodable.

The primary budget is six 120-second / 1.2-million-sample rows, followed only by
the predeclared at-most-one thermal-off row per bit at the highest failing
severity. Actual path seeds differ between severity levels, so this is not a
paired monotonic deterioration curve. Correlated slots, opposite-bit contrasts
and noise removal are not independent atmospheric trials or outage probabilities.
No population, long-bit sensitivity, five-path, field or capacity claim follows.
The [research reconciliation](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#repeated-single-bit-reception-and-channel-timescales)
keeps measured fast fading, slow Doppler change and physical propagation loss
separate.

At this initial checkpoint, 65 native GUI-profile/source checks, four parser
guards, 153 Python runner checks and rebuilt `gui_robust_live` (6.27 s) pass.
Operational receipts remain in appraisal
`local-evidence/current-production-repair/` for the matched repair claims below.
The temporary baseline executable and resolved private diagnostic payloads were
removed after comparison and review; their findings and source identities remain
here. Final checks and the remaining evidence lifecycle are recorded below.

The primary cohort took 42.248 seconds of runner wall time; the conditional
pair took 13.330 seconds on the local CPU, with `H=12`, `W=24`. Per-row final
commit process-CPU snapshots are 65.907/69.564, 70.931/70.068 and 70.527/70.362
seconds for the three primary severity pairs, then 63.349/65.944 seconds for
the thermal-off pair. They are cumulative snapshots, not sums over slots or
isolated DSP timings. Peak admitted memory across the primary rows is
1,283,410,372 bytes. These small replays fit local testing; no RunPod resource
or spending was used. The primary manifest file SHA-256 is
`a176079d36ffa8130fe0d227215a543624bc6174c2b8426d829ccd303a33138f`;
the thermal-off file SHA-256 is
`f8c4d5fef7b530cd7a29c1c95e5d2e1207f8fd3f87a8c6ca5bbf8b8531008b6d`.
Runner campaign hashes refer to normalized JSON and are distinct from these
file hashes.

A subsequent optional support diagnostic compares the raw tracker's
`{0,D/2}` delay pair with the public selective geometry `{0,d,2d}`, where
`d=1/(3*carrier_spacing)`. These are different geometries, not nested spans.
Both use the same operational timing candidates and frequency states, one
fixed timing coordinate for the aperture, and blockwise frequency maximization.
Pivoted, reorthogonalized projection checks agree with direct signed-frequency
residuals; a native injected-third-column regression verifies rank, recovery
and unchanged ordinary likelihood/work receipts. The two selected ordinal-five
thermal-off replays retain exact full-input hashes and every ordinary decision
and bit-evidence value. The diagnostic does not participate in decisions.

The correct-minus-wrong captured-energy margins are **−516.865 → +151.618**
for transmitted zero and **−25.288 → −131.931** for transmitted one. Units are
summed squared complex samples, not likelihood or dB. All relevant rank bounds
collapse to rank two/three on observed blocks. The three-delay model therefore
does not supply a generic repair. Ordinary pre-line scores minus the retained
line evidence equal the final operational scores exactly for both polarities;
the line-conditioned alternative is not the decisive branch here. Timing
support remains an open discriminator, and no operational three-tap model is
promoted from these observations.

This diagnostic executable is
`4e0462a56524b5ec714cd41e86587f28e946cc5fc05a44ba6dfc5cb6a453de46`,
native `e7146d639a7a18d494827049f8664d51b9fcce97d1802f17420a88643e3315ae`,
appraisal `6bbf0da3c7cb8cd0ac006e2a93868468210ddbd5edfb6e7daeca6fd219089b76`.
The two-row diagnostic manifest file SHA-256 is
`d559007576b1db321589f36bc7b91bce6474400914ebbf0b3fd2a48bef72ffbd`;
both executions validate in 13.858 seconds total. The expanded runner suite
passes 155 checks and rebuilt `triad_fine_session` passes in 1.69 seconds.

The follow-up timing witness preserves those same input samples and all eight
ordinary decisions/evidence vectors. Neither ordinary shortlist contains epoch
zero (25 and 22 coordinates). Across **all 54 existing public epoch-zero cells**
(nine clocks × six arrival nodes), the original two-tap model gives
correct-minus-wrong energy margins of **+6644.102 / +5679.244** for transmitted
zero/one. Both wanted maxima use the same legal public point:
`clock=+75.00375 ppm, arrival=-2.5 ms, epoch offset=0`.
The separate private exact point (`100.00500025 ppm`, `1.625798208 ms`)
improves those margins to +7798.098/+6895.904, but is not needed to expose useful
public support. The three-tap model gives smaller separation in both cases.
These energy witnesses motivate better timing nomination, not an operational
truth-fed detector or unpenalized projection score.

The [separate-attempt manifest](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/repeated-single-bit-attempts.json)
then emits six single bits at public slots `0,3,6,9,12,15`, with two scored quiet
slots after every attempt. All six sources share one evolving channel and
receiver; clocks, key, path seed and noise seed match the full-severity baseline.
Over 1,032.083 seconds / 1,981,599 samples per capture, wanted zero receives
`001110` and wanted one receives `110001`: each succeeds 3/6, with three
consecutive failed attempts at slots 6, 9 and 12. Both have six successful
physical quiet-pair completions, ten internal quiet slots and two terminal
quiet slots. Raw-bit success is distinct from those completion counts.
Last-slot cumulative process CPU is 166.069/166.958 seconds; row wall times are
15.842/16.028 seconds, still `W=24`. Repeated attempts sometimes succeed, but
the three-attempt error runs and public timing-support evidence remain
actionable receiver weaknesses.

The combined witness/retry binary is
`afe87ee3f280d0245d002fab5c6234590ae6ae2d1d5b9235ac049ae560965d71`,
appraisal `83b016e457691cc58be59144a93ff4a921708be1883c415a8295f1f634f2a513`,
with the same diagnostic native source `e7146d63…3315ae` above. The witness
manifest file hash is `4357bfcead95499a381358b79bac4d1687c41e1e041df29195060bcc7921cdcf`;
the separate-attempt hash is
`35a6a4849c85cb178b2438c39920806afd1ff0beae38f18a0e2cf9511ee548bb`.
Each original runner stops after row zero due to a validator defect: the
witness work bound was looked up at the wrong nesting level; the separate-mode
receipt incorrectly inherited the contiguous mode's 1.2-million-sample cap.
Both native receipts are complete. Runner-only corrections
validate them offline; explicit continuations execute only each original
second row. No native row-zero rerun or replacement seed occurs. All 158 runner
checks pass afterward, including the separate-mode cap boundary. The original
separate-mode invalid-receipt record remains as setup-failure evidence; the
resolved private witness payload was later removed with its diagnostic code.

The first bounded production candidate retains the old shortlist and adds at
most eight nominal-epoch cells: the four existing intermediate public clock
fractions crossed with both public arrival endpoints. Zero clock/arrival widths
deduplicate naturally. It applies to admitted unsegmented raw short tracking
with Helper off; it has no elapsed-age or nonzero-clock activation threshold.
Existing public-atom prior mass and the full worker budget remain unchanged.
On the exact six contiguous captures, correct bits improve **30/36 → 36/36**.
On the separate-attempt pair, each improves **3/6 → 4/6**: zero receives
`000110`, one `111100`. All quiet slots and six physical completions per retry
episode remain correct. The latter result includes a newly wrong bit-one attempt
at slot 15; the remaining shared error at slot 12 motivates a late timing-support
check rather than a claim that retry blind spots are eliminated.

All eight full-input hashes, sample counts and GUI profiles match their baseline.
The candidate uses binary
`e8293cacaed73c2ca228d90db94866ddc64dac664a4cc4701c713db1eb90725e`,
native `6725663be3d01815998c107d7b4d88201edffba67b7d3c66a36739a9b943ee1e`,
appraisal `95ed73435923d33b52748d69cec42ae445b12fdf056a6b7fc5cec29c10a1ad8e`.
The unchanged manifests above complete in 41.549 and 32.836 seconds of runner
wall time. Separate-attempt receipt wall times are 15.961/16.575 seconds,
final cumulative process CPU 169.70/173.45 seconds, versus baseline
15.842/16.028 and 166.069/166.958. Varying basis work grows about 6.6–10.0%
for contiguous captures and 9.1–9.2% for retries. Peak reserved retry memory
changes from about 0.886 GB to 1.017–1.032 GB as owner overlap changes; this is
distinct from the small added per-slot coordinate workspace. These abbreviated
whole-capture observations are not a sustained receiver capacity calibration.

The late separate-attempt thermal-off discriminator retains the same six-attempt
calendar and examines slot 12. Both operational decisions remain wrong, by
2457.035 and 1362.868 nats. The complete 54-cell nominal-epoch bank's best wanted
two-tap energies (11373.060 / 11109.412) remain below the current bank's best
wrong-bit energies (11581.477 / 11265.209). At the evaluator-only exact direct
timing, wanted energies instead reach 18572.219 / 18355.212. This distinguishes
an age-dependent clock-resolution gap from only a missing nominal epoch: adding
more of the same nine-clock cells is insufficient. The physical clock map uses
elapsed calendar time while the clock lattice and physical arrival interval stay
fixed. At 720 seconds, a 25-ppm mismatch accumulates about 18 ms, exceeding this
fixture's ±2.5-ms physical-arrival allowance. A local timing coordinate must
preserve that accumulated uncertainty, not reset the clock at each retry.

Both late receipts are ordered valid with 4/6 correct and all six quiet pairs.
The two-row manifest SHA-256 is
`20fcef58f438e5149082f717143052ac02c1e4def4ec9224d657756c791fd650`,
binary `6620fa727e7e91477f2d7f473513dde474af9883ede7aac06a30799248e49f0b`,
native `499af037c05087b1ea9060032b474daeb38bd8c1a33e58928360bf4a82a4c877`,
appraisal `69f54f0781fbe2603df9e6bcd7e721d7a5d05c73616761abf04c4ede551ba9f4`.
The native difference from the preceding candidate is optional diagnostic
workspace accounting. The 159-check runner suite passes. These private timing
energies diagnose the support gap; they never enter receiver decisions.

The next public-data discriminator reuses clock-zero differential spectra and
reads a bounded signed-lag interval, with analytic finite template support and
explicit correction for the mean-subtraction halo. Each retained peak agrees
with a direct finite correlation. Five/four data-selected timing nominees restore
two-tap correct-minus-wrong energy margins of **+6720.071 / +6590.763** on the
same two apertures. The strongest nominal-epoch point in both is
`98.204064 ppm, arrival=0`, inferred from the received samples; the private clock
does not select it. Each pass scans 282 public lags in 26 reused inverse
transforms, verifies 52 finite dots and quotes 277,840 extra bytes at `W=24`.
All 18 ordinary decisions, evidence vectors, work counts and full-input hashes
remain identical to the preceding thermal-off captures. This supports a
separately normalized public timing family; projection energy alone remains
insufficient for production admission.

The midpoint discriminator uses binary
`56f58178f6fe09e74f572bc09ad6245b7b86b28ca6c15bdc6dc0ce7233e0403b`,
native `173a001083a7b6e9f8b89a21537d7068a5462ec889ac1a49c903617a948ca3ea`,
appraisal `8cfbc4b93db961f87c1ad3211a3077f0c65ea552506d5d6154fd63c19d342eaa`,
manifest `275a723803128443d4b643dc2ba1eb0ef6fcbed3679a46441f0e510cd0caa726`.
A repeated parent-versus-child bound-field validator error stops its runner
after the first valid native receipt. The corrected validator and independent
review accept that preserved receipt offline; a continuation runs only the
original second row. Both actual native schemas are checked afterward, with no
replacement input or repeated first capture.

### Final bounded repair and matched preservation

Production keeps the nominal-epoch supplement and adds at most six
received-data midpoint-lag nominees to the short tracker. The old family keeps
seven-eighths of its mass; the new family has one-eighth with a denominator of
all `13 × public_lag_count` cells, including unselected cells. Shared coordinates
execute once but retain both families' mass. Existing public hypotheses and
atom weights within the old family remain intact. Direct finite-dot checks
cover signed FFT correlation, mean-subtraction correction and both FP32/FP64;
an independent linear-sum oracle checks the fine mixture normalization.
Memory admission includes the enlarged coordinate bank and lag metadata.
There is no private timing input, history enumeration, arbitrary age switch or
experimental lag cap in the operating receiver. Public capture/FFT support
still grows with accumulated clock uncertainty, so the fixed nominee bound is
not a constant-cost claim at arbitrary calendar age.

The twelve operational comparison captures use executable
`71b48baf1ff45aef736f430688d6b59305d8f957c89fc46153454a67cd7a64a8`,
native `0e6a4f7807804042058f5bfe6800e29b666a5320d0bdee68baa98644313d0644`,
appraisal `7d663d947eb6bdfe270bed29563a2fced62a4984f8793b25c12ff64230c7d849`.
The original six contiguous rows receive **36/36** correct bits; the original
two separated rows receive **12/12**, with all six quiet pairs per row.
The four fresh separated captures hold the key and oscillator seed fixed,
change path/noise to 62034/72014, and compare both bits at 40/43 dB-Hz. All
**24/24** DATA bits are correct, versus 20/24 for the nominal-only candidate.
The fresh two-row manifests have file hashes
`b87a9a1f9ca7e99550f0580ffeefe3e268b3b8f6f63c8c50766215f1662a6b8a`
and `106eae5a9e0040779c1037ce029ca9ab043934e51a603b528ebee6ca17e04ce9`.
Each is capped at 120 seconds per row / 240 seconds total; there is no seed
replacement or expanded population campaign.

All four fresh captures retain ambiguous quiet slot 8: nine of ten internal
quiet slots and both final quiet slots pass, giving five of six complete quiet
pairs. Their first two attempts already succeed. The reason is
`energy_in_public_varying_quiet_aperture`; ordinary bit evidence is about
−6.56, and the fixed-band quiet energy 3194.945 is below its 3209.125 threshold,
but the wider public-aperture energy guard rejects quiet. This is one shared
noise history, not four independent failures or an all-attempt reception blind
spot. No quiet threshold was relaxed. The runner's initial implication that
ordered-valid observations required every quiet classification to succeed was
removed; exact counts still validate. Its preserved first native receipt was
accepted offline, and only the remaining three original rows were executed.
Raw bit publication in PatternSteps remains distinct from completed browser
records, text decoding and operator filtering.

Relative to nominal-only production, varying basis work grows about 4.69–5.29%
in the contiguous rows and 5.66–5.68% in the separated rows. Aggregate app wall
times are 40.615→41.439 seconds and 32.537→32.983 seconds respectively. The
twelve-row runner total is about 142.263 seconds; maximum quoted slot workspace
is 165,394,356 bytes. These single abbreviated observations on the Ryzen 5 PRO
5650U, `H=12/W=24`, neither qualify sustained capacity nor quantify sensitivity
loss. No GPU executed and no RunPod resource was rented.

The final implementation also carries the midpoint family through supported
Helper-projected DATA preparation. The late reception check uses sequential
fallback; overlap is separately checked for preparation and admission parity.
The late 720-second valid-band regression recovers both bits, with signed
wanted margins about 63764.4/62421.0; pure Helper plus noise remains QUIET.
The original auxiliary-frequency fixture crossed Nyquist and was corrected
from 1000 to 970 Hz before drawing a supported-projection conclusion. The
late case has insufficient guaranteed Helper coarse support and therefore
exercises sequential fallback. A separate narrowed-assistance callback checks
overlap admission, preparation and identical nominees. This does not qualify
new unknown-calendar Helper acquisition. Unsupported mapped layouts retain
explicit fallback; compatible compiled bulk routes are not actual GPU evidence.

Final cleaned-source executable is
`51ff9fac8d76a4d2d46d56e154cabb7e1b3f6994ce5148254d530d8714aa74ab`,
native `a497b7fa028d48f2adb29892125136fed19557db03dbddafb986750814406052`,
appraisal `447361e7a2019ebb2d4e784da3b7e51110286151b626fa3f14b1fbd270c1c244`.
Eight frozen final-source captures check preservation and the remaining known
HF failures; all corresponding full-IQ hashes and GUI profiles match:

| Capture | Earlier source-bound result | Final result |
| --- | --- | --- |
| Original separated bit 1 | Midpoint candidate: 6/6 correct, six quiet pairs | Exact equality of all 18 decisions, evidence, work and midpoint metadata |
| EME 30 dB-Hz `e`, HF 40/20 dB-Hz `e` | All three correctly complete on native `13e3fca…` | All three remain correct and physically complete |
| HF 40 dB-Hz `A`, 62003/72003 | Four wrong bits; incorrect `_` accepted | Zero wrong bits; correct `A`, physically complete |
| HF 40 dB-Hz `A`, 62013/72013 | Eight wrong bits; incorrect `wan` accepted | Zero wrong bits; correct `A`, physically complete |
| Same HF history, 43 dB-Hz | Nine wrong bits; codec rejects | Zero wrong bits; correct `A`, physically complete |
| Matching noise control | 15/15 QUIET; no DATA/message | Preserved exactly at the decision level |

The final separated-bit manifest hash is
`2e0e1261741b5a88ed3ba771414938cfb33595a721bab4e46c44894e9f803392`;
the wanted four-row manifest remains `f2cac73b…206070`; the noise manifest is
`e64c4e2ff430dbb6ed97583fcd821d5a9b5842c7e249e6b9f9b130f388bc88b0`.
The final two-row A40/A43 manifest is
`64247fb364733f7690a5e1568533aed94eb571d027105a8c5b263716166f924f`.
Its full-IQ hashes are respectively
`c89d77dd0b8ae9ba90412d18fa04d354c81496d9c315c5ded3a496601d3836c3`
and `e43e8e23427a0db3c0df370a4c09980f09054c0d50a3b3373e40f0f77ee9943d`.
Those two final app wall times are 12.660/13.520 seconds, versus
13.871/13.858 on native `13e3fca…`; cumulative CPU is 129.414/134.180 versus
132.866/133.201 seconds. Whole-capture timing is not a service-capacity result.

A final four-score discriminator checks both bits with a ±30-Hz echo at 7-ms
delay, equal half-power rays, fixed 0.4-radian phase, zero common offset and
clock/arrival error. At 497 Hz it uses 5974 samples per score, no added thermal
noise and positive receiver variance 0.25. Both signs receive both bits; wanted
margins are 14025.124/4316.854 for bit zero and 14388.533/5041.581 for bit one
(positive/negative echo). All four finish in 0.28033 seconds summed native wall
time within the predeclared 60-second cap. Negative-sign evidence is lower;
this is not a measured dB penalty, weak-ray qualification or proof for all
independently changing rays. The temporary discriminator was removed after
review, preserving the result here.

Final checks: `CCACHE_DISABLE=1 ./build.sh test triad` passes **39/39** in
89.58 seconds. The six appraisal scenario/profile/public-receiver/admitted-
evidence/integration/runner targets pass **6/6** in 71.71 seconds. A subsequent
report-only correction replaces an incorrectly hard-coded “36 source apertures”
sentence with “these source apertures”; **155/155** runner checks pass in
1.110 seconds. Current runner SHA-256 is
`e29756691535cdec5e326545279f7ece05681fc71c0699b6b2931481563372dc`.
The same derived wording is corrected in 40 retained run/report summaries;
native stdout, IQ receipts and their capture source identities are unchanged.

Resolved selective-span, private exact-timing and midpoint-witness APIs,
campaigns and tests are removed. Three pinned temporary binaries and obsolete
private payloads (15 owned paths, 20,982,202 bytes) were deleted after their
findings were distilled here. Retained operational baseline/candidate/final
receipts and frozen inputs support the current repair claims and original
runner failures; remove redundant payloads when a later qualified source
supersedes those claims. Useful native regression tests and compatible build
caches remain. No sustained CPU calibration, new actual-device GPU measurement,
five-path sensitivity floor, field failure probability or multi-day outage
bound is established by these checks.

## Bounded short-model competition — 16 November 2026

This section records the preceding source. Its four-error HF `A` and separate
eight/nine-error A40/A43 outcomes are repaired by the later timing work above;
the original observations and cost limits remain unchanged.

Starting from `b00f9371105220a87f742922928866e97a9ba276`, the normal native
receiver compares every admitted short-block alternative even after strong
fixed-affine DATA evidence. Strong signal-versus-noise evidence does not prove
that the first model selects the correct bit under a changing channel. The
evaluation prioritizes useful reception across natural deterioration and
complete service cost, allowing measured modest sensitivity, compute or error
tradeoffs. The earlier experiments' pointwise no-new-error criterion is not a
general acceptance requirement. Raw bits, decoded content, physical completion
and operator-visible records remain separately reported.

The raw short frequency tracker also uses up to eight public quarter-clock
nominations when Helper is Off and segmented timing is unavailable. These
points already belong to the public prior; every original coordinate remains,
and the total fits the existing 32-coordinate workspace bound. Admitted
line-conditioned short evidence participates too. Medium and whole-slot drift
models still run when the resulting decision needs them; the explicit
compare-all diagnostic can force them. A profile without a short plan follows
the previous path exactly, including the slow VLF gain repair. This avoids
forcing costly long-duration searches after an already successful slow-model
decision. Public coverage, fixed model weights, full `W=2*H`, memory admission
and physical quiet completion are unchanged.

### Matched current-source discriminator

The maintained [four-row discriminator](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/bounded-short-competition.json), SHA256
`f2cac73b90991756ca271d55ad78613936350bb52bec203c65dcc03201006070`
was frozen before execution, with 120 s per row/480 s aggregate limits. All
four complete in 39.848 s with exact baseline full-IQ hashes. Three deliver
the correct complete message; the fresh-`A` case remains a communication
failure despite fewer wrong bits.

| Frozen wanted episode | Affine-first baseline | Bounded competition | Wall, baseline→candidate (s) | Candidate Guarded visibility |
|---|---|---|---:|---|
| EME disk, 30 dB-Hz, seed 9501 | One wrong bit, wrong content accepted | Exact `e`; repaired third bit | 12.847→16.417 | 3/3 DATA bits pass; visible |
| HF Worst/Each 65%, 40 dB-Hz, seed 62001/72001 | One wrong bit, wrong content accepted | Exact `e`; repaired third bit | 3.800→4.492 | 3/3; visible |
| Same HF episode, 20 dB-Hz | Exact `e` | Exact `e` retained | 3.813→4.339 | 3/3; visible |
| Fresh HF `A`, 40 dB-Hz, seed 62003/72003 | Six wrong bits; codec rejects | Four wrong bits; incorrect `_` accepted | 11.378→14.048 | 6/13; hidden |

The added work costs approximately 14–28% in these single complete replay
observations. These are not speed confidence bounds or receiver capacity.
All active DATA slots report drift as `earlier_evidence_sufficient`: the
bounded short comparison supplies the HF/EME benefit without forcing their
longer searches. Full slot workspace is unchanged: 200,933,388 bytes for EME,
164,794,413 for the two HF `e` cases, and 165,394,356 for `A`. The measured
Ryzen 5 PRO 5650U uses H=12/W=24; aggregate reservations vary with live owners.

The appraiser now serializes existing native message-level admission counts
for all six browser tiers. No extra DSP, filtering, decoding or message change
is performed to obtain them. Guarded hides the incorrect `A` record above;
that does not recover its bits. Conversely, Strict hides this correctly decoded
EME record (0/3 pass), while Guarded retains it. Displayed approximate HF dB
costs remain scoped estimates, not universal costs in every changing channel.
Unfiltered content remains available and the GUI default is unchanged.

### Fresh histories, energy contrasts and preservation

A separate maintained [six-row holdout manifest](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/bounded-short-competition-holdouts.json), SHA256
`69604e19584b4bb5190beafebfb47acf592d21763d730a27b3ea18a48c8a863e`
uses three new constructed channel histories, each at two levels separated by
3 dB: HF `e` seed 62011/noise 72011 at 20/23 dB-Hz, HF `A` seed
62013/72013 at 40/43, and EME `e` seed 9511 at 30/33. Each binary has a
120 s per-row/720 s aggregate cap. Corresponding baseline/candidate full-IQ
hashes match exactly. Energy pairs share a history and are not independent
communication trials; this is three histories, not a population estimate.

Both versions correctly complete **four of six** rows: both HF `e` and both
EME rows. No previously correct message is lost in this cohort. The fresh HF
`A` remains failed and its bit errors worsen: seven→eight at 40 dB-Hz and
seven→nine at 43. At 40 the candidate accepts incorrect `wan` rather than
rejecting; at 43 both reject. Guarded has 3/13 passing DATA bits and hides both
candidate records. The extra 3 dB does not recover this history. This is an
unresolved bright-channel error pattern, **not** evidence of a small,
energy-recoverable sensitivity penalty. The improvement on the earlier `A`
history must not be generalized to every payload placement or channel history.

The six-row elapsed totals are 59.683→70.552 s. Complete wall and CPU work
are different: the final committed-slot cumulative process CPU snapshots show
4.1%/11.1% increases for HF `e`, 31.9%/32.0% for HF `A`, and 67.1%/68.9%
for EME at the paired levels. These snapshots include common source/channel
generation and startup, and exclude later finalization; they are neither
isolated DSP costs nor capacity results. They must not be summed across slots.
The 18.2% aggregate elapsed increase is therefore not a universal compute-cost
estimate. Slot workspace and fine candidate counts remain unchanged; aggregate
reservations/owner overlap vary. The additional short comparisons are paid
work, retained for the demonstrated reception benefits rather than advertised
as a throughput optimization.

The separately frozen preservation manifest
`e8a40f6ee17d803a69b7c74eb2cf0070d58f9ad9cdd8e81c0568d1af48994581`
completes all three rows in 166.082 s, within 120/120/240 s row caps and
480 s total. The existing HF `A` noise control scores all 15 slots QUIET with
zero false DATA. The bright wrong-key case still produces three false DATA
and an accepted space; Guarded and stronger tiers have 0/3 passing bits and
hide it. This is useful operator mitigation in the tested case, not a change
to reception or a universal interference-rate claim.

The weak VLF phase-wander message retains its exact
`9140d5488776287c0a6e5ed063f72425d05189832905a89a050f9fd4836358ee`
input hash and all five earlier slot decisions, bit evidence, basis work,
fine candidate counts and gain scales. Its 64,806,069 samples still deliver
three correct DATA bits and two physical QUIET slots at approximately
−18.43 dB-Hz. Every slot reports no available short plan, zero varying work
and 364.756/1459.025 s slow coherence. It finishes in 146.748 s with the
unchanged 952,574,517-byte full slot quote; no added short search is imposed
on this long, weak profile. Its Guarded and all deeper-tier counts are 3/3.

One final fixed diagnostic reruns only fresh `A` at the already declared
43 dB-Hz with full admitted-model comparison. Its complete input
`e43e8e23427a0db3c0df370a4c09980f09054c0d50a3b3373e40f0f77ee9943d`
and all 15 decisions and bit-evidence pairs match the bounded policy exactly:
nine wrong DATA bits, codec rejection, and two scored quiet slots. Full comparison
executes drift on all 15 slots (6,853 frequency bins and 250–256 slopes), rather
than only the ending quiet slots; coherent gain remains unavailable under its
center-grid work guard. It takes 17.672 rather than 13.992 s. Aggregate peak
reservation rises from 813,613,112 to 1,260,245,972 bytes with three→five live
owners, while per-slot quotes and W24 remain unchanged. This observed overlap
is not a universal memory penalty. No score change or reception benefit supports
forcing these additional searches in this case; the residual failure persists
across the implemented alternatives. The one-row diagnostic respects its
120 s/12-million-sample caps and adds no new SNR or seed.

These results bind native source
`13e3fca3592d4a3ec89d083e7745c60914a00dd25f5c9452f6f0a7f51941e809`,
appraiser source
`a3408a2c3fbf6735906b3642f56348f07c182468ff6eb097121df46d02cd7595`
and binary
`f919e5b6b39328f76129d429d3a686d8f95a33701372d01502c369b9264e13d3`.
The matched discriminator and subsequent bounded qualification receipts are
temporarily retained in
`_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-production-repair/`
for this default-policy comparison and its unresolved wanted-data failures.
Remove the temporary baseline executable after paired comparisons; remove
redundant raw receipts when their reproduction/validation purpose ends.

Software validation: `./build.sh test triad` passes **38/38** (79.48 s),
including the shared Robust GUI integration. The expanded focused native check
also passes (5.86 s): both actually undisturbed DATA bits and thermal-only quiet
retain their complete physical observations when only the receiver allowance
is widened to 8 Hz, with real short-model work and deferred drift on DATA.
The six affected appraisal checks pass. Their first run was **5/6** because an
old assertion required the former affine early exit. Its replacement preserves
the original grid, bit, quiet, cancellation and workspace checks and verifies
the new short/long scheduling distinction; all 24 assertions pass in 1.64 s.
The other five checks, including integration, had already passed and were not
repeated without a source change.

That final test-only edit leaves native source `13e3fca3…` unchanged. The
appraiser's test-inventory fingerprint becomes
`ca05788ff0fbfd3ca6971e04dd79d4d228db9043f40d500fbdae13cc36e0d223`
and rebuilt binary
`02fcffac259c9ab90e3407d0aaf77cfb43cfa0aecea522e72aee1d2c18c9c783`.
No numerical receiver change follows the frozen comparisons. Documentation
links and `git diff --check` pass. These finite cases support the bounded
default improvement, not universal bit correctness, field outage probability,
or complete-receiver capacity. No RunPod resource was used.

## Slow gain coherence and current appraisal reconciliation — 16 November 2026

The baseline is `f739158ec591f748b18dead768061cbd23f09163`, native source
`fc5b4c596aff3866e03a9f2b4c506bec2da3b4eff0d164adcdfcaf61ffade1e6`,
waveform `independent-apsk-v2-carrier-v1`. Replaying the historical weak VLF
first-slot pair produces correct DATA in both cases; the older waveform's
first-slot miss must not be presented as a current failure. A predeclared
complete-message extension at the same level and seed does fail: the static
message delivers `e`, while the two-hour phase wander loses its third DATA bit
to QUIET, prematurely completes and rejects the content. The correct-bit gap
is 24.408 nats, but presence is 16.729 against the unchanged 21.493 threshold.

The native correction adds two slow Gaussian gain posteriors to eligible
fixed-affine coordinates, reusing their existing projected correlations and
preserving the original three models. It neither enumerates frequency histories
nor changes the full public search, noise level, threshold, codec or physical
completion rule. The [model and research limits](triad-stochastic.md#nonstationary-channel-research-reconciliation)
distinguish this fixed-prior alternative from an inferred physical channel law.
The maintained [repair pair](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/vlf-slow-gain-repair.json)
and [preservation cohort](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/vlf-slow-gain-preservation.json)
retain the bounded reproduction inputs.

The first paired candidate uses native source
`bd55c160781c70bdbc0f39a4858f803b8219360a818433d6ce077a93233690e3`
and appraiser binary
`5744a903c017ab907babb7c678c952f4e38b6c9cd1030b55ce5f7345c35c51c0`.
Both complete wanted messages now deliver `e`, with three correct DATA bits
and two scored quiet slots, without erasures, wrong DATA or premature completion.
The previously missed slot's presence rises to 39.350. The wandering case
admits coherence times 364.756 and 1459.025 seconds; the static Off case adds
no models. Complete input hashes agree across baseline and candidate:
`d36dbe175c651b535de47e47f2d6a4d7005c6e643ad2e09719b1529946933640`
(static) and
`9140d5488776287c0a6e5ed063f72425d05189832905a89a050f9fd4836358ee`
(wander). Each uses the uncompressed approximately 9.4-hour native capture,
60-second fine-cell cadence and approximately −18.43 dB-Hz reference level.
This is a constructed phase/antenna case, not calibrated installation sensitivity.
Its conditional field/noise coordinates do not establish operation at a specified
transmitter power or path loss, or a bound on multi-day atmospheric outages.

On the local Ryzen 5 PRO 5650U, H=12/W=24, descriptive complete wall times are
65.52→66.79 s (static) and 155.54→136.85 s (wander). These single matched
observations are not a speedup confidence interval or capacity forecast.
Each retains 55 coarse frequency points, three arrival points, 26 keyed
workspaces, four fine candidates and identical basis-row work. Admitted slot
workspace remains 548,341,717 bytes (static) and 952,574,517 bytes (wander),
because the added small posterior state is below the dominant stage bound.
Aggregate peak reservation depends on overlap: static uses two→three owners
and 1,765,435,785→2,294,934,873 bytes; wander uses three→two owners and
3,529,485,481→2,582,240,841 bytes. Reservations are not measured RSS, and owner
changes are not a portable limit. Ending quiet slots still pay complete search.

### Fixed preservation controls

After the paired repair, the predeclared four-case conditional preservation
campaign completed in 620.440 s with no execution failures. It used the same
native source `bd55c160781c70bdbc0f39a4858f803b8219360a818433d6ce077a93233690e3`,
the current carrier-v1 waveform, and appraiser binary
`5744a903c017ab907babb7c678c952f4e38b6c9cd1030b55ce5f7345c35c51c0`.
Its campaign hash is
`bda49d7ca9d3385c1aa0f63978c753ea9a2cab0da1d00756b2980ec994ab2b0b`;
the appraiser-source hash is
`3f308df8cb6449dabdaafd98d4071527ab62f71c993abf95e7180225a8a4b167`.
Each row is one deterministic episode; the fresh-noise row changes the noise
seed on the same physical phase-wander path and is not an independent path
sample.

| Predeclared control | Outcome | Interpretation |
|---|---|---|
| Weak wanted phase-wander with fresh noise seed | Exact `e` accepted; no premature completion or active-slot erasure | The selected weak message survives this noise holdout. |
| Noise-only phase-wander | Five scored QUIET slots; no false DATA or accepted content | Negative control passes for this one noise realization. |
| Strong wrong-key phase-wander | Three false DATA decisions; no accepted false content | Negative control fails its zero-false-DATA criterion; the exact original-source replay has the same outcome. |
| Existing +20 dB-Hz bright wanted phase-wander | Exact `e` accepted; no premature completion or active-slot erasure | Strong wanted control passes for this profile. |

The detailed outcomes, including the wrong-key false DATA count, are in
`_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-production-repair/vlf-candidate-conditional-controls-run/report.md`;
the frozen manifest and native receipts are beside it. A separate exact-source
baseline build (`fc5b4c…fade1e6`, binary
`9b0f10f8838fdf46f66b7855710b9c42af2a03874a7b625e1f9110d5f56c2eac`)
replayed the two negative controls, with identical complete input hashes and
64,806,069 samples apiece. Both versions return five QUIET decisions for noise,
and three false DATA decisions, one integrity rejection and no accepted content
for the strong wrong key. The latter failure therefore predates this repair;
neither version authenticates DATA decisions.

The matched baseline→candidate complete wall times are 184.635→204.899 s
(noise only) and 126.301→138.827 s (wrong key), about 11% and 10% longer.
Both use W=24 and respectively five and two peak owners. Peak aggregate
reservations are 5,467,838,729→5,459,224,841 bytes and
2,583,227,977→2,581,212,745 bytes. These are single descriptive observations,
including source/channel work, not confidence bounds or complete receiver
capacity. The extra gain updates have a measured unsuccessful-search cost;
the repair does not claim a universal throughput improvement. Baseline receipts
are in the adjacent `baseline-controls-run/`; the temporary source/build copy
is removed after this matched comparison.

The two remaining predeclared preservation cases also deliver exact `e`, with
three correct DATA slots and two scored QUIET slots, no erasures, wrong DATA or
premature completion: constant propagation phase inside the unchanged varying
receiver allowance, and a one-hour `(pi/2)*sin(2*pi*t/3600)` phase history with
the same maximum frequency excursion as the two-hour reference. Both keep
364.756/1459.025 s gain scales, the 21.493 threshold, 55×3 coarse grid and
26 workspaces. Their complete replay walls are 125.563/124.169 s. The smallest
DATA log-presence is 50.472/33.746, respectively. Full slot workspace peaks
at 952,577,353 bytes for each; aggregate peaks of 3,524,447,401/2,583,801,417
bytes reflect three/two live owners with the unchanged W=24 executor.

Those two rows use native source
`6b1d5682e4a971e7285a8f7324a61d674632439610c88417c350208e74951d98`
and appraiser binary
`060d532a3c95a9e8fc3f814555b3ed8c9be68e042af8ef14abd27234a2b35a36`.
Intervening changes add score receipts and scenario/test coverage, without
changing the slow-gain arithmetic. Complete input hashes are
`e0c574f2c0d420ab8e13368cc59e39705975592611c89cc0ba32534d7a8f18b2`
and `d5a44d8ee852d88b5df42939f13a72b4f45045af42b0c52b2cdcb8027600efd1`.
Receipts are in `vlf-additional-holdouts-current-run/` beside the earlier runs.
All six declared preservation cases have now run; the wrong-key zero-false-DATA
criterion remains failed on both versions. These observations do not estimate
a population false-alarm rate, independent propagation success or a day-scale
outage probability. The one-hour trajectory is a different constructed history,
not an independent draw from a calibrated atmospheric population.

The [wider-bound preservation cohort](../_appraisal/highFidelity-triple_Robust_Modem/campaigns/vlf-slow-gain-wide-bound-preservation.json)
changes only the receiver atmospheric allowance to 0.002 Hz. Its physical
two-hour trajectory, weak wanted/noise inputs and strong wrong-key input retain
the exact original complete IQ hashes above. The old gate adds no slow models
at this bound, but already receives the wanted message: its later drift plan
has 440 slopes and 163 frequency bins versus 104/79 at the narrower bound,
and the third-bit presence is 46.125. This is preservation coverage, not a new
reception-benefit witness for the clamp.

The final scale rule retains slower actual channels inside a larger allowance:
`tau=max(two_fine_cells,1/lambda)`, with models at `tau` and `4*tau`. In this
case all five slots report 120/480 s. The matched candidate still delivers exact
`e`, all five noise slots remain QUIET, and wrong-key still yields three false
DATA decisions, one integrity rejection and no accepted content. Both complete
three-row cohorts finish without censoring, within the predeclared 360 s per-row
and 1080 s aggregate limits. Neither reached a timeout or establishes a
computational ceiling.

Baseline→candidate walls are 155.510→138.577 s (wanted),
190.358→209.435 s (noise), and 133.799→141.053 s (wrong key).
The wanted DATA slots now use fixed-affine evidence; the extra slow state adds
unsuccessful-search work. Full slot workspace remains 952,577,353 bytes; W=24
is unchanged. Candidate peak aggregate reservations are
2,580,790,857/5,469,718,793/2,584,899,145 bytes with 2/5/2 live owners,
respectively. These are descriptive replays, not independent speed estimates.
They bind native source
`6188e938e1e211a017d8139dd16b603a50596a25943b9d64d87f074c3cb7e545`
and appraiser binary
`e6aab29cb95a42cd5ad8e231fae2d1ea371d0423b156d4b5f7b92226a9242c04`.
Receipts are in `vlf-wide-bound-baseline-360-run/` and
`vlf-wide-bound-clamped-gain-candidate-360-run/` under the same temporary
evidence root. The widened branch also passes the focused normalization,
two-model work, workspace, prefix/worker invariance and adaptive-preservation
checks. Already-qualified smaller-bound scales and explicit Off are unchanged.

The same current-source reconciliation reproduces the HF GUI bright-signal
wrong-bit failure and wrong-key accepted content. Comparing all admitted
models repairs the bright `e`, but changes a fresh `A` from rejection to
accepted incorrect content. It remains an appraisal option; neither a cheap
model-fit veto nor a stronger threshold is established as a general repair.
The slow-gain family is ineligible for those fast geometries and does not claim
to resolve them. The [appraisal coverage record](../_appraisal/highFidelity-triple_Robust_Modem/coverage.md#applicability-after-the-admission-and-carrier-key-changes)
preserves their current applicability and the five-link qualification gaps.

A selected-slot HF component discriminator on the later source separates the
fixed short-gain and frequency-tracking scores. For fresh `A`, slot 5, the
fixed short branch favors true bit 0 by 2281.100 nats, while the frequency
tracker favors wrong bit 1 by 263.112 and dominates both mixture components
by over 28,000 nats. Applying the reported +9.907 prior adjustment and equal
mixture exactly reconstructs the short-branch scores. A thermal-noise-off twin
at the same noisy-selected coordinates retains the reversal: the fixed branch
gap is +2056.232, the tracker gap −338.680. Its separate wrong-key shadow also
crosses the diagnostic DATA threshold. These are private component diagnostics,
not new full-message receiver outcomes or an authentication test. They localize
the issue to the tracker for this aperture; they do not identify a universal
cause or qualify a fixed-model veto. The noisy selected-observation hash
`cc2f566897e0051533365e4447d42faea0a5b3691ccdf3684c347c8d3448f289`
is identical before and after adding the clean twin, whose selected-observation
hash is `ec56c5b800bd73b0d070f5a316a59d023385d321832a37e3350829a6f51eeb23`.
The selected-aperture diagnostic has its own filter/window context; its score
magnitudes must not be substituted for the earlier complete-message scores.

The optional native raw-tracker span diagnostic then reuses those correlations
and Gram matrices. It keeps one timing coordinate through the aperture while
maximizing frequency separately in each block, matching the positive-support
frequency-history prior. Clean correct/wrong captured energies are
10361.928/10428.850 squared-sample units; numerical rank bounds coincide and
direct time-domain residual checks agree. This explains a high-SNR wrong-bit
floor on this frozen bank: finite positive Gaussian gain innovations change
history penalties but not the leading captured-energy ordering. It is not a
global impossibility result; changing templates or timing changes the premise.

An eight-coordinate public nomination tests quarter clocks already charged by
the nine-clock admission prior but absent from the ordinary raw tracker.
It preserves every old coordinate and its prior contribution, with at most 32
coordinates and unchanged full DSP workspace. The selected fresh-`A` noisy and
clean twins both change to the correct bit; clean correct/wrong capture becomes
11652.726/10428.850. The new winner is epoch 0, approximately +75 ppm and
+2.5 ms arrival, nominated from ordinary coarse scores without channel truth.
The matched wrong-key diagnostic is unchanged. This adds 734,784 rendered basis
rows per observation. The optional span scratch is 185,528 bytes for the
expanded bank, separate from 164,623,976 bytes of DSP scratch, receipt storage
and 193,032,192 bytes of charged W24 worker stacks.

Complete default-mode validation initially preserves all 60 ordered decisions
and all six message outcomes, with **no operational improvement**: bright `e`
still accepts wrong content, weaker `e` completes, fresh `A` retains six wrong
bits and is rejected, and the original wrong-key failures remain. Varying-stage
basis work grows by 9.4–10.9%; aggregate single-observation wall is
49.194→49.919 s. Experimental compare-admitted mode repairs fresh-`A` slot 5,
reducing five wrong bits to four, but still accepts wrong content. Its prior
failure therefore continues to rule out default promotion. Receipts are in
`hf-candidate-default-six-case-120-activated-run/` and
`hf-quarter-clock-compare-three-run/`, bound to native source
`8395980f7ef989574c63f4856d8921d4f0298a3235f3421998a0cfcb65410e9b`
and appraiser binary
`2875c1ee003fde409262b123540a1df9b300f518e580b56fed3f65d3fced39b6`.

A separate added-only affine test has a negative result. Affine already searches
residual quarter-clock offsets; this tests new epoch/arrival anchors rather
than a globally missing affine clock. Eight additional centers leave the bright
`e` wrong-bit score exactly unchanged in noisy and thermal-off twins, while
wrong-key evidence increases by 21.583/213.173 nats. Two fixed selected-slot
trials finish in 8.585 s within the declared 120 s each/240 s total caps. Their
31.1–31.4 million additional rendered rows cost approximately 0.12 s per
observation. The failed affine evaluator and option are removed. Retained
receipts in `hf-affine-quarter-center-shadow-run/` preserve the counterexample;
reconsider new affine centers only with evidence for a different useful anchor
rule. Neither this test nor the successful tracker shadow establishes
authentication, a general HF repair or five-path sensitivity qualification.

The last predeclared tracker discriminator lowers the existing fresh-`A` case
to 15 dB-Hz, with the same 62003/72003 trajectory/noise seeds, two-second bit
request and GUI target32 profile. A matched default-mode pair has identical
1,635,895-sample IQ/validity hash
`c11fda7e6386d3e23136ca84388f3c15656e6a59f663f8e6e969a2f7d1d992f5`.
Both produce 13 ambiguous active slots, two QUIET slots and no message. The
tracker executes, but slot 5 presence only changes −3.391→−3.028, below 21.493.
No operational benefit appears. Wall is 13.799→14.345 s; varying basis rows
increase 102,302,888→113,203,880, with unchanged W24 and 165,394,356-byte peak
workspace. The declared 120 s per-run/240 s pair caps are respected.
The conditional wrong-key pair is not run, as specified by the no-benefit stop
rule. Production quarter-clock activation is removed; the opt-in diagnostic
remains for the unresolved timing-support question. No new default search cost
is retained from this experiment. The receiver-validation appraiser binary is
`e8e84c0c84987da12e6b8b7ca0b7b8b5c93a5e8d96a8eee9e63b09e409946566`,
native source
`74709f9fd83712a1c6d7a4ffc9e0f8aea6fba76c179365b2bdecb2924bcf19f8`.

### Current-waveform cross-path reconciliation

Six existing EME/groundwave episodes are regenerated with that binary,
rather than treating the older waveform's receipts as current. The frozen
manifest SHA256 is
`3c7e93b0f5f33e2236b4eec4afe5bbd3e5f6377a7521f57b739576e818d3fb37`;
all six execute in 79.778 s within 120 s per-row/720 s aggregate caps. Three
communication failures remain; successful execution is not successful reception.
The EME pair keeps seed 9501 and differs only in C/N0. Groundwave reference and
weak rows retain their original different seeds, 9201/9202, so they do not
define a paired sensitivity curve.

| Existing episode | Reference C/N0 (dB-Hz) | Current complete-message outcome | Wall (s) |
|---|---:|---|---:|
| EME disk | 30 | One wrong DATA bit, wrong content accepted | 12.847 |
| EME disk, same physical episode | 60 | Exact `e` accepted | 13.294 |
| 1 MHz land reference | 23.347 | Exact `e` accepted | 23.968 |
| 1 MHz weak land | −16 | Three active slots classified QUIET; no message | 26.145 |
| 30 MHz land reference | 7.961 | Exact `e` accepted | 1.553 |
| 30 MHz weak land | 0 | Three active slots classified QUIET; no message | 1.569 |

The older EME failure at both levels is therefore not a current-waveform
high-energy persistence result: the 60 dB-Hz case now succeeds, while 30 still
fails. None of these six cases activates slow gain models; this is applicability
reconciliation, not causal evidence for the gain repair. Helper is explicitly
Off throughout, including normalization of an omitted-but-effectively-Off field
in the original EME 60 dB-Hz manifest. Full W24 on 12 logical threads remains;
per-owner slot quotes are 200,933,388 bytes (EME), 185,945,508 (1 MHz), and
165,707,828 (30 MHz). Configured owner limits are distinct from actual overlap.
The groundwave amplitude references and lunar scattering construction retain
their physical-model limits. These few episodes establish neither a practical
sensitivity boundary nor five-family reliability. Compact inputs and receipts
are in `current-six-crosspath-reconciliation-run/` under the evidence root below.

One separately predeclared EME 30 dB-Hz discriminator enables the existing
compare-admitted policy on the **identical** complete input
`21dc9d978a0d95f9ec0a40bd194391b53a495a72987f0b9f1faa378d9dec66f2`.
It repairs only the wrong third DATA bit; the other two DATA and both QUIET
decisions are preserved and exact `e` completes. Correct-bit separation changes
from −32.730 to +1134.130 nats, and wall rises from 12.847 to 20.222 s.
W24 and the 200,933,388-byte slot quote are unchanged. This one 1-million-sample,
120-second-capped diagnostic completes with 517,375 samples; receipts are in
`eme30-admitted-alternatives-diagnostic-run/`.

The result shows useful evidence in already-admitted alternatives, not a safe
general activation policy. The final reason reports varying evidence with a
coherent line, but the aggregate alone does not isolate which component caused
recovery. A line-presence trigger is also contradicted by the existing fresh-`A`
HF comparison: its incorrect-content result has stronger retained line evidence
(446–930 nats) than this EME case (36–41). Neither unconditional comparison nor
that trigger is promoted. Earlier staged-coherence HF supplements likewise do
not cover these cases: their public geometry requires zero delay/Doppler spread,
at most 25 Hz total frequency bound and at most 600 s aperture. They cannot
simply be enabled for HF Worst, lunar disk scattering or this long VLF capture.

Frozen inputs and compact receipts are temporarily retained in
`_appraisal/highFidelity-triple_Robust_Modem/local-evidence/current-production-repair/`
for matched production-repair and negative-control validation. The first
complete-pair manifest exceeded the runner's campaign sample cap and executed
zero cases; its corrected 100-million campaign ceiling retains the individual
70-million sample bounds. Neither that setup correction nor a completed
campaign means communication passed. Distill these results and remove redundant
raw outputs when the current comparison and unresolved failures no longer need
them. No RunPod instance or spending is involved: these checks execute the
canonical CPU implementation.

Final retained-source software checks passed: `./build.sh test triad`
**38/38** (77.68 s), including the native Robust GUI integration, and all six
affected appraisal checks (`scenarios`, `hf_gui_profile`, `hf_public_receiver`,
`hf_admitted_evidence`, `integration`, `runner`) **6/6** (61.41 s).
The earlier complete `./build.sh test gui` run passed **30/30** (154.66 s);
subsequent changes concern receiver diagnostics and bounded gain eligibility,
with no GUI control or presentation change. The first triad run was **37/38**:
the new independent dense-covariance reference incorrectly divided an already
Cholesky-whitened quadratic by the nonunit noise variance a second time. Fixing
that reference preserved its 2e-12 tolerance, erased block and merged tail;
receiver arithmetic was unchanged. The first appraisal run was **4/5**:
an existing horizon assertion expected 2400 s even for the explicitly protected
4200 s vector-oblique calendar. It now checks those two declared bounds
separately, with no channel change. Rebuilds used disabled/read-only compiler
caching because the shared cache is outside this workspace's write permission.
Final review also corrected the opt-in span report's metadata allowance: its
maximum 512 block objects alone need approximately 438,272 bytes on the tested
ABI, exceeding the original 256 KiB before temporary JSON copies. Admission now
reserves 2 MiB per requested span summary in addition to the existing metadata,
or 2,359,296/4,456,448 bytes for ordinary/expanded reports. Two frozen wanted
diagnostic replays pass in 22.422 s with all scores, spans, DSP bounds and capture
digests exactly preserved; only metadata receipts and timings differ. The runner
check passes again in 1.33 s. This appraisal-only accounting edit leaves native
source `74709f9f…` unchanged. Final appraisal source is
`7aa695700f0793748854c6bac980851d3457ce45e0d05a4bf67df6bc507e9cdc`
and binary
`8d47ee394cf832086f265cc8b62e9a635005c9a074b2d582ae1a4c73b5e04001`;
receipts are in `hf-span-receipt-reservation-parity-run/` beside the other
matched comparisons. Default receiver processing and search are unaffected.
Temporary check logs are in `local-evidence/slow-gain-validation/` until the
current repair comparison is reconciled; no release certification follows.

## Reversible Wrong Key Admission history — 15 November 2026

The label-only follow-up over `8d4d747b` names the default **Unfiltered** and
adds approximate HF visibility-loss estimates: ~1/~2 dB for 2/3 and 1/3,
~3 dB for 1/10, and ~6 dB for both deeper model targets. These reuse the
scoped observations below and the earlier finer-grid Guarded/Strict comparison;
they are not statistical bounds or universal sensitivity penalties. The deep
targets carry an asterisk explained in the control's help. Compact labels fit
the existing Encryption-key row without changing its geometry. Saved tokens,
scalar thresholds and reception/history behavior are unchanged.
The rebuilt `gui_fast`, `gui_layout`, `gui_launch_command` and
`gui_native_policy` checks passed **4/4** in 3.95 seconds after simplifying the
range labels to ~3/~6 dB, and the native
application build completed. No new receiver study or capacity claim is made.

The follow-up over `3d29090a` separates baseline GUI reception from the selected
browser filter. The user explicitly chose filtering the view and reporting how
many DATA bits pass while preserving original decoded content, rather than
re-decoding filtered bits. Native quiet completion and codecs remain unchanged.
The existing 256 MiB workspace is RAM history, not persistent disk storage.

A fresh bounded static-HF comparison was declared before its outcomes: 64
independent wrong-key draws in each long/short stratum and 16 independent
paired wanted panels at mixed C/N0 +7/+10/+13/+16 and clean −7/−4/+10 dB-Hz.
All six scalar tiers share one canonical native score per observation, for
240 total calls. Receiver key, public slot 1 and ordinal remain fixed; foreign
keys, source bits, phases and noise are independently drawn by episode.
The fixture uses 30 MHz Shift plus 500 Hz IF, Rate 36, requested 180 seconds
(actual bit 180.4 seconds), 1,500 Hz sampling, exact baseband clock, IC-7100
Shift oscillator, one arrival and all 43,938 frequency positions. Foreign
frequency offset is +6 Hz. Long/short foreign-only cases use requested
180/30 seconds at +18/+30 dB-Hz with uniform nominal-window overlap. Mixed
panels use requested 30 seconds at +26 dB-Hz and a fixed +60-second offset.

The Ryzen 5 PRO 5650U uses four disjoint three-logical-CPU allocations,
automatic `W=6` each, aggregate `W=24`, and 1.5 GiB per call. Native and
subprocess deadlines are 60/75 seconds, with a fixed 3,600-second total cap;
an error or incomplete score stops the cohort. No outcome-driven sample
extension is allowed. Manifest SHA256:
`13383071e7c0d7db0e617c7c33c1a06df6bc8012a0647135da219483253b5817`.
Relative retention uses baseline false admissions as its denominator; ten
nonbaseline-policy/foreign-stratum comparisons use `alpha=.05/10` for
simultaneous 95% one-sided bounds. Wanted crossings are descriptive and
paired levels do not multiply the independent panel count. Inclusion of the
provisional Maximum tier requires at least 15/16 correct at mixed +16 and
at least one additional foreign rejection beyond Extreme in a fixed stratum.
That selection criterion evaluates a useful option, not a calibrated risk.

Temporary receipts and analysis in `local-evidence/wrong-key-history/` served
this comparison and implementation review; their cleanup follows the retained
findings and current calibration sample below. An initial plan
incorrectly assigned the RF-only IC-7100 preset as the baseband clock and
failed profile validation; the corrected declared geometry above passed.
No statistical outcomes were collected by that setup failure.

The pilot completed **240/240** native calls in 299.22 seconds. Its source
manifest was `8db4a2c9ba0b9b50ec7788d95214bc3d24b36282a8e907b0b912803230066b04`,
probe binary `0b898014a642044a0626e2e60719d411ec1ee27cbd14dfe1d02ba51476966146`.
Current admitted 53/64 long and 58/64 short foreign draws. The original
competing-excess-only Maximum (`rho=2`) met its descriptive inclusion criterion:
it retained 0/53 and 1/58 baseline foreign admissions and received 16/16 wanted
bits at mixed +16, but only 0/16 at +10 and 3/16 at +13. Those exploratory
results are not the final stronger-tier equation.

While this pilot was running, the user explicitly requested 1/10, 1/100 and
1/1000 reduction labels and allowed trend extrapolation. The resulting model
choice was frozen before aggregate score inspection: rank baseline foreign
DATA by its gap divided by the **whole Strict margin**, fit exponential mean
excesses at the upper third and upper fifth separately in each stratum, and
compare Pareto alternatives and largest-observation influence. Use the largest
primary-model threshold across fits, rounded upward to a quarter. The tenth
uses the empirical order statistic instead. All finite extreme observations
remain included. This is a separate exploratory calibration, not retrospective
replacement of the pilot's original measured endpoint or selection criterion.

The resulting multipliers of the whole Strict margin are **1.5 / 2.75 / 4.0**.
They increase the absolute gap floor too, avoiding the structurally unaffected
below-threshold competitor class of slope-only scaling. Current, Guarded and
Strict remain exact. Counterfactual pilot retained/baseline foreign counts are:

| Static-HF reference target | Long foreign bits | Short foreign bits |
| --- | ---: | ---: |
| 1/3 (original Strict) | 12/53 | 7/58 |
| 1/10, multiplier 1.5 | 2/53 | 3/58 |
| 1/100 model, multiplier 2.75 | 1/53 | 1/58 |
| 1/1000 model, multiplier 4 | 0/53 | 0/58 |

These calibration-sample counts are not independent post-selection validation.
At multiplier 2.75 the exponential fits predict 0.169–0.785% retention, while
Pareto alternatives predict 0.407–2.638%. At multiplier 4 the corresponding
ranges are 0.0048–0.0884% and 0.0859–1.4123%. These are **model differences,
not confidence intervals**. For a 1/1000 target, alternate fitted cutoffs span
3.86–19.57 versus the deployed 4; the tail assumption matters substantially.
Omitting the largest observation as a diagnostic only lowers the worst
exponential 1/100 and 1/1000 cutoffs from 2.6116/3.9292 to 2.2259/3.3032.
The actual calibration keeps those observations. Both deepest survivors were
partial overlaps with competing scores above `T`; this is an observation,
not a causal classification of overlap types.

Under the final scalar rule, the pilot's clean wanted outcomes were identical
for all policies: 3/16 correct at −7 and 16/16 at −4 and +10 dB-Hz. Mixed
correct counts at +7/+10/+13/+16 were Current 13/16/16/16, Strict 6/15/16/16,
1/10 4/15/16/16, 1/100 2/9/16/16, and 1/1000 0/5/15/16 (each out of 16).
Current had three wrong bits at +7; the stronger policies had no wrong bits
on these mixed levels, with remainders hidden/erased. First empirical 90%
crossings occupy `(7,10]` for Current and 1/10 and `(10,13]` for the deeper
targets. The 3 dB grid does not establish an exact 3 dB penalty or a universal
6/9 dB cost. The previous strong clean Sub-9 counterexample remains applicable
to every nested stronger filter, now reversibly hiding rather than discarding
the GUI's original reception.

The [minimal scalar calibration sample](validation-data/triad-v3/wrong-key-reference.json)
retains 111 ranks plus their native source/configuration and seed recipe. It is
needed to recompute the **current dropdown constants** and compare tail models;
aggregate counts alone cannot answer that question. Replace/remove this small
dataset when the calibration is superseded. Full receipts, sampled signals and
campaign scripts are temporary, not retained as a second implementation.

A fresh **256-call holdout** was then frozen with all final constants: 64
foreign draws per stratum and 16 paired panels each at clean −4/+10/+22/+28
and mixed +10/+16/+22/+28 dB-Hz. Its one-sided 95% conditional retention bounds
use the same ten-comparison family. No threshold retuning or sample extension
follows its outcomes. The receiver key/calendar remain fixed and the foreign
draws are fresh; this does not qualify other traffic or multi-bit row error rates.
Manifest `60bd34ae1170819a567bf2438b0efc36d40ebabeef81394f9ff32cfdb1d66d01`.
The holdout completed **256/256** calls in 298.33 seconds. Every native receipt
used three effective hardware threads and six workers, one arrival and the full
43,938-frequency search. Source manifest:
`c88b47795be01dd8a73bce7f7948e922df3aada80c97b91c0a27b030add712c1`.
Probe binary SHA256:
`936d13c653ce3e63b3de21be418a8c937338e8104006daf4b3acc15165464545`.
Independent recomputation verified all 256 × 6 scalar decisions, disjoint pilot
and holdout seeds, and exact calibration-data reproduction. The two foreign
strata admitted 41/64 and 62/64 bits under Current:

| Reference target | Long retained / baseline admitted | Short retained / baseline admitted | Simultaneous 95% retention upper bound, long / short |
| --- | ---: | ---: | ---: |
| 2/3 (Guarded) | 20/41 | 34/62 | 69.22% / 71.00% |
| 1/3 (Strict) | 5/41 | 13/62 | 30.83% / 37.04% |
| 1/10 | 2/41 | 4/62 | 20.71% / 18.95% |
| 1/100 model target | 0/41 | 0/62 | 12.12% / 8.19% |
| 1/1000 model target | 0/41 | 0/62 | 12.12% / 8.19% |

For the deep targets, the separate marginal one-sided 95% upper bounds are
7.05% and 4.72%. Zero survivors therefore does not verify 1% or 0.1% retention;
the distinction between the two deep labels comes from the tail model, not
measured different holdout rates. Neither conditional retention bounds nor
model ranges qualify arbitrary foreign traffic or the majority-bit row rule.

All clean wanted policies agreed: 15/16 correct at −4 and 16/16 at +10/+22/+28,
with no wrong bits. Mixed wanted counts at +10 were Current/Guarded/Strict
16/16, 1/10 15/16, 1/100 7/16 and 1/1000 3/16. At +16/+22/+28 all six choices
received 16/16 correctly, with no wrong bits anywhere on this mixed grid.
Thus a **6 dB increase restored the two deep filters across the tested
+10-to-+16 step**; their empirical 90% crossing lies in `(10,16]`, while Current
already met that endpoint at +10. This is not an exact 6 dB sensitivity loss.
The independent pilot supplied finer, 3 dB grid evidence; neither study
supports a universal dB penalty, and their paired levels are not independent
trials. No threshold was retuned after the holdout.

The GUI implementation retains up to 4,096 baseline records, uses a combined
retained-history/content bound rather than a second independent 256 MiB cache,
and renders 64-record pages. Evidence follows cached files beyond signal-row
eviction. Filter changes do not restart active reception or transmission,
re-decode content, or invalidate original exports. Hidden selections cannot
copy/paste/save; Current restores retained records. Pending and unrated records
remain visible, and physical quiet never comes from the browser filter.
An independent source review caught and removed an existing whole-history
copy that would have grown from 64 to 4,096 records per receive update.
Counters add a fixed 64-byte summary and bounded scalar work per admitted bit;
there is no extra DSP hypothesis, search, fallback, worker or retained I/Q.
This is not a refreshed complete-receiver capacity qualification.

Checks: the final native group passed 38/38 in 76.46 seconds and the GUI
group passed 30/30 in 154.45 seconds. An earlier native run passed 37/38 because
a new exact-coefficient test assumed `(T+20)-T` was exactly 20; it differed by
one ULP. The test now uses exactly representable coefficient inputs while
retaining actual-threshold boundaries. Metadata also rejects a score whose
gap contradicts its declared baseline minimum. The dense-history web bridge
test passes with all 4,096 retained records and a near-maximum escaped draft,
remaining below the existing 8 MiB frame envelope. The final application build
completed, and the rebuilt targeted web bridge/audio/renderer checks passed
3/3 in 0.84 seconds after the final help and fixture changes.

The broader web group initially passed 10/13. Two preview checks could not
create local sockets inside the sandbox; both passed when rerun with approved
loopback access. **Fast `web_live` remains failed**, rather than being counted
as qualified by those passes. It underflowed at 48 kHz before transmit drain
and before received replay, on an unchanged Fast path. The same kind of
failure is recorded in the pre-feature
[September web log](validation-data/bookworm-offline-20260930/test-web.log).
The later pre-feature Fast waveform change lengthened the fixture's emission
from about 9.8 to 32.67 seconds, exposing its stale 30-second replay deadline.

The test now derives that deadline from the actual replay/end-of-absence time
plus the same 20-second processing allowance; its bounded two-rate CTest
envelope is 900 seconds, with all individual phase and underrun assertions
preserved. Its purported 10 Mb/s relay previously rounded every 4,096-byte
fragment's delay upward to 4 ms, imposing an ideal ceiling of 8.192 Mb/s before
overhead. A matched instrumented comparison measured 7.825 Mb/s originally
and 9.750 Mb/s with carried fractional-byte pacing. That fixture-model repair
was adopted; FIFO, fixed chunks, backpressure and the production quarter-second
audio credit remain unchanged.

Corrected pacing still underflowed after 861,600 actual played frames, with no
endpoint/drain. In the late interval, snapshots plus PCM required 10.200 Mb/s
per emitted sample time; a 114,681-byte snapshot occupied 90.43 ms. The original
matched trace underflowed after 321,600 frames. These results identify an
existing shared-pipe presentation/audio bandwidth limitation; they do not prove
that correcting timer rounding repairs the runtime. The growing Fast snapshots
do not contain the hidden Robust control or this task's reception history.
Production transport/credit was not changed, and the second sample-rate case
was not reached. The final tracked relay/deadline code matches the executed
instrumented variant; syntax and CMake configuration checks passed. This
remaining Fast web qualification limit is separate from the passing native
Robust, GUI history and bounded-frame checks.

All 496 statistical calls used the canonical native CPU implementation and
finished in about ten minutes combined, so this follow-up needed no additional
RunPod allocation. This is an outcome study, not a complete-service benchmark.
After final review, owned full receipts, manifests, scripts and diagnostic
copies were removed. Only the justified 6,221-byte current-calibration input
remains; useful build caches and unrelated evidence were retained.

## Wrong Key Admission rate and sensitivity study — 15 November 2026

This follow-up quantifies the existing policies; it does not retune them.
The reference population is independently drawn synthetic wrong keys, noise,
source bit and relative phase under declared native channel/overlap laws.
It is not a distribution-free claim about all HF or sub-9 kHz traffic.
The receiving pattern uses the fixed public Amateur roots and configured
slot 1 / ordinal 7000038; simulation seeds randomize the native channel and
noise, not the receiver's key or waveform ordinal. Daily equivalents assume
that this conditional risk represents other scored slots. They are projections,
not measured full-day or full-message outcomes. Wanted and foreign sources
share one native channel realization; independent propagation paths are not
qualified. A fixed foreign key can have a different risk than the ensemble
of freshly drawn keys.
The user's 10% interference-time stress assumption is an exposure model,
separate from admission conditional on an overlap; see the
[exposure accounting](triad-stochastic.md#wrong-key-admission).

Before confirmation, the local fixed budget was 1,080 complete native slot
calls: 300 each for long and short interfering bursts, plus 32 independent
paired wanted episodes at seven clean and eight mixed C/N0 levels. The six
primary wrong-key policy/stratum upper bounds use Bonferroni `alpha=.05/6`;
individual one-sided 95% bounds and counts remain separately labeled.
Wanted confidence intervals are pointwise, with paired comparisons across
levels and policies; correlated levels do not multiply the episode count.
The same completed native score supplies all three policies through the
production scalar gate. Incomplete runs stop the batch, are retained, and
cannot silently enter a success denominator. The fixed total deadline is
3,600 seconds, with 60 seconds of native work per call. No outcome-driven
sample extension is allowed.

This initial HF reference uses 30 MHz, the IC-7100 oscillator declaration,
Rate 36, a 180.4-second bit, 1,500 Hz I/Q sampling, 500 Hz DATA IF,
fixed arrival and a static channel. It scores the full 43,938-position
oscillator frequency search. Interferers use an independently keyed carrier
6 Hz higher. The long and short pure-interference strata request 180 seconds
at +18 dB-Hz and 30 seconds at +30 dB-Hz respectively, with onsets uniform
over intersections of the declared nominal source and scored-window envelopes.
Nominal support includes padding and tails, not just active DATA. C/N0 is
the native pre-channel reference, not measured post-fade aperture power.
Clean wanted levels are −13/−10/−7/−4/0/4/10 dB-Hz. Mixed wanted levels are
−13/−10/−7/−4/−1/2/5/10 dB-Hz, with a +26 dB-Hz, requested 30-second foreign
source at a fixed +60-second pattern offset. These are scoped references,
not the full-stress manual HF case.

The local experiment partitions the 12 logical CPUs of the Ryzen 5 PRO 5650U
into four disjoint three-CPU affinities, each using automatic `W=6` and an
exact 1,610,612,736-byte resource limit. The aggregate software budget stays
24. One matched `W=24`/`W=6` check preserved input hashes, scores, decisions
and DSP work; only timing and workspace leases differed. Independent trials
measure statistical outcomes, not complete-receiver capacity or same-stream
throughput. The fixed manifest hash is
`963c90aa6ba43cb70e2408cc44ba0b5bb96840feb3a9c256f9941afdec2f616a`.
Baseline source is `4b42411b0de5cb14732145cdf05da5f614dca4aa`; the measured
probe's source manifest is
`0b9877c3ba2ee4360cad4e71203951eb9b97bed78b450604ca31dabbedfe864b`.

After deterministic GPU checks, a separate fixed 576-call changing-HF cohort
was declared: 128 each for the same long/short wrong-key strata and 32 paired
wanted episodes at −13/−7/−1/5/11 dB-Hz, clean and mixed. It uses the full
43,938-frequency by 1,759-arrival search and the HF Best changing channel.
The six wrong-key comparisons have their own simultaneous 95% family; they
do not combine with the static cohort into a single six-comparison claim.
The manifest hash is
`e7c59c80b45ad077880d9bc048cf716c78c2d6384f2721574c5cffd9ba2c3614`.
The fixed overall deadline is 5,800 seconds, with 60 native seconds and
75 subprocess seconds per call; errors/incomplete calls stop the cohort.
The native probe is built from the same source manifest on a full NVIDIA
RTX PRO 6000 Blackwell Server Edition (97,887 MiB, MIG disabled), CUDA 12.8,
cuFFT 11.3.3, ATen/Torch 2.9.1+cu128 and GNU 13.3. The EPYC 9355 host's
13.6-vCPU quota resolves to `H=13`, `W=26`, with 16 GiB host and device caps.
Native binary SHA256 is
`dac5b1873ed607ab06c529681183342b26cb159c2bd1e7a032e60131c5cd38fe`.
The 1.9 MB allowlisted source upload took seconds. Initial CUDA compiler
discovery, absent Ninja and unrelated portable-packaging setup failures
were repaired without changing modem source. One completed broad-HF pilot
matched I/Q, final scores and decisions between native half-cuFFT and FP32;
native slot work was 4.89 and 5.94 seconds, respectively. Both actually ran
on CUDA and all three admission policies accepted this wrong-key example.
This pairing supports this study's route, not universal precision parity or
receiver capacity on another device.

The static cohort completed all **1,080/1,080** calls in 1,378.64 seconds,
without incomplete episodes. Wrong-key false-DATA counts were:

| Static HF foreign source | Current | Guarded | Strict |
| --- | ---: | ---: | ---: |
| Long, +18 dB-Hz, 300 trials | 207/300 (69.0%) | 98/300 (32.7%) | 35/300 (11.7%) |
| Short, +30 dB-Hz, 300 trials | 281/300 (93.7%) | 181/300 (60.3%) | 80/300 (26.7%) |

The six-comparison simultaneous 95% upper probabilities are respectively
75.25/39.53/16.81% and 96.57/67.08/33.25%; ordinary marginal one-sided 95%
upper bounds are 73.40/37.40/15.17% and 95.81/65.05/31.19%. Policies share
each observation; no independence between their outcomes is assumed.
The direct foreign center is within the fixed ±30 Hz search in 297/300 long
and 299/300 short draws. All draws remain in the declared denominators.

For an explicit periodic traffic reference, define **10% occupancy of nominal
source support**, with one burst of support `L` every `10L` and uniform phase.
Actual support is 183.118 seconds for long bursts and **35.518 seconds** for
the requested 30-second bursts (actual bit 32.8 seconds). The earlier scalar
illustration using 33.118 seconds did not use this quantized source and is not
the measurement conversion. With `W=183.689333`, `C=210`, the fractions of
windows intersected are 20.0312% and 61.7173%, giving 82.4141 and 253.9224
exposed windows per long-run day. `W+L<10L` ensures at most one foreign burst
per window, matching the native single-burst law. The daily equivalents below
are false DATA bits/windows, not false decoded messages or a probability of
at least one event. Parentheses give simultaneous 95% upper bounds within
the six-comparison static family; the negligible conditional thermal term
is omitted at this displayed precision.

| Static HF periodic 10% reference | Current / day | Guarded / day | Strict / day |
| --- | ---: | ---: | ---: |
| Long burst | 56.87 (62.02) | 26.92 (32.58) | 9.61 (13.86) |
| Short burst | 237.84 (245.21) | 153.20 (170.32) | 67.71 (84.42) |

These high-power foreign cases show improvement, not a thermal-noise-level
guarantee. Air occupancy alone, unspecified key/power/channel populations,
and unknown multiple-burst effects cannot establish generic daily rates.

The changing-HF CUDA cohort completed **576/576** calls in 4,164.18 seconds.
Every call executed native half-cuFFT; median/max native slot work was
4.421/6.794 seconds (whole calls 7.072/9.450), with peak reserved device
memory 176,160,768 bytes. This cohort changes arrival search, backend and
channel together relative to the static reference; their outcome differences
cannot be attributed solely to propagation.

| Changing HF foreign source | Current | Guarded | Strict |
| --- | ---: | ---: | ---: |
| Long, +18 dB-Hz, 128 trials | 57/128 | 41/128 | 23/128 |
| Short, +30 dB-Hz, 128 trials | 101/128 | 54/128 | 18/128 |
| Long: simultaneous 95% probability upper bound | 55.47% | 42.77% | 27.49% |
| Short: simultaneous 95% probability upper bound | 86.86% | 53.14% | 22.98% |

With changing-channel padding, `W=204.752667`, `C=210`, and long/short
`L=197.183333/49.583333` seconds. The same periodic 10% law gives
83.86494/211.04032 exposed windows/day. The six-comparison family is separate
from the static family; parentheses again give its simultaneous 95% upper
bounds. Four long and one short direct-center-plus-common-wander envelopes
extend beyond the frequency search; all remain in the declared denominator.

| Changing HF periodic 10% reference | Current / day | Guarded / day | Strict / day |
| --- | ---: | ---: | ---: |
| Long burst | 37.35 (46.52) | 26.86 (35.87) | 15.07 (23.05) |
| Short burst | 166.52 (183.30) | 89.03 (112.15) | 29.68 (48.50) |

Clean wanted correct counts at −13/−7/−1/+5/+11 dB-Hz were
0/0/27/32/32 out of 32 for every policy, with no wrong bits. Zero additional
losses across 32 paired panels gives the same marginal 8.94% upper bound
on any extra loss across that fixed grid. In the mixed case, Current correct
counts were 14/18/18/14/32, Guarded 8/14/9/7/32 and Strict 6/6/2/3/32;
wrong-bit counts were respectively 9/12/12/17/0, 5/7/8/11/0 and 4/5/2/4/0.
Remainders are erasures. All policies received 32/32 correctly at +11.
The coarse 6 dB grid does not resolve a sensitivity difference; its lower
levels are nonmonotonic and contaminated by chance-correct foreign bits.

For clean wanted reception, all three policies produced identical outcomes
at every tested level: 0/32 correct at −13 and −10 dB-Hz, 4/32 at −7,
and 32/32 at −4, 0, 4 and 10. No wrong bits occurred. There were no extra
Guarded/Strict losses in any of the 32 independent paired panels; the
ordinary one-sided 95% upper bound on a panel having any extra loss across
this fixed grid is 8.94%, not a bound obtained from 224 independent trials.
The observed clean sensitivity transition has no policy shift at the tested
resolution; this is not a universal zero-dB guarantee.

With the +26 dB-Hz short interferer, wanted C/N0 +5 yielded correct/wrong/
erasure counts of 16/16/0 (Current), 10/8/14 (Guarded), and 5/4/23 (Strict).
At +10 all three received 32/32 correctly. At still lower wanted power many
baseline admissions happen to match the wanted bit by chance, so a 50%
correct-bit crossing would not identify useful sensitivity. A separate
refinement was therefore declared after the completed coarse grid: 64 fresh
independent panels at each of +5/+6/+7/+8/+9/+10 dB-Hz, all policies paired,
384 calls, 1,500-second fixed total budget. It is a new sensitivity study,
not an extension of either primary wrong-key sample. Manifest SHA256:
`81b67188c9ae9a7ace38b09b66e54e5153bd051756a633a8fada92eedf61f423`.
All 384 calls completed. Each table entry is correct/wrong/erasure out of
64 independent panels, paired across policies and levels:

| Wanted C/N0, dB-Hz | Current | Guarded | Strict |
| --- | ---: | ---: | ---: |
| +5 | 41/23/0 | 22/12/30 | 13/2/49 |
| +6 | 49/15/0 | 36/3/25 | 17/0/47 |
| +7 | 60/4/0 | 51/1/12 | 30/0/34 |
| +8 | 63/1/0 | 61/0/3 | 47/0/17 |
| +9 | 64/0/0 | 63/0/1 | 61/0/3 |
| +10 | 64/0/0 | 64/0/0 | 63/0/1 |

Using 90% correct reception as an analyst-selected descriptive endpoint,
the first tested crossings are +7/+8/+9 dB-Hz. Thus this case has observed
Guarded/Strict shifts of approximately **+1/+2 dB** at 1 dB grid resolution,
equivalent to 1.26/1.58 times received energy at unchanged geometry. They
are not confidence-qualified exact dB shifts, general penalties, or equal
time multipliers under changing coherence. The paired panel counts with
any baseline-correct bit withheld somewhere on the grid are 40/64 and 50/64.
Report both incorrect bits and erasures: reduced total correct count alone
does not distinguish discarding a chance-correct interference bit from
losing a reliably received wanted bit.

The long Sub-9 reference uses physical carrier 8 kHz (200 Hz synthetic IF
plus 7,800 Hz Shift), Rate 0.1, GPSDO-OCXO/shared-clock declaration,
Sub9 Best channel and the full 78 coarse workspaces / 141 arrivals / two
frequencies. A requested 3,600 seconds quantizes to a **5,760.4-second bit**.
The foreign source is +6 dB-Hz at +0.0005 Hz. Its center lies outside the
tiny fixed oscillator grid (±0.0000008 Hz), but the observed pilot center
and common-wander range lie inside the admitted changing-channel range
±0.0010008 Hz. This is a synthetic declared channel, not field qualification
of propagation at these spreads. See the existing Sub-9 limitations in
[_ref's method/context](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/00-method-and-modem-context.md).

The first 100 Hz capture failed native Nyquist admission. A valid 600 Hz
capture exceeded a 24-second CPU pilot cap. Its CUDA pilot later exceeded
87 seconds during fine work after real GPU execution; neither incomplete
score is a rejection or a probability sample. An exact native CPU replay
completed in 27.54 seconds on the 5650U (`W=24`), including 14.42 seconds
of scoring. At `W=6` it completed in 36.01 seconds, scoring in 22.65 seconds.
Both inputs and final scores are identical: I/Q SHA256
`63cb73a5043cb4167c3200763fbe33c7ed9e0398f60ea89105018ff562c7ba94`,
operational scores 1244.099774/1205.945195. Current falsely admits this
fixed-aligned pilot; both added policies erase it. Tight-clock CUDA used
FP64 fine work and many small submissions. The cost difference identifies
a suitable measured CPU route; submission overhead is a source-based
explanation, not a completed causal GPU profile or a hardware ceiling.
The planned 900-second GPU retry was superseded by this cheaper completed
native route. These slot timings are not complete-receiver capacity tests.

A separate 32-draw Sub-9 cohort was frozen before its outcomes, with fresh
foreign keys, source bits, phases and channel/noise seeds, uniform nominal
overlap and slot 1. Actual scored window `W=6774.041667`, recurrence `C=6750`,
and foreign nominal support `L=6734.591667` are used for exposure. The sample
cap is ten million, admitting 8,110,224 generated samples and 4,064,425 scored
samples. The three policy upper bounds form a separate simultaneous 95%
family (`alpha=.05/3`). One strong wanted control is separate from its `n`.
Original manifest SHA256:
`43abb71b311f71a96e37006263b2dcc1594aeb09ad796ee3b6c7784be2bc6d7f`.
The original four-lane `H=3/W=6`, 120-second-per-call execution **failed**:
episode 027 hit its deadline, leaving eight completed calls (including the
wanted control), one incomplete and 24 unstarted. No fixed-N interval was
claimed for that incomplete execution.

The resource amendment keeps exactly the original 32 foreign draws, with
no replacement, exclusion or added sample. Two `H=6/W=12` lanes retain the
aggregate `W=24` budget, 2 GiB per-call allowance, a 300-second native /
315-second subprocess limit and a 5,400-second total cap. Replay manifest:
`9ec7f73b8eb712fb76659ce35a40fd1db3976d4acb40d0b6f9f032378db61681`.
Its first wave deliberately contains failed 027 and previously completed
slow 008; they complete in 72.39/71.13 seconds with identical input hashes,
and 008 also has exactly identical raw scores and decisions. Duplicate
executions are not independent observations. The endpoint remains the
completed-slot admission decision, not passing the original 120-second
service bound. The amended **32/32** cohort completed in 940.97 seconds.
All seven previously completed foreign inputs reproduce exactly the prior
I/Q hashes, raw operational scores and policy decisions; the originally
incomplete case also retains its input hash and now completes.

| 96-minute Sub-9, 32 independent foreign draws | Current | Guarded | Strict |
| --- | ---: | ---: | ---: |
| False DATA | 19/32 | 1/32 | 0/32 |
| Marginal one-sided 95% probability upper bound | 74.03% | 13.98% | 8.94% |
| Simultaneous 95% probability upper bound, three policies | 77.47% | 17.47% | 12.01% |
| Periodic 10% reference, estimated false windows/day | 1.524 | 0.0802 | 0 observed |
| Corresponding simultaneous 95% upper bound/day | 1.989 | 0.449 | 0.308 |

The exposure fraction is 20.0586%, or 2.56750 exposed windows/day. Zero
observed Strict admissions therefore does **not** establish a zero rate,
a thermal-level rate, or useful wanted reception. The separate wanted
controls below are essential to interpreting this apparent rejection gain.

All 32 direct foreign centers lie inside the expanded ±0.0010008 Hz search,
but only one full-capture center-plus-common-wander envelope is wholly
contained. These envelopes include pre-roll when the foreign source may be
absent; excursions do not prove that 31 actual foreign bursts were outside
the search. To distinguish this issue, a separate predeclared **16-draw
cochannel** cohort set foreign offset to zero, with fresh independent seeds
and otherwise identical geometry, power and overlap law. Both `H=6/W=12`
lanes retained aggregate `W=24`, 300/315-second native/subprocess caps and a
2,700-second total cap. Manifest SHA256:
`72224b8966d3fdc7dd40c7435e2964ca941ced2ceb02424fcf8683cc59d3b3d2`.
All 16 completed in 497.79 seconds, and all full-capture frequency envelopes
were contained. These draws are not pooled with the adjacent-frequency 32.

| 96-minute Sub-9 cochannel, 16 independent draws | Current | Guarded | Strict |
| --- | ---: | ---: | ---: |
| False DATA | 8/16 | 1/16 | 0/16 |
| Periodic 10% reference, estimated false windows/day | 1.284 | 0.160 | 0 observed |
| Marginal one-sided 95% upper bound/day | 1.852 | 0.678 | 0.438 |
| Simultaneous 95% upper bound/day, three policies | 1.976 | 0.830 | 0.580 |

The wider bounds reflect the smaller independent sample. Under the same
periodic traffic model at 1% occupancy, all reported daily point estimates
and confidence bounds scale by one tenth. This scaling does not cover a
different burst law or multiple interfering bursts in a scored window.

The wanted Sub-9 control exposed a separate, material sensitivity failure.
With no interferer, wanted bit 1, seed 40029721 and C/N0 +6 dB-Hz, Current
correctly receives the bit while **both Guarded and Strict erase it**.
Scores 19907.940448/21869.129450 give gap 1961.189002, below required
Guarded 1993.938012 and Strict 4976.905072. The bit/key/calendar and full
window are valid; this is caused by the final gate, not a source mismatch.

A fixed ten-call diagnostic then reused that seed/bit at −24/−18/−12/−6/0/+12
dB-Hz and added two fresh channel/noise seeds (bits 0 and 1) at +6/+12.
All ten completed, with a 300-second per-call / 1,800-second total cap and
the same aggregate CPU budget. Manifest:
`9e22392adb80db258bff3a9c13d1d73583dbcd9c6abb85522241b8ff860eecd4`.
These fixed diagnostic controls are not a probability population.
The probe varies C/N0 through its declared noise density at fixed normalized
waveform and receiver profile; this does not cover every physical signal-gain
change or noise-calibration error.

| Same-seed clean Sub-9 C/N0 | Current | Guarded | Strict | Gap / competing excess |
| --- | --- | --- | --- | ---: |
| −24 dB-Hz | Scored quiet | Scored quiet | Scored quiet | Competing score below threshold |
| −18 dB-Hz | Correct | Correct | Correct | Competing score below threshold |
| −12 dB-Hz | Correct | Correct | Correct | 2.0697 |
| −6 dB-Hz | Correct | Correct | Correct | 0.4197 |
| 0 dB-Hz | Correct | Correct | Erasure | 0.2121 |
| +6 dB-Hz | Correct | Erasure | Erasure | 0.0986 |
| +12 dB-Hz | Correct | Erasure | Erasure | 0.0428 |

The two fresh seeds remain correct under Current at both high levels.
Guarded passes one at +6, then erases it at +12; it erases the other at both
levels. Strict erases both at both levels. Thus raising modeled C/N0 can make
these presets **lose** a previously received wanted signal. There is no
single added-dB interpretation for this profile. Current remains the
appropriate unchanged default when preserving this tested long-window
sensitivity matters. This is not an asymptotic theorem for all keys/channels.
The receiver's short observation blocks and persistent gain models can fit
both slow keyed hypotheses; the noise-relative scores can grow faster than
their separation. This explains the measured trend without attributing it
to an arbitrary free gain in every block. No threshold retune or additional
DSP fallback was introduced to rescue the diagnostic.

The decisive clean wanted control remains cheaply reproducible with the
canonical native probe (automatic workers respect the caller's allocation):

```sh
build/dev/triad_receiver_probe admission --encrypt on --stratum active \
  --max-samples 10000000 --memory-limit-bytes 2147483648 --deadline-ms 300000 \
  --sample-rate 600 --data-if 200 --rf-hz 7800 --rate .1 --seconds 3600 \
  --base-oscillator gpsdo-ocxo --shift-oscillator baseband-clock \
  --spread-preset sub9-best --timing-policy broad --slot-index 1 \
  --cn0 6 --seed 40029721 --bit 1
```

Changing only `--cn0` to −6 or +12 reproduces the paired contrast. This is
a bounded diagnostic, not a new independent trial or a capacity pass.

Coverage remains bounded. Literal full-stress manual HF geometry hit native
public-template preparation limits before GPU scoring; the completed HF
cohorts do not qualify that case. A near-day Sub-9 CPU plan at 300 Hz sampling,
100 Hz IF / 7,900 Hz Shift, Rate .10001 and requested 86,390 seconds admits
an actual 86,394-second bit with all 78 workspaces / 141 arrivals / two
frequencies. Its 600 Hz counterpart hit the mapped-template work limit.
The valid lower-sample-rate near-day plan was not scored, so it supplies
neither a sensitivity estimate nor a wrong-key probability. Geometry alone
explains why a long window can encounter many short foreign bursts despite
10% occupancy; the one-burst cohorts do not qualify such repeated-burst
mixtures. For an illustrative `W≈C≈L≈24 hours` periodic reference, even
`p=1` gives about 0.2 false windows/day; with much shorter bursts every window
can instead be exposed and the geometry-only bound approaches one/day.
These are traffic/decision-count bounds, not measured preset performance.

Exploratory reapplication to the completed static score bank reproduced the
production presets, then checked stronger scalar alternatives without extra
DSP. With the Strict absolute floor, `rho=.5/1/2/4` leaves long wrong-key
counts 18/13/10/10 and short counts 26/4/0/0 (each out of 300). However,
mixed wanted +10 dB-Hz correct counts fall from 32 to 25/4/0/0 (out of 32);
at +5 all are erased by those alternatives. Eight long-case false admissions
already have the competing score below `T`, so increasing `rho` alone cannot
reject them. An absolute DATA log-threshold increase of `ln(10)`, `2 ln(10)`
or `6 ln(10)` leaves all 281 short-case false admissions; +100 nats leaves
272 while erasing every clean −4 dB-Hz bit. These same-bank counterfactuals
are not independently validated presets or rate calibrations. They explain
why no threshold retune follows these measurements.

The new `admission` probe and adjacent-frequency extension passed their
focused checks. The full native group passed 38/38 before the offset-only
extension. A first synthetic-key setup used roots of the wrong length and
ran no signal assertions; the repaired implementation derives two distinct
64-byte roots. Independent source review checked composition, normalization,
one-score policy evaluation, uniform placement and the frequency translation.
The final GUI help now warns that strong long-window wanted signals can be
erased and higher SNR may not restore reception. `./build.sh test gui` passed
30/30 in 154.46 seconds after that text change. No production thresholds,
DSP search, allocations or worker defaults changed in this follow-up.

The RunPod allocation was terminated and deleted after fetching the complete
GPU results. Provider billing then showed zero GPUs, vCPUs and storage, and
$0/hour ongoing spend. The observed account-balance decrease was about $3.83
(about 1.4 hours of allocation), not an invoice-certified job charge. No
persistent volume was created. Temporary local manifests, receipts and
analysis under `local-evidence/wrong-key-rates/` were removed after their
results were reviewed and distilled here; the reusable native probe, scoped
findings, original failures, reproduction command and provenance remain
maintained. Useful existing build caches were retained.

## Wrong Key Admission — 15 November 2026

Working change over `f674f9aeac3136917996a08b07f57496dfbe195d` adds the
shared GUI choice immediately left of Encryption key entry and the saved
`--wrong-key-admission current|guarded|strict` setting. Current preserves prior
decisions. Guarded and Strict apply the final competing-bit margin in the
[operating/research guide](triad-stochastic.md#wrong-key-admission), after
normal fallback and quiet scoring. Their nested DATA acceptance sets can only
remove bits; they cannot turn an erasure into quiet, change waveform duration,
reduce the public search, restart fallback or change `W=2*H`.

The finite diagnostic budget was fixed before each stage: the existing
strong/weak neighboring-carrier pair first; 15 native slot calls covering
noise, matched/foreign gains and four fixed receiver roots; then 18 calls on
two fresh carrier/bit/noise strata with weak wanted, wrong-key-only, noise and
mixed signals. No seed or timing-driven extension sought a favorable result.
These are mechanism/sensitivity checks, not a rare-event rate experiment.
Stop at this bounded evidence rather than attempt a multi-day tail simulation.
Tests use the canonical C++ scorer on the Ryzen 5 PRO 5650U, 12 effective
logical CPUs, 24 workers and native automatic arithmetic. They are not GPU,
field, unknown-address acquisition or complete capacity qualification.

The current fixture in `test_triad_native_receiver.cpp` uses seed 37,
Rate 120, a 1737 Hz configured carrier and an actual +12 Hz offset within a
±24 Hz search. Its strong foreign 1725 Hz receiver produces bit scores
1793.376/1742.063 and false DATA under Current. Both added margins erase it.
The matching strong score pair is 3552.108/14747.792; the one-fifth-amplitude
wanted control also remains DATA. Four deterministic foreign receiver roots
on the same capture have gap/excess ratios 0.0249, 0.1934, 0.0605 and 0.1234:
The roots set AES first bytes 1–4 and Serpent first bytes 14–17, with all
remaining bytes zero. The 15-call batch scores noise once, matched/foreign
pairs at amplitudes 1.0/0.2/0.08/0.05/0.035, then these four roots at 1.0.
Guarded erases two and Strict all four. This is dependent same-IQ evidence,
not four independent propagation episodes or a wrong-key probability estimate.

Fresh strata use `(noise seed, wanted bit, configured carrier Hz)` of
`(74,0,1743)` and `(113,1,1719)` with the same +12/±24 Hz geometry.
Foreign receiver roots set their AES/Serpent first bytes to 89/102 and 172/185,
respectively, with other bytes zero (synthetic test keys). Both baseline
wrong-key-only results are false DATA; both stronger policies erase them.
Actual Strict rescoring has exactly equal raw bit scores, coarse-workspace
counts, basis work and the 161,025,406-byte DSP lease. Matched amplitude 0.065
retains DATA at log evidence 23.845/26.786 versus threshold 21.493; amplitudes
0.057/0.061 already fail Current. Noise remains physically scored quiet.

The mixed controls add an opposite-bit waveform with configured carrier
31 Hz higher but the same received center/offset and noise seed, amplitude
0.38, as `noise + gain*(wanted-noise) + .38*(foreign-noise)`; wanted
amplitudes are 0.07/0.16. Current and Guarded receive all four wanted bits.
**Strict erases one wanted bit**, seed 113 at amplitude 0.07: bit scores
61.3182/68.0045 give gap 6.6862, versus required Guarded 6.2851 and Strict
14.5614. Preserve this cost. A dominant accidental single-bit fit can still
pass either policy. No daily-rate multiplier is assigned to these presets.

The archived protected first-aperture counterexample was also rerendered with
its original native libraries/headers and waveform context: matching I/Q
digests and wrong-key scores 15,934,599.373/15,487,375.909 reproduce the
[original false DATA](../_appraisal/highFidelity-triple_Robust_Modem/hf-normalized-null.md).
Applying the new scalar rule offline rejects it and preserves the wanted pair
194,740,790.599/31,591,513.615. That is an archived-score discriminator, not a
current carrier-bound waveform run. An initial scratch include/ABI setup
failure executed no valid assertions; matching the archived headers repaired
the reproduction. Temporary scratch implementations were removed after these
findings were distilled.
The output-only scratch executables' hashes were not recorded; these
diagnostics are source/recipe-bound findings, not hash-bound certification.

Current native correctness checks pass **38/38** (`CCACHE_DISABLE=1
./build.sh test triad`), including strict wanted-control checks for near-noise
threshold, rising/falling drift, long/broad drift and weak diffuse fading.
The probe's Strict five-slot `DDDQQ` stream decodes `e`, preserves both scored
quiet slots and the incomplete-EOF control. The initial default-cache build
failed before assertions because its configured sandbox-external ccache was
unwritable; rerunning with `CCACHE_DISABLE=1` passed without changing the
build profile or global cache. GUI tests pass **30/30** (`CCACHE_DISABLE=1
./build.sh test gui`). The initial frontend smoke run passed 7/8: its framebuffer
fixture expected eight Fast controls, while the current Fast screen has ten.
The discriminator compiled HEAD's baseline `src/gui/screen_console.cpp`
(without the new dropdown) into the current framebuffer test binary, retaining
the original eight-control cap; it reproduced `TUNE 10/10 > 8`
(exit 1). The Fast purpose fields entered in `7b31fde98` (12 November) while
the eight-control cap and exclusion note date to `0dcdfecf3` (29 September).
Updating Fast/Robust/Legacy inventories for both developer modes and asserting
Wrong Key Admission metadata setup/downward propagation fixed the fixture.
Final `CCACHE_DISABLE=1 ./build.sh test frontends` passed
**8/8** in 21.51 s; focused `gui_fast`, `gui_layout`, `gui_contract` and
`gui_launch_command` passed **4/4** in 3.91 s.

The final bounded service comparison made 24 native calls in 26.7279 seconds:
one untimed fixed preflight per binary and stratum, followed by three rotating
paired looks for each of two strata. All arms used 24 workers on the Ryzen 5
PRO 5650U (12 effective logical CPUs), Release/GNU 14.2, CPU AVX2 and native
automatic routing with FP64 guards. The stream uses FP64 exact-one-arrival
chirp-Z; the Helper/noise case uses Q15 products and an FP32 FFT. Baseline and
current are the isolated pre-change executable and
`build/dev/triad_receiver_probe`. OS-visible memory is 15,677,412 KiB;
automatic application memory limits varied from 9,054,653,645 to
9,284,935,680 bytes across the timed calls without changing DSP shapes.
The stream reached five simultaneous owners and all 24 executor workers.
They share build-options
SHA-256 `63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`
and configured commit `f674f9aeac3136917996a08b07f57496dfbe195d`; source
manifest SHA-256 is `ae0ac43a3497547c83e682394dc16c80d44c4622850e7ace83b44aca0ad43551`
for baseline and `fb50b74454701c5a1c4fb0dcccda80a7dcfde3ccbe53a1bdbac508124f765e02`
for current. Executable SHA-256 is `405fd9ef3ebd3453774e989e57922dd9bb6589684f2470ea6b07313a0dd637c4`
and `3189130c31f86be456227281dc268f972c2945107f276d7c168ec2fe9f06313d`,
respectively. Both calls use `--max-samples 600000 --deadline-ms 20000`.
The baseline stream command is `message --slots 5 --seconds .1 --rate 120
--shift-oscillator crystal --rf-hz 200000 --seed 74207 --cn0 9`; the
baseline Helper/noise command is `regression --stratum noise --units 1 --helper on
--helper-power-w .1 --sample-rate 6000 --data-if 1725 --seconds .1
--shift-oscillator crystal --rf-hz 200000 --seed 74207 --cn0 9`. Current and
strict calls additionally pass `--wrong-key-admission current` and
`--wrong-key-admission strict`; baseline passes neither. Per stratum,
baseline/current/strict were preflighted once, then timed in rotating orders
baseline/current/strict; strict/baseline/current; current/strict/baseline.
For paired log ratios `l_i=ln(candidate_time_i/baseline_time_i)`, the
descriptive interval is `exp(mean(l) ± 4.30265273 * sd(l)/sqrt(3))` (Student
`t`, 2 degrees of freedom), not a coverage claim for future runs.

| Stratum and identical input hash | Native slot scores and decisions (baseline / current / strict) | DSP work and peak workspace |
| --- | --- | --- |
| Five-slot `DDDQQ` stream, seed 74207, Rate 120, 9 dB-Hz; `89e3f100543c718c92334adcd1f38919bddc4f42f2836d4f71c5deab668757e1` | Slots 0–2 raw operational bit scores: `[72.98051924867104,-7.532447698163896]`, `[70.83041923270437,-5.810479836539603]`, `[-6.574686470242291,68.03587648452418]`; log-evidence 72.287372, 70.137272, 67.342729; decisions DATA 0, DATA 0, DATA 1 in every arm. Quiet-slot raw scores are `[-8.030673921296703,-10.199198588912878]` and `[-8.369328870885738,-7.737190299566725]`; both decisions are quiet in every arm. Stream decodes `e`, verifies physical completion and the separate EOF control. | Identical work: one arrival, 1794 coarse frequencies, 26 workspaces, four fine candidates, 47 affine/adaptive blocks, 12 adaptive peak states, 276 transitions, 846,576–846,768 basis rows; slot workspace 162,488,669–162,505,217 bytes. Coarse arithmetic is FP64 exact-one-arrival chirp-Z; Helper disabled. |
| Helper-enabled quiet/noise slot, seed 74207, Rate 120, DATA IF 1725 Hz, 9 dB-Hz; `ee5248625a9f3a35a6ef697ba0f1c8f02193e76383a2aa6e415eb14002a7a9f0` | Raw scores `[-8.086749025634216,-7.857600726371052]`, log-evidence −7.965626; baseline, current and strict all score quiet. | Identical work: one arrival, 1794 coarse frequencies, 26 workspaces, four fine candidates, 198 affine/adaptive blocks, 12 adaptive peak states, 1176 transitions, 14,325,360 basis rows; workspace 181,201,677 bytes. Q15 products with FP32 FFT; Helper reports `no_coarse_pilot`. |

Across both strata the three modes retain identical input hashes, raw scores,
decisions, DSP counters and workspace; this timing pair did not trigger a
wrong-key rejection. Paired receiver-service wall-time candidate/baseline
geometric ratios and descriptive small-sample Student log-ratio 95% intervals
were: stream current **1.0034** `[0.7861, 1.2807]`, strict **1.0203**
`[0.8380, 1.2423]`; Helper/noise current **0.9641** `[0.7599, 1.2231]`,
strict **0.9829** `[0.7479, 1.2917]`. Each interval spans 1 and is wide; these
three paired looks per stratum neither establish a timing penalty nor a speedup.
The comparison is scoped to those two native shapes and their receiver-service
phase; it is not a real-time or complete-receiver capacity certification.
An earlier Helper plan preflight was refused before receiver execution because
the fixed 550/950 Hz Helper tones overlapped the data-separation guard at the
requested Robust carrier; the measured Helper-enabled stratum instead uses
the explicit 1725 Hz DATA IF above. The initial ccache failure likewise ran no
receiver assertions.

After review and distillation here, the task's paired receipts, baseline
executable, orchestration script and framebuffer diagnostic artifacts under
`local-evidence/wrong-key-admission/` were removed. This summary is the durable
validation finding. No RunPod instance was needed for these short CPU checks.

## Configured-carrier waveform keys — 15 November 2026

The `independent-apsk-v2-carrier-v1` derivation adds the configured physical
carrier, rounded to `max(10 Hz, Rate × Spreading / 10)`, to every waveform child
context and the payload mask. Baseband plus Shift determines the frequency;
hops, Doppler and searched offsets do not. The GUI, native probe/exporter and
current appraisal source callers supply that coordinate. This replaces the
old derivation without compatibility fallback. See the
[wire context](crypto.md#canonical-robust-waveform-purposes).

The bounded native receiver check uses seed 37, Rate 120, Spreading 1, a
1737 Hz configured transmitter, an actual +12 Hz offset and a ±24 Hz receiver
search. The same I/Q is scored with matching 1737 Hz and neighboring 1725 Hz
settings (rounded to 1740 and 1728 Hz). At the original fixture gain the
operational log-evidence is 14747.1 versus 1792.68: **both are admitted** against
the 21.4934 threshold. The initial strict foreign-key rejection check failed;
frequency binding must not be described as reliable rejection of strong foreign
signals. The existing fixed-affine statistic compares against white noise,
not against an interfering keyed waveform, consistent with the earlier
[wrong-key findings](../_appraisal/highFidelity-triple_Robust_Modem/hf-normalized-null.md).

One prespecified follow-up scales both signal paths to one-fifth amplitude,
retaining the exact noise samples, key, geometry and search. Matching evidence
is 569.222 and the bit is admitted; foreign evidence is -6.26214 and no DATA is
admitted. Both scores complete. This demonstrates separation for one bounded
fixture, not an admission probability or daily false-admission rate. No seed
search, threshold change or receiver algorithm change was used. The requested
**Wrong Key Admission** control was deferred at that checkpoint, which added
only the configured-frequency key context. The subsequent
[heuristic control](#wrong-key-admission--15-november-2026) above retains this
original false admission as its default-policy regression control.

Existing drift regressions retain their original zero-centered synthetic
source coordinate. An intermediate test edit applied 1725 Hz to those sources
and the weak rising-drift example missed admission; restoring the original
fixture coordinate passes both rising and falling drift under the new key
domain. This is not sensitivity qualification for every keyed waveform.
Historical source-bound sensitivity and capacity receipts remain historical.

Validation on the working change over `cf2eb489`: `./build.sh test triad`
passes **38/38**, and `./build.sh test gui` passes **30/30**. The source tests
cover same/different buckets, the 10 Hz floor, fractional rounding and ties,
all three lanes, payload masking, invalid coordinates, equivalent baseband/Shift
settings and hop/Doppler independence. Three changed appraisal callers pass
syntax checks; no appraisal campaign was rerun. Local documentation links and
`git diff --check` pass. Builds used `CCACHE_DISABLE=1` after the default compiler
cache was read-only; that initial setup failure ran no assertions.

The old Python FFTScreen scalar receipt failed because it described the previous
keyed waveform. That obsolete value was removed; current coarse winner, rank,
public coverage, exact worker consistency and arithmetic comparisons remain.
A proposed singleton reference could not select the native scalar route for
this two-ray geometry, so it is not claimed as replacement oracle coverage.
The expanded monolithic receiver check exceeded its 60-second CTest deadline;
the same deterministic cases now run as separate channel strata, each retaining
that deadline (carrier 20.67 s, short/long/broad drift 5.50/6.74/7.13 s, fading
7.53 s, remaining native checks 31.10 s in this run). These are functional test
outcomes, not receiver-throughput measurements or release certification.

## Robust changing-channel receiver follow-up — 13 November 2026

The bounded changing-channel work extends the native GUI and receiver using a
declared synthetic family: up to 90 Hz common Doppler wander, an independently
varying echo Doppler within the same 90 Hz limit and an interior excess delay
within the configured 21 ms support (HF Worst at 3× margin), plus Each 65%
Gaussian-erf selective modulation. Frequency trajectories retain phase
continuity; echo power changes while the direct path remains present. The
simulator reports each realization. These are bounded engineering stresses,
not measured atmospheric distributions or a universal HF channel model. See
the [channel-model research reconciliation](triad-stochastic.md#nonstationary-channel-research-reconciliation)
for literature, the earlier weak-sweep failure and its labelled dechirp-oracle
control, and the limits of the chosen channel family.

The current native CPU path combines original coherent evidence with bounded
short-block changing-channel likelihoods, coherent-drift matching, a chronological
frequency-state mixture, and independent/causal-AR medium-duration Gaussian
gain alternatives. Frequency-state continuity alone does not preserve phase
coherence between the short blocks. The medium branch retains useful history
under its declared correlation model. It keeps the original
public coordinates and physical
quiet-slot test. On the Ryzen 5 PRO 5650U, the 12-hardware-thread allocation
resolved to `W=24`; the 6 kHz configured-calendar fixture retained an automatic
owner ceiling of 24, subject to its explicit memory admission.
The target was 32 dB-Hz and the simulated received level was 20 dB-Hz; each
DATA bit lasted 11.6 seconds. This is a focused low-level deterministic stress
fixture, not a sensitivity threshold or an absolute receiver-capacity test.

Three separate channel seeds (101, 503 and 1009) each decoded the expected
three DATA bits and then scored two physical quiet slots. Every replay verified
the full source and quiet completion. Each channel trace covered about 252.4
seconds of baseband samples; reported native receiver process CPU was about
30–31 seconds per replay, with 24 workers. Seed 101's DATA log-e values were
47.47, 55.87 and 59.84 against a conditional threshold of 21.49. The other
two seeds also passed all three DATA decisions. A 3/3 deterministic result does
not establish population reliability, general weak-signal sensitivity or
complete-receiver service for the manual workload suite. Separate 80 dB-Hz
three-seed controls passed at 1 GiB with 7 ms/30 Hz echo settings and 38 Hz
common wander; their easy coherent route does not test the low-level
90 Hz changing-channel state model.

The implementation stages retain their original outcomes. At a 1 GiB receiver
limit, seed 101 failed workspace admission before a scored slot. At 2 GiB, the
earlier receiver patch decoded the first two bits but left the third ambiguous:
log E 16.74, below the 21.49 threshold, so the declared message failed despite
physical quiet completion. The subsequent chronological frequency-state
patch passed seeds 101, 503 and 1009 at the same 2 GiB limit. These are distinct
source-manifest builds under configure commit `a0801df`; the 2 GiB intermediate
seed-101 report records manifest
`6609c39dc9ce8fb078cace6f6391e67209fe833a57e53d2adeb28daa9d45ebb0`, the tracked-frequency
seed-101 report `2dcec1ac2bd7c1ce8d0934cfd69278f1948435a81e78744e5f0a1d3d30ede4f2`,
and the seed-503/1009 reports
`c9e6a939020f6d8e8129bde251dbfc19879ba5ec8cac03de7efc0aba548f874f`. All were
CPU automatic arithmetic on GNU 14.2; they do not represent GPU execution.

With a 3 GiB limit, the Helper-present seed-101 DATA run also decoded all three
bits and completed two quiet slots, but all slots reported `no_coarse_pilot`;
the DATA result therefore does not demonstrate successful Helper acquisition
or Helper assistance. A separate Helper-only native fixture using the same
channel bounds correctly returned quiet (`log E = -4.3150`, quiet energy
4846.25 below threshold 5058.86), with 13 tracked states, zero DATA energy and
nonzero Helper energy. That compact owner-reported receipt is a control, not a
full stream-replay or broad Helper-acquisition qualification.

Additional bounded discriminators exposed two distinct weaknesses. A 34.8-second
weak linear sweep missed at both 497 Hz and 6 kHz capture rates, at equal noise
density `0.25/497` and signal gain 0.05; the matched static 6 kHz control passed
with log evidence 128.71. Removing the 131,072-row drift-FFT cutoff admitted
215,249 raw rows through reduced-rate proposals but did not alone fix reception.
Giving only the exact public timing to the production drift scorer restored
106.56 versus -10.62 evidence. This labelled oracle isolates a coarse timing
proposal failure; it is not an operational receiver input. Segmented coherent
public timing now repairs both cases: log evidence 126.02 at 497 Hz and 104.48
at 6 kHz. A fresh descending-sweep seed 983 with a different epoch and 17 ms
arrival offset recovered bit 1 with evidence 88.95. A 69.6-second, 6 kHz
holdout (seed 503, gain 0.06) recovered bit 1 with evidence 430.87 in 12.64
seconds at four workers. A separate 32 kHz stage check scored 1,123,420 raw
rows with reduced-rate proposals and exact final drift evidence 153.09; its
three supplied timing candidates included truth, so it qualifies long-capture
scoring, not acquisition. Fixed raw-row and ridge-count cutoffs were removed;
bounded proposal work, alias limits and memory admission remain explicit.

An independent native Gaussian-ray case (width 1.5 Hz, seed 101, gain 0.05,
34.8-second duration, 497 Hz capture) originally missed; its noise-only
control returned no bit. This family lacks the GUI stress model's persistent
direct path. Shared constant-frequency segmented proposals, proper power
mixtures and medium coherent gain integration repaired the miss. The final
public cadence is `0.375/max(0.75, width)` seconds, clipped to 0.25–0.5 seconds;
the causal AR option uses the declared positive width, with a 0.35 Hz fallback
when no width is declared. This finite AR approximation is not an exact
Gaussian Doppler spectrum. At 0.25-second cadence the original seed admitted
bit 0 with alternatives 49.55/10.73; a fresh seed 1009 with shifted epoch,
17 ms arrival and +4.5 Hz offset admitted bit 1 with 9.44/49.31. The matched
zero-signal control produced 7.40/9.43 and no DATA. Its short quiet aperture
does not qualify expanded-band quiet completion. These one-worker diagnostics
took 10.6–11.9 seconds. The maintained regression now checks the original weak
positive and its matched null at four workers.

A slower, weaker Gaussian holdout (0.15 Hz, gain 0.035, seed 211) remains an
erasure. Retaining causal history raised correct-bit evidence from 7.94 to
21.82, while wrong-bit evidence rose from 7.09 to 9.14, for about 2% more slot
time (10.90 to 11.12 seconds). This conditional sensitivity benefit justifies
retaining the AR alternative despite no admission on that trial. Earlier
descending-sweep gain-0.067 testing likewise retained a near-threshold miss
(19.34 versus 21.49); a stronger fresh gain-0.08 fixture passed. These finite
controls do not establish a sensitivity boundary.

A native tone-only negative control (0.5-amplitude, 80 Hz) falsely passed as
DATA with log E 1238.75 and a bit-score gap of 93.84. A subsequent proper
joint line-plus-DATA model rejects that control and recovers a keyed signal
coexisting with the line in focused native tests. It preserves unnotched
expanded-band energy for quiet classification. Maintained tests also pass a
1.5-amplitude line exactly halfway between coarse frequency bins and the same
line coexisting with keyed DATA. These checks do not establish rejection of all
interference or all off-grid lines. The GUI now plots the actual operational
bit alternatives and reports their required separation; its older affine and
adaptive traces remain diagnostics. Real TX PCM stays identical to the clean
canonical emission with simulation disabled in either duplex state.

A predeclared full-worker failed-Helper/null cost check before the wide-arrival
refinement used the native
slot path on one immutable proper-complex white-noise capture (seed 211), not
independent simulations as a throughput substitute. Settings were 6 kHz,
rate 36, ten added symbols with interleave, HF Worst/Each 65%, 3× margin,
target 32 dB-Hz and assumed DATA 20 dB-Hz, Helper 0.2 W, public arrival
[-7, 7.07] seconds, and a 6 GiB explicit lease. Its 428,840 rows covered
71.473 seconds. Native calendar metadata confirms 90 seconds between public
DATA starts, 11.6 seconds of active bit duration and 11.8675 seconds of source
support. Capture aperture and active duration are not recurrence denominators.
Both the supplemental-model-disabled control and enabled
receiver completed quiet with `no_coarse_pilot`. With automatic H=12/W=24,
wall times were 2.413/4.707 seconds, process CPU 24.280/41.231 seconds, and
slot DSP bounds 185,231,712/262,963,608 bytes. The enabled path reported
`segmented_timing_public_work_limit`, then executed short-state (1.207 s) and
drift (1.050 s) fallback work; medium gain is inapplicable to this broad width.
This exposed a gap in broad segmented timing admission that was investigated
below. It is a single affected-work comparison against a model toggle, not an old
binary comparison, full-stream memory accounting, sustained backlog service,
or complete-receiver capacity qualification.

Widening only a weak sweep's declared arrival/frequency support then exposed
an avoidable acquisition cliff. At 6 kHz, 34.8 seconds, gain 0.05, arrival
[-7, 7.07] seconds and combined center support ±90 Hz, the ordinary search
missed. Merely admitting three times the segmented work (80 short blocks,
161 frequencies) still missed in 12.76 seconds. Longer independent blocks
also missed: maximizing each block over all frequencies overfit noise paths
and discarded the true timing. Even exact timing initially failed because
the drift proposal's coarse slope spacing left gaps that its few ±1 exact
neighbors could not reach. A labelled exact-slope control restored evidence
126.97; that control is not an operating input.

The repair uses two public coherence scales (full curvature and curvature/8),
full combined center support, and bounded local frequency-path proposals in
the slower scale. Two FP32 score planes retain state; no sequence of channel
histories is enumerated. The shared spectra are charged together and private
per-worker scratch is charged at its sequential maximum. Eight timing
candidates reserve two for fast proposals, one for unlinked slow proposals
and five for smooth paths, filling unused places after deduplication. Local
frequency moves cover every grid cell within the derived hop. The exact drift
stage refines slope/frequency basins using bounded proposals and keeps original
public prior weights. A curvature/16 trial and independent long-block maxima
did not repair the miss; neither is retained as a competing implementation.

The final dual-scale receiver, with GPS-like static ±0.009 Hz plus ±89.991 Hz
atmospheric support, recovered the original seed 37 bit 0 with evidence 117.23
in 17.53 seconds, reserving 943,269,463 bytes at automatic W=24. A fresh seed
983, epoch +3, arrival +2.413 seconds and descending +5.5→−4.5 Hz recovered
bit 1 with evidence 133.43 in 18.29 seconds. The matched zero-signal control
returned no DATA in 17.64 seconds. A further predeclared seed 619 used gain
0.08, epoch +2, arrival +2.013 seconds and a +55→−55 Hz sweep; it recovered
bit 1 with evidence 219.77 in 17.46 seconds. Its short-block alternatives
20.61/20.85 were insufficient alone. All four used 6 kHz, the same broad
public bounds, fewer than 327,000 captured rows and a 2 GiB explicit lease,
without supplied truth coordinates. These are affected-slot measurements,
not sustained complete-stream capacity.

The dynamic work reference now uses combined oscillator-plus-atmospheric
support instead of the legacy oscillator grid alone. Maintained planner checks
compare 8+82, 0.009+89.991 and 90+0 Hz splits, including local CFO spacing,
arrival/epoch coverage, drift decimation and both coherence scales. A native
regression caught an intermediate loss on a previously passing narrow weak
sweep; the final geometry dispatch preserves its original proposal behavior.
This distinction avoids making a zero atmospheric field an accidental switch
for otherwise equivalent broad dynamic searches.
Final review also caught an over-padding admission cliff at 30/32 Hz: a
field/extent guard left local frequency spacing unnecessarily fine and omitted
the timing bank. Spacing now follows coherent block duration independently of
that fine-stage dispatch. Maintained planner checks admit 30/32/33 Hz (17×329,
17×351 and 18×273 block/frequency shapes); the measured 90 Hz shape and original
narrow 8 Hz shape are unchanged. This planner check is not an additional
reception measurement at those intermediate bounds.
A compact full-production regression retains the same weak broad-frequency
signal with a ±0.5-second public arrival interval (601 arrival points). It
recovers bit 0 with evidence 102.15 using both coherence banks and the smooth
proposal path, taking 5.84 seconds with four explicit workers and a
112,138,511-byte DSP bound. This is the maintained functional check; the
larger ±7-second replays above are bounded service diagnostics.

Repeating the identical failed-Helper/null cost capture after the repair gives
2.174/19.965 seconds wall time and 24.666/210.112 seconds process CPU for
supplemental models disabled/enabled. DSP bounds are
185,231,712/737,456,196 bytes at H=12/W=24. Both still return complete quiet
with `no_coarse_pilot`. The enabled result now executes the smooth-frequency
timing bank (17 primary blocks, with the separate faster bank), with 15.634
seconds coarse and 4.293 seconds fine work; short-state and drift components
take 1.410 and 0.852 seconds. This substantially increases work over the
non-varying model toggle, because the previously omitted broad dynamic search
now runs. It does not establish sustained service, other-device performance,
or reuse of earlier local CPU calibration. The single-slot DSP bound also
excludes complete-stream application, owner-stack and retained-result costs.

Validation uses deterministic strata before bounded signal/null discriminators:
Off/clean TX, streaming continuity, changing trajectories, weak structured
drift, independent diffuse fading, noise, line coexistence, long capture and
configured-calendar message/physical quiet completion. Each diagnostic used
one or the predeclared three seeds, stopped after the declared check, and kept
fresh holdouts separate from repair cases. No population reliability interval
is claimed from these small samples. The local processor was Ryzen 5 PRO 5650U
(6 cores/12 hardware threads, approximately 15 GiB RAM), GNU 14.2 native CPU
automatic arithmetic; proposal precision and final FP64 scoring follow the
production dispatch. No GPU execution, RunPod spending, other-device
qualification or calibrated manual-case capacity follows from this work.

The final `./build.sh test triad --stop-on-failure` passed **33/33** checks in
67.40 seconds, including the native receiver (52.13 s), GUI Robust (4.85 s)
and native orchestration (6.23 s). `./build.sh test gui --stop-on-failure`
passed **30/30** in 154.58 seconds before the last coarse-only admission repair;
the final triad run repeated the affected Robust GUI coverage. Earlier full
runs and their corrected setup failure (`off` instead of the supported `none`
spread preset) are distinct from this final pass. Native/data-path regressions
described above were actual executed assertion failures, not setup errors.
Changed-document local links and `git diff --check` pass.

Configure base is `a0801dfabff3e03d67be34203af7c8ec0f621ce0`. Final transitive
source manifest is
`be47453d07fb27afe4cf4c168f76390de193cf57a403009a1078dccca6f01aa2`,
build-options hash
`63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`,
and probe binary SHA-256
`0f88a51d5761413fb189d12fc4450f6411ec7baab3ae75889a2ac7c015c056f7`.
The final broad failed-Helper cost pair used source manifest
`eca6a1c8c753bd9dadf5a6cbdefd99cf398a56add3818c5f6636e02a022e907c`;
the last planner repair leaves its executed 90 Hz route unchanged. These
identities establish provenance, not release certification. The temporary
captures, copied diagnostic sources, binaries and logs were removed after their
failure/repair findings were distilled here; maintained regressions retain the
current reproducible checks. Compatible build outputs and caches remain in use.

## Fast acoustic acquisition and noise-reference follow-up — 12 November 2026

The cable was disconnected for this follow-up. Actual playback used the ALC257
Speakers port and capture used Internal Microphone through PipeWire's default
48 kHz analog devices, with left-mono output at the application's 100% setting.
No mixer settings were changed. The host is a Ryzen 5 PRO 5650U with approximately
15 GiB RAM; these are native CPU results, not GPU or other-host qualification.

The original [−9 dB diagnostic capture](../local-evidence/fast-acoustic-live/stress-minus9-419.json)
failed before training acquisition. That diagnostic value is between menu
choices. The former modeled receiver excluded rates below 800 baud, leaving
this slow waveform to a coherent fit that could not handle the selected
frequency-varying channel. The bounded extension admits compact acoustic-short
QPSK at 48 kHz, 180–<800 baud and ±100 Hz. Public startup geometry bounds its
epoch search. Smooth RRC modeling matches the transmitter's low-rate cubic
reconstruction: the 183-baud, eightfold-grid pulse check measured NMSE
`4.35e-12`. Canonical [replay of the identical failed PCM](../local-evidence/fast-acoustic-live/stress-minus9-repaired-replay.json)
now recovers exact bytes and scored physical completion, with no reapplied
channel effects. Replay rejects malformed/nonfinite PCM and retains EOF as an
incomplete outcome. A low-rate silence-tail defect was also fixed: emitted
silence now includes the full public observation aperture needed to score
six seconds of absence. EOF and decoded source lengths remain irrelevant to
physical completion.

Calibrated noise found a separate acquisition failure in both the original
coherent path and the production receiver: −10 and −13 dB reference-SNR fixtures
failed with or without a 30% echo at +100 ppm clock error. The long-preamble
frequency fit could select the wrong phase-turn alias at these slow symbol
rates. A scoped 64-symbol public-repeat estimate resolves that ambiguity;
independent marker checks are unchanged. The repaired [five focused checks](../local-evidence/fast-acoustic-live/weak-preset-fixed-tests.log)
pass preset selection and both flat/echo noise fixtures at both actual menu
settings. These use fixed white noise density over the original 17.5 kHz
reference band, exact source recovery and scored physical absence. The preset
solver already jointly selected constellation, coding and symbol rate; the
repair makes the corresponding receiver operate at its selected lower rates.
These deterministic examples are not a statistical reliability measurement or
qualification of simultaneous weak noise and HF-worst fading.

The subsequent native GUI runs uncovered a separate payload failure at the
default −6 dB profile despite acquisition and approximately 28–30 dB measured
reference-band signal power. The [seed-420 attachment capture](../local-evidence/fast-acoustic-live/stress-minus6-420.json)
reproduced a failed source frame after verified bootstrap. A bounded inverse
regularization adjustment based on the protected cycle's independently held-out
prediction residual recovered this same PCM and preserved seeds 417/419. It is
a prediction-mismatch correction, not a calibrated noise estimate. It changes
payload inversion, predictive presence and tracking; separate quiet projections
retain their original regularization and absence criteria.

The [exact `fox` seed-421 capture](../local-evidence/fast-acoustic-live/fox-minus6-421.json)
then exposed bootstrap failure, before protected feedback exists: 140 of 1,944
hard signs were wrong. Half/double initial regularization, applying the current
interval's pilot-tracker proposal, and a more permissive resolved-path split
did not recover it. Those exploratory implementations were removed. At the
same bandwidth and symbol rate, [rate-2/3 coding recovered `fox`](../local-evidence/fast-acoustic-live/fox-minus6-rate23-421.json)
in 12.525 seconds with both frames passing, and the [encrypted seed-420 attachment](../local-evidence/fast-acoustic-live/keyed-minus6-rate23-420.json)
completed authentically in 14.4 seconds with all three frames passing.
The former rate-3/4 failures remain relevant to explicit manual selections.

The short single-carrier QC/QPSK preset now selects rate 2/3 when the fixed
60-byte keyed fixture retains its interval count. Symbol rate, bandwidth and
short-text airtime stay unchanged. For the 1,944-bit case, steady public source
capacity decreases about 13.7% and keyed capacity 25%; a larger attachment can
need another interval. This is an explicit resilience preference, not a claim
of optimal throughput. Both endpoints need matching updated coding settings.
The [final focused batch](../local-evidence/fast-acoustic-live/stronger-short-focused-tests.log)
passed 8/8: preset and pulse checks plus actual reference-noise flat/echo
fixtures at −10, −13 and −16 dB. The −16 dB correction extends coherent
64-symbol offset unwrapping to 90 baud; it does not extend the sparse path
below 180 baud. [Raw-clock selection](../local-evidence/fast-acoustic-live/narrow-grace-fixed.log)
also passes one-sample and 4,093-sample chunks without duplicate intervals.

Before the coding update, physical probes passed the −6 dB profile with effects Off and with the
screenshot's crystal/IC-7100-at-30-MHz, HF-worst and each-65% AM effects. Additional
held-out [−10 dB](../local-evidence/fast-acoustic-live/stress-minus10-419.json) and
[−13 dB](../local-evidence/fast-acoustic-live/stress-minus13-420.json) profile tests
recovered exact three-byte sources and physical completion in 22.525 and
40.025 seconds of input. Receiver work took 3.146 and 3.283 seconds, maximum
single pushes 0.724 and 0.782 seconds, and bounded DSP workspace 58.18 and
57.66 MB. Neither capture clipped. Those menu values are waveform assumptions,
not measured acoustic SNR. The stronger `C/N0=40 dB-Hz` synthetic stress check
corresponds to −2.43 dB in 17.5 kHz and must not be described as −13 dB noise
qualification. No numerical CPU ceiling or arbitrary-room guarantee follows.

After the coding update, before the later baud cap, the actual native GUI button passed [public `fox`,
encrypted `fox`, and an encrypted binary attachment](../local-evidence/fast-acoustic-live/native-acoustic-stronger.log)
under the screenshot's selected impairments, in 12.65–12.67 seconds each.
The received Signals row, exact text copying and saved binary (including its NUL
byte) were checked. Actual capture and waterfall revisions advanced before,
during and after testing, including tab switches. The then-current rate-2/3
[−10 dB](../local-evidence/fast-acoustic-live/native-acoustic-minus10.log) and
[−13 dB](../local-evidence/fast-acoustic-live/native-acoustic-minus13.log) GUI
presets also delivered exact `fox`, in 22.67 and 40.22 seconds. Measured
reference-band SNR was approximately 28–34 dB in these room tests; these are
physical waveform/resilience checks, distinct from the calibrated weak-noise
fixtures. No physical −16 dB profile or combined −16 dB noise/HF-worst claim
is made. The original GUI failures remain in `native-acoustic-default.log` and
`native-acoustic-residual.log`.

The combined-noise review found a remaining gap: at −6 dB over 17.5 kHz,
with the selected oscillator, HF-worst and selective-AM effects together,
the 920-baud profile failed acquisition. An identical-noise effects-Off control
passed. Fixed-density `C/N0=36.430380487 dB-Hz` was used, not per-window
normalization of the fading waveform. An 8 dB selected-band margin at about
580 baud still failed; 9 dB at 461 baud passed only the original case, failing
independent cases. Diagnostic relaxed acquisition with rate-1/2 coding and
depth-two interleaving at 920 baud also failed a holdout, including the original
±280 Hz frontend. No relaxed acquisition threshold, altered decoy rule or
exploratory tracker implementation was retained.

The resulting automatic short QC/QPSK policy caps single-carrier baud at
231.13026: approximately 12 dB selected-band margin at expected −6 dB. Where
the resulting modeled margin is below 9 dB, rate 1/2 is preferred if the fixed
60-byte keyed fixture keeps the same interval count. Thus −10 dB uses the same
baud with stronger coding, while −13 and −16 dB narrow further. Higher-SNR OFDM,
manual overrides and radio presets are unchanged. Default baud decreases about
74.9%, on top of the coding tradeoff above; the short-text estimate grows from
12.30 to 32.67 seconds. This is a measured sensitivity/resilience tradeoff,
not a computational ceiling. The default now uses the narrow receiver's
±100 Hz coverage; the former 920-baud geometry used the wider path.

All six actual resolved-preset joint cases pass: −6 and −10 dB each use seeds
419/420 for encrypted attachments and 421 for public `fox`, requiring exact
recovery and physical completion. The −10 dB cases keep
`C/N0=32.430380487 dB-Hz`. The [focused batch](../local-evidence/fast-acoustic-live/joint-final-focused-tests.log)
also passed preset, recurring-transfer and flat/echo checks; its sole failure
was an outdated GUI band-edge fixture. The narrower default legitimately fits
the old 15 MHz crystal shift. A 16.7 MHz fixture preserves the intended
reject/accept/reject transaction, and [the rebuilt GUI test passed](../local-evidence/fast-acoustic-live/final-gui-test.log).
Combined HF-worst qualification below −10 dB is not implied by the separate
−13/−16 dB flat-noise/echo fixtures.

At that capped geometry, the [native GUI default check](../local-evidence/fast-acoustic-live/native-acoustic-final.log)
passed public text, encrypted text and an encrypted binary attachment in
32.06–32.09 seconds each, with 321–322 waterfall updates and continuing capture
across tab changes. The subsequent [−10 dB GUI check](../local-evidence/fast-acoustic-live/native-acoustic-minus10-final.log)
failed protected LDPC alignment. Two later captured GUI trials passed exact
`fox` in about 32 seconds, but do not causally repair that original intermittent
failure. Retain the failure and matched positive captures until the current
receiver investigation resolves their purpose. Expected −10 dB is still a
waveform assumption, not the measured room SNR.

The later Console-controls change separates declared receiver assumptions,
resolved wire geometry and injected test effects. Six fields share one compact
Console row. Mode switches preserve the declaration and wire format; ordinary
Live audio remains canonical, while Simulation and explicit loopback inject
effects. GUI Search margin expands receiver bounds only. Explicit adverse
acoustic-short declarations select the bounded QPSK single-carrier family;
explicit clean declarations recover the former throughput choices. Invalid
declarations block starts in every mode. The [focused checks](../local-evidence/fast-acoustic-live/assumptions-focused-tests.log)
passed presets, simulation and GUI behavior; expanded declaration and layout
checks also passed. Independent review restricted fixed SSB search admission to
its established ±280 Hz frontend envelope. Native compact-window testing caught
a one-pixel page overflow, then a separate detail-text overflow; these failures
remain recorded. The repaired [native GUI fixture](../local-evidence/fast-acoustic-live/declared-gui-console-contract.log)
passes with declarations editable on Console in both Live and Simulation,
channel drafts preserved, and correct mode/tab transitions. Captures at
1194×720 and 1244×720 confirm the complete compact row and footer fit. The
fixture also required updating its old expectation that Live disables these
controls; that was a stale assertion, not a production failure.

The broader GUI run exposed a separate monitoring defect: invalid declaration
or waveform drafts canceled a valid listener, including a valid restart already
queued by a preceding edit. Admitted listens now retain a validated settings
snapshot; invalid drafts preserve it while blocking new starts and displaying
the validation error. Valid edits still perform the normal serial handoff, and
device/key/mode/cancellation ownership changes retain their existing behavior.
The regression covers rapid valid-to-invalid edits, a declared unsupported
125-baud waveform, continuing PCM/waterfall updates and valid Auto recovery.
An initial rerun also exposed an inconsistent error-text assertion, which was
corrected without relaxing its exact capture-open or continuity requirements.

These fixed-ray results do **not** establish tolerance of randomly changing
atmospheric Doppler. A new explicit native Gaussian diagnostic uses independent
equal-mean-power rays at 0/7 ms, each with Doppler width 30 Hz = 2σ, without
adding the old inter-ray frequency separation or normalizing fades. With the
same declared limits, profile and strong fixed noise density, the
[fixed control](../local-evidence/fast-acoustic-live/declared-minus6-421-fixed.json)
received exact `fox`, while [Gaussian evolution](../local-evidence/fast-acoustic-live/declared-minus6-421-gaussian.json)
produced no acquisition proposals. Its ±90 Hz engineering spectral allowance
fits the admitted ±100 Hz search; this is a time-model failure, not an audio-band
or noise-level rejection. Local public-training holdouts show useful prediction
over roughly 4–8 ms, with substantial degradation across longer gaps. A native
blind timing/delay diagnostic at 1000 baud subsequently separated two Gaussian
cases from wrong public sequences and noise. A dense-pilot feasibility check
recovered public and authenticated protected cycles without payload truth, but
did not fabricate physical completion. Production streaming, oscillator/AM
coverage, weak-noise behavior and scored quiet were separate outstanding gates
at that component checkpoint.

The subsequent explicit 1000-baud QPSK candidate uses four data symbols between
sixteen-symbol public pilot words, rate-1/2 QC coding and one fixed carrier.
Its native receiver combines coherent and locally varying channel estimates
using independent public pilot evidence. Each physical ordinal enters one
codec once; integrity results do not select or retry a receiver model. A
filtered frontend, gain-scaled inverse regularization, recurring verified
marker timing and actual selected-symbol receipts repaired identified channel,
capture-level and display errors. This candidate is **not selected by Auto**.

Strong full-channel digital cases 419 and 421 recovered authenticated and public
content respectively, including six seconds of scored physical absence. The
retained clean speaker/microphone capture also recovered exact `fox` on replay.
Checks cover both directions of a clean/Gaussian transition within a coding
interval, gain factors 1/0.1/0.01, an interior erasure followed by resumed data,
EOF without enough observed absence, and a late public marker ordinal. A
12-second white-noise check stayed unacquired. After extending the early
frequency proposals, its maximum processing burst is 147.8 ms, modeled backlog
105.0 ms and workspace 4.18 MB. These are scoped functional and service checks,
not a maximum CPU-throughput measurement.

The current combined receiver's [clean reference-noise control](../local-evidence/fast-acoustic-live/dense-final-weak-clean.json)
passes at −6 dB in 17.5 kHz (`C/N0=36.43038 dB-Hz`), with exact two-frame
recovery and 6.275 seconds of physical quiet. Calibrated weak Gaussian reception
remains unresolved. The [full-stress physical case](../local-evidence/fast-acoustic-live/dense-acoustic-full-421.json)
does not acquire: capture peak is 0.911, with no clipped/nonfinite samples or
reported discontinuity. Its [matched digital seed/configuration](../local-evidence/fast-acoustic-live/dense-digital-full-421.json)
passes exact two-frame reception and 6.264 seconds of quiet, implicating the
additional physical path rather than that fading realization alone. Receiver
work was 6.742 seconds for the failed 21.886-second physical capture and 2.440
seconds for 19.211 seconds of digital input. Neither high-SNR digital success
nor a clean-room replay qualifies live combined stress. A separate cadence-only
reentry check exposed two issues: provisional data was assembled before the
model verified its own marker, and selecting only the first tied delay proposal
could discard the independently verifiable path. Data assembly now waits for
own-marker authorization; a fixed four-candidate proposal shortlist preserves
ties for the unchanged independent holdout. The targeted reentry check recovers
the subsequent interval without any pre-authorization data evidence. This
does not repair the separate full-stress physical acquisition failure.

An additional control exposed start-time dependence: prepending 15,505 zero
samples to the same passing strong digital waveform, without filtering,
resampling or new channel effects, loses acquisition. Its receiver service was
4.774 seconds for 19.959 seconds of input. The former strong acquisition ranked
phases on one short snapshot-relative training window, so moving the launch
changed the sampled fade. Pooled public proposals repaired that early rejection,
but initially exposed biased startup clock estimates during payload. A failed
startup-only timing refinement was removed. A scalar public-pilot correction
now adjusts demapping on fixed raw rows with separate proposal and held-out
pilots; it does not change physical cadence or earlier receipt ranges. Exact
public reception passes at zero, 15,505 and 24,193 samples of leading silence;
keyed case 419, clean −6 dB reference noise and the noise/erasure/EOF controls
also pass. A verified recurring public marker can now correct the epoch without
altering the clock slope or earlier receipts. The bounded search continues at
later markers after missing markers; a seven-interval check recovers after four
absent markers. A separate encrypted 128-byte attachment under full Gaussian
case 419 recovers all five coding frames, exact authenticated content and
6.2645 seconds of physical quiet. It uses 4.968 seconds of receiver work for
35.147 seconds of input and 13.09 MB DSP workspace. This qualifies that sampled
longer transfer, not arbitrary message length.

A flat-channel frequency-edge check exposed a different early ranking defect:
the true training phase was discarded before its carrier offset was tested.
A bounded coarse frequency bank now ranks phases before the unchanged marker
verification. Both −99.822 Hz and +99.661 Hz edge checks receive exact `fox` and
score physical completion. The same fixed onset, keyed, weak-clean and negative
controls still pass; no marker threshold was relaxed.

A weak-marker prior now derives its gain/slope variance from disjoint public
training, measured sideband noise and declared channel variation. It treats
the candidate and decoys identically and preserves the verification gates.
The explicit 1 kbaud Gaussian −6 dB case now acquires and observes physical
quiet, but one of its two LDPC frames still fails: this is an acquisition
improvement, not a successful weak transfer. Clean −6 dB and the strong keyed
five-frame controls remain exact. Two further bounded diagnostics pooled the
existing public pilot witnesses, then applied the same prior to their local
fits. They retained more units but still failed complete decoding, with about
10–11% hard errors among non-erased bits in the final diagnostic. Neither pilot
change is adopted. The experimental dense profile remains separate from Auto;
these results do not establish a default for weak, randomly varying propagation.
An evaluation-only matched replay then supplied the exact simulated geometry
to the payload inverse. Its identical PCM and pilot masks still failed the
same protected frame; first-interval sign errors fell from 181 to 164, with
the second unchanged at 195. This rules out timing correction alone as a
repair for that realization, without proving that recovery is impossible.

The original full-stress physical capture still does not acquire. A frozen
room-FIR replay initially had a failed identity control and was inconclusive.
After the onset/tile repairs, the same retained identity reconstruction passes
exact reception and physical completion, while its matched 25-tap, 10 ms FIR
counterpart still fails acquisition. Both use the same additional frontend,
onset and hardware-clock mapping. This now isolates static coloration as
sufficient to reproduce acquisition failure. A public-only factorized fit of
a shared 25-tap room response and two varying ray gains then reduced the
frozen control's marker error from 0.609 after two fixed updates to 0.0233
after eight. Its independent decoys remained above 13.7; an identity control
also passed. That result uses supplied timing and the exact reconstruction
operator. On the actual microphone recording, the corresponding eight-update
physical-operator fit plateaus in training and leaves marker error 0.517,
above the unchanged 0.14 admission limit. It therefore is not adopted in
production. The frozen-control success does not qualify actual acquisition,
and no private learned hardware transfer or channel seed is supplied to the
production receiver.

Both current `_tests_FastModem.md` rows were also run through the native
declaration-aware manual runner with the agreed −174 dBm/Hz noise density and
384-byte encrypted attachments. The original row margin is a receiver bound;
injected effects use margin one, matching the GUI. The [first combined run](../local-evidence/fast-acoustic-live/manual-declarations-simulation.json)
passed the HF row but stopped the EME row at the deliberately short 25-second
wall limit after both frames had validated. A [single EME rerun](../local-evidence/fast-acoustic-live/manual-declarations-case2-completion.json)
with a 40-second wall limit recovered exact authenticated content and physical
completion. These are two sampled simulation passes, with 6.122/6.129 seconds
of scored quiet. EME receiver service was 24.266 seconds for 22.272 seconds of
input, with 4.239 seconds of modeled backlog; this is not a sustained real-time
capacity pass. Actual acoustic manual-row checks are separate below.

The subsequent short actual speaker/microphone checks of both manual rows
failed before synchronization with capture-FIFO overflow. The [HF capture](../local-evidence/fast-acoustic-live/manual-declarations-acoustic-hf.json)
needed 9.852 seconds of receiver work for 1.889 seconds of consumed input;
the [EME capture](../local-evidence/fast-acoustic-live/manual-declarations-acoustic-eme.json)
needed 8.872 seconds for 1.718 seconds. Both filled the existing eight-second
FIFO. HF had 108 clipped samples in the consumed prefix and 429 in the complete
retained capture; EME had none in the consumed prefix but 107 in the retained
backlog capture. Both modeled playback waveforms briefly exceeded the DAC range,
where playback clamps them. These failures establish an
unsuccessful-acquisition service problem on the physical input, not successful
live manual-case qualification. Search cost and physical-path mismatch require
separate diagnosis; increasing storage alone is not a sustained-service repair.

A bounded exact-input profile found that repeated pulse and periodic-template
construction used 48.5% of SSB model-learning time. Direct periodic convolution
now shares the immutable transmit/word spectrum and explicitly preserves the
finite clipped endpoint cells and tap-time moments. Pulse, acquisition and
candidate-search checks pass 3/3. The same two-second HF prefix retains all
19 fits, 15 projections, eight gates and recorded marker decisions; the maximum
training-NMSE difference is `2.63e-14`. Receiver time fell from 11.203 to 6.506
seconds (1.72×), with essentially unchanged 60.70 MB workspace. This alone still
exceeds real time on unsuccessful acquisition. The timing control included
small diagnostic timer/trace overhead and is not a complete-transfer capacity result.

The subsequent bounded native acquisition pool preserves ordered decisions,
the full `2*H` worker budget and explicit memory admission. An earliest-public-
interval fence settles pending fits before the first interval can be lost.
The identical two-second prefix now takes 1.789 seconds, with all 19 fits
committed, no pending/discarded work, 15 helpers under the 24-worker budget,
815 MB peak reservation and 193,024 KiB measured process high-water RSS.
The increased 15.553 CPU-seconds represents productive same-stream overlap;
this short burst alone is not a real-time capacity qualification. Acquisition,
ordering/cancellation, pulse, low-power-tail/resumption and weak-terminal
checks pass, and the encrypted HF simulation still receives exact content
with 6.122 seconds of scored physical quiet.

Native paced replay of the complete original 9.900/9.725-second HF/EME
recordings finishes with no pending fits. Maximum scheduled-input backlog
is 4,220/10,884 samples against the 384,000-sample live FIFO; final lag is
4.55/3.40 ms. These recordings still fail acquisition. Fresh actual acoustic
tests at the selected 100% gain also finish normally without audio errors or
lost input: HF/EME peak FIFO occupancy is 6,000/26,400 samples. Both remain
unacquired and contain 914/75 clipped samples. A separate explicit 50% HF
control removes clipping (capture peak 0.768, requested playback peak 0.628),
but still does not acquire; its FIFO peaks at 7,904 samples. This separates
the repaired input-service failure from the unresolved acoustic acquisition
failure, and shows that clipping is not its sole cause. The diagnostic gain
does not change the GUI selection or system mixer. An exact-input trace
finds the correct raw epoch in the unclipped capture's proposals, but the
independent late-training prediction errors are 0.217/0.252 against the
unchanged 0.20 gate. The matched digital prediction error is 0.0159 and its
marker error 0.0228. Thus the true onset is present; the additional physical
channel defeats the current model before marker verification. See
[the scoped capacity record](fast-radio-capacity.md) for timing and resource limits.

Loopback SNR now uses an actual pre-transmission microphone baseline and
complete capture-spectrum windows within native observed-payload ranges.
Display-only timestamps exclude receiver lookahead and delivery latency.
The GUI labels the last loopback signal estimate and its capture age; quiet
input does not invent or refresh a signal SNR. Missing comparable baseline,
clipping, insufficient spectral resolution or observation support makes the
estimate unavailable. This is separate from EVM and never adjusts transmission.
Synthetic telemetry checks establish reference-band normalization and delayed
history association; physical GUI observations establish integration, not an
independent calibrated accuracy bound for a changing room.

The latest aggregate Fast run passed 69/72 checks. Two failures were stale
full-rate/weak-noise fixtures after Auto narrowed: their explicit clean-link
presets now preserve the original full-rate convolution oracle and 6 dB
selected-band FEC conditions, with all strict assertions retained. The corrected
filter, weak-QC and convolutional checks pass 3/3. The third failure exposed the
monitor handoff defect described above. The final shared GUI group passes
30/30, including its corrected rapid-edit and invalid-waveform regression.
The six focused dense-receiver checks also pass after the marker-prior change,
covering declared noise rejection, erasures, physical quiet, frequency edges,
transitions and recurring re-entry. These are combined rerun coverage, not a
claim that the original full run passed unchanged.

The [ordinary Live native acoustic check](../local-evidence/fast-acoustic-live/final-native-peer-rerun.log)
keeps the GUI's adverse receiver declarations while a separate native Session
plays canonical clean `fox` through the actual speakers. The normal receive
path produces the physically complete public Signals row and exact clipboard
text in 35.498 seconds; waveform/waterfall updates continue across Console and
Simulation tabs. Its first preflight failed before transmission because the
fixture read the margin's selection field instead of its text field; that
setup failure remains recorded separately.

The [latest actual acoustic loopback GUI check](../local-evidence/fast-acoustic-live/final-native-loopback.log)
then passes public `fox`, keyed `fox` and a keyed three-byte binary attachment
under crystal/IC-7100 at 30 MHz, fixed HF-worst spread and each-65% AM, at the
selected 100% application gain without mixer changes or synthetic loss/noise.
Exact Signals/copy/explicit Save (including the binary NUL byte), physical
completion and continuous before/during/after/tab plots pass. Durations are
32.060/32.049/32.151 seconds, with 321/321/322 in-test waterfall updates.
Measured signal SNRs are 29.5/37.4/34.5 dB in the 17.5 kHz reference band;
the selected −6 dB preset is not an actual weak acoustic qualification.
The latest native compact-window/mode fixture also passes; rendered Console
captures confirm the six-field row and footer fit at 1194×720.

The final [EME manual simulation refresh](../local-evidence/fast-acoustic-live/final-manual-case2-simulation.json)
also receives the exact authenticated 384-byte source over two LDPC frames,
with 6.128875 seconds of physical quiet. Receiver work is 23.213 seconds for
22.272 seconds of input (24.001 seconds whole-process elapsed and 34.660 CPU
seconds). This finite success is explicitly not a real-time capacity pass.
Together with the parallel-receiver HF simulation above, both current manual
rows have a complete native simulation result; their acoustic failures remain.

Both rebuilt GUI binaries pass their self-check. The Rev shared Fast check
passes 1/1. Its native Fast/developer visibility fixture first reaches the
chosen 90-second cap while making progress; a rerun within the repository's
330-second native allowance passes in 155.77 seconds, with 525 painted frames
and no failed frames. This uses software rendering and makes no GPU claim.

The [final development identity record](../local-evidence/fast-acoustic-live/final-identities.json)
hashes 92 source/configuration files, 11 final binaries and 33 evidence files.
It distinguishes the dirty worktree and combined rerun coverage from release
certification. Successful final native-capture PCM and rendered PPM
intermediates were retired after verification; compact logs, hashes and the
two inspected PNGs remain. Obsolete copied onset/layout diagnostics and the
rejected FIR/pilot experiment executables were also removed. Unresolved-failure
PCM remains available for the specific channel-model questions recorded here.

Temporary captures and receipts in `local-evidence/fast-acoustic-live/` support
the original-failure comparison and current acoustic acceptance. Retain them
through review of these changes, then preserve the findings here and remove
redundant recordings, diagnostic executables and copied source. This is a dirty
development-worktree checkpoint, not release certification.

## Fast live-channel and cable receiver follow-up — 12 November 2026

This follow-up supersedes the fixed 10% loopback gain and synthetic live noise
in the preceding checkpoint. Simulation retains the normalized receiver-input
link budget: increasing path loss lowers C/N0 and increases receiver noise
relative to the normalized signal. Live loopback ignores power, path loss and
noise settings entirely, including invalid retained drafts, and uses the chosen
TX volume. Explicit oscillator, spread and selective-AM effects remain enabled.
The three simulation-only fields are disabled in live modes.

Loopback now reports preparation, input/output opening, sending, physical
completion waiting and closing. TX queued and RX captured counts have distinct
meanings. Source EOF continues quiet playback until receiver completion or a
bounded failed trial, avoiding shared ALSA server drain waits. Stoppable ALSA
playback and suspended-stream recovery check cancellation. Capture startup,
post-source failure and pending audio handoff have explicit bounds; the complete
transaction retains its 75-second audio and 110-second elapsed limits. Foreign
driver open/configure/close calls still depend on the provider returning, so
these are application cancellation bounds, not a universal hard join guarantee.

A subsequent physical negative control exposed a second failure mechanism:
four failed full-size LDPC frames could block the consumer for 1.96 seconds,
overflowing wire's one-second capture FIFO. It reproduced without concurrent
build work. Full-size wire capacity profiles now reserve one second plus half
a second per locally selected frame (three seconds at depth four, at most nine
seconds). Existing modeled, OFDM and classic budgets remain unchanged. This is
bounded burst absorption, not sustained decode-capacity qualification; actual
overflow still fails closed. Cancellation also holds GUI controls until its
terminal snapshot has been published, closing a brief stale-status race.
The [repaired physical negative control](../local-evidence/fast-live-channel/live-wire-36-negative-buffered.json)
returns `decode_failed` after scored physical completion in 9.72 seconds, with
no published source or audio error. Its peak backlog is 92,400 of 144,000
samples. The [subsequent native cancellation/restart check](../local-evidence/fast-live-channel/native-cancel-restart-fixed-30.log)
publishes no content on cancellation, resumes capture, then receives exact
`fox` through the native button in 10.64 seconds. This also verifies that the
failed probe released audio. Original overflowing probes and the premature
GUI-status assertion remain in the same evidence directory.

The wire profile had two separate receiver defects. Long-window CFO estimation
aliased offsets outside approximately ±5.6 Hz; a public 64-symbol repeat now
unwraps it. HF Best could fail raw marker acquisition before equalization. A
bounded public-training decision-feedback candidate now handles that multipath
case, with independent marker signs and measured-input presence checks. Neither
fix changes transmitted geometry or selects a waveform using private channel
knowledge. See the [operating details](fast-mode.md).

Matched `fox` PCM that previously failed at Auto 25 dB/HF Best now decodes
exactly, including a held-out crystal-clock/IC-7100 30 MHz case with a 173-sample
leading offset. The [native regression](../local-evidence/fast-live-channel/wire-regression.txt)
also passes two input chunk sizes, a missing interior interval, noisy physical
quiet and repeated-training rejection. Measured complete decode takes about
1.12–1.15 seconds for 10.38 seconds of input on this Ryzen 5 PRO 5650U; reported
DSP workspace is 200,104 bytes, including fixed acquisition scratch. Training
without a marker rejects 1.89 seconds of input in 0.163 seconds. These bounded
checks establish neither a maximum throughput nor a reception probability.

The native GUI button, visible Signals row, exact text copy and continuing
waterfall were exercised through the connected analog headphone-to-microphone
route (PipeWire, 48 kHz, default analog input, left mono). Audio cable/Auto 25 dB
with HF Best passed at 30% in
[10.64 seconds](../local-evidence/fast-live-channel/native-wire-hf-best-30.log).
Adding crystal baseband and IC-7100 shift at 30 MHz passed at 100% in
[10.58 seconds](../local-evidence/fast-live-channel/native-wire-hf-best-shift-100.log).
The screenshot configuration—Speakers/mic short, Auto −6 dB, HF worst, each
65% selective AM and 30 MHz IC-7100 shift—passed public `fox`, encrypted `fox`
and an encrypted binary attachment at 30%, each in about 12.66 seconds. The
explicitly saved attachment preserved its NUL byte. The
[native receipt](../local-evidence/fast-live-channel/native-acoustic-30.log) and
[reviewed screenshot](../local-evidence/fast-live-channel/fast-live-loopback-received.png)
show actual received content and resumed live monitoring. No mixer settings
were changed. A separate clean-cable 30% probe recorded capture peak 0.174 with
zero clipped samples and confirmed synthetic link-budget application was off.

Auto 36 dB/4096-QAM with HF Best remains a negative result: acquisition succeeds,
but decision-feedback errors prevent verified payload recovery. The
[bounded discriminator](../local-evidence/fast-live-channel/wire-4096-diagnostics.txt)
isolated feedback error propagation; a small soft-decision alternative also
failed. Correct-symbol feedback was a diagnostic oracle only, never a receiver
implementation or passing qualification. Bidirectional or pilot-anchored
sequence equalization remains a conditional alternative, not an implemented
feature. The passing Auto 25 dB case does not qualify this higher constellation.

The [full Fast group](../local-evidence/fast-live-channel/fast-suite.log) passed
53/53 in 369.67 seconds before the final queue/cancellation-latch adjustments.
The rebuilt [affected queue, Session and GUI checks](../local-evidence/fast-live-channel/final-affected-tests.log)
then passed 5/5 in 154.94 seconds after those adjustments.
The ALSA contract passed 1/1 in 14.75 seconds, and its remaining interleave and
continuous-clock checks passed 2/2 in 15.73 seconds. The initial focused
Session mismatch failure was a fixture clock error: prerecorded input was
consumed in real time while post-EOF playback ran accelerated. Its corrected
audio-paced fixture retains the exact-source mismatch assertion. The original
failure, native test compile error and premature cancellation-status assertion
remain available alongside their repairs.

Both final desktop executables pass their shared self-check; the Rev/Clang
build also passes its focused Fast GUI check 1/1. The
[source/binary identity receipt](../local-evidence/fast-live-channel/source-binary-identity.json)
binds 70 changed source/test/build files over `8f725cc2` and 13 binaries,
including the earlier dirty Fast work. No commit or release was made.

Temporary receipts under `local-evidence/fast-live-channel/` support these
current failure/repair and physical-audio claims; retire them when superseded.
The sole retained synthetic capture, `wire36-417-hf-best.f32`, preserves the
unresolved 4096-QAM feedback-propagation comparison; remove it once a replacement
receiver or a durable reproducer resolves that question. Redundant positive
captures, copied headers and standalone experimental implementations were
removed after their findings were distilled here.
This is local application and audio validation, not RF propagation or release
certification. Earlier checkpoints below retain their original source scope.

## Fast end-user loopback reception — 12 November 2026

The later end-user report supersedes the quarantine/idle-stop behavior recorded
in the earlier Fast checkpoints below. A successful Session decode previously
discarded its received file, and separate GUI history, outgoing-direction and
preview gates excluded loopback results. Selecting loopback also stopped idle
listening. These policies explained a passed `fox` test with an empty Signals
browser and retained plots.

Completed exact-source loopback reception now uses the ordinary bounded received
history, restricted text, copy/paste and explicit attachment Save path, with
`LOOPBACK TEST` provenance. Public checksums remain unauthenticated. The
controller serially hands audio from idle monitoring to the bounded test and
back; last-test status is separate from current capture. Tab/model changes do
not stop monitoring. Stalled detection follows actual PCM progress, and old
single-carrier constellation points are identified separately. At this earlier
checkpoint the fixed test gain was displayed as 10%; the later live-channel
follow-up above replaces it with the selected TX volume.

The opt-in native FLTK test exercised the actual Console button and visible
Signals widget on this Ryzen 5 PRO 5650U Linux host, through the connected analog
headphone-to-microphone route (PipeWire, 48 kHz, left mono, default analog input,
not the monitor source). The screenshot configuration was used: Speakers/mic
short, Auto −6 dB, 40 W/130 dB/−174 dBm/Hz, crystal baseband, IC-7100 shift at
30 MHz, HF worst 30 Hz/7 ms, each 65% AM and margin 1. Public `fox`, authenticated
encrypted `fox`, and an encrypted three-byte binary attachment all passed in
12.6911, 12.6923 and 12.7157 seconds. Text copied exactly; the explicitly saved
attachment preserved its NUL byte. Each test produced one visible completed
row. Waterfall revisions advanced 127 times per test and 6–8 times in each
0.7-second monitoring window afterward, including on the other tab. No mixer
settings were changed. See the [native receipt](../local-evidence/fast-end-user/native-cable-widget-wait.log)
and [reviewed native screenshot](../local-evidence/fast-end-user/fast-live-loopback-received.png).

The focused Session cable check and `gui_fast` passed. The first broader GUI
run passed 28/30: one old fixture incorrectly required a Live plot before
supplying PCM; the new mock could insert an idle-tone block between playback
blocks when its independent threads drifted. The fixes check actual capture
ownership and preserve the emitted virtual PCM timeline with stop-aware waiting.
The [corrected focused rerun](../local-evidence/fast-end-user/targeted-rerun.log)
passed all three tests: full `fast_session`, ordinary `gui_fast_live`, and the
new Application `gui_fast_loopback` regression (208.34 seconds total). Together
with the first run's 28 unaffected passes this covers the current GUI group;
it is not a single 30/30 run. The separate [native FLTK group](../local-evidence/fast-end-user/native-suite.log)
passed 3/3 in 109.67 seconds, including adapter and document conformance.
Original [GUI failures](../local-evidence/fast-end-user/gui-suite.log), build
setup failures and the two native-driver failures remain retained: the driver
first activated a hidden Console control, then assumed `Fl::wait` had completed
a presentation update. Neither failure qualified the full acceptance path;
the final native run waits for the actual visible, enabled button and row.

Both desktop executables were rebuilt. The alternate Rev/Clang build passed its
[self-check](../local-evidence/fast-end-user/rev-self-check.log) and
[focused shared Fast GUI check](../local-evidence/fast-end-user/rev-fast-check.log)
1/1; the physical native-button check above used FLTK. The previous Rev native
adapter qualification remains separately scoped; it was not repeated here.
The [source/binary identity receipt](../local-evidence/fast-end-user/source-binary-identity.json)
binds the changed source/test/build inputs over `8f725cc2` and the seven tested
or rebuilt executables, including the earlier dirty Fast work.

These are bounded local application/audio checks, not RF propagation, other
platform runtime or sustained receiver-capacity qualification. Temporary logs,
the screenshot and final identity receipt under `local-evidence/fast-end-user/`
support this current validation claim; retire them when superseded. The obsolete
Session-only cable helper source/binary were removed after this replacement;
their historical receipts and waveform findings remain separately scoped.

## Fast simulation and manual-channel investigation — 12 November 2026

The Fast testing page now uses the native analytic transmitter, sampled channel,
receiver and protected source codec. Controls cover the declared link budget,
oscillators, RF shift coordinate, Doppler/delay spread, selective AM and margin.
Live remains a separate audio-device mode. Simulation cannot acquire hardware
audio, and cancellation, EOF or a failed test limit cannot manufacture physical
completion. The native probe and ordered manual-case runner test encrypted XZ
attachments with exact byte comparison and record limits, channel/profile
settings, device formats, workspace, capture backlog and receiver costs.

On the Ryzen 5 PRO 5650U, the initial unchanged-receiver patch passed all
**39 Fast checks** and **29 shared GUI checks**, including analytic/real waveform
parity, continuous channel state, delayed tails, encrypted simulation completion
and cancellation. Native adapter checks exposed outdated tab assertions and an
invalid-draft re-entry issue; the assertions and draft editor were corrected.
All **three native FLTK checks** then passed on a private 1920×1440 Xvfb
display. Two test fixtures now account for window-manager resize limits;
the corresponding desktop probes passed too. A separate desktop run's external
clipboard contention was resolved by display isolation, not a clipboard change.
These checks are not a new release certification or a qualification of the
optional Rev frontend.

The public Fast SSB default is now QPSK rate-1/2 with 16,200 coded bits
(7,200 information bits), depth 1 at 48 kHz. The typed receiver facade dispatches
this profile and the exact explicit 4096-QAM/8/9/64,800-bit profile to the
modeled SSB receiver; unsupported sample rates and unqualified variants retain
the standard SSB fallback. The focused dispatch group passed 9/9 tests
([log](../local-evidence/fast-channel/ssb-v3-final-facade-focused.log)). The
shared Session loopback test also passed on the EME419 cable case: an encrypted
31-byte diagnostic authenticated over 16 intervals/two LDPC frames/four
iterations, physical completion was observed at tick 1,084,512, and 512 actual
input I/Q points were reported. It exposed no file, peaked at 5.823 seconds in
the 8-second capture queue, and reported no Session error; ALSA warnings remain
in stderr. This is bounded Session/cable functionality, not an end-to-end file
transfer or a GUI-button hardware run. The loopback action received direct GUI
testing and independent source review; Live audio remains unqualified.

The first final Fast CLI group run exposed a stale public-SSB assertion that
required throughput above 1,000 bit/s. The adopted 50 MB public-file geometry
computes 912.222 bit/s including exact wire and tail overhead; encrypted bulk is
895.317 bit/s. This is a test-expectation mismatch rather than a receiver
failure. The corrected targeted `fast_cli` test passed in 31.45 s, and the
corrected `fast_ssb_pulse` test passed in 0.05 s; see [CLI rerun](../local-evidence/fast-channel/ssb-v3-final-cli-corrected.log)
and [fixture rerun](../local-evidence/fast-channel/ssb-v3-final-fixtures-corrected.log).
The original combined 52-test run passed 49 and failed three: that CLI assertion,
the `gui_fast` fixed diagnostic-action assertion, and the short-LDPC profile
validation. Correcting the first GUI assertion exposed another stale expectation:
that an empty Console draft should block loopback or bypass device acquisition.
The final test now checks active loopback status and `Waiting for device`;
direct GUI testing passed and independent review accepted the test-only changes.
No production source changed for these three corrections. In that earlier
checkpoint, the GUI CTest group passed 29/29, but its wrapper exited 1 after
sandbox Xvfb could not bind local sockets and cleanup reported a missing
process. That wrapper result was an environment failure, not a GUI test failure
([log](../local-evidence/fast-channel/ssb-v3-final-gui-group.log)). A
separate native run outside the sandbox passed all three GUI workflow,
adapter and document checks under private Xvfb ([log](../local-evidence/fast-channel/ssb-v3-final-native-group.log)).
The software display emitted AMDGPU permission warnings; this was not GPU
execution or a GUI-button hardware run. The original 52-case batch had 49
passes; all three failed cases subsequently passed their corrected focused or
shared-GUI checks. This coverage came from separate runs, not a single 52/52
batch.

Both exact `_tests_FastModem.md` rows failed acquisition in the initial receiver
and in separate bounded 60-second physical cable tests. The operator confirmed
−174 dBm/Hz and the usual connected cable; the failed manual-row captures had no
clipped samples and at most 25 ms backlog. A shortened clean encrypted cable
baseline recovered, and an independent flat 35 dB software case recovered
80 KiB at 10.58 kbit/s including startup and quiet. The −28 dB one-baud prefix
failed its first full acquisition opportunity. These original results do not
qualify complete sensitivity.

Separate acoustic-short checks used unencrypted QPSK/LDPC 1,944-bit, rate-3/4,
depth-1 software messages. The original FOX/long-source exploration used an
intermediate `datapump/fast/v2`, `short/v1`, `sc-training/v2` wire identity.
Its initial baseline `fox` and 1,024-symbol/16-pilot-per-16-data candidate
failed, while a 2,048-symbol preamble with 16 pilots per 32 data recovered the
three-byte `fox` on seeds 417/418. Its seed-418 640-byte six-interval run first
failed three LDPC frames; receiver-only protected feedback with three disjoint
FIR fits and 104-bit alignment fill excluded then passed. These exploratory
results are not current-format qualification; the truncated candidate JSON and
initial failure remain preserved.

Current v4 software receipts recovered the public three-byte `fox` on seeds
417 and 418, with two LDPC frames, no frame failures and physical-quiet
completion. On seed 418, a 640-byte adverse fixture passed all six physical
intervals with identical outcome hash across 1,201-, 257- and 4,096-sample
input chunks; each run had zero frame failures and about 5.43–5.46 s receiver
time for 19.82 s audio. A weak-AWGN v4 `fox` also recovered exactly in two
intervals for all three chunk sizes on the selected coherent `standard-fast`
path. This was not a separate full sparse-only run, so it does not establish a
current-v4 sparse-route failure. These matched results verify
only the stated deterministic fixtures and chunk invariance, not sensitivity,
population reliability, long-bulk service or capacity. See the
[current simulation receipts](fast-radio-capacity.md#acoustic-short-manual-row-follow-up)
and [binary hashes](../local-evidence/fast-followup/final-core-binaries.sha256).

The Live loopback action sends the selected nonempty Console message/file
through Session, publishes captured input, applies fixed native gain/time bounds
and does not truncate the source. Current-v4 selected-source cable checks passed
for both public and encrypted three-byte `fox` at the acoustic-short −6 dB
preset (not a measured cable SNR): two of two LDPC frames, zero
failures and physical completion, with encrypted authentication. No file was
exposed. Captured queues peaked at 150,128 and 141,728 samples respectively;
the actual input diagnostics retained the final 512 points. Both used the
default analog 48 kHz left-mono route at gain 0.1. Input polling observed 125
capture-progress updates per run, with maximum gaps about 100.4 ms; this does
not bound GUI paint/scheduling cadence. ALSA slave-open warnings
preceded the working PipeWire path and no Session error occurred. Input-clock
completion ticks are not comparable to the transmit-side quiet-start timestamp,
so do not infer cable quiet latency from those fields. This confirms a short
same-host Session loopback, not full acoustic/manual-row performance. See the
[parsed evidence](../local-evidence/fast-followup/fox-cable-evidence.json),
[public](../local-evidence/fast-followup/fox-cable-public.jsonl) and
[encrypted](../local-evidence/fast-followup/fox-cable-encrypted.jsonl) receipts
and their retained stderr files.

The initial GUI run passed 28/29; the stale `gui_fast` assertion then passed in
a corrected 1/1 focused rerun. Separate native FLTK checks first measured the
common SNR line at 1,267 px against a 1,212 px limit, then passed 3/3 after it
was shortened. Console,
Simulation and Modem Details screenshots were reviewed at 1,194×720 and
1,244×720. These are separate test and display checks, not one combined suite
or hardware-button qualification ([GUI runs](../local-evidence/fast-followup/gui-final.log),
[corrected assertion](../local-evidence/fast-followup/gui-assertion-test.log),
[initial FLTK](../local-evidence/fast-followup/native-fltk-final.log),
[repaired FLTK](../local-evidence/fast-followup/native-fltk-repaired.log)).
The Fast core CTest group later passed 49/49 in 267.24 s, including typed
controls ([log](../local-evidence/fast-followup/fast-final-ctest.log)); three
GUI-focused checks ran separately, not as one 52-test passing run. A subsequent
source review identified a text-fit risk in a long Loopback Details note that
the earlier width checks did not cover; this was not an observed overflow. The
note was split across two lines and the FLTK fit loop extended to cover all
profiles, live SNR and Loopback text/file states. The narrow `--fast-mode`
native fit check passed at 720 px height and 1,194/1,244 px widths, and its
focused GUI test passed 1/1 after the final wording change. See [fit receipt](../local-evidence/fast-followup/native-loopback-fit-receipt.json),
[native log](../local-evidence/fast-followup/native-loopback-fit.log) and
[GUI rerun](../local-evidence/fast-followup/gui-loopback-fit-test.log). These
close the Fast GUI width checks; the separate Rev runtime results follow.

The Rev adapter's initial native group passed 4/5 but failed the Robust→Fast→
Robust minimum-size transition. A pump-only reproducer confirmed the mismatch:
the physical client height and minimum-size hint were both 1,134 px, while at
scale 1.04205 the logical height remained 720 px against its 1,088 px minimum.
Source review traced
the notification gap to `NativeWindow::setSize` updating its cache before the
matching `ConfigureNotify`, which then omits the resize callback. The narrow
`RevApp::apply_window_sizing` fix invokes the existing `onResize`
handler only after its own enlargement, preserving native API semantics. Source
review accepted the change. The first fix build failed before assertions because
Xlib's `Cursor` name conflicted with the Rev style enum; the corrected build
succeeded ([setup failure](../local-evidence/fast-followup/rev-minimum-fixed-build.log),
[corrected build](../local-evidence/fast-followup/rev-minimum-fixed-build-2.log)).
The full Rev adapter run then passed with 994 frames in 320.83 s, within its
unchanged 330 s limit, and the separate native GUI group passed 4/4 in 20.24 s.
Together these are five checks across two runs, not one aggregate test group.
Runtime evidence is Linux with private Xvfb and software GL/LP2 only; macOS and
Windows remain unqualified ([initial 4/5](../local-evidence/fast-followup/native-rev-final.log),
[full adapter pass](../local-evidence/fast-followup/rev-minimum-visibility-fixed-test.log),
[native GUI 4/4](../local-evidence/fast-followup/rev-native-remaining-fixed-test.log),
[pump reproducer](../local-evidence/fast-followup/rev-minimum-pump-test.log)). The
current source/binary binding is recorded in the
[follow-up identity receipt](../local-evidence/fast-followup/final-source-binary-identity.json).

The canonical fixed 16/16-pilot v3 HF417 full run decoded both LDPC frames but
failed physical-end completion at EOF, withholding the file (208.534 s
whole-process CPU, 206.019 s receiver wall / 67.8155 s audio; 139.705 s modeled
backlog). Fixed-8 HF417 later passed a full native simulation and one physical
headphone-to-mic cable run: exact encrypted 367-byte authentication, 64
intervals, two LDPC frames, and about 6.1 s quiet. Cable RX was 67.0086 s for
68.0793 s audio; the 8-second FIFO peaked at 6.448 s and drained to 2.121 s.
Audio-ready VmSize exceeded 512 MiB, so the successful cable used a 1 GiB
virtual-memory limit. The earlier 512 MiB FIFO overflow and separate
`std::bad_alloc` are retained. This is one seed and one host/device, not RF,
reliability or receiver-capacity evidence. Fixed-8 interruption/resume and
positive/null controls passed. See the [HF results](fast-radio-capacity.md#native-v3-receiver-evidence).

For QPSK depth-1 at 16,200 bits, the actual protected payload is 7,200 bits
(the nominal rate-1/2 label). HF and EME both passed the exact 367-byte short
simulation, but its 895 bit/s bulk estimate is below the 64,800-bit geometry's
1,052 bit/s estimate (both encrypted bulk estimates). Public 50 MB SSB finite-
source estimate is 912.222 bit/s including fixed overhead. Physical short-code cable checks also passed: EME on a
pre-value-only binary and corrected HF after an earlier retained-prefix refit
caused FIFO overflow. The corrected HF run preserved all 32,768 LLRs in matched
replay while reducing two refits from roughly 4.8 s to 1.14 s; on cable it
completed with two refits, 16 intervals and 2 LDPC iterations. EME completed
16 intervals/2 frames/6 iterations. Both have narrow single-cable provenance
and are not sustained capacity qualification. One optimized recurring EME
simulation recovered a 1,000-byte source over three exact cycles with two phase
updates, but overall service remained 1.0244 s/s. At the precise SSB short/long
family boundary, the nominal SNR label is allowed to differ because the
7,200/16,200 protected-bit format begins with a verified tracking geometry and
has a lower bulk ceiling than the modeled 64,800-bit family. Other channels and
within-long-family SNR labels retain strict monotonicity; no rate cap was added.

Canonical 64,800-bit EME has not passed physical completion: its cable attempt
overflowed the 8-second FIFO after 22 intervals and 80 no-detections from
22.14 s, with no bootstrap or authenticated file. The earlier sparse full
simulation also failed late (57/64 intervals, three erasures); the forced-local
prefix failed startup, and a fresh pooled-start pair missed its fixed gate. A
separate midpoint-model EME simulation passed one exact source but was slightly
slower than real time by receiver wall and is not cable evidence. Keep the
failures and this isolated simulated pass distinct; details are in the
[capacity findings](fast-radio-capacity.md#native-v3-receiver-evidence).

A 4096-QAM, LDPC 8/9 flat-channel seed-417 run recovered/authenticated a
23,000-byte encrypted file over 160 intervals/five LDPC frames and five
iterations, with 6.149 s quiet; it used 20.645 s receiver wall for 33.792 s
audio (0.611 s/s). Whole-file goodput was 5.445 kbit/s; three measured source
areas gave a derived 10.693 kbit/s sustained interval rate after bootstrap/final
fill were excluded. A fresh seed-418 bootstrap was accepted but its
32-slot run intentionally stopped at 6.45 s without file or exported feedback
event. A separate exact 16-QAM short-code comparison passed HF and failed EME;
QPSK is the adopted common manual format, with no further constellation sweep.
Neither result qualifies a manual fading row; the high-rate profile remains an
explicit alternate, not the default. The exact profile dispatch is focused-test
qualified, but these measurements themselves are native probe results and not
manual-row evidence.

A reviewed outer-pilot prefix comparison found a flat-channel fit improvement,
but did not establish adoption benefit in the single HF/EME manual-seed
comparison. The layout used 20% pilot power and 54.2% fewer baud than full-band
v3, and failed the independent EME marker check; it did not test FEC, physical
completion or live acquisition. Keep this scoped finding and its limits in the
[capacity guide](fast-radio-capacity.md#bounded-outer-pilot-comparison).
The adopted QPSK SSB digital PCM spectrum has a 99% occupied interval of
404.66–2593.87 Hz and 99.9% interval of 338.75–2648.07 Hz; power outside
300–2700 Hz was −62.08 dB in the specified 2–18 s Hann-windowed measurement.
This is a generated PCM result, not radio or RF-emission qualification; see the
[spectrum receipt](../local-evidence/fast-channel/spectrum/README.txt).
Ignored `local-evidence/fast-channel/` retains compact receipts/hashes and three
actual ADC captures needed for physical-input and bit-exact replay findings:
the [original HF overflow prefix](../local-evidence/fast-channel/ssb-v3-hf417-short-ldpc-cable.f32le),
[corrected HF pass](../local-evidence/fast-channel/ssb-v3-hf417-short-ldpc-optimized-cable.f32le),
and [EME pass](../local-evidence/fast-channel/ssb-v3-eme419-short-ldpc-cable.f32le).
These samples cannot be regenerated by the synthetic channel model; remove each
when its replay/validation claim is superseded, retaining the concise result and
original failure. Obsolete copied sources, executables, archives and dumps are
removed. The compiled Session proof wrapper is also removed; its small [source](../local-evidence/fast-channel/ssb-v3-session-live/session_test.cpp),
[receipt](../local-evidence/fast-channel/ssb-v3-session-live/eme419.json),
stderr and [hash manifest](../local-evidence/fast-channel/ssb-v3-session-live/sha256.txt)
remain as the shared-Session validation record.

## Manual rate, spreading and burst trends — 12 November 2026

The eight current manual rows were screened for a lower computational burden.
None had a conservative capacity qualification applicable to this native
revision. The Nov 8 files named `robust-manual-*-current` retain historical
timings from another runtime and smaller worker allocations; they were not used
as current CPU calibration. The editable `_tests-RobustModem.md` was preserved.

These are **configuration comparisons, not implementation speedups or new
complete-capacity passes**. All tests used the current native CPU receiver on
the Ryzen 5 PRO 5650U, six cores/twelve logical CPUs, `W=24`, automatic arithmetic
and an explicit 9 GiB application limit. The full-slot comparisons executed
FP32 coarse FFTs with higher-precision fine/state processing. They retained each
row's channel, link power, clock and public search bounds, native target C/N0
32 dB-Hz and modeled duration. Changing rate or spreading changes the waveform,
work and sometimes cadence; it does not compare identical input samples.
Enabling spreading from Off also explicitly enables Interleave.

### Bounded method and measured directions

Native correctness passed **33/33** with `./build.sh test triad` (48.56 s).
Planning screened admission and the 24-slot calendar before timing. A successful
`plan` response with no slot workspace was rejected as inadmissible. Short
non-admitting fine diagnostics covered noise (2-second prefix and 1/2/4-second
continuation) and cold-maximum (two half-second segments and their transition).
They generally showed little change per processed second; they do not measure
full acquisition or all candidates. Material directions were followed with
production whole-slot `regression --units 1` and fresh seed 97021 after seed
84021, reversing baseline/candidate order. Whole-slot times below include the
separately measured audio projection, exclude simulation and one-time profile
preparation, and retain recurring receiver preparation.

The predeclared descriptive rule required at least 15% improvement on both seeds
to call a material repeatable direction; two observations do not supply a
confidence interval. HF-FHSS meets that rule on active and quiet slots. The
smaller row-5/7 improvements are directional, with varying magnitude. EME 2x
spreading meets it on quiet slots; active-slot changes ranged from 14.4% to
28.4%. Timing replicas are not a communication-success probability estimate.

| Row and changed setting | Active slot, baseline → candidate | Quiet slot, baseline → candidate | Change in cost per scheduled second |
| --- | ---: | ---: | --- |
| 2, groundwave GPSDO: Off → 2x Interleave, keep 1 Hz | Not measured | 11.029 → 8.728 s | **1.1% higher**: cadence shortens 690 → 540 s, cancelling the 20.9% slot saving |
| 5, HF encrypted GPSDO: 36 → 72 Hz, keep 10x | 1.413 → 1.201 s | 1.321 → 1.177 s | 15.0% / 10.9% lower; cadence stays 60 s |
| 6, HF-FHSS: 36 → 72 Hz, keep 10x | 20.754 → 1.783 s | 22.883 → 2.163 s | **61.3% / 57.5% lower**, after cadence changes 270 → 60 s |
| 7, HF unencrypted GPSDO: 36 → 72 Hz, keep 10x | 1.433 → 1.154 s | 1.330 → 1.178 s | 19.5% / 11.4% lower; cadence stays 60 s |
| 8, EME: Off → 2x Interleave, keep 360 Hz | 3.276 → 2.576 s | 4.155 → 2.877 s | **21.4% / 30.7% lower**; cadence stays 90 s |

Entries are two-seed means. Dividing a slot by its public cadence describes
scheduled slot demand, not measured steady-state receiver service. HF-FHSS's
full-slot window contracts from 271.673 to 37.135 s and its slot DSP/workspace
bound from **1150.2 to 293.2 MiB** (74.5% lower). Mean active coarse work drops
from about 11.50 to 0.65 s and fine work from 9.15 to 1.11 s. Its active-slot
busy-hardware fraction was 87–91%; worker permits were not used as the hardware
denominator. The initial 20-second HF-FHSS attempts timed out before a completed
slot receipt; those failures remain, separately from the bounded 45-second
follow-ups that completed. No timeout was treated as a measured slot duration.

The remaining rows and unfavorable directions matter:

* **Row 1, sub-9 kHz:** 0.1 → 0.2 Hz halves the planned receive window
  (6766.977 → 3390.977 s) and nearly halves its slot bound
  (1344.4 → 686.9 MiB). Fine cost per processed second stayed approximately
  flat. Cadence also nearly halves, so this does not establish lower average
  CPU demand. Enabling 2x Interleave at 0.1 Hz gives a 5310.977 s window and
  958.6 MiB bound, with no demonstrated fine-cost benefit. Full acquisition
  and whole-slot timing were not run for these long windows.
* **Row 2:** halving rate to 0.5 Hz increased measured sequential whole-slot
  phase work from 11.006 to 20.420 s while cadence changed 690 → 1350 s.
  The resulting 5.2% lower model duty is a single descriptive point, with a
  larger window and workspace. It is not a meaningful established ceiling
  improvement. Rates 1.5/2 Hz exceeded the bounded public-template work limit.
* **Row 3, groundwave Helper:** 2x Interleave reduced sequential phase work
  14.844 → 11.904 s, but cadence 720 → 570 s left model duty **1.3% higher**.
  Halving rate increases full-prior coarse tiles 4 → 302; its bounded noise
  acquisition did not complete. Rates 1.5/2 Hz also exceed the template limit.
  Sequential phase sums are model inputs, not production overlap upper bounds.
* **Row 4, HF Helper:** 54 Hz at 10x and 72 Hz at 5x are admitted. Active
  acquisition completed within the 16-tile diagnostic budget, with sequential
  phase costs 4.113 → 3.836 / 3.943 s. The corresponding quiet searches stopped
  at 16 tiles; they do not establish full failed-Helper service. At 54 Hz the
  planned full prior drops 359 → 284 tiles, but its sampled tile cost did not
  improve. Halving rate grows the full prior to 705 tiles. Keep these directions
  provisional. Rate 72 Hz at 10x, or spreading 20x at 36 Hz, overlaps the public
  Helper exclusion and is inadmissible.
* **Spreading is not a general cost reducer:** row 5 at 10x → 5x had a 10.5%
  active-slot saving but a 4.8% quiet-slot increase in the first matched pair;
  that direction was not promoted. In row 6, 72 Hz/5x has a 143.673 s window
  and 591.1 MiB bound, less attractive geometrically than 72 Hz/10x. Half-rate
  FHSS rows 6/8 and half-spreading row 6 have no protected dwell cells; row-6
  20x exceeds the template-work bound. These are configuration rejections.
* **Do not raise EME rate on timing alone:** 720 Hz missed DATA on seed 84021;
  540 Hz missed DATA on 84021 and passed on 97021. The failed active observations
  were scored quiet. Their faster quiet processing does not rescue reception.
  At unchanged 360 Hz, 2x Interleave passed both sampled active and quiet checks;
  4x was rejected by the fixed Helper-tone separation guard.

### Burst waits and complete short-message checks

Minimum waits are whole logical DATA-bit waits. They preserve the scored quiet
completion rule and do not reduce one slot's peak workspace. With the original
waveforms, the smallest tested wait that lengthened minimum cadence was one for
rows 1/2/3/6/8, and five for rows 4/5/7. The 24-slot minimum cadences were:

| Row | Minimum wait | Original → changed cadence |
| --- | ---: | ---: |
| 1 | 1 | 6750 → 12510 s |
| 2 | 1 | 690 → 1260 s |
| 3 | 1 | 720 → 1290 s |
| 4 | 5 | 90 → 120 s |
| 5 | 5 | 60 → 90 s |
| 6 | 1 | 270 → 540 s |
| 7 | 5 | 60 → 90 s |
| 8 | 1 | 90 → 120 s |

One matched five-slot `DDDQQ` message replay of original row 5 with wait 5
changed receiver service plus full capture audio projection from
7.602/266.473 to 7.564/386.473 seconds: **31.4% lower average service per input
second**, at longer message latency. Original row 8 with wait 1 changed
18.430/461.253 to 18.363/581.253 seconds, **20.9% lower**. All four replays
decoded `e`, scored both quiet endpoints and passed the incomplete-EOF control.
They are short startup/drain-inclusive replays, not ready-backlog qualification.
Peak reserved totals increased slightly (row 5: 1.825 → 1.846 GB; row 8:
1.897 → 1.925 GB); waiting is not a memory optimization.

Fresh-seed single-message checks also passed for row 5/6/7 at 72 Hz/10x and
row 8 at 360 Hz/2x Interleave, including both physical quiet endpoints and the
incomplete-EOF control. They support these bounded candidate configurations;
they do not establish population sensitivity, a conservative state envelope,
sustained backlog capacity or an unknown-calendar receiver. No modeled
nominal/conservative complete-service interval was qualified by this experiment.
Do not combine the separately tested burst and waveform changes and inherit
their results without checking the new calendar and reception.

### Identity and retention

Source was `98b82ccce0439a1882c68b14204abd2851cba29e`, native source manifest
`d32beb7d614f8f34fa239b6218402b176a6747fb85b63ac844b12340657e8a9a`, probe SHA-256
`3dbc006ac6615ceafa4379932c354cd3e67edbb754d9c408bdd481108e03d27b`, GNU 14.2
Release/AVX2. Manual-list SHA-256 was
`b04820861e1ffe4f8b22e2c1f7c0dfc77035cf43126f25f7f4a828fa0f5fdc6b`.
Local CPU timing directly answered this comparison; no RunPod resources were
created. Native probes were serialized, with 15-second fine, initially
20-second whole-slot and selectively 30/45-second follow-up limits. Sample
bounds were 4,194,304, selectively 9,000,000 for long groundwave observations.
All incomplete acquisition, deadline and decision receipts remain distinct.

Temporary plans, orchestration and raw receipts in ignored
`local-evidence/rate-spread-burst/` support these unresolved candidate settings
and their paired comparisons. Its `candidate-cases.md` is a separately parsed
eight-row input list: rows 5/6/7 use 72 Hz/10x, row 8 uses 360 Hz/2x Interleave,
and the other rows remain unchanged. Its normalized native arguments match those measured
above; it is a candidate list, not an adopted or qualified manual suite. Burst
alternatives remain separately described above. Retain these artifacts until accepted qualification or a
replacement comparison supersedes this claim; then preserve these concise
findings and remove the redundant raw files. Generated capture spools were
temporary native-owned files, not retained datasets.

## Current receiver overlap qualification

The current source has identified two admission/clock defects for bounded
follow-up: the receiver admission quote used a current-group maximum proxy
instead of the next window's full-`W` shape plus incoming payload only for
vacant ready-prefix positions (and could charge a phantom owner at `O=1` or a
full owner cap), and Helper arrival-clock bounds mishandled signed arrivals,
including negative allocation. The current changes address those rules; live
RSS admission still applies and does not promise unconditional admission after
process growth. Regression measurement is production whole-slot time; backlog
measurement is whole-stream ready-interval service. Summed serial slot and
continuation phases remain model inputs, not a universal upper bound.

Fine/source cancellation and owner-stop now propagate stage tokens while
preserving the recorded first failure; coarse Helper and mapped-profile owner
stops follow the same rule. Helper arrival-clock bounds now account for the
global capture-lead clock term and signed corner cases. Exact forward and
inverse corner bounds and prefix behavior were checked with nonzero
`first_sample`, real ±10 ppm offsets and more than one onset aperture; serial,
executor and reverse-order results matched. The focused Helper check passed in
0.901 s. Fine, stream and receiver checks passed 3/3 in about 29.7 s. The
forecast producer/consumer scope correction passed 9 focused Python tests.

Fine-owner observation work is bounded to one CPU dispatch ahead while owner
GPU statistics run, using existing full-`W` and two-ring bounds; it adds no GPU
reentrancy or threads. CUDA FIR staged-fill/GPU-compute overlap is implemented
with one first chunk per worker outside the device lock, one charged four-chunk
host stage, one packed upload, and CPU filling the next leaf while the prior
asynchronous FFT runs. The filter is the first leaf of the coarse stage;
device output and batch work remain serialized, with fixed memory and full-`W`
limits.

Current focused checks passed: triad 33/33 (89.58 s), GUI 29/29 (106.45 s),
and forecast producer/consumer tests 9/9. Sanitizer runs passed for Helper,
fine and stream paths. The full sanitizer coarse run and the earlier
overlap-only sanitizer run each timed out at 60 s; both failures remain
recorded. A 30 s coarse trace used 41.1 s CPU and progressed through fixed
cases and mapped W=3 before timing out on mapped W=1 direct. The focused ASan/
UBSan error/drain check using `--overlap-failures` passed in 0.086 s (exit 0);
it verifies waiting for peer stop/drain while preserving the original
exception. The full default-Release coarse suite passed after the final
test-only addition (17.10 s reported, 17.11 s wall). Five Ada component tests
passed.

Matched CPU measurements are descriptive only. Row 4 whole-slot mean changed
from 4.37909 to 4.30098 s (1.78% lower). The two-pair row-5, O=1/W=24
ready-input mean over 240 s of samples changed from 6.7975 to 6.6501 s;
measured busy fraction changed from 88.05% to 89.76%. These do not establish
receiver capacity. The baseline runtime manifest is
`0f827344bba1af64d0f821400e49c8ae6fbb67df25a45834bb458c80b3e934f0`; the
reviewed baseline source commit is `8b8429164c9ce60680329db8de52a0545985d495`,
despite stale configure commit
`22e92c3581b341c0c1293c24e084dc59fa493bb7`. Candidate manifest is
`d32beb7d614f8f34fa239b6218402b176a6747fb85b63ac844b12340657e8a9a`.

One matched actual Radeon run processed 279,864 packed-FP16 score rows; the
whole five-slot comparison changed from 1.016828020 to 0.977967 s. This is a
single-pair result, not an automatic-mode or capacity qualification. On the
full RTX PRO 6000 Blackwell, the original two-pair large-workload whole mean
changed from 3.603 to 3.101 s (13.9% lower), and a fresh-seed comparison
changed from 3.106 to 2.422 s (22.0% lower), with matched work/evidence. The CUDA
receiver comparisons used explicit W=32 and a 16 GiB host limit on both NVIDIA devices.
For the large FIR case, the host workspace quote rose by 1 MiB and GPU peak
memory was unchanged. The measured receiver path used half cuFFT and recorded
zero credited HMMA/IMMA calls through the instrumented lag-matrix paths.
Internal Tensor Core use in other library kernels was unmeasured. Separate
Tensor Core component tests passed for their tested operations and geometry.

On the RTX 4090 (Ada), the two-pair large-workload whole mean changed from
6.021763 to 5.395978 s (10.39% lower); a fresh-seed comparison changed from
5.788111867 to 5.000541055 s (13.61% lower). The failed-Helper whole comparison
changed from 7.415118945 to 7.149399353 s (3.58% lower), with the same work over
8,596 frequencies. The original small ready-interval mean regressed from
0.823282 to 0.870745 s (5.77% slower), while a fresh holdout improved from
0.894183115 to 0.789065660 s (11.76% lower); that holdout's whole-stream time
changed from 2.866356449 to 2.533790929 s. These bounded, mixed comparisons are
inconclusive for general backlog service. Both NVIDIA receiver paths used half
cuFFT with zero credited HMMA/IMMA calls through the instrumented lag-matrix
paths. Five separate Ada component tests passed.

On Blackwell, backlog evidence is mixed. The original small-backlog two-pair
mean was 5.7% slower on whole-stream time and 14.6% slower on the ready interval. A
fresh holdout changed whole-stream time from 0.910624566 to 0.786921768 s and
its authoritative ready-interval wall from 0.274719919 to 0.248533140 s (9.53%
lower). Retain the negative original result: these bounded pairs remain inconclusive for a general
backlog or capacity claim. Overall, no receiver-capacity, communication
sensitivity, global-optimum or end-to-end Tensor Core claim is established.
Retain raw artifacts under ignored
`local-evidence/overlap-review/` only for current qualification and
source/input matching, with NVIDIA receipts in its `blackwell/` and `ada/`
subdirectories. Remove these raw artifacts when accepted follow-ups supersede
that purpose.

## Receiver overlap and transform-order follow-through

**Question.** Does the native CPU receiver leave hardware idle behind serial
steps, per-wave barriers or fixed work shares, and does removing that waste
shorten elapsed same-stream service without changing any result?

Baseline is commit `22e92c3581b341c0c1293c24e084dc59fa493bb7`; the candidate is
this change on top of it. Both are Release builds from the same profile with
GNU 14.2.0, measured through `triad_receiver_probe` (SHA-256 `de87c65b…beb514`
and `01693f3f…d951d4`) on the Ryzen 5 PRO 5650U: six cores, twelve logical
CPUs, `H=12`, `W=24`, powersave governor. A desktop session used about 0.44
logical CPU throughout, so about 96% busy is this host's practical ceiling.

### Where the hardware was idle

Stack samples of the baseline single-owner row-5 replay found fewer than twelve
runnable threads in 11 of 70 samples. In the emptiest, 18–22 of 24 workers were
blocked behind one source-tile fill in the mapped basis, or asleep between the
fine stage's per-segment dispatches while the root predicted, pruned and
projected the next blocks. Coarse work used fixed shares: 26 lane jobs on 24
lanes, and one fixed share of each tile per lane. Helper acquisition, the
observation filter and the mapped-profile gate's controls each ran on one
thread with the rest of the pool idle.

Three measurements bounded what scheduling alone can recover:

| Observation | Result |
| --- | --- |
| Dynamic coarse jobs alone, quiet row 4, 64 tiles | Busy 88.2% → 94.1%, but elapsed only 23.9 → 23.5 s and CPU 251 → 265 s. |
| Current build, six pinned cores versus twelve logical CPUs | 6.10 versus 6.07 ms per public frequency, at 99.8% and 93.1% busy. SMT adds no coarse-search throughput on this host. |
| Observation filter split into parallel chunks, row 2, with the earlier stage-major transform | Coarse preparation CPU 16–17 → 27–33 s with no elapsed gain. Reverted; not repeated with the blocked transform. |

A profile of the quiet row-4 search then put about 47% of its time in the
transform's scalar parts: bit reversal, the first two stages and one call per
small butterfly group. Row 6 spent about 46 ms of CPU per 262,144-point
transform. The larger loss was unproductive busy time, not idle time.

### Changes

- One executor stage per owner with urgent and background leaves; see the
  [overlap contract](development.md#overlap-within-a-slot). Standalone sessions
  use a private instance, replacing their separate thread pools.
- Coarse: per-workspace bank jobs, the observation filter as one overlapped
  job, and each native advance searched as one span of dynamically claimed jobs.
- Fine: fixed-affine states as background chains, observation one dispatch
  ahead, source tiles filled about two tiles ahead, shared-locked tile lookups
  and one rank evaluation per pruned state.
- Transform: cache-blocked stage order, vectorized first stages, branch-free
  bit reversal, omitted butterflies for outputs the caller does not read, and
  correlation spectra stored in transform order.
- Helper: one shared branch-filter plan and response for all pilot branches.

### Results

The final set was fixed before it ran and alternated builds: two looks of the
12-slot row-5 replay at the default and one-owner allocations, one DATA slot
for rows 2–8, one unassisted quiet row-4 slot and one five-slot row-4 message.
Earlier exploratory runs are not counted. Elapsed seconds, baseline → current:

| Same-stream work | Baseline | Current | Ratio |
| --- | ---: | ---: | ---: |
| Row 5, 12 slots, `W=24/O=24`, 686.47 s of baseband | 25.891, 25.905 | 17.909, 17.942 | 1.44× |
| Its process CPU seconds and busy fraction | 293.2, 295.1; 94.4%, 94.9% | 206.3, 207.5; 96.0%, 96.4% | |
| Row 5, 12 slots, one owner | 27.155, 27.005 | 18.497, 18.124 | 1.48× |
| Its ready-input interval, commits 4→8, s/s; busy | 0.03783, 0.03773; 88.9%, 88.5% | 0.02544, 0.02564; 93.8%, 93.6% | 1.48× |
| Row 4 message: three DATA and two unassisted quiet slots, 446.60 s of baseband | 245.39 | 124.17 | 1.98× |
| Its DATA commits and final commit | 14.3, 22.8, 38.4; 245.4 | 7.1, 11.1, 16.6; 124.2 | |

| One slot, standalone, `W=24` | Baseline | Current | Ratio | Fine busy |
| --- | ---: | ---: | ---: | --- |
| Row 2 DATA | 12.95 | 10.75 | 1.20× | 78% → 92% |
| Row 3 DATA | 16.14 | 13.58 | 1.19× | 81% → 93% |
| Row 4 DATA | 6.44 | 4.18 | 1.54× | 91% → 97% |
| Row 5 DATA | 1.87 | 1.34 | 1.40× | 88% → 95% |
| Row 6 DATA | 35.54 | 21.21 | 1.68× | 86% → 95% |
| Row 7 DATA | 1.88 | 1.34 | 1.40× | 87% → 96% |
| Row 8 DATA | 5.05 | 3.33 | 1.52× | 89% → 93% |
| Row 4 quiet, complete 8,596-frequency search | 113.36 | 56.16 | 2.02× | 91% → 96% |

The quiet row-4 window is 71.5 s on a 90 s cadence. That one slot's elapsed
ratio fell from 1.26 to 0.62 s/s; an earlier, warmer baseline run took 138.5 s.
Row 6's coarse search fell from 13.79 to 4.32 s, and row 2's coarse preparation
from 2.23 to 1.43 s. The one-owner replay is now within 3% of the default one.

**Identity.** Every listed pair has the same input digest (row 5
`0673bd95…e12f4493`, row-4 message `f75ebde4…973ea250`). Apart from elapsed
times, the automatic memory limit and workspace quotes, the receipts are
identical field for field: decisions, bit and quiet evidence, coarse proposals
and refined coordinates, work counters, mapped-basis cache receipts and
messages. `test_search_fft` compares both transform kernels bit for bit with a
stage-major reference, including signed zeros, output prefixes and permuted
input.

**Functional checks.** Triad 33/33, contract 35/35 and GUI 29/29 passed. A
scratch ThreadSanitizer build, which is not a maintained profile, reported
nothing in the FFT, stream, fine, coarse, native-receiver and message tests or
in a five-slot, four-owner row-5 message.

### Limits and unresolved work

- One host, power- and thermally limited, with a desktop session running.
  Elapsed values moved by several percent between thermal states, so only
  alternated pairs are compared. This is not a capacity pass or an interval.
- Row 1 was not measured: its capture exceeds the declared sample budgets. The
  stream scheduler was measured on row 5 and one row-4 message only.
- **Earlier local CPU phase costs and forecasts no longer apply** to coarse,
  fine or Helper work. Refresh the affected calibration before any forecast.
- No accelerator ran. Compiled CUDA was not built; only the Vulkan plan check
  in the triad group passed. Device tiles and device fine statistics keep
  their previous order.
- The Radeon/native chirp-Z crossover constants were measured against the
  earlier transform and now understate native speed. Re-measure before retuning.
- Slot workspace quotes changed by 0–0.9%: row 2 +0.20%, rows 5 and 7 +0.14%,
  row 6 +0.88%, row 8 +0.12%, rows 3 and 4 unchanged.
- Helper acquisition was serial in this comparison: 0.34–0.36 s per Helper DATA
  slot, 8.6% of the row-4 DATA slot. The
  [next entry](#helper-acquisition-overlap-follow-through) overlaps it. FHSS
  template-bank preparation stays near 8 s at 85% busy. Its FIR transforms are half padding, because 16,384 input rows plus
  682 taps round up to 32,768; another chunk size changes rounding and needs
  arithmetic qualification. In the current quiet row-4 search the transform's
  butterflies are about 49% of thread samples; the spectral product, the
  scalar score loop and arrival selection are about 18%, 14% and 12%.

The 28 final receipts are in ignored `local-evidence/receiver-overlap/` to
support this table and the identity claim. Retire them when a refreshed local
calibration or an accepted follow-up measurement supersedes them. The baseline
binary is not retained; rebuild it from its commit.

## Helper acquisition overlap follow-through

**Question.** Helper acquisition ran on one thread before the coarse stage. Can
it use the slot's lanes and run beside coarse preparation without changing a
result, and what does that cost in memory?

Baseline is the build of the [previous entry](#receiver-overlap-and-transform-order-follow-through)
(`triad_receiver_probe` SHA-256 `01693f3f…d951d4`); the candidate is this change
on top of it (`3110f88f…d4a026`). Host, build profile and allocation are unchanged:
Ryzen 5 PRO 5650U, twelve logical CPUs, `H=12`, `W=24`.

### What the baseline did

On row 4, acquisition took 0.36 s on one thread with every other lane idle:
frequency refinement of four targets 0.14 s, four branch filters 0.08 s, two
arbitrary-length periodograms 0.075 s, pilot fits 0.054 s and the filter plan
0.011 s. Coarse preparation then ran for 0.30 s at 85% busy.

Its workspace bound was 1,024 bytes per prefix sample plus padding: 522 MB on
rows 3 and 4, which was 87% and 98.6% of those slots' whole lease. A counting
allocator measured 36.0 MB of live scratch for the same acquisition.

### Changes

- Helper: the two pilot fits, each target's refinement and the branch filters
  run as waves on the owner's lanes, with outcomes replayed in index order.
  Refinement shares one window table. The bound is the largest step of the
  public geometry plus a 16 MiB allowance for interval lists and records.
- Coarse: a session can take its intervals from an acquisition that runs while
  it prepares, and plan its grid afterwards. The slow mapped profile keeps its
  bank in the time domain until the intervals fix its route.
- Slot and stream: a [second, wider lease](development.md#overlap-within-a-slot)
  holds acquisition scratch beside preparation. Stream admission grants it only
  from memory beyond a sequential lease for every owner the cap still admits,
  so a window that becomes ready later keeps its place and its full shape.
- Executor: a closing pool no longer starts a worker for a stage that is still
  open, and a stage publishes leaves only once their workers could be started.
  The previous entry's stage rewrite had introduced both hazards.

### Results

The set was fixed before it ran and uses the final binary. Complete slots use
`regression`, one unit per fresh process, alternating the baseline, the slot
in sequence (`--helper-overlap off`) and the overlap lease. Elapsed seconds,
median and range:

| Complete DATA slot | Looks | Baseline | In sequence | Overlap lease |
| --- | ---: | ---: | ---: | ---: |
| Row 4 | 5 | 4.114 (4.054–4.196) | 3.986 (3.953–4.069) | 3.981 (3.884–4.101) |
| Its Helper, then Helper + coarse | | 0.361, 0.927 | 0.234, 0.820 | 0.453, 0.741 |
| Row 3 | 3 | 13.033 (13.024–13.160) | 12.906 (12.857–12.919) | 12.708 (12.672–12.815) |
| Its Helper, then Helper + coarse | | 0.300, 2.060 | 0.184, 1.910 | 1.054, 1.812 |

Row 3 is 1.01× shorter in sequence and 1.03× on the overlap lease, with
ordered ranges. Row 4 is 1.03× shorter either way. Its Helper and coarse
stages together fall from 0.93 s to 0.82 s in sequence and 0.74 s overlapped,
but the overlapped stage varied between 0.70 and 0.86 s, and the two slot
medians do not separate. Three earlier sets of the same design, taken before
this entry's last corrections and not counted, had the overlap lease 0.10 to
0.12 s ahead on row 4.

| Memory | Baseline | In sequence | Overlap lease |
| --- | ---: | ---: | ---: |
| Helper bound, rows 3 and 4 | 521.6 MB | 53.0 MB | 53.0 MB |
| Row 4 slot lease | 529.0 MB | 177.9 MB | 222.7 MB |
| Row 3 slot lease | 596.7 MB | 243.2 MB | 245.0 MB |
| Row 4 message, peak reserved | 3.64 GB | | 2.09 GB |

Measured live Helper scratch is 36.0 MB at these rows' 6 kHz geometry and
4.4 MB on the 512 Hz test fixture, whose bound is 21.4 MB.

The row-4 message has three DATA slots and two unassisted quiet slots at the
default allocation, with five owners running and every slot on the overlap
lease. In two looks of alternating order its DATA commits arrived at 6.8, 10.7
and 15.4 s and at 6.5, 10.4 and 14.9 s on the baseline, and at 6.3, 10.2 and
15.0 s and at 5.9, 10.0 and 14.6 s now: 0.3 to 0.6 s earlier. Its last commit,
which waits for the quiet slots' complete coarse search, came at 120.6 and
121.0 s against 122.1 and 120.2 s: unchanged.

Controls kept their leases, scores and times: row 8, whose Helper is gated,
3.14 and 3.15 s against 3.11 and 3.14 s; row 5, with no Helper, 1.27 and
1.30 s against 1.25 and 1.27 s.

The probe's `slot` phases keep the sequential order. Their Helper phase fell
from 0.362 to 0.218 s on row 4 and from 0.297 to 0.195 s on row 3, for 7% and
14% more CPU. The coarse preparation that follows did not change in those
single looks: 0.280 and 0.285 s on row 4, 1.458 and 1.371 s on row 3.

### Identity

Every alternated look has the same input digest and the same score: decision,
bit, evidence, quiet statistics, coarse and fine work counters and Helper
status, on all three variants. The `slot` receipts of rows 3 and 4, including
the frequency-grid digest, and the message receipts are identical field for
field apart from elapsed times, workspace quotes and reservations, and the new
overlap flag.

### Functional checks

Triad 33/33, contract 35/35 and GUI 29/29 passed on the final source. A
scratch ThreadSanitizer build, which is not a maintained profile, reported
nothing in the FFT, Helper, stream, fine, coarse, native-receiver and message
tests; in overlapped and in-sequence row-4 slots and an overlapped row-3 slot;
or in a five-owner Helper message stream whose slots all overlapped.

New tests compare every Helper field exactly with and without lanes, with jobs
run in reverse order, and where a pilot fit fails on one lane; pin which steps
form each wave, including branch filters split to fit the bound; and hold
measured Helper heap inside the largest bounded step. They compare a session
that acquires during preparation with the session its intervals build
directly, on the fixed frequency route and on both routes of the slow mapped
profile, and observe that preparation was released first. They score the
Helper fixture on both leases, in sequence, at three worker shares and as two
owners of one pool; and admit stream owners together and one after another,
on either side of each memory threshold.

### Limits and unresolved work

- One host. With `W=2*H` and SMT, an overlapped serial chain shares hardware
  threads with the bank: the Helper interval grew from 0.23 to 0.45 s on row 4
  and from 0.18 to 1.05 s on row 3, where the mapped-profile gate holds the
  owner and acquisition runs without waves. This bounds the gain here.
- The gain is latency inside a slot. Total message time did not change.
- Acquisition's two periodograms remain single transforms, and several onset
  apertures still run one after another.
- A compiled backend and the exact one-arrival profile keep the sequential
  order. No accelerator ran.
- When acquisition and preparation both fail, either failure can be reported.
- Not exercised by any test: more than one onset aperture, a refinement that
  fails on a lane, a worker that cannot be started while a stage posts, and
  the GUI's stage-time label.
- The probe's `slot` and `continuation` phases, and the capacity and forecast
  tools that add them, measure serial-model phase costs. They are not a
  universal upper bound for production overlapping work; contending jobs can
  make elapsed time worse. The `regression` command measures production
  whole-slot time, while ready-backlog qualification separately measures
  whole-stream service. Local CPU calibration still needs its refresh.
- The compiled ATen test that calls the Helper directly was not built here.
- Two findings predate this change and are not fixed: the forecast tool
  expects a controller scope name the probe no longer emits, and a public
  arrival window whose onset bounds cross sizes one Helper array from a
  negative difference.

The 40 receipts named `helper-*` are in ignored
`local-evidence/receiver-overlap/` beside the previous entry's, to support
these tables and the identity claim. The baseline binary is kept with them as
`helper-baseline-triad_receiver_probe`, because the previous entry's source
has no commit of its own to rebuild it from. Retire all of them when a
refreshed local calibration or an accepted follow-up measurement supersedes
them.

## NVIDIA and Intel GPU-toggle integration

The shared C++ automatic selector tries compatible CUDA first, then a compatible
physical Vulkan device; it never reports CPU-only work as GPU execution. The
bounded hardware comparison used a full RTX PRO 6000 Blackwell Server Edition
(96 GB), on a cloud AMD EPYC 9554 host with 16 explicitly allocated logical
threads (`H=16`, `W=32`), three slot owners and an 8 GiB application-memory bound.
This is cloud-hardware evidence, not a Ryzen CPU forecast. Both public geometries
used one ray, no Helper and 26 workspaces: broad search, 1,794 frequencies ×
2,111 arrivals with FFT 4,096; and long transform, 70 frequencies × one arrival
with FFT 65,536. The configured-calendar stream included a complete five-slot
message, two scored quiet slots and reacquisition. CPU/GPU input digests matched
within every pair.

The bounded method was predeclared: capability/phase/memory checks, existing
native component checks, automatic and FP32-minimum GUI Session checks, then a
five-slot pair and one 25-slot same-stream pair per geometry. Each had a
180-second bound; invalid work or unbounded reservation stopped a stratum.
Candidate seeds were 74021/74022; fresh five-slot holdouts used 74023/74024.
The ready-input backlog interval is separately measured from the commit of
public slot 8 through slot 16 (eight commits), after all interval input is ready;
its two initial uncommitted slots are below eight, so startup carryover is
excluded. Full-stream service includes startup fill/drain and bounded spool
read/verification, whose counters are also reported separately. These are
paired descriptive measurements, not capacity passes or confidence intervals.

| Geometry | 25-slot complete stream, CPU → GPU | Ready-input interval, CPU → GPU | Fresh five-slot holdout, CPU → GPU |
| --- | ---: | ---: | ---: |
| Broad | 8.2028 → 1.3200 s (**6.21× faster**) | 2.6703 → 0.36386 s (**7.34× faster**) | 1.6653 → 0.4391 s (**3.79× faster**) |
| Long | 16.3110 → 16.4030 s (**0.56% slower**) | 5.1647 → 5.2332 s (**1.33% slower**) | 3.4278 → 3.5058 s (**2.27% slower**) |

The first all-CUDA candidate exposed many small fine dispatches. It completed
the broad backlog in 8.0367 s CPU / 2.5076 s GPU, but the long backlog took
16.5915 s CPU / 25.2777 s GPU. The 11 November 2026 timing checkpoint's
qualified follow-up used native fine work only for automatic CUDA when public
maximum block rows ≤512, projected modes ≤40, one tap, AVX2 and admitted worker
share ≥32; larger blocks, multiple taps and smaller worker shares retained CUDA
fine. This bounded stage-routing candidate improves the broad measurements
while the long case remains slightly slower. Its guard is historical and was
superseded by the current 1024-row/26-worker route recorded in
[current CUDA precision and Tensor dispatch](validation.md#current-cuda-precision-and-tensor-dispatch).
The result therefore supports the earlier scoped automatic route, not a claim
that GPU selection accelerates every geometry.

The timing candidate checks bind probe hash
`b19a5e4ddb29b3d064a2bdedf2d5932c9cb3c8ed4aa1d608af8e4194231755d` and source
manifest `e6c46ae3e600ae330ae023d49f1ae04109a2ca34840f40664f572eca89548508`.
The final CMake-only CUDA architecture-override correction changed no DSP code;
the fresh-seed holdouts bind final probe hash
`cfb636c0d665622892e8ff923a07028a726455b8cc7e9abe53b139021667a0ba` and source
manifest `95eb309f65200223774215e17bbb4ba92433cdd472c2197d7c274dbea34550d7`.
The final CUDA bundle contains six cubins for SM 8.9 and SM 12.0. CMake had
initially selected only SM 12.0 despite an `89;120` cache request; explicit
architecture overrides disabled and per-target `CUDAFLAGS` passed for both
architectures. Preserve this build failure and its correction separately from
the DSP result. Subsequent native changes add Vulkan shared-workspace admission
and reporting after the CUDA source manifest above. The later shared coarse
release-before-parallel-retain change leaves CUDA implementation unchanged; its
interaction was reviewed, but not revalidated on actual CUDA hardware. The PRO
6000 results do not cover later source revisions.

Resource receipts show the tradeoff. The CUDA device workspace limit was
99,243,065,344 bytes; peak device allocator reservation was 1,782,579,200 bytes
on the broad backlog and 681,574,400 bytes on the long backlog. GPU receiver
host peaks were 2,346,828,227 and 3,282,152,345 bytes, versus CPU peaks of
2,029,184,451 and 2,788,859,865 bytes under the same 8 GiB cap. Spool residency
was the same in each pair: 37,380,096 bytes broad and 229,109,760 bytes long.
CUDA uploaded 107,512,372 bytes / downloaded 56,516,000 bytes on broad work;
the long case transferred 2,992,899,308 / 186,502,000 bytes and synchronized
29,850 times. These transfer, workspace and host-memory costs accompany the
slightly slower long result; they are not hidden by arithmetic peak claims.

Preserved integration failures and repairs: the first CPU probe run segfaulted
because the probe dereferenced an absent CPU bulk backend while collecting a
receipt; a null-safe receipt fix passed both affected probe tests. A 1 GiB GUI
test limit failed bounded captured-input admission (peak RSS 937,516 KiB); the
shared GUI Session checks passed after using a 2 GiB test limit. The default
CMake cache initially built only SM 12.0 and was repaired as described above.
After these fixes, the native triad group passed 33/33, affected GUI/CI/package
checks 6/6, and Python orchestration passed 71 tests. Local Radeon tests with
CUDA disabled passed native Vulkan and shared GUI Session automatic/FP32-minimum
paths. The full run's `local-evidence/gpu-toggle-validation/` files retain the
original failures and source-bound measurements until the open hardware checks
are resolved; remove these temporary results after distilling those outcomes.

These measurements do not establish sustained receiver capacity, Ryzen CPU
throughput, sensitivity populations, universal GPU benefit, or Intel UHD 630
compatibility. FP16 cuFFT remains unresolved and is distinct from measured
FP16/INT8 matrix contractions. No Intel test host was available. SecureCloud
and regional deployment attempts for RTX 4090 and RTX 5090 did not
produce an instance: the 4090 capacity had expired, and deploy requests for the
other available consumer card returned no instance. There is no new consumer
GPU measurement. The tested PRO 6000 pod was terminated; no pod or persistent
volume remains. No cloud identifiers or account details are part of this
record.

## Local Radeon Vulkan shared-workspace follow-up

The automatic Vulkan workspace path was exercised on the local Ryzen 5 PRO
5650U's Radeon Graphics (`RADV RENOIR`) with `H=12`, `W=24`, one numerical
thread per receiver worker and a 6 GiB application limit. The bounded
`automatic-workspace` checks planned `F=35`, then narrowed execution to `F=1`
while retaining all 156 score rows; this held the full 3.410 GB prior lease and
observed a 328 MB device peak. A 1 MiB shared FIFO exercised
cancellation, a small successor, wakeup and explicit native fallback.

The corrected full-public-slot pair used 710 Hz input, 120 Hz data IF, a fixed
240-second active interval, two rays, 78 workspaces, 26 frequency points,
two arrivals and an 88,979-row template with a 131,072-point FFT. CPU and GPU
active and noise inputs matched within each pair. Both GPU calls completed all
4,056 public scores (`26 × 78 × 2`) and exact fine work. The active stratum
detected bit 0; the noise stratum completed as quiet. The Vulkan receipt records
nine FFT calls, 20 dispatches, and 3,410,726,688 bytes actual peak/lease against
a 4.2 GB shared-workspace cap. Summed phase wall time, excluding simulator and
profile preparation, was 8.651 s CPU / 8.533 s GPU for active and 8.783 s CPU /
7.495 s GPU for noise. This is one descriptive pair per stratum, not a sustained
capacity or statistical claim.

An earlier `Fs=480` candidate was rejected by the transmit-bandwidth/sample-clock
guard before DSP. The `Fs=960` Hz request with a 335-second active interval
stopped at the plan's template-work bound; neither is a DSP failure. The
corrected 240-second full-public-slot pair and workspace checks completed. At
the pre-label-fix candidate, the triad group passed 33/33, actual Vulkan GUI
checks passed 6/6, and 72 focused Python tests passed. The final GUI group passed 29/29; the full
GUI binary built and both CI/package checks passed (2/2). An initial
strict-validator update passed 41/41 Python tests against a fixture that gave
pinned Vulkan a positive shared cap; review then found pinned receipts may
correctly report zero. The corrected fixtures and validators pass 41/41 focused
and 72/72 combined Python checks. Rebuilt automatic and pinned GPU slot probes
each completed all 146,432 scores and detected the expected DATA bit: automatic
reports `backend_shared` with an 8,996,048-byte owner allocation/lease, while
pinned reports `per_coarse_session` with zero shared counters. The final physical
Vulkan, portable host-scoring and FIFO admission checks pass 3/3. CPU-only
LibTorch parity and GPU-unavailable checks also pass; an initial unavailable
check used the wrong command-line flag and was corrected without a source change.

The final probe source manifest is
`50a53856782b7f8176828dfa94500f195ff7f426ad282723dd12ef2494f3c455`, and its binary
SHA-256 is `b3eadc0ded2dd6c2b8e10a1c4a45777dbf8774593b3cdbf00367e04f1a76e1dd`.
The recorded full-slot timings predate the receipt-label/validator correction;
it changes reporting only, so no performance rerun was needed. The original
wrong-fixture pass remains a failed qualification step, not evidence for the
final pinned/automatic distinction. Superseded cloud upload bundles and a
duplicate result copy were removed; the source-bound observations and original
failures remain temporarily in `local-evidence/gpu-toggle-validation/` for the
current integration claims and unresolved hardware checks.

## Lower-precision and workspace investigation

The current investigation starts from `eddd12719b475e36b6d503b4965e0805f6ab7736`.
At that checkpoint the qualified AVX2 FP32 FFT was already the operating CPU
frequency search, so the initial local comparisons used automatic FP32 as the
CPU control. Later Q15 auto-selection and its control are described separately.
Historical FP64 fine-state and tight-clock failures motivate retaining those
routes; they do not prove every present native geometry needs FP64. Full-band
FIR and Helper filtering remain FP64 as a conservative choice; a frequency shift
before the Helper filter alone does not isolate its input band.

The latest integrated source manifest is
`ce949002f0c6990aca9271f34c304588eaf608cadd6483df927fd742663113bc`, with
native probe `fa1f80f8c66afe11aed5848ffcebfb8f2705f6cbf6de18a76130f6f2a15ad522`
and GUI binary `ec4e5a843cff7ec8f4af4e55c2d095ef3afd135a41739e30a85e0f717a95cea8`.
The native triad group passed **33/33** and affected checks **3/3**. The initial
integrated build stopped before tests because the default ccache directory was
read-only; the same build and checks succeeded with `CCACHE_DISABLE=1`. The
actual Radeon GUI Session checks passed for automatic GPU and FP32 minimum,
each reporting 130 actual GPU score rows, and for native-lag selection, which
decoded `e`/`001` with two physical quiet slots and zero GPU scores. Earlier
measurements below retain their respective source and binary provenance.

### CPU one-ray preparation and admission

For exact-projection, single-ray native AVX2 frequency work, the public one-column
Gram ratio is 1. The candidate converts each FP64-checked template directly to
its FP32 spectrum during bank preparation. It accounts for simultaneous FP64
projection/FP32 plans and conversion transients, then releases the former before
scoring. Two-ray conditioning, mapped, lag, bulk, tight-clock, scalar and forced
FP64 paths retain their previous bounds. The reduced lease is an admitted DSP
bound, not a measurement of RSS or a reduction in the global `W=2*H` allocation.

The local Ryzen 5 PRO 5650U uses `H=12`, `W=24`; baseline and candidate probe
source manifests were `ca22d0ce467751510fd123304e4fae7bfe2c62942109f1db14833b82e356604a`
and `508a908820b662510ca42cdf880d8a9c21364c2ee1005e522d4b455cd19e9876`, and
binary hashes were `e337eb61a5243ba3f4d22682132582248fe10ee0f22b4c56ec40299b9f8fdd39`
and `86cb3b9e38b38a9857d622fe10d17538921111fe7077f60d5fa6dca699d6ed50`.
The identical build-options hash was
`63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`.
The native group passed **33/33**; focused precision scaling, cancellation,
admission and FP64 reference checks passed.

One first-use pair and three alternating complete five-slot pairs per geometry
preserved input, scored work, DATA/quiet decisions and evidence. Narrow
service medians were 354.304→338.277 ms, 1.047× (paired 1.019–1.081×); the
1,794-frequency broad medians were 1,836.094→1,842.809 ms, 0.996× (paired
0.939–1.055×). A brief metadata hash check overlapped the broad batch, so its
timings are descriptive and do not establish an improvement or regression.
Both short fixtures retain a 162,326,981-byte slot lease because fine work
dominates, regardless of the smaller coarse quote.

The fixed long noise pair retained exact proposals, work and quiet evidence at
65,536 FFT rows, 343.2-second public bit and 516,837 captured rows. Coarse
admission fell 291,691,128→252,893,816 bytes (13.30%); complete slot admission
fell 300,542,893→261,745,581 bytes (37 MiB, 12.91%). Post-preparation retained
bytes were unchanged at 14,719,312. The single fixed-order coarse timing was
1.480→1.615 seconds; unchanged simulator and fine stages also slowed, so this
pair establishes neither a causal regression nor a speedup. The initial long
fixture expected 65,536 FFT rows but actually produced 32,768: its geometry
assertion failed. The corrected 335-second request produced the reported long
pair and passed; the original mismatch is not a passed 65,536-point test.

The subsequent CPU spectral-product investigation added AVX2 SIMD FP32 products
and a scaled Q15 complex product with FP32 inverse FFT and FP64 scoring. The
Q15 path is selected automatically only for native, exact-projection,
single-ray AVX2 frequency searches outside the compact tight-clock class and
the separately selected exact chirp-Z route; explicit
FP32 and FP64 controls remain available. Each complex product uses bounded INT32
pair sums and a FP32 inverse FFT. There is no separate Q15 FFT-size cutoff; the
checked native transform/workspace bounds remain. Unsupported explicit Q15
requests fail before DSP. Other automatic geometry retains FP32 or the existing
FP64 guards. The following initial results measured sizes through 65,536;
the later integrated 131,072-row check is reported separately below.
This is a Q15 spectral product, not an integer FFT.

For four paired opposite-bit short trials, FP64 and Q15 had matching observed
crossing brackets at 0.3125 dB-Hz resolution and Q15 passed +0.9 dB
controls. One 65,536-point/139-frequency long active case had a matching
`[-10.625, -10.0]` dB-Hz bracket and an independent quiet control. These few
trials do not establish population sensitivity or a statistical loss bound.
In a three-arm five-slot 1,794-frequency comparison, SIMD FP32 improved median
service 2.0330→1.7369 seconds (1.171×, paired 1.112–1.213×) against the previous
scalar-product implementation, with identical fine evidence and full work.
Q15 had median 1.7021 seconds against SIMD FP32, but paired ratios crossed one
(0.940–1.035×), so no reliable additional throughput gain is claimed. That
experiment binds source
`f1ca4a01e18acce669d29ab82dc6a54f6a4292f7da036eb89deff5aff512274d`.

The later source `203ff175c20b92f70f6eb2e2f0f4f87f1cce7a149e1f518270b9addb513de036`,
probe `80b6d7cf75fad14f14d466e31e66f7a9c60d3ef775298e9d29937500ae4bb8dd`,
passed **33/33** native tests plus the FFT test, with matching Q15/FP32 quiet
and near-threshold evidence and work in the long 65,536×139 geometry. The
complete-slot admission bound fell 261,747,517→254,931,773 bytes (6.5 MiB,
2.60%); three paired long-slot timings were effectively neutral, with ratios
1.044, 1.013 and 0.964. This is a bound reduction, not measured RSS or a
sustained receiver-capacity gain. A later source-bound automatic-policy comparison
(`f5461cbe9f467e127f815e315bc5b57e24b46de22052db063aef9f98c98f6757`) used
one first-use and three matched pairs per shape. Automatic Q15/FP32 control
median service was 1.4610/1.4723 seconds for the 2,048×1,794 five-slot message
and 3.4199/3.3481 seconds for a 65,536×139 quiet slot; the paired ratios varied
and do not establish a general speedup. Input, work and fine evidence matched.
The native group passed 33/33 (61.68 seconds) plus four GUI/FFT checks after an
initial plan-rejection assertion mismatch was repaired. These frequency-route
measurements predate the separately selected chirp-Z route below.

The integrated source `021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8`
and probe `be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`
also exercised Q15 versus explicit FP32 at FFT 131,072, 137 frequencies,
26 workspaces and one arrival with `W=24`. Matched inputs and complete work
included 1,016,037 captured samples and 48,769,776 fine rows per slot; evidence
matched exactly. Both noise cases were quiet, and both `-13` dB-Hz weak-active
cases remained quiet despite an expected DATA decision (log-e 20.25024 versus
threshold 21.49338). This matched miss is retained and does not establish
successful reception or a sensitivity bound. The full slot admission bound
fell 488,861,997→475,230,509 bytes (13 MiB, 2.79%). One FP32/Q15 quiet pair
took 7.4166/7.0225 seconds and one weak-active pair 7.3113/6.9669 seconds;
single pairs are descriptive, not sustained throughput evidence. A deterministic
131,072-point coherent integer-product/FFT stress check also passed.

### Exact one-arrival chirp-Z follow-through

Automatic native CPU reception can now select the existing FP64 chirp-Z
frequency scan for a no-Helper/no-bulk, exact-projection, single-ray,
single-arrival profile when its predicted radix-row work is at least fourfold
smaller than the frequency route. The existing lag-group size, group and
workspace limits still apply. All public frequency scores remain evaluated.
Explicit FP32, FP64 and Q15 policies retain the frequency-route comparator;
the admission quote covers both possible routes and supports narrower permitted
intervals without undercharging. The execution receipt identifies
`exact-one-arrival-chirp-z`.

On integrated source `021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8`
and probe `be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`,
one first-use and three alternating matched fresh-process looks used 24 workers,
1,794 frequencies, 26 workspaces and one arrival. Median complete five-slot
service was 1.44610 seconds for explicit Q15 frequency, 0.375587 for automatic
CPU chirp-Z (**3.85×** versus Q15), and 0.574771 for actual packed Radeon work.
The CPU/GPU comparison is 1.53× in favor of CPU on this geometry; it is not a
universal device ranking. Whole-process medians were 2.5320, 1.2724 and 1.5955
seconds respectively. Input, public work, fine rows, evidence, physical DATA
and two quiet decisions, and EOF controls matched. Each GPU run executed 279,864
packed score rows including EOF and no FFT. A separate 9 dB-Hz holdout matched
all three plus an explicit FP64 frequency control; one holdout is not a
population sensitivity bound. The complete slot lease was unchanged because
fine work dominated. Native group **33/33** and four affected GUI/FFT checks
passed on this integrated source. Receipt summaries in ignored
`local-evidence/exact-native-chirpz/` support this current comparison; remove
them after the claim or remaining work is superseded.

The native exact one-arrival route now supports one uniform group through the
public 65,536-point maximum, when the same cost and admission rules hold; the
mapped lag route retains its existing group limit. A one-frequency native-only
case can use an FP64 singleton dot when its modeled work is at least fourfold
lower than the frequency FFT. One matched active and one matched quiet slot on
source `f14b49ac912cfad6cddd474491f2a856554f78441ede0e1301dc575e2a7684f5`,
probe `16130273e89aff6266fecd80d15aac845c433bcea3bdc563843b33f38ca1ef19`,
preserved input, work, fine evidence and decisions against explicit Q15. The
single-pair slot timings were 0.65428→0.59878 seconds active and
0.58495→0.59487 seconds quiet, with unchanged 164,489,981-byte slot bound;
these are descriptive, not a receiver speed claim.

A valid large native active fixture at 600 Hz sample rate and 620 seconds used
93,802 projected template rows, 55,037 frequencies, 78 workspaces and one
arrival. It completed all **4,292,886** public coarse rows in four tiles,
with 78 forward and 78 inverse FFTs plus setup. Coarse preparation/search/
selection took 1.792/2.554/0.196 seconds, fine work 4.412 seconds and whole
process 10.29 seconds. The expected active bit was missed: the slot scored
quiet with log-e 4.3404 below threshold 21.4934. This establishes complete
bounded work, not successful reception or a sensitivity threshold. Its source
snapshot was `063b32c95c9813dd409fed22a7df0eb84ffa003288d6f6fa74e03dde8f970b77`;
an earlier 240 Hz/360 Hz proposed fixture exceeded the transmitter Nyquist
skirt and was plan-only, not a completed simulated reception.

An independent synthetic post-channelization check evaluated four points of a
different giant geometry (126,232 template rows, 54,718 frequencies) against
scalar long-double direct dots. Maximum absolute complex-correlation difference
was `1.47e-8`; the largest `20·log10(|candidate|/|reference|)` deviation was
`2.42e-11` dB. A first wrapper used stale source attribution and was excluded;
the corrected oracle does not establish receiver sensitivity.

Single-arrival peak compaction removes the inert second peak placeholder while
retaining every public score and the existing final candidate limits. A matched
giant plan with 126,232 template rows, 54,718 frequencies, 78 workspaces and
FFT 262,144 reduced the `W=24` complete-slot bound 517,227,936→414,795,840
bytes, saving 102,432,096 bytes (97.69 MiB). The `W=2` bound fell
393,178,560→290,746,464 bytes by the same amount. These are no-DSP admission
quotes, not measured utilization, RSS, added owners or receiver throughput;
explicit owner and worker caps remain unchanged.

The shared GUI's GPU Default setting may choose this native chirp-Z route for
the qualified local Radeon under the observed cost crossover and worker-adjusted
public-geometry guards, while retaining device initialization and the full
fallback reservation. Strict headless accelerator probes and explicit GPU
precision modes still require their requested device execution. Actual routing
is exposed to the GUI; the matched source-bound CPU/Radeon result above informs
this decision, not a general GPU ranking. The crossover scales the measured
`26×1,750×1,794 = 81,627,000` direct-term anchor by increases in estimated
native radix work over `53×4,096×12 = 2,605,056` row-stages or fewer than 24
admitted workers. The separate estimated fourfold native work advantage remains
required. An admissible 126,232-row/54,718-frequency public plan implied about
538.76 billion dense direct terms versus 0.741 billion modeled CPU row-stages;
that geometry was plan-only, and its predicted crossover is not a measured GPU
latency, utilization or receiver-capacity result.

On the final `ce949002…` source, the actual-device Vulkan suite and plan test
passed. Intermediate and giant tests checked native-preferred versus strict-GPU
planning, identical public frequencies/workspaces, and an adequate union quote
for a narrower singleton GPU alternative; giant planning did not execute DSP.
In an actual strict 1,794-frequency slot, automatic mode scored 46,644 packed
rows and 81,627,000 terms with no FFT or native-lag fallback. The explicit FP32
control scored the same 46,644 rows with 151 FFT calls; decisions and fine
evidence matched. The strict 65,670-row case also matched its prior bit and
evidence while executing 6,864 packed rows and 450,758,880 terms. Test binary
SHA-256: `d49cd705ba4e30cc0f92b6a0966be9e3a7599dae8031cb1a88f147e1202bdb55`;
fixture source SHA-256:
`161c0a7e0b28ce4ffd7164dbbbe5c17018e960d8a8c474732d114745262162f7`.

### Bounded integer and Radeon screens

On the local 5650U, AVX2, FMA and F16C are exposed; F16C converts half/single
values and is not native FP16 arithmetic. This host does not expose VNNI/AMX.
A bounded real-dot discriminator used 4,096 and 65,536 terms with coherent
midpoint and seeded uncorrelated inputs, independently normalized operands,
wider accumulation and FP64 reconstruction. Q15 normalized squared-score
errors ranged `1.77e-5`–`1.22e-4`; Q7 errors ranged `0.0218`–`0.0402`, against
the existing `3e-6` direct-score diagnostic. FP32 products with FP64 sums gave
`8.0e-9`–`7.1e-8`. Exact scalar integer sums and executed AVX2 widening/multiply
instructions were verified. An unsigned-by-signed byte multiply-add saturation
control returned 32,767 for a mathematical sum of 64,516. These are bounded
dot tests, not complete receiver decisions or throughput; they do not rule out
other scaling, residual, mixed-precision or integer formulations.

On the Radeon, two temporary native Vulkan candidates were screened against the
existing `3e-6` independent direct-score diagnostic before long transforms or
timing. The FP32 baseline passed at 16 and 256 FFT rows, maximum normalized
errors `6.18e-7` and `7.63e-7`. Packed-FP16 products executed 50 scores at 16
rows, with error `1.62065e-3`; RADV ISA showed two `v_pk_mul_f16` and one
`v_pk_add_f16`, as well as four narrowing and two widening conversions.
VkFFT half storage with FP32 computation executed 50 scores at 16 rows, with
error `2.09520e-3`; the ISA showed FP32 arithmetic and half conversions, not
packed FP16 arithmetic. The latter reduced the small-fixture device-owner peak
from 6,880 to 5,024 bytes (27.0%) and uploads from 704 to 448 bytes. The
required FP32-sized VkFFT scratch remains; the experiment did not establish a
lower stream-admission bound. Its initial setup failed with
`VKFFT_ERROR_INVALID_user_tempBuffer_too_small` before any score assertions;
the subsequent scratch-size correction produced the reported results. The
diagnostic score failures are not equivalent to measured reception sensitivity.
These first screens were followed by the targeted tests below, rather than
treated as an automatic rejection of lower precision.

For longer transforms, the first half-storage and packed-product normalization
experiment underflowed at FFT 65,536, giving error 1.0 and 1.58 respectively.
The repaired power-of-two Parseval normalization gave `0.026928` and `0.001757`
respectively versus FP32's `0.000118` for amplitude extremes `2^-80` and
`2^40`. The diagnostic score differences remain visible. Four same-noise
paired trials over 181 frequencies and both bits gave identical FP32,
half-storage and packed-product decision brackets at 0.5 dB-Hz spacing; four
quiet controls, a long-transform quiet check and failed-Helper quiet also
matched. The first Helper control was invalid because its tone overlapped DATA;
the corrected 1,725 Hz Helper at 6,000 Hz sample rate produced the intended
`no_coarse_pilot` quiet result. A 181-frequency five-slot signal plus physical
quiet and EOF control matched. These are small deterministic and paired trials,
not a population sensitivity or false-alarm guarantee.

The intermediate automatic Vulkan path used FP16 **storage** with FP32 FFT
and product arithmetic only on qualified local RADV 25.0.7 PCI `1002:1638`
hardware and 2,048-row, one-ray, 26-workspace, one-arrival searches with
181–1,794 frequencies. It normalizes using exact power-of-two energy scales and
requires representable variance. Other hardware, geometry and out-of-range
normalization report FP32 storage or the appropriate existing native fallback.
The selected precision/reason and actual per-mode score rows appear in receipts;
this is not packed-FP16 FFT computation.

Two fresh 1,794-frequency paired trials had the same FP32/automatic crossing
brackets `[6.5, 7.0]` and `[5.0, 5.5]` dB-Hz, using 26 matched active pairs
and two quiet controls. A separate 9 dB-Hz five-slot message matched DATA,
two physical quiet decisions, public work and EOF. The 0.5 dB bracket spacing
and only two fresh draws limit sensitivity inference. For the fixed broad
high-SNR message, one first-use and three alternating FP32/automatic pairs gave
service medians 1.44509→1.01680 seconds (**1.421×**; paired ratios
1.295–1.457×), and whole-process medians 2.60192→2.09178 seconds (1.244×).
The first-use pair was 1.56168→1.01526 seconds. Input and full work matched,
including 279,864 actual score/frontier rows with EOF. Peak device memory per
owner fell 21,311,408→15,790,000 bytes (25.9%); the shared-RAM admission at
the same device cap did not decrease. In a separate single 16 MiB-per-owner
pressure pair both completed; half storage used 906 versus 1,350 FFT calls and
submissions and service was 0.97461 versus 1.35172 seconds. One pair is not a
sustained capacity result. The packed-product alternative was slower than FP32
in its scoped three-arm broad trial; this does not reject a different packed FFT.

These half-storage Radeon checks bind production source manifest
`203ff175c20b92f70f6eb2e2f0f4f87f1cce7a149e1f518270b9addb513de036`, probe binary
`0b8440988da49d178bcf0c909547a83450db88229f67c7c6a64e86be30b6b2c2`, and test
binary `e0c9aa5cc6060f94c29e8a0fe3103c7e81b691c288009b119cdf42e48319e6c6`.
The compact `sensitivity-final-broad-{holdout,message}.json`,
`sensitivity-final-message-timing.json` and `sensitivity-final-pressure.json`
receipts retain these current claims. The source-bound test suite and 37 wrapper
tests passed; larger sensitivity populations and other devices remain unqualified.

### Packed direct correlation follow-through

The subsequent native Vulkan path evaluates a single-arrival correlation
directly, rather than through FFT convolution, where the measured geometry and
device qualify. The automatic local route uses packed FP16 products, wider FP32
accumulation/reduction and FP64 scoring; actual `v_pk_mul_f16`/`v_pk_fma_f16`
instructions were observed. The widened selector requires the same RADV/PCI/driver
and storage features, shader FP16 and compute-subgroup support, one ray, one
arrival and no more than 131,072 unpadded projected template rows. Frequency and
workspace totals run as independent dots in bounded tiles under native plan and
device limits; not every admitted combination has direct empirical coverage.
Power-of-two normalization must be representable. Longer eligible direct work or
unrepresentable normalization retains FP32 direct arithmetic. Other unsupported
shapes retain eligible half-storage FFT, FP32 GPU work or the existing explicit
native fallback. A faster dense dot does not establish superior FFT asymptotic
scaling.

On frozen production source
`8cfc77fe05a086a5666c4c5e3924423f622fab1e375ac233efeb4f7bbfd6b53d`, probe
`59d7bebee72ecfb373065ce048a7098059f091ee2abfefd868287da07dd7c44d`, one
first-use plus three alternating pairs of the complete five-slot 1,794-frequency
message matched full work and decisions, including 279,864 score/frontier pairs
with EOF. Compared with the earlier half-storage FFT, median receiver service
fell 1.28209→0.677818 seconds (**1.891×**, pairs 1.749–2.171×) and whole
process time 2.517→1.822 seconds (1.381×). The direct case executed 489,762,000
complex dot terms, no FFT calls and 450 rather than 906 submissions. At a 1 MiB
per-owner cap, FP32 and packed direct both completed matched work; observed
device peaks were 749,968 and 399,968 bytes, while the same-cap host admission
quote was unchanged. Device bytes and admitted workspace are distinct.

In a separate matched four-arm trial, half-storage FFT, FP32 direct,
FP16-storage/FP32 direct and packed FP16 direct completed in 1.03723, 0.57812,
0.64286 and 0.57268 seconds respectively. Packed versus FP32 direct was
essentially tied in that experiment (paired 0.975–1.078×); versus the same
half-storage input with FP32 direct it was 1.122× faster. Four fresh paired
draws with both bits over 181 and 1,794 frequencies, four quiet controls, two
five-slot 10 dB-Hz messages and weak-Helper/no-pilot checks matched decisions
and complete work. These finite fixtures do not establish a population
sensitivity or false-alarm bound. Additional tests covered a one-frequency
message; a 1,799-frequency, 78-workspace, 1,750-row case with both bits and quiet;
and a 34,470-row, 65,536-point geometry with 139 frequencies, near-threshold DATA
and quiet. For the last case matching FP32/direct decision brackets spanned
`-10.625` to `-10.0` dB-Hz; the single `-10.0` active-slot service was 4.473
seconds for FFT, 3.128 for FP32 direct and 3.048 for packed direct. These are
single observations, not repeated complete-stream throughput measurements.
The wide 1,799-frequency complete message used 841,932 score/frontier pairs;
its single-case service was 4.934/1.841/1.722 seconds for those three paths.
These extra strata support the stated envelope, not unmeasured combinations.

The widened production selector was then exercised on actual Radeon hardware:
one-frequency and 1,799-frequency/78-workspace complete messages, the
34,470-row/139-frequency near-threshold active slot, and a new
1,888-frequency/78-workspace active slot with 147,264 packed score rows and
257,712,000 dot terms. A wider 5,384-frequency case had two arrivals and
correctly fell back to the FP32 FFT, executing 839,904 score rows. The latter
is a geometry fallback, not a failed packed computation. Actual-device checks
passed 2/2. This widened-source evidence binds manifest
`149d74436ea2bc27aa7c973fd8dde90005b5386407984914bbae7fa1cd8e7811`, probe
`25bba84c61f2a9c3b8c7597a4e638021c85a4587772145495be7f921ff9e011f` and test
binary `cbeed244a3af7a9a240c024cea30bcaeb6408567840c04334c13e863dc743e48`.

A subsequent bounded long discriminator at 65,670 projected rows, FFT 131,072,
26 workspaces, 264 frequencies and one arrival used seven actual-device native
calls on the same `149d744…` source. The packed/FP32 direct controls matched
input, public work, near-threshold active decisions and separate quiet; each
evaluated 6,864 scores and 450,758,880 dot terms. Single-case service was 4.908
seconds packed, 4.940 seconds FP32 direct and 8.328 seconds FFT; device-owner
peaks were 13,695,328, 26,829,328 and 203,953,584 bytes respectively. This is
not a population sensitivity or sustained throughput claim. Across three matched
direct looks, median dot time was 0.063386 seconds packed versus 0.182378 seconds
FP32 (2.88×), combining storage and arithmetic effects; no same-storage control
was measured for this longer geometry. Median complete-slot service was
4.811 versus 4.940 seconds (1.027×). The later 131,072
row selector boundary is a conservative numerical envelope, not a hard
sensitivity boundary. Observation energy and the largest nonzero bank-column
energy determine common scales; the observation and largest column have norms
in `[64,128)`. At `K≤131,072`, the L2 bound for flushing subnormal FP16 operand
components is at most `sqrt(2K)·2^-14`, no more than one unit roundoff relative
to that largest norm. Weaker columns can have a larger relative error, and
product underflow and FP32 accumulation are separate error mechanisms. Direct
empirical near-threshold work currently extends to 65,670 rows; it does not
prove the reception sensitivity of every geometry. The integrated source
`021dace5b6915523b60f29be932ccb5d4a06eda4da4680fb7603a5a541fa01a8` passed
actual-device checks at the 131,072/131,073 selector boundary; probe
`be0e7751728a9d88637a649c22925cf2ea095d8759d6a50db219ef78896270c1`, test
binary `c72bdf19349f27433dbd874e01b8f95b69533974347b5127cebc879c655c1421`.
On that integrated build, the 65,670-row active case retained matched input,
decisions, evidence and 6,864 score rows with 450,758,880 actual packed dot
terms, zero FFT calls and a 13,695,328-byte device-owner peak. These checks do
not establish the direct/FFT performance crossover for every admitted F×K shape.

For the earlier fused product trial, 489,762,000 complex terms counted at eight
real operations per term give 47.0/47.6/54.5 GF/s equivalent for FP32 direct,
FP32 direct with half storage and packed direct. This is a scoped operation-rate
calculation from measured stage time, not a hardware utilization measurement.
At the advertised 1.8 GHz, the conditional seven-CU Radeon model gives 1.61
TFLOP/s FP32 or 3.23 TFLOP/s packed FP16 arithmetic peaks. The mixed direct
kernel also uses conversions, FP32 accumulation, memory operations and other
work, so the ratio to either ideal peak is not a measured utilization figure.
The present data do not distinguish memory traffic, scheduling, synchronization,
instruction mix and clock effects as the principal limit; hardware counters or
equivalent profiling are needed before assigning that cause.
See the [Radeon arithmetic ceiling](triad-performance.md#radeon-arithmetic-ceilings)
and [AMD's packed-FP16 guidance](https://gpuopen.com/learn/first-steps-implementing-fp16/).

Before adopting half storage, the FP32 Radeon path was compared with the then-current automatic CPU
on the same fixed 377,637-row, five-slot, 1,794-frequency case, 4 GiB and W=24.
One first-use pair and three alternating fresh-process pairs preserved input
digest, full work, decisions and `5e-6` evidence, with actual GPU scoring and
no coarse fallback. CPU/GPU service medians were 1.68099/1.46446 seconds
(1.148×; pairs 1.124–1.157×), while full invocation medians including startup,
simulation, profile and EOF were 2.84888/2.67355 seconds (1.066×). The first
CPU/GPU service pair was 1.4340/1.4822 seconds and full invocation was
2.4396/2.7577 seconds. Each GPU process incurred five cache misses and 25 hits;
the later pairs are not cross-process warm-cache evidence. Peak device bytes per
owner were 21,311,408; peak admitted shared-RAM reservations were approximately
1.67 GB CPU and 3.18 GB GPU. This is not an APU capacity gain or sustained
backlog test. The source manifest was
`508a908820b662510ca42cdf880d8a9c21364c2ee1005e522d4b455cd19e9876`, build
hash `1402552abf5b6fbff4e950c89404fc95182ca1e59e25f11137439237b7078f55`,
and probe binary hash `b7042b90dfce4166381e3cb335c53199d562cd549b73f2fd7e1ea8907ac8e0a1`.
The restored Radeon backend passed its actual-device frequency-grid, frontier
and native-fine correctness check.

The exact production route planner identifies a configured public broad-timing
fixture at inner rate 1, two added symbols, crystal base oscillator, `cn0=0` and
five slots. With no spread and with `hf-best` it reports `lag_groups`, 2,711
projected template rows, 213 frequencies, 71 arrivals, 78 workspaces, one
frequency group and FFT 4,096; projection is 5 Hz with source step 60 and the
100.005 ppm clock span is outside the compact FP64 class. The planned coefficient
storage is 4,619,544 bytes, within the shape bound for a declared 16 GiB arena.
At `H=12`, `W=24`, the CPU coarse bounds are 54,959,672 bytes for one ray and
61,112,504 for two rays. The five-slot fixture requires 4,057,300 and 4,057,301
samples respectively. These are no-DSP geometry results: actual accelerator
capability, dynamic allocation, matrix execution, complete costs and physical
decisions remain separate gates. Planner receipt source manifest:
`02b3d8b76c2fa68c79f70a02704b9de6e41de5a2f861ae736b7f7f54a6027829`.
Two focused planner checks passed. An initial local planner build stopped before
assertions because ccache could not write its configured read-only directory;
rebuilding the same targets with `CCACHE_DIR=/tmp/pumpmodem-native-ccache`
succeeded before those checks. This was a local setup failure, not the later
cloud compilation or a numerical test failure.

On a full NVIDIA RTX PRO 6000 Blackwell Server Edition (SM 12.0, 97,887 MiB,
driver 595.91.07, CUDA 12.8.93 and LibTorch 2.9.1+cu128), the canonical native
CUDA test passed with 35 actual FP16 and 35 actual INT8 GEMMs, zero capability
fallbacks, and the retained 30/60 dB composite Helper/DATA checks. This is
component and bounded receiver evidence for the frozen source, not proof that
the planned public lag route runs on matrix arithmetic. Probe binary SHA-256:
`3152ae5fb3262b9190d420fad532a8e507758f6cdabeb7328c663f1cf39ef0be`;
CUDA test binary SHA-256:
`415ad48f4640edcf4106d8478fb37239340ea9d380aa4bd9e930981ca264fb02`;
build-options hash:
`156c8e3becf263a3e99647ce07a50fd644336e76b642a4c3c227851694371cf7`.

The configured-calendar lag fixture's CUDA FFT control failed the existing
native mapped-template projector fidelity gate before coarse search at 1,500 Hz
input sampling. A bounded 6,000 Hz discriminator retained the planned
K=2,711, F=213, A=71 and 78-workspace geometry but failed the same guard.
Both receipts report `executed=false`, zero GPU score rows and zero matrix
calls. FP16/INT8 lag comparison was therefore not admitted; these failures do
not establish matrix precision, sensitivity, performance or complete-service
behavior. The experimental pod was terminated with no remaining pod or
persistent storage.

The gate's failure was traced to cubic mapped-template interpolation near the
finite source endpoints. A bounded exact FIR edge-residual correction preserved
the existing fidelity thresholds: one representative previously failing guard's
projector error fell `0.01377818→9.828e-8`, relative template L2
`0.01384220→1.032e-7` and Gram error `0.00284682→8.747e-8`. Focused native
tests passed and CPU active/quiet controls each completed all 1,179,594 public
lag scores, with the active input identical to the failing cloud capture. The
corrected coarse lease is 55,155,704 bytes, while the complete slot remains
177,034,256 bytes because fine work dominates. The new quote includes both edge
scratch and other workspace-layout changes; do not attribute the entire delta
from the earlier planner quote to the edge correction alone.

A second bounded actual RTX PRO 6000 Blackwell run used corrected source
`8cfc77fe05a086a5666c4c5e3924423f622fab1e375ac233efeb4f7bbfd6b53d` and probe
`551bc0e91825baab9388cf5b587448a6bd9e3c376ddfa1885e26493869b17a4a`.
The native CUDA suite passed again. One active and one quiet slot each for
FFT, FP16 and INT8 completed all 1,179,594 public scores at `H=30`, `W=60`;
each matrix case executed 36 GEMMs with no capability fallback. All expected
DATA/quiet outcomes matched. FP16 active/quiet and INT8 active proposals matched
the FFT control; INT8 quiet changed proposal coordinates and added 13,500 fine
rows while retaining quiet and a `1.8e-15` log-e difference.

| Complete coarse phase, including preparation and selection | Active | Quiet |
| --- | ---: | ---: |
| FFT | 0.36352 s | 0.27627 s |
| FP16 matrix | 0.41407 s | 0.43040 s |
| INT8 matrix | 0.35463 s | 0.33786 s |

INT8 search tiles alone improved 1.459× active and 1.184× quiet, but its
preparation increased 0.274→0.289 seconds and 0.196→0.266 seconds respectively;
the complete coarse result shows no consistent advantage. Peak charged device
workspace rose from 35.722 MB for FFT to 1,070.772 MB FP16 and 1,068.216 MB
INT8; actual coarse-only allocator peaks were 27.263, 155.189 and 140.509 MB.
Each stratum has one matched pair per mode, with variable fine-stage and process
times; it is not a receiver throughput estimate or sensitivity bracket. The
predeclared stop did not expand this unfavorable affected-stage/memory result
into a five-slot CUDA campaign. This does not reject other matrix geometries or
the separately documented historical component wins. Both cloud pods were
terminated without retained volumes.

The scoped CPU comparison receipts in ignored `local-evidence/lower-precision-cpu/`,
Radeon screen/paired receipts in `local-evidence/radeon-precision-screen/`, and
sanitized native CUDA test and projector-failure receipts in
`local-evidence/lower-precision-cuda/` support this current investigation; remove
them after their findings and any remaining qualification are resolved or
superseded. No general CUDA lag speedup, sensitivity probability, complete
capacity or refreshed CPU forecast is claimed.

## Native arithmetic selection

The earlier working change from `eaa0b805beaf788973da252dce03bb565e9f85be`
selected post-filter FP32 frequency FFTs on AVX2, including live/simulated GUI
reception. Later automatic spectral-product selection is described above.
Every template Gram must have minimum/maximum eigenvalue ratio at least `1/16`.
This is an empirical qualification envelope, not a universal roundoff theorem.
Exact power-of-two block scaling preserves tiny inputs and noise-normalized
scores. Full-band filtering, coordinates, Gram/quadratic scoring and fine state
retain double precision. Unqualified CPU kernels, lag groups, compact tight-clock
geometry and ill-conditioned banks retain FP64. The original full hypotheses,
`W=2*H`, conservative memory admission and physical completion remain intact.
See [the operating policy](search-arithmetic.md).

On the Ryzen 5 PRO 5650U, Release/GCC 14.2, `H=12`, `W=24`, the frozen narrow/wide
coarse comparison used one separate first-use pair and three alternating pairs,
under a 120-second cap per geometry. Both policies use identical input fingerprint
`10788907945148184106`, complete search work, exact proposal coordinates/order
and the unchanged `3e-6` score gate. FP64 retains its original combined bank
preparation; FP32 includes scaling, conversion and its extra preparation dispatch.

| Complete coarse phase | FP64 median | Automatic FP32 median | Speedup | Paired ratio range |
| --- | ---: | ---: | ---: | ---: |
| 78 workspaces, 22 frequencies, FFT 2048 | 86.948 ms | 86.714 ms | 1.003× | 1.003–1.099× |
| 78 workspaces, 211 frequencies, FFT 4096 | 517.336 ms | 440.245 ms | 1.175× | 1.117–1.224× |

First-use FP64/FP32 times were 65.720/68.132 ms and 407.556/433.202 ms respectively;
they are not pooled with the three recurring looks. Narrow service is effectively
unchanged, and this is not a universal latency win. An earlier 1.45× wide pilot
also split FP64 preparation and could not establish the original-path gain;
the corrected comparison above supersedes that pilot.

Complete native `message` checks used the same three DATA plus two scored quiet
slots, seed 74021, 600,000-sample cap, 10-second invocation deadline, 4 GiB
allocation and 24 workers. Each geometry had one first-use pair plus three
alternating pairs. `--seconds .1 --rate 120` gives 10.4-second public bits; the
capture has 377,637 rows. The unchanged input, work, decisions, EOF negative
control and `5e-6` evidence gates passed. Timings include receiver preparation,
search, fine work, ordered completion and bounded verified spool reads; simulator
generation and one-time profile/audio preparation are reported separately.

| Complete five-slot service | FP64 median | FP32 median | Speedup | Paired ratio range |
| --- | ---: | ---: | ---: | ---: |
| Exact shift oscillator, 1 frequency | 367.740 ms | 361.300 ms | 1.018× | 0.985–1.031× |
| Crystal shift, RF 20 kHz, 181 frequencies | 535.864 ms | 542.943 ms | 0.987× | 0.948–1.032× |
| Crystal shift, RF 200 kHz, 1,794 frequencies | 2,002.082 ms | 1,673.750 ms | 1.196× | 1.184–1.244× |

After the first two neutral results (20.61 seconds total), one additional
search-dominant stratum was declared with the same gates and 120-second cap.
It finished in 23.91 seconds and all five automatic slots actually used FP32
frequency search. Its first-use pair was 1,951.291/1,604.695 ms. No further strata
were sampled. Three pairs are descriptive evidence, not a sensitivity probability,
sustained backlog test, manual-case capacity pass or refreshed CPU forecast.

Numerical checks cover independent FFT/DFT agreement, scalar/AVX2 agreement,
65,536-point round trips, unchanged full search/proposals, `2^-100` input scaling,
near-decision DATA on both sides, strong Helper and failed-Helper quiet. A fixed
1.5 ms two-ray delay with 0.024 Hz Doppler has minimum Gram ratio 0.0826204 and
passes FP32/FP64 work, exact proposal and score gates; 0.020 Hz gives 0.061346 and
selects FP64 just below the cutoff. The final expanded coarse check passes.
The native group passed **33/33** (52.13 seconds), GUI **29/29** (146.31 seconds),
then the added shared profile-policy check passed. Native FFT checks also pass.
The rebuilt actual Radeon passes its direct-grid, exact-frontier, near-decision
and quiet tests. Optional CUDA algorithms are unchanged; their native comparison
fixtures explicitly retain the FP64 oracle. No new CUDA-device run is claimed.

Radeon profiling bounded quadratic FP64 scoring to at most **0.37%** of complete
coarse time, below the predeclared 1% screen. A structured product dispatch passed
accuracy but slowed the wide warm mean from 439.63 to 502.59 ms; it and the
temporary instrumentation were removed. CPU controls also varied, so those
descriptive timings do not isolate a shader-only causal gain. The retained GPU
bulk path is FP32. This device has no accelerated INT8 dot path. Packed FP16 or
FP16 storage remain distinct unqualified alternatives: VkFFT half storage still
computes in FP32, but could independently affect memory traffic.

The original aggregate-initializer compilation failure was repaired by preserving
the `Workspace` field order. An initial ill-conditioning test retained Doppler
that kept its two templates independent; correcting the fixture exposed the
intended FP64 guard without relaxing a gate.

Final production source manifest:
`ca22d0ce467751510fd123304e4fae7bfe2c62942109f1db14833b82e356604a`.
Build-options hash:
`63a1a31d84dfb81f992610f16c646d5f49b6f69bf7f0a0efa97ccd9180f14d41`.
Native probe binary:
`e337eb61a5243ba3f4d22682132582248fe10ee0f22b4c56ec40299b9f8fdd39`.
The final paired receipts in ignored `local-evidence/native-arithmetic/` support
this source-bound claim; remove them when it is superseded. Superseded pilots,
the experimental baseline executable, profiling captures and temporary diagnostics
have been removed. Existing build caches remain useful. The local GUI binary has
been rebuilt with the new automatic policy and optional Vulkan support.

## Shared GUI GPU selection

The working change from `81372e34a220f67d44b5d1b67ae03ae3bf1456e8` adds the
shared **GPU** checkbox immediately left of Developer, visible independently
of Developer mode. CPU remains the default. The saved `--gpu yes|no` preference
selects the existing native Vulkan backend for live reception and simulation;
requested device failures remain visible. The default/minimum desktop widths
grow by 64 pixels to retain the existing header's readable mode/status width.

The backend/cache belongs to one session settings generation. Device creation
and teardown stay on the manager, after capture/receiver owners drain; polling
copies only a bounded work receipt. Suspension/resumption clears old-generation
work counters. The full worker budget and DSP memory admission are unchanged.

CPU-only GUI checks passed 27/28 initially; the sole failure was the old exact
message-editor width assertion after widening the header. Updated geometry,
launch round-trip and native GUI policy checks then passed 3/3. The rebuilt
Vulkan-enabled GUI passed 10/10 affected shared checks, including CPU reception,
Fast/Legacy preservation and explicit missing-device failure/recovery. The
alternate-frontend group passed 8/8, including framebuffer and terminal views.
FLTK adapter and document conformance passed in a private Xvfb display. The
restricted invocation could not connect to its display; the host invocation
passed without changing the user's desktop. Visual inspection of the rebuilt
window confirmed a checked GPU control directly left of unchecked Developer
mode, with the actual Radeon device reported as ready.

On the actual `AMD Radeon Graphics (RADV RENOIR)` device,
`build/dev/test_gui_robust_live --gpu` decoded `e`/`001` through the GUI's native
Session path, with all three DATA and two physical quiet decisions and **130
actual GPU score rows**. GPU off/on and suspension/resumption passed. This
bounded functional fixture covers at most five scoring windows and 600,000
samples, with a 90-second receive limit. It is not a new speedup, capacity,
all-audio-device or release claim. The local `build/dev/datapump-gui` includes
Vulkan support; the portable build option still defaults off.

## Local Radeon Vulkan backend

Work starts from clean `9606771b10b813d6c7d64ecd6dae4d43329e5466`. The local
Ryzen 5 PRO 5650U exposes its Cezanne GPU, PCI `1002:1638`, through amdgpu and
Mesa `25.0.7-2+deb13u1` as `AMD Radeon Graphics (RADV RENOIR)`, Vulkan device API
1.4.305. The normal restricted sandbox hides the render node; authorized host
execution enumerates the actual integrated GPU without driver or permission
changes. A separate `llvmpipe` software device is excluded from GPU qualification.
The installed OpenCL loader exposes zero platforms.

The device reports FP16 and FP64 shader arithmetic, 8/16-bit buffer storage,
64-lane subgroups and four compute-only queues. Its integer-dot extension reports
no accelerated 8-bit or packed-4×8 variants. Host-visible/coherent/device-local
memory is shared physical RAM, not additional dedicated VRAM or demonstrated
zero-copy throughput. The kernel advertises a maximum 1,800 MHz graphics clock.
These are capability observations, not measured arithmetic rates.

The optional native Vulkan implementation targets filtered DATA frequency FFTs,
scoring and exact peak frontiers while retaining native FIR, Helper, template
generation, lag/tight-clock routes and fine processing. The public route chooser,
full worker budget, search coverage and scored completion contract remain in
force. See [scope and tests](triad-performance.md#local-radeon-vulkan-processing),
[precision boundaries](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics)
and [the conditional hardware model](triad-performance.md#radeon-arithmetic-ceilings).
The final cached-program revision passes the independent frequency-grid oracle
(`3e-6`), exact arrival-frontier cases and native-fine receiver comparisons
(`5e-6`), including near-decision and quiet observations. Memory checks cover
simultaneous owner charges, retained cache admission with no active owner,
one-byte-short rejection and cache reuse. Correctness and both timing cases bind
native source `902288108c1d89298cfddfbc94d8bec07d1f9e05df9923b16c7a981fef2737ba`,
build options `1402552abf5b6fbff4e950c89404fc95182ca1e59e25f11137439237b7078f55`
and fixture `cda15a9bc5449d605cefbe47aba0ea93337f194ab4b3bff2f083ea8413bad7fa`.
The runtime is GNU 14.2 Release, VkFFT 1.3.4 and glslang 15.2.0, with native
`H=12/W=24`. The resulting `test_triad_vulkan` binary SHA-256 is
`1c464d5a12c56556b4bb24de58818914ab30a62da1121cdf5ca2c6970be4bda7` and
`triad_receiver_probe` is
`221477e33705adc2f87a5dc1e34b27e932e6bdd6a91d54a2acbb58485df9e289`.
The required native CPU group passes 32/32 in 28.28 seconds. The actual Python
`NativeProbe` wrapper also completes a Vulkan regression (four dispatches,
26 score rows) and a configured-calendar message (24 dispatches, 156 score
rows), both at `W=24` with actual GPU work and cache bounds checked. These
integration checks do not extend the timing result to complete receiver capacity.

Earlier test-only failures are preserved:
the full `H=12/W=24` plan has one complete tile rather than the fixture's assumed
multiple tiles; native lag preflight conservatively retains a device quote; a
fine-memory check used a coarse-only observation window; and a simultaneous-owner
check confused shared aggregate receipt counters with per-owner allocation.
Those corrections did not change GPU arithmetic or numerical tolerances.

The initial uncached small case at source `b1b000b6868a4ea92f1996c5be5768ca68f4e2f662eead862e0d49e75dc8a27c`
was about 2.5× slower on Vulkan. Increasing batch
width to 64, reusing work buffers and using direction-specific plans reduced
wide-case submissions from 100 to 28 but did not by itself improve the complete
phase. Its actual peak grew from 31.1 to 207.7 MB. Caching serialized FFT programs
then removed repeated compilation while keeping device plans and buffers private
to each owner. The cache admits at most eight entries/8 MiB of payload plus a
64 KiB metadata reservation, charged for the backend's entire lifetime.

Both final cases retain the identical 8,565-sample input, fingerprint
`10788907945148184106`, all 78 workspaces and every public hypothesis. The wider
interval was declared separately after retaining the original result. Each case
uses one separate first-use pair followed by three alternating CPU/Vulkan pairs,
with a 120-second cap. Means below include preparation, search, selection and
owner teardown, but exclude separately measured backend creation.

| Public frequency interval | Frequencies / arrivals / FFT rows / tiles | CPU mean | Vulkan mean | CPU/Vulkan speedup; paired range |
| --- | --- | --- | --- | --- |
| `[-0.2,0.2]` Hz | 22 / 25 / 2,048 / 1 | 77.117 ms | 107.629 ms | **0.717×**; 0.701–0.737× |
| `[-2,2]` Hz | 211 / 31 / 4,096 / 9 | 524.061 ms | 412.809 ms | **1.269×**; 1.213–1.331× |

These are descriptive measurements, not confidence bounds. The first-use pairs
were CPU/Vulkan 72.320/213.012 ms for the narrow case and 405.327/610.745 ms for
the wide case; backend creation added 28–32 ms separately. Every measured warm
Vulkan look has zero program-cache misses: four hits and 327,312 payload bytes
for narrow, seven hits and 607,996 bytes for wide. Wide FFT plan initialization
averages 7.149 ms, shader compilation 11.907 ms and submitted-work wall time
269.081 ms, with 28 synchronizations and 63 shader dispatches. These nested
diagnostic timings must not be added again to complete coarse time.

Actual per-owner device peaks are 95,544,976 bytes (91.1 MiB) narrow and
207,686,448 bytes (198.1 MiB) wide. Admission conservatively charges the full
configured 256 MiB device ceiling plus host staging/metadata and the persistent
cache; the APU consumes shared host RAM. Generic allocation counters aggregate
owners, while `vulkan_peak_owner_bytes` reports the per-owner peak.

The wide result is a measured benefit for this complete coarse geometry, with
remaining CPU preparation included. It does not establish whole-receiver
speedup, sustained backlog capacity, GPU utilization, packed-FP16 arithmetic or
a CPU forecast. The narrow result and cold costs remain material limitations.
The default native CPU path and full worker budget remain unchanged; the shared
GUI now exposes the optional backend through its GPU checkbox. The
[arithmetic model](triad-performance.md#radeon-arithmetic-ceilings)
separates ideal peaks from the current measured stage bounds.

Temporary `local-evidence/amd-igpu/` receipts serve the current exact-device
qualification and source-bound speedup claim. Keep only the capability,
original failure and final measurement evidence needed for those claims; remove
transient shader copies, diagnostic builds and redundant logs when their purpose
ends. The reusable verified dependency cache serves the current optional build.

## Representative receiver hardware checks

The [target contract](triad-performance.md#representative-receiver-hardware)
covers RTX 4090 desktop, RTX 4090 Laptop, RTX 4060 Ti 16 GB, Ryzen 5 PRO 5650U
and Ryzen 7 8840U CPU/integrated-graphics systems. The PRO 6000 remains the
development preference. Hardware-specific checks must bind actual device,
memory, runtime and executed arithmetic; a target profile or compatible build
does not establish performance on that device. At this earlier checkpoint the
project had no AMD GPU backend. Its Ryzen CPU execution is CPU evidence, not
integrated-GPU qualification; the later Vulkan work has a separate entry above.

The native component mode and opt-in CUDA workflow now bind compiled
production/build/fixture identity, actual device/runtime, fixed inputs, complete
work and executed arithmetic. They distinguish matrix comparisons from full FFT
peak reduction and from relative receiver regression. The local Ryzen 5 PRO
5650U uses the reported native `avx2` FFT path with H=12/W=24. At compiled source
`99d21d1b61225bf8d988039029bd13d9a809a07e683ab9a072df130c74f0a3a6`,
the ordinary native group passes 32/32 in 30.71 s, the focused probe check passes
1/1 in 5.46 s, and optional CPU ATen passes 1/1 in 11.67 s. The wrapper's 32,
workflow's 18 and source-bundle's nine unit tests pass. A CPU-only component
request reports unavailable after one describe call and zero component
measurements. These checks do not qualify integrated graphics or receiver
capacity.

Initial Ada validation on an RTX 4090 (SM 8.9, CUDA 12.8, LibTorch 2.9.1) failed
the unchanged full-CUDA mapping oracle before component timings. It reported
zero differing unrotated and 645 differing rotated float components against
the installed ATen expression. The compiled source is `99d21d1b…` and fixture
source is `d916f8548659cf1fa0c5c09be4ed363d5d880cf3a8bc3fcd4f364fe37e8fe0f9`.
One bounded discriminator isolated final complex-product FMA association:
unrotated values, cycles, angle, sine/cosine and reconstructed reference were
bit-exact, while the installed expression associated the imaginary sum
differently from the fused mapper. The largest absolute mismatch was
`5.960464477539e-8`. This reproduces the old arithmetic-contract failure; it is
not a claim that a tolerance was met or that the installed oracle passed.

The separately reviewed repair declares
[`fp32-rn4-v1`](gpu-precision-controls.md#portable-fp32-mapping-arithmetic), with
four individually rounded products followed by rounded subtraction/addition,
for both the fused mapper and its native reference. FP64 is unchanged. At
source `ed59216a5d53cb6db6c7f05e2861e230dbdb7efbc74bf1ed9e25eb808725b22b`,
the ordinary native group passes 32/32 in 30.24 s and CPU ATen passes 1/1 in
13.24 s. The actual CUDA suite then failed the newly added composite
unit-rotation/negative-phase golden before performance measurements. A bounded
discriminator found the fixture had assumed equal rounded sine/cosine: actual
cosine was `0x3f3504f3` and sine `0x3f3504f4`; both mappers agreed on the result
`-2^-25 + i`. The corrected composite fixture uses exact zero-angle and
negative-integer wrap. Six independent multiplication goldens and the 139×17
exact mapper comparison remain unchanged. Review passed; the corrected fixture
source is `7d12a9d779d08189f909129e521d75afade872897752f4beedccfd3186c247bd`,
with production source `ed59216a…` unchanged. The corrected full CUDA suite
passes in 17.114 s, including exact mapping/arithmetic goldens, native receiver
comparisons, near-decision/quiet checks and +30/+60 dB Helper composites. Its
matrix fixtures execute 35 FP16 and 35 INT8 GEMMs with zero capability
fallbacks. The original mapper comparison and receiver tolerances remain
unchanged; the reference now follows the explicit arithmetic revision. A
separate launch attempt could not find `/usr/bin/time` and executed no test
assertions; the successful run used the shell timing facility.

The maintained component command then passes all three selected comparisons
in five native calls and 6.439 s total, including process/control overhead,
within its original 300-second budget. It uses matrix case zero and the fixed
peak shape with an exact 2,048 MiB workspace. The runtime is GNU 13.3 Release,
CUDA 12.8, cuBLAS 12.8.4 and LibTorch 2.9.1 on the actual RTX 4090, SM 8.9.
Build identity is
`05ae3890dd37ccd47a679f023b76b84d4c1502997fda2b9be44ec8a6fbd9390d`;
the source/fixture above and `fp32-rn4-v1` bind both binaries and all calls.

| Native component | Reference → candidate fresh-owner geometric mean | Candidate/reference ratio, descriptive 95% interval |
| --- | --- | --- |
| FP16 lag matrix versus FFT; K=769, F=129, A=65, W=33, two taps | 24.201 → 6.875 ms (3.52×) | 0.2841 [0.2152, 0.3751] |
| INT8 lag matrix versus FFT; same geometry and input | 24.259 → 6.939 ms (3.50×) | 0.2860 [0.2126, 0.3849] |
| Exact CUDA frontier versus full-score readback/host peak selection; K=8989, F=32, A=10553, W=78, FFT=32768 | 346.219 → 40.172 ms (8.62×) | 0.1160 [0.0336, 0.4011] |

Each comparison uses exactly three alternating pairs. Fresh-owner timing
includes preparation and first search on an initialized device, excluding
teardown. Resident lag ratios are 0.1193 for both matrix modes; peak search-only
ratio is 0.0818. Matrix batches reach 4,096 rows, retain the full score grid,
and report zero fallback, nonfinite or saturated values. Maximum normalized
score error is `4.8082e-6` for FP16 and `2.4454e-4` for INT8, within the unchanged
gates. The frontier comparison computes all 26,340,288 scores and preserves exact peak
semantics. These are same-binary component observations, not a relative-commit
regression result, a full 8,596-frequency receiver replay or sustained service.
The test records executed FP16/INT8 cuBLAS products; it did not collect
instruction-level Tensor Core evidence or utilization. It does not select a new
default, supply a CPU forecast or qualify real-time receiver capacity.

Existing PRO 6000 measurements below retain their original source and hardware
scope; they do not override either Ada failure or qualify the revised automatic
fine arithmetic. RTX 4090 Laptop, RTX 4060 Ti 16 GB
and Ryzen 7 8840U remain unmeasured, and no Tensor Core utilization or complete
capacity pass is claimed.

Temporary receipts in `local-evidence/representative-hardware/` retain both
original failures, the setup failure, source/binary/fixture identities and raw
checks supporting these current claims. Remove redundant build logs, transient
diagnostic copies and superseded outputs after cleanup; retain necessary claim
and failure evidence until it is superseded. The temporary cloud instance was
stopped and terminated; the refreshed provider view confirms no remaining
compute, storage or endpoint resources and no ongoing hourly charge. No
persistent volume was created. Provider/account records stay outside the checkout.

## Native matrix and preparation follow-through

The historical audit records dispositions for 21 technique groups:
[eleven GPU groups](triad-gpu-optimization.md#native-disposition-of-the-historical-techniques)
and [ten CPU groups](triad-performance.md#earlier-cpu-implementations-worth-reconsidering).
The canonical C++/CUDA implementation adds optional post-channelization FP16
and INT8 lag matrices, bounded workspace/arrival batching, exact
descriptor reuse, shared Gaussian transition preparation and an exact CUDA
arrival-peak frontier, plus a guarded automatic half-cuFFT arithmetic route for
qualifying broad searches. The FFT remains the search algorithm; the separately
bounded dense lag-matrix candidates do not replace it automatically while the
INT8 rule was being qualified at this earlier checkpoint; the later bounded
automatic candidate result appears in
[current CUDA precision and Tensor dispatch](validation.md#current-cuda-precision-and-tensor-dispatch).
Implementation does not
transfer historical performance or qualify complete-receiver capacity; see the
[precision contract](gpu-precision-controls.md#stage-boundaries-and-optional-matrix-processing).

The ordinary baseline triad group passed 32/32. The pre-tail-repair checkpoint passes:
triad 32/32 in 31.52 s, optional ATen CPU 1/1 in 13.75 s, and wrappers 41/41.
Its actual GPU compiled source manifest is
`bbcf0d9e0063fe71213034041d3883e292e4d9d2c9ee83f8ef4e6372fbc891b6`.
It passes both exact-frontier fixtures and the full CUDA suite, including
composite Helper/DATA at +30/+60 dB, the unchanged 128 MiB bound, and repeated
fresh fine-owner allocation stress. Independent source review passed. These
functional checks do not establish complete-receiver capacity.

The bounded CPU preparation comparison used the native production receiver,
GNU 14.2 Release, H=12/W=24/O=24, one numerical thread per worker and an 8 GiB
bound. It used seed 74021, three alternating baseline/candidate pairs per
active, quiet/noise and erasure stratum, with a 30-second per-call and
180-second aggregate cap. Exactly 18 calls completed in about 5.12 s. The
compiled source manifests were `3aaf0fbfeb9f311ebe66b640b9aa2ae3f3caf8b4c0164d99caad199d000c5847`
and `8e48e4da565ef37c0f66124b6194af25c390993d0367a076a4a23c7abe8ad668`.
This is a phase comparison, not a sustained backlog or capacity test.

| Fine service stratum | Candidate/baseline geometric mean | Paired log-ratio 95% t interval |
| --- | --- | --- |
| Active | 0.912935 | 0.641551–1.299118 |
| Quiet/noise | 1.014148 | 0.792482–1.297817 |
| Erasure | 0.883256 | 0.630811–1.236727 |

All intervals cross one; **no CPU speedup is demonstrated**. The first active
and noise pairs stopped on an exact receipt mismatch: coarse retained-memory
metadata increased by 464 bytes. Inspection classified only that fixed metadata
delta, retaining the original failure. Reusing the ten original calls and
executing only the eight remaining calls preserved the sample budget. DSP
inputs, state, scores and work then matched exactly; erasure needed no metadata
exception. Three-pair intervals are descriptive and do not establish a portable
CPU cost or complete-receiver forecast.

On a full RTX PRO 6000 Blackwell Server Edition (SM120, CUDA runtime 12.8,
cuBLAS 12.8.4), FP16 and INT8 each passed the bounded matrix fixtures with 35
actual GEMMs and no capability fallback. The post-channelization `CoarseBulk`
benchmark then measured three alternating pairs for each arithmetic and shape.
Each batch reached 4,096 matrix rows, made 19 deterministic direct-DFT checks, and
retained every public score. These are initialized-device fresh-owner costs:
owner creation, preparation and first lag calculation are included; process/CUDA
startup and owner teardown are excluded. Resident timings reuse immutable input
and do not measure fresh live reception. The table uses only the final three
pairs per arithmetic/shape bound to the `bbcf0d9e…` checkpoint and recorded
test/probe binary hashes; earlier pre-removal timings remain separate provenance.
DFT check coordinates may repeat;
their count is not a number of independent statistical trials.

| Shape (samples / frequencies / arrivals / workspaces; two taps) | Arithmetic | Fresh-owner geometric mean, FFT → candidate |
| --- | --- | --- |
| 769 / 129 / 65 / 33 | FP16 | 11.885 → 3.824 ms |
| 769 / 129 / 65 / 33 | INT8 | 11.858 → 3.681 ms |
| 2,048 / 65 / 64 / 32 | FP16 | 8.687 → 3.445 ms |
| 2,048 / 65 / 64 / 32 | INT8 | 9.068 → 3.283 ms |

The second shape initially failed 4,096-row batch admission at 1 GiB. Its scoped
2 GiB rerun completed all three pairs; this does not revise the portable memory
default or erase the original failure. Maximum normalized active-score errors
were `4.80022e-6` for FP16 and `2.44530e-4` for INT8 across these two shapes.
The traces record `cutlass_80_tensorop_i16832gemm_s8` INT8 and
`cutlass_80_wmma_tensorop_s161616gemm_f16` FP16 kernel launches.
The hardware-counter request failed with `ERR_NVGPUCTRPERM`; instruction counts,
occupancy and high utilization are not established by these receipts. Bulk cost
improvements do not establish complete-receiver speed or CPU capacity.

The new FP4 implementation was removed after failing its unchanged numerical
gates. Single-product E2M1/block-16 arithmetic failed the 1% normalized score
error gate with `0.050318` (about 5.03%). A high/residual repair using actual
decoded residuals and all HH/HL/LH/LL products improved that error but changed
the unique noise peak at K=769/F=129; a later 16× residual-scale repair still
selected 1803 instead of oracle 4536. The oracle's relative runner-up gap was
0.004041, while normalized maximum error was 0.003461: passing the aggregate
1% error screen did not preserve the required peak. Four FP32-accumulating
products with FP64 reconstruction and doubled packing were fully charged.
Three-pair active fresh-owner means were 4.482 ms versus FFT 12.183 ms for the
769-sample shape, and 3.534 ms versus 8.480 ms for the 2,048-sample shape. Those
favorable active costs do not fix the noise failure or demonstrate an advantage
over qualified INT8. There is no current FP4 option. The successful historical
NVFP4 results remain applicable to their original implementation and workload.

An intervening FP4 retry is invalid
as patched-source evidence: a timestamp-preserving bundle overlay left changed
sources older than existing Ninja objects, so a refreshed source manifest did
not prove that the changed translation units rebuilt. The
[bundle extraction guidance](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
now requires timestamp-safe extraction or a fresh source/build and verified
recompilation. Preserve both the numerical failure and stale-object retry.

The first public-slot check was unresolved. Row 5 CPU active/noise
checks passed, while CUDA produced matching expected decisions but its receipt
failed with `Missing CUDA coarse arithmetic policy`. After the receipt repair,
both active and noise CPU/CUDA pairs pass numerical and physical-decision
checks, with identical input, public acquisition and fixed work. The original
forecast gate rejected the new `cuda_arrival_peak_rows` telemetry. Review found
that this was overly conservative: CPU work remains projected from unchanged
workspace/frequency pairs, scores and retained peaks/proposals. Source-bound
offline reanalysis now passes both row-5 and both wide pairs, including strict
work comparison and exact coarse-search/selection CPU demand equality. It adds
no DSP calls or calibration and preserves the original failed summaries.
Row 4 CPU active/noise failed with
`Robust likelihood block cannot resolve DATA and Helper bands`, preventing the
paired CUDA checks; a separate original-baseline active replay reproduces the
same error, establishing that it predates these changes. An explicitly modified
wide-unassisted row 4 used the full
8,596-frequency `frequency_tiles` search: GPU-host CPU active/noise calls took
27.437/27.651 s, while each CUDA call reached its 30-second native deadline.
No same-input public lag-group pair admitted the optional matrix candidates.
These failures cannot be replaced by the favorable component batch results;
neither a complete-receiver pass nor sustained input-backlog capacity is shown.

The wide-search diagnosis found a concrete transfer/host-selection bottleneck:
F=8,596, L=10,553, 78 DSP workspaces and NFFT=32,768 require 7,075,659,864
logical scores, or 56,605,278,912 bytes of full FP64 score readback. The deadline-limited CUDA
call completed 132/269 tiles and downloaded about 27.815 GB while native
non-maximum suppression remained serial on the host. The new device frontier
retains the same full score calculation and peak semantics but returns at most
two peaks in 40 bytes per workspace/frequency: 26,819,520 bytes for the full
geometry. It preserves plateau midpoints, tie priority, independent endpoint
eligibility, separation and ordered publication; nonfinite values in any input
score still fail. Added core checks passed 1/1 in 4.28 s, and source reviews and
actual CUDA exact-frontier fixtures pass.

The bounded frontier benchmark used this arrival/workspace/FFT geometry with
32 frequencies, a 2 GiB bound, two warm-up scorings and three alternating timed
pairs (eight scorings total). All 26,340,288 scores, exact frontier entries and
16 fixed direct-DFT checks pass; each side performs the same 32 forward and
4,992 inverse FFTs. Fresh-owner geometric mean falls from 240.668 to 17.617 ms
(13.661×), including owner creation, preparation and search but excluding
process/CUDA startup and teardown. The descriptive paired-log-ratio 95% t
interval is 0.041642–0.128675 for candidate/baseline. Downloaded bytes fall from
210,722,304 to 99,840. This is measured component improvement; the separate
full-width check below establishes its own narrower result.

The subsequent four-call 8,596-frequency public-slot experiment passes both
active and noise CPU/CUDA pairs. Every call retains the original input/grid,
L=10,553 with 78 DSP workspaces, all 269 tiles and all 7,075,659,864 logical
scores; CUDA frontier counters confirm complete coverage. Total call elapsed times are 28.171 s CPU
versus 5.070 s CUDA for active and 26.111 s versus 4.781 s for noise. These are
one pair per stratum, not timing confidence intervals or sustained-stream
throughput. The earlier CUDA run was censored at its 30-second deadline, so it
does not provide an exact old/new GPU speedup ratio. Numerical/physical and
fixed-work gates pass. The initial forecast rejection on the new frontier
telemetry is retained; the corrected logical-work projection passes all four
recorded row-5/wide pairs. No complete forecast is calculated: the remaining
strata and operational challenges, source-matched current local CPU calibration,
independent holdouts and same-stream backlog checks are still required.
Quiet-slot message completion and local CPU capacity remain outside this check.

The cleaned full CUDA check also failed a 128 MiB bound during coarse-bank
preparation after the DC-policy fixtures, despite about 94 GiB of physical
device memory being free. The ATen 2.9.1 source investigation identified a
process-lifetime cuBLAS workspace cache of 8,519,680 bytes per handle/stream.
The repair retains direct small Grams, computes singleton DC in C128 even in
precise stages, and routes every remaining CUDA fine Gram through an owned C128
ZGEMM with a charged 4 MiB caller workspace. CPU `at::bmm` is unchanged. The original
failure remains; the repaired full CUDA suite passes the unchanged 128 MiB
fixture and repeated fresh fine-owner allocation stress. Independent review
passed. This resolves the tested Gram lifetime paths, not all possible backend
memory behavior or every configuration.

A separate public fine-only case subsequently reproduced a constructor failure
at a 13-sample tail. The initial shared public-extent merge repair passed its
targeted check 1/1 in 1.59 s. The repaired calendar merges a short public tail
into its preceding segment before samples are exposed, without dropping or
padding input. Workspace preflight validates the exact projection bins without
allocating them. Review of both changes passed. The new compiled source
`0b8165730edee9f6248f5c7fba1038a914ad454b10fea4d6e57a5ba52f280ab0`
passes ordinary triad 32/32 in 30.71 s and the full CUDA suite after a
timestamp-safe rebuild, including the unchanged 128 MiB, strong-Helper and
owner-lifetime checks. Optional ATen CPU also passes 1/1 in 13.75 s. The four-call
row-4 check passes both active/noise CPU/CUDA pairs with the original input/grid,
all 429,013 rows processed in 286 blocks/143 segments, a 1,513-row final block
and no discarded or invalid rows. Active call elapsed time
is 3.137 s CPU versus 5.088 s CUDA; noise is 27.934 s versus 6.729 s. Each pair
passes numerical, physical-decision and logical-work gates at H=27/W=54 with
8 GiB host/2 GiB device bounds. Active Helper acquisition narrows the search to
44 frequencies/two tiles. Noise finds no coarse pilot and retains all 8,596
frequencies/269 tiles and 7,075,659,864 scores; its coarse-tile phase takes
25.581 s CPU versus 2.251 s CUDA. These one-pair observations are not a portable
speed ranking.

One affected local row-4 active call also completes in 11.607 s at H=12/W=24,
O=24 and an 8 GiB bound (11.557 s named phases; 7.122 s fine work). It is a
timing observation, not calibration of all
strata, a fresh holdout or a capacity pass. Old fine-tail calibration is invalid
for the changed source; new complete forecasts still need applicable calibration
and holdouts. The `bbcf0d9e…` wide-slot and bulk measurements above retain their
pre-repair source binding. Unchanged component costs are not silently relabeled
as validation of the changed fine path.

Temporary receipts in `local-evidence/native-tensor-current/` bind this current
investigation's source, binaries, inputs and failed/continued checks. The findings
and remaining limits are distilled here. Cleanup removed 53 redundant temporary
files, including five retired orchestration scripts, empty captures, duplicate
command/hash copies and superseded successful timing/profile logs. Eleven private
source/binary/archive copies were removed; useful builds, caches and the ATen
installation remain. The temporary cloud instance and volumes were confirmed
absent; operational receipts remain outside the checkout. Hash-referenced
originals, failed gates and source-bound raw receipts supporting current claims
remain until the claim is superseded or its remaining question is resolved.
Changed matrix work is not mapped
onto the existing CPU FFT forecast; no current matrix or preparation result
qualifies complete-receiver nominal/conservative service or real-time capacity.

## Current CUDA precision and Tensor dispatch

The current C++ CUDA receiver adds guarded native half-cuFFT for broad
post-channelization frequency search on SM89 and SM120. Automatic selection
requires NFFT 4096–2^20, at least two arrivals, a double-precision
template-Gram minimum-eigenvalue ratio of at least 1/16, and
`NFFT × admitted_frequency_tile × min(W, 8) × taps ≥ 2^19`, plus successful
plan and full-workspace admission. This cost gate replaces earlier fixture-only
NFFT=131072/F≥32 caps. It scales and packs isolated spectra and
templates; FP64 scoring and persistent state remain. Before batch commitment,
a failed guard, plan or full-rectangle allocation admission selects the full
FP32 FFT route. After batch commitment, arithmetic-flag or CUDA failures fail
closed before publishing results; they do not retry with FP32 or retain an
unquantized host bank. A post-dispatch OOM remains an explicit failure. The
kernel evidence establishes cuFFT half arithmetic. Tensor Core use inside
those library kernels remains unmeasured. Exact power-of-two energy scaling
is a checked input/range guard; it does not prove cuFFT's internal rounding
behavior.

The final bounded half-scope extension is source-bound to backend SHA-256
`5e72324c3a81ece99ed5aa91a7b2f2cc794b5fe2f7b413d2d0927f4f6a90cb45` and test
SHA-256 `60fca3b8e3ce69a36bee13e006203b181150f2bb60627c81f64110e7df6d3eaa`.
The compiled source manifest is
`53c40bf182aba4123762448b87e7c1aba654d72712461c966bf5a685973709c6`. The
RTX PRO 6000 probe binary is
`62ed656aaffb320cff75ff1fa89fd06ce34297d7baab99fe4e77946e1519a9b6`; the
RTX 4090 probe binary is
`05fd473e37704d92227f91a09b2f1cbfc0997d98c122c2c45877999b94252a3b`.
The wider transform/work-volume gate removes the previous fixture-only caps;
it is still subject to the Gram, plan, memory and fail-closed dispatch rules
above.

| Matched geometry (same inputs; complete scores) | Coarse active/noise FP32 → half | Charged workspace FP32 → half | Actual live peak FP32 → half | Decision |
| --- | ---: | ---: | ---: | --- |
| RTX PRO 6000; N=262144, K=169275, F=137, A=2, W=26; 7,124 scores | 1.73590/1.63881 → 1.60634/1.63448 s | 15.143 → 1.163 GB | 3.443 → 1.163 GB | Same bit 0 / quiet outcome |
| RTX PRO 6000; N=131072, F=8, A=2, W=26; 416 score rows | 1.59403/1.53190 → 1.53945/1.60851 s | 1.934 → 0.166 GB | 471.77 → 166.38 MB | Same paired decisions |

The automatic PRO 6000 receiver also exercised N=262144 active input across
7,124 half-FFT score rows and N=131072 noise input across 416 half-FFT score
rows. Both receipts had zero half-FFT fallbacks, matching decisions between
the FP32 and half CUDA routes on identical inputs, and complete work evidence.
Raw inputs, commands and receipts are retained under
`local-evidence/tensor-auto-policy/half-scope/`.
`local-evidence/tensor-auto-policy/half-scope/`.

The 262144 transform has mixed active/noise timing, while both geometries cut
the charged workspace substantially. These are paired observations, not a
throughput distribution or proof that half is faster on every case. A first
Fs=1500 Hz setup represented a 670-second configured signal. Its measured
phases totaled about 4.1 seconds before hitting the unchanged 30-million
raw-template-sample cap, before coarse processing. The corrected Fs=750 Hz
case retained the intended 250 Hz projected span and completed; the failed
setup remains recorded and is not benchmark evidence.

A dense N=2^20 component ran on both the RTX PRO 6000 and RTX 4090. The measured
relative RMS error was 0.0022467 on each, and relevant subnormal values were
preserved in that component. A conservative bound for worst additive subnormal
contribution at this maximum transform is about 0.14% in L2 norm. Neither this
component nor the analytical bound establishes near-threshold receiver
sensitivity or an allowable dB loss; receiver decisions and paired signal/noise
evidence remain the relevant gates.

Same-run CUPTI resource-callback capture identified the loaded half-cuFFT
symbols `vector_fft4096(__half)` and `regular_fft_factor256(__half)` in the
executed kernels. `cuobjdump` of those loaded SM120 cubins found HADD2/HFMA2/
HMUL2 instructions in both functions (97/178/72 in the vector function and
64/150/95 in the factor function, respectively), alongside minor FP32
instructions. These are static opcodes in captured executing functions, not
dynamic instruction counts, utilization, or a claim of Tensor Core execution.
Hardware counter collection remains unavailable under the provider's
`ERR_NVGPUCTRPERM` restriction; no permission bypass was attempted. The
evidence and exact commands are retained in
`local-evidence/tensor-auto-policy/cuda-half-instructions.txt`.

On a full RTX PRO 6000 Blackwell, a matched 63 MiB-admission broad coarse case
scored all 98,465,484 public scores and completed every physical quiet slot.
FP32-to-half coarse time was 340.82 to 213.36 ms (1.597x); charged workspace
fell from 54.05 to 35.53 MB. Actual allocator live peak increased from 8.45 to
16.05 MB because half admitted a larger batch. This distinction matters: the
workspace bound improved while realized allocation increased. In the broad
search receipts, half coarse totals on PRO 6000 were 0.3387/0.2883 s to
0.2691/0.2668 s (active/noise), and on RTX 4090 were 0.2053/0.2050 s to
0.1939/0.1893 s. Peak reserved memory fell about 63.4%; actual allocator peak
live fell about 32% in that different batch configuration. These are bounded
single paired observations, not a throughput distribution.

Long two-tap NFFT=131072, K=86072, F=70, A=2 retained all coarse work with a
candidate/baseline timing ratio of 0.979 (about 1.55 s, effectively neutral in
this one pair). Reserved workspace fell from 7.57 GB to 581.7 MB; actual live
peak fell from 1.72 GB to 581.6 MB. This qualifies substantial workspace
capacity for this geometry, not a long-transform speedup. In a matched
63 MiB-complete-message stream, automatic CUDA elapsed 1.79235 s FP32 versus
1.80531 s with half (0.72% slower); the predeclared ready eight-slot interval
improved 0.54196 to 0.48879 s (1.109x), and the fresh five-slot interval
0.47687 to 0.43092 s (1.107x). All physical decisions matched. The complete
stream result rules out claiming a universal whole-receiver improvement; the
interval-specific result and saved workspace remain relevant to backlog and
admission.

Sensitivity spot checks used four independent paired seeds per level and
verified identical input hashes, per-slot decisions, and physical completion.
At 6 dB-Hz both arithmetic paths failed all four expected-message cases; at
7.5 dB-Hz each succeeded on two of four. A separate INT8 near-threshold set at
−9 dB-Hz used four seeds on both the PRO 6000 and RTX 4090: FP32 and automatic
INT8 each completed one expected message and failed three. Every paired slot
decision matched on the identical input, complete work was valid, and the
receipt recorded zero unwitnessed IMMA calls. These bounded samples found no
differential decision failures; they are not a sensitivity-loss estimate or
evidence of zero dB loss. Longer messages, broader channels and independent
holdouts remain unqualified.

Preparation and Tensor dispatch also changed. Prepared right-hand operands and
cuBLASLt reduced the measured full-stage matrix cost relative to the earlier
matrix route and reduced reserved rectangles. For K=2711, F=213, A=71, W=78
(1,179,594 scores), one active/noise pair of full-stage totals
(`coarse_prepare` + every `coarse_tile` + `coarse_selection`) was: FP16
0.407/0.372 s baseline, 0.345/0.325 s with prepared B, and 0.328/0.308 s
with cuBLASLt; INT8 was 0.306/0.336 s, 0.270/0.276 s and 0.251/0.263 s,
respectively. Reserved peak rectangles fell from about 1.07 GB to 183 MB
(FP16) / 178 MB (INT8); actual allocator peak-live stayed about 121–126 MB.
This is one active/noise pair per stage and arithmetic, not a timing
distribution or whole-receiver result. The paired kernel trace reported
executed HMMA-compatible FP16 and IMMA-compatible INT8 cuBLASLt kernel families;
this is matrix-contraction evidence, not an FFT or complete-receiver result.
Same-run CUPTI capture of the cuBLASLt INT8 call identified its loaded
`cutlass_80_tensorop_i16832gemm_s8_128x64_64x6_tn_align16` SM120 cubin;
`cuobjdump` found 32 static IMMA opcodes in that 1,216-instruction function.
This proves packed integer matrix instructions exist in the captured function,
not their dynamic count or utilization. The capture is retained in
`local-evidence/tensor-auto-policy/cuda-int8-instructions.txt`. External
instruction/counter profiling is still denied as noted above. The current
automatic INT8 structural candidate uses padded matrix cost ≤30 paired-FFT row
stages, one complete grouping, one tap, K≤8192, F=8–256, at least 1664 main
complex rows, tile width ≥26 and A≥32, with shared cuBLASLt state. The F≥8 entry
is the current source condition; for low F the planner may choose a larger
frequency grouping. The 1664-row floor avoids packing overhead on undersized
contractions. A complete matrix rectangle lease is required before dispatch;
insufficient pre-commit admission retains the FFT. The accepted failure-
recovery repair fails closed after dispatch on invalid arithmetic flags or CUDA
faults before publishing a batch; there is no host unquantized retry.

Automatic INT8 receiver results support its use as a bounded coarse-speed
candidate but show mixed full-service costs. In the complete-message check, the
PRO 6000 took 8.243806 s with FP32 FFT and 8.247843 s with automatic INT8; the
RTX 4090 took 7.042574 s and 6.846154 s. In the 25-slot backlog, PRO 6000 total
time improved 38.145562→37.484921 s and its ready interval 12.63247→12.39032 s;
RTX 4090 total time regressed 34.698003→35.900528 s (3.47%), while its ready
interval improved 11.55376→11.47646 s. Summed coarse-stage time improved
4.454590→3.690055 s on PRO 6000 and 4.886528→4.398459 s on RTX 4090. The
IF450/F192 holdout improved 8.31166→8.237773 s and 7.687852→7.527696 s,
respectively. These brief measurements show a real coarse-stage benefit but no
uniform whole-stream gain; the 4090 backlog regression must remain visible.

Matrix workspace is a material cost, not a saving against FFT. With three
owners, reserved/live peaks were 533/364 MB versus 107/20 MB for FFT on the
PRO 6000, and 438/222 MB versus 107/20 MB on the RTX 4090. Therefore the
automatic route must obtain the complete rectangle lease before dispatch and
keep the FFT when admission fails. The current correctness run passed the
repaired route checks, but these samples do not establish sustained capacity or
population sensitivity. Separately, the
AVX2 CPU fine route now admits up to 1024 rows for one-tap, at-most-40-mode
geometry when at least 26 workers are admitted. In sampled 750- and 1023-row
fine clears, CPU was about 4–6x faster than the GPU. That measured route is
geometry-conditional and does not change the full `W=2*H` worker budget.

Final PRO 6000 CI passed six tests, recorded in
`local-evidence/tensor-auto-policy/final-pro-receipt.log`. On the RTX 4090,
an earlier six-test CUDA CI group passed before the final half-scope extension,
as recorded in `local-evidence/tensor-auto-policy/cuda-ci-4090.log`. For the
final source, two arithmetic checks (automatic and half-FFT) passed in
`half-scope-build-4090.log`, and three freshly relinked GUI checks passed in
`half-scope-gui-4090.log`. Two GUI test names overlap between those runs, so
there are five unique final-source tests. The build log's two GUI checks used
stale binaries and are not counted as final-source tests.
The separate `final-4090-receipt.log` records the dense half-FFT check and
binary hash, not those arithmetic/GUI suites. Earlier local triad, GUI, Python,
and bundle groups passed 33, 29, 42, and 10 checks. The final incremental
CPU-only LibTorch guard-source library, probe and test-executable build passed;
no runtime probe was run for this source increment. Earlier CPU-only LibTorch
and native probe functional checks passed, but the broader groups were not
rerun. These functional checks support the route and failure behavior; they do
not strengthen the limited timing or sensitivity sample counts above. Backend,
test, source-manifest and per-device binary hashes are bound above.

Both RunPod pods were fully terminated and verified at 21:43 UTC; neither left
a persistent volume. Cleanup removed obsolete local source bundles, copied
source and raw campaign outputs. Remote build/profile artifacts were deleted
with the pods; useful local build caches remain. Retain the matched receiver
receipts, declared strata, compact half/INT8 instruction captures and final CI
receipts only while they support current source/input matching or unresolved
near-threshold claims; delete them when those claims are superseded or resolved.
No cloud cleanup remains.

All GPU timings above include the affected search stages as specified in the
receipts; complete-stream timings include physical quiet-slot completion.
These are brief bounded cases, not sustained backlog/capacity qualification or
a population sensitivity result. The remaining
`local-evidence/tensor-auto-policy/` receipts serve source/input matching,
retained failures and instruction inspection. Remove them when their current
qualification or unresolved-claim purpose ends. Current NVIDIA dispatch distinctions and sources are
summarized in [hardware/library dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch).

## Native throughput and precision investigation

The completed investigation measured native same-stream CPU throughput,
GPU precision/acceleration and source-matched local CPU costs with bounded
checks. The baseline compiled source was
`c9a797431bd9d35a0bd2c17149dbcf6211a2296c02ff8c67499c90e236099226`.
Preserve the global `2*H` worker policy, full resource accounting, complete public
work and physical completion while investigating serialization and repeated work.

The authorized cloud envelope was at most eight hours and $20/hour aggregate.
The selected task used one full RTX PRO 6000 under a $2.50/hour ceiling and $25
total budget, without persistent volume. The roughly 100-minute rental ended
with confirmed deletion by 20:47:10 UTC; no pod or cloud volume remains. The
observed balance decrease was about $4.08, not a finalized invoice.
Provider/account details and operational logs stay outside the checkout.
See [the completed scope](triad-performance.md#native-throughput-and-precision-investigation--10-november-2026).

Verified current evidence remains scoped to the measured source:

| Check | Outcome and limit |
| --- | --- |
| Clean CPU baseline | Native triad group 32/32 passed, 32.67 s, before the current changes. |
| Current functional groups | GUI 28/28 passed in 106.60 s; contract 35/35 in 30.61 s; triad 32/32 in 30.62 s. The first triad run passed 31/32 because its CLI test expected a superseded argument; the corrected expectation then passed in the full group. These are functional checks, not capacity qualification. |
| Actual CUDA baseline | Exact `6bc9b0e3`/`c9a797…` baseline built the probe and passed the canonical tensor test within its 45-second bound on a full RTX PRO 6000 Blackwell Server GPU: 97,887 MiB, SM 120, driver 595.91.07. The pinned image supplied Torch 2.9.1/cu128 with C++11 ABI. |
| Native CUDA work | One-unit regression and active/noise slot checks passed expected decisions and input-identity checks, with actual device work and a 16 GiB device bound, W=4/O=1 and 32 GiB host bound. These are functional checks, not a throughput, tensor-core or capacity result. |
| Ingress completion wait | The single-completion-wait repair built and passed 3/3 focused checks in 11.93 s; the source change was independently reviewed. No measured speedup is claimed. |
| Coalescing/Helper fixture | Coalescing and the strong-Helper arithmetic fixture passed the CPU ATen check, 1/1 in 1.63 s. Six paired CUDA cases passed coalescing parity with fewer copies and mixed timings. The later fused complex64 repair and direct-Gram measurements have the narrower outcomes recorded below. |
| Shared receiver executor | Built and passed three focused checks in 10.09 s: stream 0.06 s, GUI 3.64 s and probe 6.39 s. Independent review found no remaining source flaw in the reviewed concurrency, lane or memory boundaries. The later concurrent-leaf exception and retained-idle-stack contrasts passed in one focused test (1/1). The matched-I/Q timings below do not support a gain. |
| Mixed-stream regression v2 | Current and baseline binaries each passed the same one-unit stream preflight at W=4/O=2, an 8 GiB bound, 600,000-sample cap and 12-second call bound, with 377,637 identical input rows and matched work. Both first attempts at 512 MiB failed controller admission; 8 GiB is this fixture's allocation, not a portable minimum. The completed fresh-call comparison below is inconclusive overall, with its mixed stream passing the slowdown gate. |

The current compiled C++ manifest is
`3aaf0fbfeb9f311ebe66b640b9aa2ae3f3caf8b4c0164d99caad199d000c5847`.
Its combined CUDA check passed the canonical tensor assertions and an actual
five-slot message with two running owners. The
[combined receipt](../local-evidence/native-throughput/combined-cuda-3aaf0fbf/combined-cuda.json)
binds the native binaries and device/runtime; it does not certify every row.

The CUDA setup first attempted a script before its upload completed, then found
`nvcc` absent from the SSH PATH despite being installed, and Ninja unavailable.
Those attempts reached no receiver assertions. Adding `/usr/local/cuda/bin` to
PATH and installing Ubuntu `ninja-build`/`libssl-dev` resolved the prerequisites;
the actual baseline CUDA checks above then ran. The baseline CUDA receipts have
been retrieved; operational records remain outside the checkout.

The first matched CPU comparisons did not improve elapsed service. Both replayed
12-slot I/Q with SHA-256
`0673bd95f9b4b92317dc951161859b9f8d621e0a44c714c121dce175e12f4493`.

| Comparison | Measured point | Interpretation |
| --- | --- | --- |
| O=2 primary pair | Whole service 37.63 → 38.75 s; central four commits 12.0835 → 12.7259 s; hardware busy fraction 87.70% → 88.88%. | About 2.98% slower overall and 5.32% slower centrally. Main scoring/work matched exactly, including both messages and scored-quiet completion. |
| Original default baseline, H=12/W=24/O=24, 8 GiB cap | Whole 37.0251 s; first slot 2.6940 s; first message 36.9523 s; CPU 391.82 s; busy 88.19%; reported RAM 1.655 GB. | Reference point, not a capacity qualification. Its sampled owner counter undercounts actual overlap. |
| Initial shared root priority | Whole 39.0495 s; first slot 28.5360 s; busy 85.96%; 12 scorer roots/24 pool threads; reported RAM 2.016 GB. | About 5.47% slower overall with a material head-of-line latency regression. |
| Leaf-first, narrow admitted scratch | Whole 38.2704 s; first slot 18.5478 s; busy 82.68%; ten roots/24 threads; reported RAM 1,976,414,672 bytes. | Still slower; finite drain retained only three- and two-lane scratch shapes. |
| Full-W scratch, before oldest-slot priority | Whole 36.8415 s; first slot 20.9921 s; first message 31.6315 s; busy 86.67%; reported RAM 3,176,535,312 bytes. | Wider scratch reduced the finite commit-7→11 drain from 10.2118 to 5.2063 s. This finite tail is not sustained-throughput evidence; the point alone is not a speedup qualification. |
| Current full-W scratch plus oldest-slot leaf priority | Whole 37.6654 s; first slot 7.5399 s; first message 21.7266 s; CPU 381.879 s; busy 84.49%; nine roots/24 threads; reported RAM 3,160,298,768 bytes. | All 12 reported scores/work and physical messages match the baseline. First message is earlier, first slot remains later, and whole service is about 1.73% slower with lower busy fraction. No throughput/utilization gain is claimed. |

The current [oldest-slot receipt](../local-evidence/native-throughput/row5-default-oldest-first.json)
and preceding [full-scratch receipt](../local-evidence/native-throughput/row5-default-full-scratch.json)
retain the exact matched input and configuration. All current owner scratch
shapes were W=24 under the same aggregate pool and 8 GiB memory cap. The oldest
eligible public slot receives leaf priority; queued roots use otherwise unneeded
lanes. The focused stream check passed 1/1 in 0.08 s for that candidate. This is
the accepted finite-work latency tradeoff: earlier first-message delivery,
without a throughput/utilization gain or capacity qualification.

The O=2 invocations are incomplete overall: subsequent 6- and 12-worker pilots
hit their 60-second deadline. Keep those failed additional pilots alongside the
completed primary observations. The default central interval also remains
unqualified: four central commits cannot exclude the seven uncommitted slots
already present at its start (`interval_could_be_explained_by_startup_carryover`).
The O=24 policy permits up to 47 startup-carryover slots; that theoretical bound
is not the observed count.

A final-source explicit O=2 replay retained the same input hash, all 12 reported
scores/work and both physical messages. Its valid ready-input interval, commits
4→8, covered 240 represented seconds with one carryover slot. Central service
rose from 0.050348 to 0.052515 s/s (about 4.30% slower), while busy fraction rose
87.70%→88.70%. Whole service rose 37.63→38.07 s (about 1.17% slower), and RAM
1.361→1.475 GB, with two running roots and 24 pool threads. The
[final O=2 receipt](../local-evidence/native-throughput/row5-backlog-final-o2.json)
is a complete primary comparison; the original invocation's later pilot
timeouts remain separate failures. This explicit O=2 point neither qualifies
default O=24 nor supports a throughput gain.

The old `peak_active_owners` field was sampled after commit and undercounted
scoring overlap. In particular, its O=2 value of one does not mean the baseline
was serial: raw slot timings show later baseline slots overlapping in waves.
The shared pool enables earlier/sliding overlap and records roots directly,
but these points show neither a speedup nor a capacity pass. Higher reported
utilization alone cannot reverse the elapsed-service failure.

The first real fused complex64 row-5 pair failed exact adaptive fine-work
parity: its two-second prefix evaluated four additional candidates, increasing
basis rows from 23,136,000 to 23,160,000. Equal retained-state and transition-edge
counts did not satisfy that gate. An isolated pointwise fixture had no
unrotated float-component mismatches and 975 rotated mismatches. Inspection of
the installed C10 CUDA binary found two `FFMA` instructions in the final complex
multiply, whereas the initial fused implementation used four `FMUL` and two
`FADD` instructions. The narrow explicit final-FMA repair passed independent
source review and actual RTX PRO 6000 exact pointwise, canonical full-slot and
original failed-row checks, including exact work/state at its 2/3/4/6-second
checkpoints. Six paired observations then preserved exact scores, work and state.
For this one row/seed and bounded continuation, DATA and QUIET fine times fell
36.2% and 34.1%; measured receiver-phase subtotals fell 17.1% and 15.3%. Kernel
launches fell from 58,333 to 10,615 and device peak from 96,468,992 to 90,177,536
bytes. These are scoped GPU phase improvements, not whole-stream throughput,
communication reliability or local CPU forecast qualification.
This matches the installed C10 binary's arithmetic, not a promise of bitwise
behavior across arbitrary LibTorch/compiler/device versions. The oracle must
reject a changed installed arithmetic path.

An actual-GPU FP16 Gram-shape experiment met its arithmetic check (reported
error `1.314e−4`) but took 184 µs, versus 119 µs for FP64 batched multiplication
and 33 µs for the direct FP64 experiment. That low-precision candidate is stopped;
its WMMA name supplies no instruction-level tensor-core execution proof. The
direct-FP64 Gram path was subsequently implemented and independently accepted
for its bounded CUDA shape of at most 128 modes. Six paired DATA/QUIET checks
preserved exact work/state with maximum score difference `1.86e-9`. Relative to
the repaired fused mapper, median fine-time ratios were 0.46138/0.44773 and
receiver-phase ratios 0.83629/0.79657. Allocator peak reserved memory fell from
90,177,536 to 69,206,016 bytes. The
[paired receipt](../local-evidence/native-throughput/direct-gram-row5-97021/paired-summary.json)
retains the one-row/seed limits and separate timing gates; these ratios are not
whole-stream or CPU capacity results.

The first complete GPU v2 run took 181.2 s and 91 calls, but six of nine models
failed: a first scored slot cost about 0.319 s versus 0.110 s subsequently.
The original report remains a failure. The revised policy executes every
active/noise unit as a fresh `regression --units 1` process and every mixed unit
as a fresh `message` process. It describes the first scored slot in a fresh
process, neither a fully cold GPU device nor steady state. Earlier same-process
v2 reports are incompatible; no extra looks were used to repair the mixture.
The frozen driver SHA-256 is
`73ae19dd896d4a07dbcd79786c4018a6802c3cf3a0ea682988b01a9939bf3dd7`.
Seventy-seven focused checks passed. Its fresh-call
[CPU baseline](../local-evidence/native-throughput/cpu-regression-v2-fresh-baseline.json)
established all nine models in 101.675845 s/163 calls at W=4/O=2, 8 GiB,
600,000 samples and a 300-second overall cap: nine active/noise looks and five
stream looks. The [candidate comparison](../local-evidence/native-throughput/cpu-regression-v2-fresh-candidate.json)
took 103.679875 s/163 calls, with all nine models supported. Mixed-stream means
were 0.630636/0.634083 s, and the ratio interval `[0.841389, 1.201541]` passed
the declared 1.30 slowdown gate at five looks. All eight standalone phase
comparisons remained inconclusive at nine looks; the overall result is
inconclusive, not a regression pass. No rerun was used to narrow those intervals.
The fresh-call GPU run established a baseline in 272.966 s/135 calls with all
nine models supported; active/noise/stream means were 0.32219/0.32291/0.95147 s.
It has no GPU reference comparison. Baseline establishment is not a regression
pass or capacity qualification.

Forecast accounting was repaired to charge audio frontend cost by represented
continuous PCM seconds rather than slot cadence, and to test independent
adaptive-work counters separately. Retained-state count 64 and emission count
88 must not be mixed into a drift ratio; exact affine-work equality remains
required. Fifteen focused checks passed; stable-counter reanalysis passes and
the genuine 25→64 drift still fails. CUDA compiler/cache guards separately
passed 51 unit checks.

The current stock row-5 CPU collection completed 33 calls with verified source
identity. Nominal and conservative point estimates were 0.062998 and 0.269030
s/s; logical I/Q-plus-PCM replay was 0.056741 s/s, giving point margins of
93.70%, 73.10% and 94.33%. I/Q replay took 38.709 s for 686.473 represented
seconds, with 86.94% hardware busy fraction, eight running owners and
3,168,302,352 bytes reported RAM. Messages, reacquisition and codec checks
passed. These are CPU-only points, not a full interval capacity pass.

Row-5 CUDA checks passed all five paired strata and scored quiet in automatic
precision, but its high-SNR challenge changed a losing acquisition clock
coordinate from 0 to −0.0003 ppm. Correct bits and 4% work drift did not satisfy
exact acquisition agreement. One explicit-FP64 check passed that challenge;
it qualifies only that explicit arithmetic check, not the automatic class.
The 5 dB near-decision challenge was outside the declared score band
(14.6199 versus 21.4934, tolerance ±3.224). Independent CPU training at seed
86021 placed both 5.5 and 6.5 dB below threshold, failing the predeclared bracket;
that failed construction produced no qualification report. A separate bounded
CPU-only construction reused seed 86021, with lower 6.5 and fixed upper 14.5 dB-Hz
and at most five bisection calls. It succeeded in five calls, fixing 8.0 dB-Hz
at log evidence 21.858229 versus threshold 21.493 ±3.224, without CUDA feedback.
The independent CPU cost holdout at seed 97021 remained unchanged. The final
explicit-FP64 row-5 run passed all five strata and three operational challenges
with exact work/acquisition and zero adaptive drift. CPU and GPU near-decision
log evidence both measured 21.85822942. This repairs that explicit precision
route; the automatic-class failure remains a separate result.

The [official all-eight forecast report](../local-evidence/native-throughput/forecast-row5/all-eight-fp64-forecast.json)
supports three partial row-5 fine-phase estimates:

| Component | Estimated s/s and interval | Independent CPU holdout s/s |
| --- | --- | --- |
| Cold maximum | 0.259870, `[0, 0.611954]` | 0.250479 |
| Terminal | 0.015253, `[0, 0.126661]` | 0.014189 |
| Erasure | 0.015019, `[0, 0.055098]` | 0.014460 |

Here s/s means CPU service seconds per represented baseband second.

Active and quiet cache misses/prepared-row counts differed by about 9–11%,
outside the unchanged 5% calibrated cost-regime gate despite exact CPU/GPU
paired work. Their estimates remain unresolved. Required matching same-stream
calibration/backlog and complete holdout/uncertainty gates also remain open.
Nominal and conservative GPU-assisted service and complete capacity are
unresolved; the other seven rows have geometry-only coverage. The partial
component forecasts cannot be reported as an all-row or complete-receiver pass.

Strong-Helper +30 and +60 dB checks now pass on CPU and CUDA. The original
+60 dB fixture failed placement at 1,725 Hz IF before receiver assertions;
the valid fixture uses 9,000 Hz sample rate, 2,450 Hz IF and noise variance 0.03.
Its exact 544,313,305-byte workspace lease also replaces an insufficient
512 MiB attempt. These repaired fixtures do not erase the original setup and
admission failures.

The CUDA build now requires NVCC and the CUDA SDK. The actual Blackwell build
uses `CMAKE_CUDA_ARCHITECTURES=120` and `TORCH_CUDA_ARCH_LIST=12.0`. These identify
the measured SM 120 device; they are not a portable architecture default. The
optional CPU ATen build and default application without Torch remain unchanged.

Retain the matched receipts, original failed checks and incomplete pilots in
[`local-evidence/native-throughput`](../local-evidence/native-throughput/) to
support this checkpoint's scoped claims and unresolved gates. The bounded
campaign has ended; no additional run is implied. Tensor-core execution and
complete capacity remain unqualified. Retire these raw receipts after applicable
replacement evidence resolves the gates and preserves the original failures,
source/configuration provenance and margins.

The baseline/candidate comparison no longer needs copied executables or source
archives. Retire those obsolete bundles and builds while retaining needed raw
results and hashes; never relabel old costs as new calibration. Release artifacts
and currently useful build profiles keep their separate retention contracts.

## Native codec and measurement migration

**Completed baseline checkpoint.** The checks below precede the current
throughput/precision changes and retain their original source and limits.

The migration from `1c58507a5178c7700cd15d0fcdd05a0091c6dba9` adds bounded native
long-message intervals, lazy emission,
shared native CPU/tensor measurement paths and a native real-audio frontend,
while retiring Python receiver DSP. The default application requires neither
Python nor Torch; optional compiled ATen bulk operations retain production receiver
control and state. CUDA device execution was unavailable during this local
checkpoint.
Implementation, numerical comparison and capacity qualification remain separate.

The long profile keeps the short codec through 16 bytes and uses fixed 1,216-slot
marker/codeword intervals beyond that boundary. Its private-key tag binds each
word to its actual public calendar address; it does not authenticate the whole
endpoint. Early short-prefix ambiguity and missing whole final uncompressed
intervals retain the limits in [protocol](protocol.md#completion-and-authentication-limits).
No new pass is inferred from the earlier short-profile GUI checkpoint below.

Final local checks and scoped supporting evidence are:

| Check | Outcome and scope |
| --- | --- |
| `./build.sh test triad` | 32/32 passed, 29.99 s, after the final spool/Helper rounding correction. |
| `./build.sh test contract` | 35/35 passed, 21.32 s. |
| `./build.sh test gui` | 28/28 passed, 106.67 s, including Fast and Legacy. |
| Default application | `datapump-gui` rebuilt and `gui_self_check` passed; `ldd` reports no Python or Torch dependency in this build. |
| CPU ATen integration | Current optional LibTorch check 1/1 passed, 1.33 s. An actual `tensor-cpu` regression unit completed the expected DATA decision with the common current source manifest. No CUDA execution. |
| Session/audio/Helper repair | Earlier focused `gui_robust_live`, `robust_audio_frontend` and `triad_helper_burst` 3/3 passed, 4.30 s. Long PCM/cancellation, quotas and full-duplex reservations are covered by these checks and the final groups; memory handling was independently reviewed. |
| Independent receiver parity | Four complete native slots and both full coarse routes matched the baseline exactly. Execution-witness defects were reproduced and repaired, with focused checks passing. |
| GPU tooling and build closure | 21 focused tests passed, including full synthetic forecast serialization, plus actual native CPU slot/terminal smoke checks. Requiring unavailable CUDA was correctly rejected. The later proposal bundle passed nine focused packaging tests and the current `triad_gpu_diagnostics` CTest (1/1, 0.77 s) after the full 32-test group; fresh extracted CPU LibTorch compilation, parity execution and `describe` also passed. These are not CUDA results. |
| Extended Message edit path | `gui_adapter_boundary`, `web_bridge`, `web_worker`, `web_renderer` and `web_preview_client` 5/5 passed, 2.42 s; loopback `build_preview` and `web_preview` 2/2 passed, 3.63 s in the permitted run. Native pipe/relay and client coverage only, not actual TX or Wasm runtime. |
| Native probe/spool | Earlier focused `triad_receiver_probe` and `triad_probe_spool` 2/2 passed, 5.13 s, on source manifest `2a309b945ea1a2397313156567014503441ed75e40fda91e06227c80e618b2d2`. The bounded pilot passed after backing-accounting repair; the final triad group passed after the subsequent portable rounding fix, which leaves this host's reservation unchanged. This is not manual-case calibration. |

The final compiled source manifest is
`c9a797431bd9d35a0bd2c17149dbcf6211a2296c02ff8c67499c90e236099226`;
the native probe SHA-256 is
`8b197fb56c98774bba2548f73a61e0dea6aa871de279c41be6b43e0f0b1c027b`.
These identities bind this local checkpoint, not a published release.

One bounded native CPU abbreviated run followed the final groups. Automatic
allocation resolved H=12, W=24 and owner cap 24, with one numerical thread per
worker and a 9,983,840,256-byte memory cap. All eight active/noise slot, coarse,
fine and audio models reported `abbreviated_holdout_supported`; the run status
was `baseline_established`, with comparisons `uncompared`. Active used 27 batch
receipts through nine looks, noise 15 through five looks, with only the declared
1/2/4-unit batches. This establishes a current native baseline, not a cross-revision
regression pass, real-time capacity or communication sensitivity.

The superseded v1 `regression-baseline.json` contained 559,086 bytes with SHA-256
`08380276aa9c15e53718d32dbb9e2d95000648031affdfa36f1079ea1f370905`.
This source-scoped result does not supply the current v2 comparison. Its
applicable outcome is preserved here; it has no active baseline-retention duty.

The independent clock-envelope reproduction first rejected a legal signed-arrival
case (`−1000`, zero Helper lead) in `PublicProfile`. The repaired public window
admitted it with a 46,938,976-byte workspace bound. Nominal physical draws and TX
addresses remain unchanged; only the quiet-evidence aperture narrows where needed.
This fixes the public-window bound rather than altering the simulated signal.

The frontend regression first reproduced rejection of a legal 4 MiB escaped
Message edit, then an invalid 2 MiB `<` draft overflowing snapshot JSON. The
repair raises only edit-frame allocation to 4 MiB, enforces published control
limits and preflights JSON expansion at 6 MiB within the existing 8 MiB envelope.
Decoded Message content remains limited to 1 MiB; other event values retain
1 MiB and errors 4096 bytes. The passing checks retain those rejection boundaries.

A bounded native spool fixture generated 827,637 I/Q rows, stored 14,069,829 bytes
and retained no full-capture array in RAM. Exact-input W=2/O=2 and W=1/O=1 replays
on H=12 each committed ten valid slots, two actual `e`/quiet episodes and the
predeclared ready interval from commit 3 through 6. Both reported
`peak_active_owners=1`; that after-commit counter does not establish actual peak
scoring overlap. This is not a multiowner speedup or manual-case capacity result.
Service includes separately quantified spool reads/hash; generation and spool
writes are excluded. Full logical PCM projection is a separate measured phase.
Controller wall removes measured condition-variable wait and spool I/O/hash but
may overlap DSP; controller thread CPU still includes spool work.

The fixture's tmpfs file occupied 14,073,856 charged RAM bytes. A separate
low-memory preflight planned 22,967,637 samples within sample/disk bounds, but
its 390,451,200-byte temporary backing exceeded the 256 MiB total budget. It
exited 2 before capture, after profile preparation. Verified ordinary disk has
a separate storage budget; tmpfs or unknown backing conservatively reserves
file extent against memory. These are bounded lifecycle/admission checks, not
CPU capacity or sensitivity results.

The CUDA-enabled C++ host branch passed a GCC 14.2/C++20 syntax check with
`DATAPUMP_TRIAD_TENSOR_CUDA=1`, CPU LibTorch 2.15 headers and verified NVIDIA
CUDA 13.0.2 runtime/CRT/CCCL headers (2.56 MB). The source SHA-256 was
`29e9fc8dbaa0ca8db3757636a7387c96f00a499e63d28a8ba3acde77907994b6`.
The upstream `C10_CUDA_NO_CMAKE_CONFIGURE_FILE` switch skipped the CPU package's
absent generated CUDA configuration; no substitute headers were invented. The
check exited 0 without warnings. Header provenance used the
[NVIDIA redistribution manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.0.2.json);
temporary downloads were removed. This does not test CUDA linking, device
execution, numerical fidelity, memory or performance.

The earlier bounded RunPod proposal prepared CUDA checks before provisioning.
It has been superseded by the
[current investigation](triad-performance.md#native-throughput-and-precision-investigation--10-november-2026).
Its initial archive omitted
`triad_probe_spool.hpp` and `triad_fixture_key.hpp`; an explicit allowlist repair
and recursive local-header closure test corrected that packaging failure. The
fresh extraction built the probe, exact bundled tensor parity test and Helper
exporter, with no unresolved or Python libraries and the same compiled source
manifest as the local CPU build. The corrected archive has 512 files and
1,370,721 bytes, SHA-256
`2bfe44676020d21a88023c5f57a7df5b26fb0537404f865ab299709a4da5adad`;
its internal manifest is
`dd17c1c270cc750d5c7dfd0a254cdf0fb15393fd1427e700013918962fa4a22a`.
The bundled test SHA-256 is
`4e7b734f09cf4b9270ff9b4df53ed8285f196683b5de8d82aeacdb512f13f2ee`.
The completed matched comparison superseded this archive's retention purpose;
its hashes and packaging findings remain here while the obsolete source bundle
is retired. The packaging checkpoint contained no account metadata or provider
action.

No local functional check remains pending in this checkpoint. Actual CUDA
linking/device execution/performance, Windows runtime, physical audio devices,
Wasm runtime, full manual-case capacity and multiowner throughput qualification
remain unrun. No hosted CI job, publication or release certification was performed.
The configured-calendar scope remains; unknown waveform epoch/ordinal/timing
acquisition is unqualified.

Python retirement removed 38 obsolete modem/DSP tools, 35 implementation-only
tests and two old sensitivity/confirmation scripts. Remaining Python modules
handle native orchestration, statistics, case parsing and physical topology.
Obsolete copied source, bytecode, bridge build artifacts and mixed/source
archives were removed from ignored evidence; numeric inventories, expanded
failed-check receipts, source-hash provenance and release assets were preserved.
Old numeric captures are not a dependency of the native implementation. Retain
any raw data only for a specifically identified unresolved question or release
duty, then remove it when that purpose ends.

Current native benchmark commands are documented in the
[computational guide](triad-performance.md). The bounded baseline above does not
supply a full manual-case capacity result; that requires applicable complete-work
coverage, source identity, uncertainty and independent holdout checks. Historical
Python calibration, transport applicability exceptions and old GPU forecasts
are inadmissible for that native runtime. Preserve their original failures and
margins as historical findings. The new native Fejer real-audio path replaces
an unused Python Kaiser experiment; that replacement is not a numerical-parity
claim. No heavy calibration campaign is authorized by this documentation update.

## Native GUI integration

**Earlier short-profile checkpoint.** The outcomes below predate native long
messages and the current benchmark/tensor/frontend migration. They retain their
own source and coverage; their passing counts do not qualify those later changes.


The operating architecture is now a single C++ stack linked into the GUI:
canonical source/Helper, complete slot scoring, bounded message/FEC processing,
in-process continuous scheduling and the native audio session. Python is absent
from this operating path. The old Python process scheduler and sealed-input IPC
are reference/historical machinery, not an application deployment requirement.
Live reception is scoped to an explicitly configured public calendar and keyed
ordinal, with a declared local live-noise model. Unknown-start acquisition,
external radio retuning, all-configuration sensitivity, native CPU capacity and
physical-device qualification remain outside the current evidence. These Linux
functional checks do not qualify Windows, browser/Wasm operation or a release;
release certification requires exact published source and binary identities.

The current functional checks have these outcomes. Combined coverage below
reuses unchanged passing cases after scoped corrections; it is not a claim
that every full group was rerun.

| Check | Outcome |
| --- | --- |
| `./build.sh test triad --jobs 1 --build-jobs 4` | 30/30 passed, 63.36 s; `CCACHE_DIR=/tmp/pumpmodem-native-ccache`. Includes native source/Helper/scorer/message/stream, `gui_robust_live` (3.24 s), Python references and GPU diagnostic unit checks. |
| `contract` | 32/32 passed, 28.04 s. |
| `gui` | Initial 27/28 passed in about 107 s; corrected `gui_native_policy` then passed in 0.05 s. All 28 cases have passing coverage across those runs. |
| `frontends` | Initial 7/8 passed in 21.36 s; corrected `framebuffer` then passed in 0.28 s. All eight cases have passing coverage across those runs. |
| `web` | Initial 11/13 passed in 147.59 s, including `web_live` in 137.59 s; both localhost preview setup failures passed in the permitted rerun, 3.56 s. All 13 cases have passing coverage across those runs. |
| Rev application build | Passed with Clang 19; runtime native-window checks remain separate. |
| FLTK native-window group | Final rebuilt group 3/3 passed, 93.82 s: workflow 0.14 s, adapter 93.55 s and document 0.11 s. Earlier failures and repairs are recorded below. |
| Rev native-window group | 5/5 passed, 335.29 s: workflow, adapter, platform and both coordinate-scale checks. The rebuilt final-copy layout/lifecycle probe also passed with nine painted frames and zero failures. |

Native-window checks used an isolated Xvfb display with software OpenGL. The
full Rev group and final rebuilt FLTK group used:

```sh
DISPLAY=:93 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 CCACHE_DIR=/tmp/pumpmodem-native-ccache ./build.sh test native --backend rev --jobs 1 --build-jobs 4
DISPLAY=:93 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 ctest --test-dir build/dev --output-on-failure -L '^native_gui$' -j 1
```

This validates the exercised native frontend paths, not physical audio devices
or a hardware GPU/driver combination. The rebuilt Rev
`timeout 90 build/rev/test_rev_adapter --layout-lifecycle` probe used the same
display/software-rendering environment and passed after the final shared
airtime-label change. No native runtime check remains pending in this checkpoint.

The GUI failure was an obsolete attachment-service fixture for Robust's short
profile; the framebuffer failure expected the retired `Short` label instead
of `Target SNR`. Scoped fixture/help corrections preserve received-content and
state checks and add 16/17-byte text and 208/209-bit boundaries. Rebuilt
`ctest --test-dir build/dev --output-on-failure -R '^(gui_native_policy|framebuffer)$' -j 2`
passed both affected cases. An earlier stale Fast service fixture was corrected;
`gui_fast` passed in 4.54 s and the full GUI run's Fast live case in 106.99 s.
Web preview failures were sandbox localhost setup restrictions, resolved by
rerunning those checks with permission, without a source fix. Initial failed
invocations remain failures.

The initial FLTK native group passed 2/3 cases. Adapter diagnosis separated
fixture assumptions from a product defect.
Tab press/release checks now tolerate unrelated asynchronous revisions while
preserving their actual input/state contract. A compression-label fixture's
80 ms wait preceded the 100 ms native presentation interval; waiting boundedly
for the actual visible text exposed genuine glyph clipping. The product fix
shortened only the airtime label to, for example,
`3 DATA + 2 scored quiet slots (~300 s)`. The focused `--compression-labels`
check passed in 0.44 s, covering maximum 208-bit input and actual Compression
and narrow Console glyph bounds. Updated `gui_native_policy` copy assertions
passed in 0.05 s. FLTK, Rev and native-worker applications were rebuilt; the
final full FLTK group and focused Rev check above then passed. Those measured
outcomes, rather than the fixture corrections alone, close the runtime checks.

These results are functional coverage, not a new GPU workload forecast, native
CPU calibration or capacity qualification. No all-configuration operating pass
or final release-source identity is claimed. An initial aggregate compile
stopped on a missing `pcm_scale` declaration and was repaired before runtime
validation; that failed invocation executed no runtime assertions.

`ctest --test-dir build/dev --output-on-failure -R '^gui_robust_live$'`
passed in **3.41 seconds**. Its bounded simulation decoded `e` from actual native
`001` DATA scoring and two fully scored quiet slots, with validated content and
ordered pattern records. This one configured-calendar stratum used 1500 Hz
sampling, 500 Hz IF, positive noise at modeled 80 dB-Hz, exact oscillators and
Helper/FEC/spread/selective/FHSS Off. Its declared bounds were five scored
windows, 600,000 samples and 90 seconds wall time. The first fixture used
6000 Hz with a minimum 60-second public cadence and failed its sample-budget
preflight; the corrected legal 1500 Hz/500 Hz fixture retained the same cap.
The earlier failed setup is not counted as a completed reception trial.

A separate dated real-PCM fixture passed Hilbert conversion and complete native
DATA-1 scoring, then rejected transport EOF as message completion. It retained
actual receiver I/Q and waveform observations without publishing a complete
message. The same test covers audio routing/options, AMATEUR, one-shot force,
cancellation and session lifecycle through mock device providers. These mocks
are not physical-device qualification. The actual simulation and PCM receive
paths keep native DSP, score evidence and codec processing real.

An independent memory discriminator originally measured **183,910,400 bytes**
RSS against a **167,772,160-byte** cap: a real overrun, not an accepted timing
variation. Admission had credited aggregate resident growth against pending
scorer work that had not yet allocated its full DSP. The repair retains
outstanding DSP/stack/output reservations separately from process footprint;
initialized I/Q is covered once by the greater of footprint and the
baseline-plus-owned-input floor. The corrected deferred-DSP check withholds
the third slot rather than spending the same resident-memory credit twice.
Explicit-worker resolution also preserved
`H=12, W=27, C=27` when only `W` was supplied. Windows automatic-cap source now
uses the minimum of physical, commit and virtual availability; that source
inspection is not a Windows execution result. These bounded resource checks
supply neither an OS-enforced hard memory limit nor native CPU capacity.

Bounded migration comparisons reported the following matched-input evidence:

| Component | Observed coverage and limit |
| --- | --- |
| Helper | Nine reference cases matched status/reason, endpoints within `1e-10` and levels within `3e-6`; these do not establish all Helper-on DATA joins. |
| Native basis | Cached dense one-lane parity was exact; three-lane maximum difference was `4.78e-15`. Separate interpolation gates cover their declared geometry. |
| Source/exporter | 1,050,000 I/Q and validity samples plus ten public slot receipts matched exactly across DATA/Helper and selected channel cases. Shared storage changes workspace accounting, not sample semantics. |
| Duration planner | Twenty-three reference rows matched candidate durations within `3e-12`; the model remains provisional and does not guarantee sensitivity. |
| Full slot scoring | Four bounded clear/encrypted DATA and noise-quiet cases passed alongside invalid-input/resource/cancellation checks. A 71,154-row encrypted two-ray fine comparison matched affine/adaptive/combined scores within `1.183e-7` and the quiet threshold exactly. Different traversal counters are not matched CPU work. |
| Message/FEC | Configured Off/RS20/RS60, exact raw bypass, bounded erasures, quiet completion and rearming received focused checks and independent review. A leading ambiguous/quiet/DATA chronology bug was found and repaired with a distinct RS20 regression. |

This is numerical/functional coverage, not a throughput comparison or a success
probability estimate. Independent Message/FEC, Session/profile, source and
scheduler reviews found no remaining blocking issue in their reviewed scope.
The scoped complete-receiver mathematical review resolved coarse tie ordering,
lag and shared-endpoint workspace defects; their targeted regressions are
included in the passing triad group. The reviewed source, documentation and
lifecycle scope has passing functional coverage, including the final native runtime checks above. This checkpoint is
not physical-device, sensitivity, native-capacity, Windows/Wasm or release
certification. Temporary parity captures and command logs serve those checks; remove redundant
captures when the comparison and review are resolved, retaining concise
findings and evidence required for unresolved failures. No heavy CPU-capacity
rerun is part of this integration checkpoint.

## Sustained CPU hardware scaling

The Ryzen 5 PRO 5650U in the AC-powered ThinkPad L15 Gen 2a ran checked,
pinned AVX2 FP64 FMA work at 1, 2, 6 and 12 workers. Up to six workers occupied
distinct physical cores. Twelve used both siblings on all six cores. Each
primary run lasted 180 seconds and the table uses its final 120 seconds.

| Workers | Sustained GFLOP/s | Approximate active clock (GHz) |
| ---: | ---: | ---: |
| 1 | 51.641 | 4.04 |
| 2 | 95.691 | 3.74 |
| 6 | 214.277 | 2.81 |
| 12 | 237.934 | 2.55 |

Twelve workers delivered 11.04% more checked compute throughput than six and
2.49× the two-worker result. In the 90-second reverse-order revisit, the last
60 seconds measured 235.612 versus 216.224 GFLOP/s, an 8.97% gain. Twelve
workers averaged 11.45 busy logical CPUs in the primary run. These gains
describe register-resident FMA work. Same-stream receiver timings establish
how much of that parallel capacity the DSP can use.

**Allocation policy:** these measurements must not be used to reduce the
receiver's default worker budget. Keep all available logical hardware threads
available despite power/thermal limits or temporary/minor timing effects on
this host. Other processors and changing workloads can benefit from the
additional capacity. Low utilization calls for bottleneck analysis, not a
smaller global default; see the [CPU allocation contract](development.md#receiver-cpu-allocation).

The clock estimates use pinned-worker APERF/MPERF deltas. Boost was enabled
under `amd-pstate-epp` with the balanced profile and `balance_performance`
preference. AMD specifies 6 cores, 12 threads, 2.3 GHz base and boost up to
4.2 GHz. Linux documents that active frequency responds to workload and
operating conditions. [AMD specifications](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-5000-series/amd-ryzen-5-pro-5650u.html) · [Linux amd-pstate documentation](https://docs.kernel.org/admin-guide/pm/amd-pstate.html)

Although package-energy counters were unreadable, a separate 180-second run
with the exact same executable exposed readable AMD firmware power telemetry.
It sustained 240.037 GFLOP/s, 2.560 GHz and 11.51 busy CPUs. All 24 steady
five-second samples reported the sustained power limiter (SPL), with no
thermal limiter flags; median socket consumption was 11 W and Tctl was
58.75 °C. Across the eight receiver holdouts, 76 of 77 samples also reported
SPL, sometimes with current or fast package-power limiting. A low-load
control reported 1 W and no limiter flags. This directly establishes active
power limiting on the measured balanced profile. The configured ceiling is
unknown: sampled 11 W consumption is neither that ceiling nor AMD's specified
15 W default TDP. These observations do not supply a clock or GFLOP conversion
factor for receiver costs. The receipt preserves raw v2.2 metrics, the driver
mapping, hwmon cross-checks, timing windows and the unchanged original scaling
records. [Linux AMD telemetry](https://docs.kernel.org/gpu/amdgpu/thermal.html#gpu-metrics) · [Renoir/Cezanne firmware mapping](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/pm/swsmu/smu12/renoir_ppt.c)

Separate 20-second streaming-memory observations gave 9.362, 10.882, 9.312
and 9.012 useful GB/s at 1, 2, 6 and 12 workers. The 12-worker run averaged
8.80 busy logical CPUs while other desktop activity was present. These brief
observations do not establish an intrinsic SMT bandwidth penalty. Keep the
[scaling JSON](../local-evidence/cpu-scaling-current.json) while it supports
the worker-allocation decision and its telemetry limits. Remove the raw
report when a matched scaling check supersedes that evidence.

## Continuous receiver restructuring

**Historical runtime scope.** This section records the former Python-orchestrated
receiver with native DSP kernels and independent process owners. References to
“current” or “final” below identify that measured revision, not the new native
C++ GUI runtime. Process startup, resource trackers, copied transport and `memfd`
were part of that implementation and are superseded operationally. Retain its
failed gates, timing/memory limits and source identities; none transfers native
scheduler qualification. The [current ownership contract](development.md#receiver-cpu-allocation)
preserves `2*H`, full memory accounting and ordered physical completion.


**Final shared-input sustained observation.** One bounded 49-window run on the
final receiver used its automatic allocation: 24 software permits on 12 visible
hardware threads, with an 8,748,048,384-byte operating memory cap. Across all
417 predeclared eligible backlog intervals, it averaged **8.325 monitor-adjusted
busy hardware threads out of 12 (69.37%) for 51.898 seconds**. The longest
uninterrupted eligible stretch was **32.899 seconds at 7.921 busy threads**.
Eligibility followed input and backlog readiness, excluded owner startup and
ended at the start of the final window's assembly; it did not select CPU peaks
or a particular owner count. Thirteen services overlapped at most, and all 24
permits were allocated for 50.382 seconds of eligible coverage. The raw mean,
including monitor overhead, was 8.390 busy threads.

Owner startup occupied 21.238 seconds externally (21.183 seconds in the
receiver). The complete receiver lifecycle was 161.091 seconds, including
86.128 seconds through the final input push and 16.437 seconds from final
assembly to drain. Startup overlaps those lifecycle phases and must not be
added to them. Fixture generation took another 11.688 seconds; the supervisor
finished in 178.304 seconds under its 210-second bound. Peak sampled RSS was
5.167 GB and peak prospective admission was 8.744 GB under the 8.748 GB cap.
All 49 windows completed in order using sealed shared input, with two scored
quiet slots and continued listening; there were no retries, fallbacks or
remaining children. The full input digest matched before ingress, and source,
native artifacts and case identities matched after execution.
The declaration also required a full input digest after processing, but the
driver did not recompute it; that check is **unverified**. Source inspection
shows that ingress copies the input chunks and window assembly creates fresh
arrays, but this is not a substitute for the missing post-run digest. The input
is one DATA bit `0` followed by 48 quiet slots; its one-bit codec frame is
physically complete but invalid, not a decoded message demonstration.

The original predeclared exact historical-parity gate **failed** in 17 slots.
Independent examination found each difference was the number of dispatched
tracking update tasks, which changes with the worker partition. A separate
post-hoc comparison excluding only that allocation-dependent counter found all
other compared work, scores, coverage, addresses and decisions identical. This
does not change the original failed gate. The earlier 81.09% observation below
used the pre-memfd receiver and a larger 9.513 GB cap; the two runs do not
establish a causal speedup or slowdown. The current observation demonstrates
productive same-stream parallelism, not full saturation or all-case pipeline
capacity. Keep the raw result, failed gate and independent audit while they
support the current timing chart; replace them when an equally scoped current
measurement supersedes this claim.
[Final shared-input measurement](../local-evidence/final-backlog-cpu/result.json) ·
[Independent audit](../local-evidence/final-backlog-cpu/independent-audit.json)

**Scope: complete processing on a configured public calendar.** The retained
continuous listener preserves the configured keyed ordinal sequence. It detects
bits or nulls, confirms completion with two scored quiet slots and resets codec
state while keeping the global waveform ordinal. No semantic transmitter-identity
classifier is required. Acquisition of unknown waveform epochs, ordinals and
timing remains unqualified; configured-path estimates do not cover that search.
The full hardware-thread default
and the following qualified optimizations remain applicable.

**Current shared-input transport:** the Linux pipeline transfers sealed,
read-only memfd input instead of retaining several private copies per job.
Capability is checked once before admission; unsupported systems retain the
copied transport and its full reservations. Full DSP/native/input/result bounds
remain charged. The parent reserves one temporary assembly buffer, closes its
writable mapping before sealing, then retains only the FD and metadata until
successful release or safe retry. Children close every view and mapping before
acknowledging completion. FD-backed storage stays reserved even while unmapped;
there are no generic RSS discounts. Failed cleanup retains ownership and its
reservation. Finite and explicitly serial processing do not perform the probe.

A bounded full-size test held **three simultaneous 691,812,637-byte inputs**
for row-1 public ordinals 24–26. Canonical planning admitted **three eight-worker
jobs instead of one copied-input job** under the same 9072 MiB cap. Complete
modeled ownership, including one assembly buffer and transport overhead, was
**8,025,467,981 bytes**, leaving **1,487,213,491 bytes**. Sampled RSS peaked at
3.172 GB. Exact bytes, read-only mappings, immutable seals, malformed-descriptor
rejection and release/crash cleanup passed. This 7.440-second phase used
synthetic typed input: it establishes full-sized transport and conditional
admission, **not three-window row-1 DSP throughput**.

The native comparison used the same captured input through copied and shared
transport, with **24 total workers and three concurrent eight-worker services**.
All **839 checked input, placement, work, score, message and EOF fields matched
exactly**. Both decoded `001` to `e`, completed after two scored quiet slots,
scored a third quiet slot while listening continued, and rejected incomplete EOF
as physical completion. The shared path released all six parent descriptors.
A controlled collection boundary ensured the matched three-job wave; its elapsed
time is not a throughput estimate. Focused checks passed **63 stream tests and
35 backend tests**; the named triad group passed **22/22 in 30.30 seconds**.

The first private harness stopped after the full-size phase and copied native
control because it read transport metadata from an outdated receipt location.
The correction ran only the missing shared native look; neither completed phase
was repeated and production source remained frozen. Both supervisors confirmed
cleanup. Active execution totaled 62.561 s; the observed wall span was about
134 s within the original 240-second budget. Preserve the original harness
failure separately from the passing continuation.
[Full-size and copied-path receipt](../local-evidence/shared-input/final/result.json) ·
[Shared native continuation](../local-evidence/shared-input/final/continuation/result.json) ·
[Named checks](../local-evidence/shared-input/triad-final.log)

**Measured result before memfd transport:** automatic operation admits up to `2H`
independent owners sharing exactly `2H` software permits, subject to actual
backlog and full-DSP memory admission. `H` is detected from affinity/quota,
not fixed at twelve. Child bootstrap imports occur once before readiness;
future children are budgeted from their immutable pre-DSP RSS/high-water
measurements, without duplicating the parent's input heap. Parent and live
owner memory remain fully charged. A bounded reorder window lets completed
future slots release their owners before chronological message commitment.

That revision's **49-window** native capture sustained **9.730 monitor-adjusted busy
hardware-thread equivalents out of 12 (81.09%)** over **55.337 seconds** of
predeclared backlog coverage. Eligibility required all input accepted, the first
slot committed and fully received work still waiting; owner-startup intervals
were excluded. Neither achieved width nor measured CPU rate selected the bins.
Thirteen child services overlapped for 23.483 s and twelve for 30.176 s of that
coverage; all 24 permits were occupied for 52.363 s. Fourteen owners were
resident. Only 1.93% of eligible time reached 95% hardware occupancy, so sustained
full utilization remains unproven. Short counter bins have scan/quantization
jitter; the 55-second average is the useful measured point.

All 49 windows completed in order and the first 25 exactly matched the earlier
capture's work, scores and message result. Source, native artifacts, input and
allocation identities passed independent audit. Current owned-resource
accounting peaked at 9.463 GB; the larger final prospective admission was
9.475 GB, both below the frozen **9072 MiB (9.513 GB)** cap. Sampled aggregate
RSS peaked at **5.795 GB**. These are different quantities: reserved worst-state
DSP memory is not measured RSS. There was no fallback or workspace retry.
The full run and cleanup took **153.746 s** within 190 s; the receiver lifecycle
included **24.132 s** of one-time owner startup. This is one pre-memfd
utilization observation, not a measurement of the later shared-input revision,
a paired speedup or an all-row capacity result.
The final source passed **131 receiver checks, 23 backend checks and 22/22 named
triad checks (30.59 s)**. Temporary benchmark callers were removed after audit.
[Current backlog measurement](../local-evidence/resource-backlog-cpu/result.json) ·
[Independent audit](../local-evidence/resource-backlog-cpu/independent-audit.json) ·
[Current timing chart](receiver-parallelism.html#cycle) ·
[Final named checks](../local-evidence/parallel-routes/triad-expanded-final.log)

The preceding follow-up had twelve resident owners but only eleven overlapping
services, so its predeclared twelve-service sustained check failed. Its retained,
explicitly exploratory eleven-service analysis averaged **9.152 adjusted busy
threads** across 26.964 s. It exposed parent-heap-dependent startup admission;
the final architecture above corrected that accounting and removed the hardware
count as a software-owner ceiling. The failed check remains failed, and its
timings are not a paired control for the changed implementation.
[Preserved failed check](../local-evidence/resource-steady-cpu/result.json) ·
[Exploratory audit](../local-evidence/resource-steady-cpu/exploratory-width11-audit.json)

The current development changes preserve the full logical CPU budget and
remove repeated work from the dominant noise search and fine state updates.
The comparisons below use the same captured input, receiver work and allocation
with the optimized callback enabled or disabled. They are scoped development
comparisons; their exact source/native identities are in each receipt. They
must not be multiplied together or treated as complete receiver capacity.

The coarse CPU path shares immutable observation spectra between workspace
partitions. A strict-FP64 native rank-two kernel combines quadratic scores and
bounded peak selection inside the existing worker pool. Conservative arithmetic
ambiguity guards retain canonical peak selection for near ties, and retained
scores are recomputed with the canonical arithmetic before global selection.
Unsupported ranks, guarded Grams and larger retained-peak counts keep the
general path. The final twelve-worker row-4 noise comparison reduced median
elapsed time from **0.451572 to 0.295567 seconds (1.528× throughput)**. Busy-core
equivalents increased from **6.66 to 8.12**. A disjoint frequency holdout reduced
0.478227 to 0.326784 seconds (1.463×). All 1,248 raw peak identities, ordering
and published scores per look matched bitwise. The entire bounded qualifier
took 5.124 seconds; it did not execute the full broad frequency grid.
[Guarded coarse comparison](../local-evidence/continuous-coarse/guarded-native-discriminator.json)

Complete mapped CPU FP64 one-/two-tap pairs now use native candidate-parallel
Gaussian updates on that same pool. Gram validation is shared across coherence
models; each candidate advances the two blocks in chronological order using
PSD square-root factors and Cholesky solves. The controller publishes results
only after every tile joins. Generic, erasure and device paths retain the
general implementation. At twelve workers, row 7's continued fine time fell
from **403.174 to 334.730 ms (1.204×)** and its model phase from 97.034 to
19.366 ms. Fine busy cores increased from 3.174 to 3.262, with 14.65% less CPU
time. Row 7 at six workers improved 1.331×. Row 4 at twelve improved 1.095×,
below the declared 10% secondary gain target; that outcome is retained.
Logical work, state order and threshold outcomes matched under the declared
posterior tolerance. These four-second continued sessions are not successful
message trials or whole-receiver saturation measurements.
[Row 7 / twelve](../local-evidence/structural-fine/row-7-native-gaussian-integrated-abba.json) ·
[Row 7 / six](../local-evidence/structural-fine/row-7-6-native-gaussian-integrated-abba.json) ·
[Row 4 / twelve](../local-evidence/structural-fine/row-4-12-native-gaussian-integrated-abba.json)

The post-native row-7 profile averaged 340.674 ms without instrumentation and
346.774 ms with it (1.79% higher). Candidate waves occupied 160.470 ms and
averaged 5.50 busy-core equivalents within those waves. Controller preparation
occupied 55.141 ms, including 14.592 ms of native-cache requests/waits; adaptive
transitions occupied 37.478 ms. Native model updates now occupy only 20.470 ms.
These spans are nested where stated; callable overlap is not an OS scheduling
trace. Native rendering and basis FFT dominate worker CPU time. The profile
retained exact work, ordered states, posterior bytes, scores and endpoints.
[Post-native profile](../local-evidence/structural-fine/row-7-post-native-residual-attribution.json)

Mapped descriptor preparation now validates one bounded geometry bank and reuses
its extended-precision affine terms across both chronological blocks. Internal
staging accepts at most 1,024 rows; guarded fallback and public native calls remain
bounded to 128. Pin ownership and numerical evaluation order are unchanged, and
the shared two-block workspace explicitly reserves an additional 512 KiB.
A frozen four-variant comparison took 17.83 seconds, including fixture setup
and warmups. Mapped reuse reduced row-7 four-second continuation pushes after a
fixed two-second prefix from **329.192 to 301.838 ms (8.31%, four of four paired
wins)**; busy-core equivalents rose from **3.301 to 3.584**. This metric excludes
prefix, progress queries and finish. Row-4's cold two-segment maximum-state
branch improved from **222.853 to 213.626 ms (4.14%)**. Scores, posteriors,
state ordering, threshold sides, native fills and logical references matched
exactly. These are fine-stage gains, not complete-receiver throughput.

The same comparison rejected a shared timing-transition plan: it halved stencil
enumeration but failed its prospective standalone and incremental gain gates.
Typical elapsed time averaged 7.31% and 9.19% higher; this does not isolate that
entire difference as transition cost. The transition implementation, its tests
and altered work-count guard were restored to the baseline. No favorable rerun
was taken. The private comparison driver was removed after independent audit;
retain its compact receipt and audit while they support this mapped optimization
and the rejected alternative. Baseline variants shared the patched constructor
workspace allowance, so this is not a whole-HEAD memory comparison.
[Fine reuse comparison](../local-evidence/structural-fine/transition-mapped-plan-qualification.json) ·
[Independent audit and provenance](../local-evidence/structural-fine/transition-mapped-plan-audit.json)

A subsequent small reuse change shares repeated mapped-span pointer tables and
avoids repeated lookup of tiles already pinned in a block. Six matched pairs
preserved exact scores, posterior state, native/cache counters, tile contents
and LRU order, with unchanged measured sources and binaries. Row-7 continued
fine pushes averaged **168.448 → 166.591 ms (1.10%)**, with two of four paired
wins; row-4 capped pushes averaged **215.130 → 209.564 ms (2.59%)**, with one of
two wins. These noisy point observations support retaining the bounded removal
of duplicate work under the user's incremental-improvement policy; they do not
establish precise speedups or whole-receiver saturation. Both variants used six
workers and the same constructor/workspace formulas. The single qualifier took
4.37 seconds; independent audit accepted its exactness and provenance. Private
experiment code is retired after audit; the compact receipt remains while this
optimization is current.
[Span-directory qualifier](../local-evidence/stage-directory/qualify.json)

An additional bounded experiment compared 12, 18 and 24 software-worker permits
on this same twelve-logical-CPU machine, with one captured stream and a frozen
5120 MiB memory cap. One seven-window screen took 124.27 seconds:

| Controllers × workers | Complete lifecycle (s) | Six quiet-slot service union (s) | Busy-core equivalents |
| --- | ---: | ---: | ---: |
| 2 × 6 | 23.893 | 18.316 | 3.846 |
| 2 × 9 | 24.691 | 18.740 | 4.350 |
| 2 × 12 | 25.795 | 19.613 | 4.236 |
| 3 × 6 | 22.334 | 16.475 | 5.265 |
| 6 × 4 | 23.421 | 16.584 | 7.045 |

These are selection observations, not five independently qualified speedups.
The selected three-controller route then received the single predeclared ABBA
comparison on thirteen windows, measuring the last six quiet slots after six
quiet settling slots. Its median selected service union fell from **18.613 to
17.889 seconds (3.89%)**, below the fixed **15% adoption gate**. Complete
lifecycle fell from **41.163 to 38.409 seconds (6.69%)**, while busy-core
equivalents rose from **4.116 to 5.778**. Both paired service ratios improved,
but the gate was not relaxed or retried. The qualifier took 160.78 seconds.
All nine looks matched exposed receiver work, scores and ordered message results
exactly; source, binaries, inputs, software permits and memory remained valid.

Selected service is the union of actual child DSP and selected parent assembly,
transport, result and commit intervals. It is not the sum of overlapping owner
walls or a commit-to-commit approximation. Raw ingress, original native setup,
owner startup and bounded shutdown are charged in complete lifecycle time.
CPU activity uses the separately matched processing monitor interval and twelve
as its hardware denominator. The original 15% gate remains rejected. The user
subsequently directed that increased productive utilization with a modest
throughput gain is worthwhile, particularly for less power-limited processors.
That changed adoption criterion justified continuing the architecture beyond a
fixed three-controller policy. It is not a relabeled statistical pass or a
favorable timing retry.

An intermediate resource-driven implementation targeted `H` independent owners and
`2H` shared software permits, with `H` determined by affinity and quota. Every
mode uses `2H` when workers are omitted; explicit caps remain exact. Owners are
created only for ready complete windows that fit conservative public-geometry
DSP bounds. Each job reserves its required workspace, leaving remaining RAM
available for later input and other jobs. Completed future results can release
their owners before ordered commit, within a separate bounded reorder buffer.
Automatic operating RAM uses cgroup-aware available memory less an OS reserve;
the old 3840/5120 MiB experimental caps and half-memory rule are not operating
limits. That intermediate source passed 137 focused DSP/planning checks, 54 parent
scheduler checks and 20 process-backend checks, with independent source review.
The required native triad group passed **22/22 in 29.86 seconds**. These checks
preserve exact allocations, complete-work memory admission, ordered two-slot
completion, lazy growth, refilling behind a slow frontier and bounded cleanup.
The earlier five-window default check failed only its private
message-event expectation; its corrected continuation was not run before this
architecture superseded that plan.
[Worker screen](../local-evidence/receiver-oversubscription/screen.json) ·
[Independent ABBA qualification](../local-evidence/receiver-oversubscription/qualify.json) ·
[Predeclared rules](../local-evidence/receiver-oversubscription/predeclared.json)

That intermediate default then passed one matched **25-window** comparison
on the same configured row-7 capture, using a shared **9,118,859,674-byte** cap
derived from available RAM. This is a new scoped comparison, not a retry of
the earlier 15% gate:

| Measured quantity | Explicit two owners / 12 permits | Automatic / 24 permits |
| --- | ---: | ---: |
| Actual maximum concurrent slot owners | 2 | **12** |
| Selected quiet-slot service union, s | 71.295 | 67.552 |
| Complete lifecycle, s | 77.991 | 77.264 |
| Average busy hardware-thread equivalents | 4.208 | **8.633** |

Automatic execution held twelve overlapping child services for **36.407 s**
and refilled owners for slots 13–24 while peers were still active. The busy-thread
average covers the whole 77.083-second processing interval, including preparation,
fill and drain; it does not measure CPU use within that full-width interval.
The single pair establishes productive increased concurrency, not a precise
statistical speedup. All 25 slots and 2,473 exposed logical/score values matched
exactly, including DATA/null decisions and ordered completion after two quiet
slots. There were no workspace retries or serial fallbacks. Modeled peak ownership
was **9.094 GB** and sampled aggregate RSS **5.399 GB**, both below the shared cap.
All 66 source hashes, both native artifacts and input hashes remained unchanged.
Independent audit accepted the receipt; the supervisor completed in **164.589 s**
within 220 s and verified no surviving owned children.
[Resource-driven comparison](../local-evidence/resource-parallelism/result.json) ·
[That revision's triad checks](../local-evidence/parallel-routes/triad-final.log)

Pre-memfd all-case public planning provides a **prospective memory admission envelope**
at the separately frozen 9072 MiB operating cap. Initial/recurring ready-window
snapshots over ordinals 0–23 and 24–47 admitted widths
**1, 7, 6, 4, 13, 4, 13, 9** for rows 1–8, sharing 24 permits. The 16.294-second
pass queried public geometry and loaded one canonical child per row to measure
bootstrap overhead; it generated no IQ or templates and performed no scoring.
These widths depend on the declared retained input and runtime reservations;
actual startup RSS and ready work must be checked again. They are neither
measured throughput nor universal maximum widths. A smaller retained input
range can admit differently. Row 1's roughly
113-minute windows make input ownership particularly costly: twelve input/IPC
reservations alone exceeded 30.9 GiB under that copied transport. The shared-input
qualification above removes those repeated copies and admits three row-1 jobs;
the other rows' widths have not been requalified for that transport. More RAM can
therefore enable materially more useful concurrency on other machines. The
older H-cap envelope remains a scoped preceding observation, not a current limit.
[Pre-memfd per-case admission envelope](../local-evidence/resource-parallelism/admission-final.json)

An earlier bounded scheduling discriminator processed two adjacent quiet
windows from **one continuous native capture** with separate persistent Python
controllers and native source owners. Both arrangements shared the same
twelve-thread budget. Two concurrent six-worker jobs reduced whole-slot bank,
DSP, IPC and ordered quiet-state service from **8.913410 to 6.481362 seconds
(27.29% less elapsed; 1.375× throughput)** compared with sequential twelve-worker
jobs. Busy-core equivalents increased from **2.94 to 4.45**. All logical work,
coverage and ordered decisions matched; the largest numerical difference was
4.44e-16. This experiment excludes production ingress and pruning, so it does
not yet qualify the integrated listener or other manual rows.
[Two-controller discriminator](../local-evidence/continuous-process/process-discriminator.json)

One predeclared width extension compared four three-worker jobs with two waves
of two six-worker jobs on four adjacent windows. Four owners reduced elapsed
time by **14.39%**, below the fixed **15% incremental adoption gate**, while
raising busy cores from 4.43 to 5.99. It also increased aggregate CPU work and
peak sampled RSS to 2.354 GB. All sixteen evaluated slots preserved logical
work and ordered decisions; maximum numerical difference was 1.33e-15. No
threshold was changed or favorable retry taken. Two owners were selected at
that stage of the investigation; this historical decision is not a portable
owner cap and does not reduce the aggregate hardware-thread budget.
The combined scheduling investigation took 107.53 seconds, including an initial
memory-admission rejection before DSP and the explicitly revised two-owner
partition. The global 3840 MiB budget was unchanged.
[Width extension](../local-evidence/continuous-process-width/process-discriminator.json)

That first production integration used the bounded two-owner scheduler.
Ordinary `push` can accept input while DSP runs; `resume(wait=True)` drains
accepted work. A lone job keeps the full visible logical-thread allocation;
two ready jobs share it. Private native owners isolate the mutable keyed basis
address, and the parent commits results in public-slot order. Runtime, input,
IPC and result ownership share the local memory reservation. Memory admission
can select a latched full-budget serial fallback. Transport failures and
interruption fail closed, and child teardown is bounded. Service accounting
unions actual background execution with controller API intervals, excludes
application polling delays and records one-time owner startup separately.

Independent source review accepted the integration; 80 focused parent/lifecycle
checks and 14 backend checks passed. The final required triad group passed
**22/22 in 32.84 seconds**, including the refill and auxiliary-memory corrections
below. The native functional gate passed in 24.15 seconds:
`001` decoded to `e`, quiet slots 3 and 4 completed the message, and quiet slot
5 was scored while listening continued. Four of six slots used the split
worker budget; there was no memory fallback. A truncated-input control remained
physically incomplete. An initial stdin-based launcher failed before DSP because
spawn could not reopen `<stdin>`; the identical call then used a guarded script.
No production code changed to repair that launcher. A subsequent narrow
ingress correction makes buffer backpressure wait only until space is available,
so the producer can refill while paired jobs continue; explicit drain/EOF still
wait for all accepted work. Its discriminating regression is included in the
80 checks. The native functional receipt predates that refill correction and
does not measure the integrated scheduler's throughput or qualify all cases.
[Final triad log](../local-evidence/continuous-pipeline/triad-final.log) ·
[Native pipeline gate](../local-evidence/continuous-pipeline/functional-gate.json)

The production row-7 backlog comparison then passed its predeclared integration
gates in **83.43 seconds**, including setup and cleanup. ABBA used ordinary
16,384-sample `push` calls and `resume(wait=True)` on the same five captured
windows: DATA bit 0 followed by four quiet windows. Both automatic looks used
allocations **12, 6, 6, 6, 6**, with no fallback or retries. All 1,649,627 supplied
samples were covered once (274.9378 seconds of physical input). Independent
audit checked 1,856 exact fields and 392 numerical fields across twenty slot
results; every exposed numerical value matched exactly. Deeper candidate/cache
parity remains scoped to the earlier two-owner discriminator.

| Production metric | Serial 12 | Automatic two owners |
| --- | ---: | ---: |
| Median complete receive service | 21.6792 s | 18.9520 s |
| Controller/worker service union | 21.6779 s | 18.9514 s |
| Average busy-core equivalents | 2.909 | 3.566 |
| Listed-process sampled CPU time | 63.055 s | 67.585 s |

This is **12.58% less elapsed service (1.144× throughput)** with **22.62% more
productive CPU occupancy**, at 7.18% more listed-process CPU time. These CPU and
busy-core figures cover the sampled evaluator/controller/native processes;
they exclude the shared resource tracker identified below. Elapsed service
includes its startup. The comparison includes
one-time spawned-owner startup (1.255 and 1.292 seconds). Subtracting only that
measured startup gives an 18.45% reduction over the five-window service; this
descriptive calculation is not a separate sustained-throughput measurement.
Full twelve-thread saturation and other rows' parallel capacity remain unproven.
All 66 measured sources and both capture hashes stayed unchanged.
[Production comparison](../local-evidence/continuous-pipeline/production-qualifier.json)

The sampler observed 1.318 GB across its listed evaluator/controller/native
processes. A subsequent audit found that it omitted Python's shared spawn
resource tracker (12.76 MB in an isolated matching-runtime check). That omission
does not exhaust the observed margin to the 3840 MiB limit, but it exceeds the
old conservative reservation's 7.64 MB headroom. The corrected implementation
reserves at least 32 MiB for that shared helper before spawning and retains the
reservation after closing owners or falling back to serial execution. It does
not create or kill the process-wide tracker itself. Explicit serial execution
from the outset creates no tracker and has a different memory allowance from
automatic fallback.

A **14.71-second current-source continuation check passed** on the exact
929,627-sample prefix of the production capture. The three DATA/quiet/quiet
results, work and numerical fields matched the earlier comparison exactly;
allocation was 12, 6, 6, without fallback. Sampled aggregate RSS including the
tracker was 1.277 GB. The 12.70 MB tracker was covered by the 33.55 MB reservation,
and modeled peak ownership was 4,015,521,077 bytes within the 4,026,531,840-byte
limit. Source/binary identities and post-exit cleanup checks passed. This
qualifies the corrected admission and work paths; the original ABBA remains
the timing evidence for its recorded source, not a rerun of the corrected
budget path.
[Tracker-budget continuation](../local-evidence/continuous-pipeline/tracker-budget-gate.json)

The [historical two-owner execution chart](receiver-parallelism.html#two-owner-baseline) records
**206 actual events** from a fresh five-window run on `c54e2469`, using the same
1,649,627-sample capture (274.9378 seconds of physical input). It shows parent
ingress/startup/assembly/commit, both owners' nested preparation/search/fine
stages and a preselected early fine cycle in quiet window 3. It remains an explicitly historical comparison beneath the current
49-window CPU trace; the older `3b423f9` fine trace is also retained as history.

The plain control took **18.8923 seconds** and the traced run **18.8647 seconds**.
Their observed difference is −0.146%; this single ordered pair is neither a
speedup estimate nor a causal estimate of tracing overhead. Work, scores,
decisions and message state matched exactly. Both used allocations
**12, 6, 6, 6, 6**, with stable source, binary, capture and adapter hashes. The
whole bounded capture/check took **40.64 seconds**. No receiver code changed.

The timestamps show actual refill overlap: window 3 starts at 11.645 seconds
while window 2 runs until 13.377; window 4 starts at 13.386 while window 3 runs
until 17.455. The selected affine/adaptive cycles took 27.490/39.859 ms, each
with six basis jobs and six native Gaussian candidate jobs. They are fixed
early cycles, not a warmed average. Recorded call intervals include waits and
nested work; their overlap does not establish simultaneous OS execution or
full CPU saturation. Wall time uses the shared monotonic clock; thread CPU
values are independently sampled same-thread deltas, with small clock-sampling
differences possible. The chart does not qualify additional configurations or
blind reception.

Independent audit checked all event identities, nesting, dispatch/collection
order, physical coverage, source hashes and the twelve-worker permit ceiling.
Retain the compact trace while it supports this audited chart. The temporary
adapter, regenerated captures and redundant owner sidecars were removed after
acceptance (28.09 MB). Browser checks covered window navigation, interval
inspection, both fine-cycle views, historical disclosure and a 390-pixel layout;
no horizontal overflow or console warnings/errors were observed. Unique element IDs,
local links and JavaScript syntax also passed checks.
[Historical two-owner timing receipt](../local-evidence/current-timing/timing-trace.json) ·
[Clock semantics](https://docs.python.org/3.13/library/time.html#time.perf_counter)

Private pipeline drivers and raw captures were removed after independent
audit (about 44 MB). Keep the compact comparison, functional/admission receipts
and original setup failures while they support the current scheduler claims;
remove them once superseded by an equally scoped validation. An unqualified
general parallel-forecast prototype was also deleted rather than maintained
alongside the canonical sampler. It produced no qualified capacity result.

One guarded FP64 phasor-recurrence experiment reused each direct phase anchor
for 32 samples without changing interpolation coordinates. The frozen paired
comparison preserved numerical/work gates but saved only 7.53% and 6.91% of
continued fine time on rows 7 and 4, below its declared 10% adoption gate.
The variant was removed. An earlier attempt was invalidated by a concurrent
source edit; that failure remains recorded separately. No source or binary
changed during the single authorized replacement comparison.
[Rejected recurrence](../local-evidence/structural-fine/phase-recurrence-discriminator.json)

Two-tile preparation overlap failed the declared 10% useful-gain gate before
and after native scoring. Wider workspace FFT batches and Python-only bounded
peak selection were slower. Those variants and their private drivers were
removed. The installed SciPy 1.18.1 FFT backend uses DUCC, releases the GIL for
the transform and already caches plans. The repository's power-of-two-only
native FFT would increase a representative 19,600-point transform to 32,768;
there is no established benefit from replacing this backend.
[SciPy wrapper](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/scipy/fft/_duccfft/pyduccfft.cxx) ·
[DUCC plan cache](https://raw.githubusercontent.com/scipy/scipy/v1.18.1/subprojects/duccfft/ducc0/fft/fftnd_impl.h)

A bounded fresh-start supervisor prototype was rejected and removed. It tried
ordinal-zero starts on the public 30-second grid while continuing one acquired
message. In a native test with independent receiver configuration, leading
noise and a legitimate restart behaved correctly, but the eighth attempt
mistook an active message's ordinal-one DATA for a new ordinal-zero message.
Its DATA-versus-noise score was about 59.5 million against a 21.49 threshold.
The test stopped at that first mismatch after 36.175 seconds, with unchanged
source, input and binary identities. A noise-only admission bound does not
decide competition between two signal explanations. Subtracting existing
different-window scores or selected likelihood lower bounds would not repair
that model error.
[Failed native gate](../local-evidence/continuous-fresh-native-gate.json)

The same prototype has an unsuitable search-cost structure for the long-bit
cases: complete windows every 30 seconds would give 225 starts per 6,750-second
row-1 slot, before any competing-message work. This is a hypothetical work
density, not a measured CPU forecast. Native DATA keys incorporate start epoch,
symbol ordinal and local tile even without payload encryption, so one
UTC-shifted correlation prefix cannot represent all those starts. Retain this
failed receipt and its predeclaration/log while they support the unresolved
blind-acquisition design question; the unused supervisor, emitter fixture and
associated tests have been deleted. This rejects the ordinal-zero supervisor,
not continuous bit/null reception. Further acquisition needs efficient address
hypotheses, potentially staged or Helper-assisted, with declared detection/error
bounds and incorrect-address checks. A semantic new-transmitter classifier is
not required. The configured-calendar estimates must keep the unimplemented
epoch/ordinal/timing search explicit.

Keep these compact receipts while they support the current implementation,
its numerical qualification and rejected alternatives. Private prototype
implementations have no continuing purpose. Replace raw receipts when a
matched current qualification supersedes their claim, preserving the concise
findings here. The earlier interactive report below remains a labeled baseline.

## Receiver restructuring

This section records the preceding source-pinned restructuring and the baseline
used by the interactive report. Later changes are recorded above.

That receiver stages immutable candidate plans, fuses mapped rendering,
DFT projection and sufficient statistics, and advances the two ready blocks
of a segment in one model job. Exact bit-pair geometry is evaluated once for
both independent DATA hypotheses. Global timing transitions and pruning retain
their original boundaries. Partial erasures and generic nonzero-null callbacks
keep the ordinary path. Worker failure drains submitted work, retains native
pointer owners and prevents publication of an incomplete incremental result.

At twelve workers, matched four-second continuation comparisons measured
**1.750× throughput on row 7** and **1.664× on row 4**, with 37.0% and 31.4%
less CPU time. The control disables the fused callback inside the same source
and uses the same capture, coordinates, allocation and native artifact. These
are fine-stage comparisons, not whole-service speedups or ratios against an
older calibration. Useful work, retained states and endpoints matched; maximum
lane-score differences were 5.68e-13 and 2.27e-13. Busy cores rose from 2.81 to
3.09 and from 3.02 to 3.44. **Whole-receiver saturation is still unproven.**
[Row 7](../local-evidence/structural-fine/row-7-12-final-overall.json) ·
[Row 4](../local-evidence/structural-fine/row-4-12-final-overall.json)

For the six-second row-7 session, Gaussian jobs fell from 144 to 72, mapped
worker submissions from 1,776 to 288, and physical cache hits from 18,083 to
236 while all 18,088 logical row/tile references remained. Exact bit-pair reuse
alone saved 18.9% of CPU time and 6.3% of elapsed time in its twelve-worker
comparison. The six-worker wall-time improvement was smaller and noisy; the
DC control established CPU-time savings but no reliable separate elapsed gain.
The full logical-thread default remains in force.

The preceding source's row-7 residual profile preserved work, states, scores and
endpoints in all five observations. Unmarked four-second continuations averaged
0.423 s and 3.16 busy cores. Instrumentation added about 2% on average. In the
marked path, mapping/statistics consumed 50% of elapsed time, model barriers
24%, and adaptive transitions 9%. These inclusive spans and worker CPU counters
must not be summed as independent elapsed costs. Fixed-affine setup, collection
and prediction together accounted for only 5.6%, an optimistic bound on savings
from persistence alone. A compiled batched state-update interface or safely
overlapped candidate lanes is a more substantial future restructuring, requiring
new numerical and transition-boundary qualification.
[Final residual profile](../local-evidence/structural-fine/row-7-final-residual-attribution.json)

A separate bounded chronological trace at source `3b423f9` supports the baseline
[interactive cycle chart](receiver-parallelism.html#cycle). One warm look and
four plain/traced ABBA looks took 4.34 s including fixture setup; no production
instrumentation was retained. The traced four-second continuation contains 16
candidate waves of twelve jobs, 16 model waves of three jobs and three native
cache fills. It reaches twelve overlapping worker job intervals. Within its
415.29 ms of instrumented push spans, 170.84 ms has no tracked worker job in
progress. That is neither an idle-CPU measurement nor a whole-receiver serial
fraction: controller work, native child work, gaps between wrappers and OS
scheduling are distinct. The traced mean push time was 6.84% higher than the
plain mean in these ABBA looks;
untraced controls averaged 379.32 ms. Work, ordered states, scores, endpoints
and channel posterior bytes at push boundaries matched exactly. These short
controls establish trace fidelity, not a new sustained capacity estimate.
The checked endpoint remained incomplete/ambiguous; this is not a successful
message trial. The displayed 417.03 ms axis additionally includes 1.74 ms of
probe checkpoint/digest gaps outside the push spans.
The receiver advances its phase/clock hypotheses using captured sample
timestamps, so CPU waiting does not age the captured waveform. Independent
observation or fixed-affine basis preparation can potentially overlap current
work, while adaptive state
prediction and posterior publication retain chronological dependencies. The
report's double-buffer schedule is an explicitly hypothetical model, with no
measured speedup claimed. Keep the [timeline receipt](../local-evidence/structural-fine/row-7-execution-timeline.json)
while it supports this current scheduling investigation; replace the snapshot
and remove the receipt when a changed scheduler supersedes it.

A bounded ready-lane overlap discriminator replayed a real row-7 segment's
prepared candidate work alongside the other lane's ordered model updates on
the same twelve-worker pool. Fresh model copies, pinned native owners and
49.22 MiB of scratch preserved statistics, posterior state and scores; raw
energy reduction differences stayed within the declared tolerance. Five warm
and twenty interleaved replays took 2.55 s internally. Six candidate jobs saved
a paired median 0.818 ms, below the predeclared 3 ms useful-gain gate; nine jobs
lost 0.924 ms. Even the positive result would save only about 1.5% of the final
fine slice before live planning/commit overhead. This does not justify a live
cross-lane scheduler change. The private driver was removed without changing
production sources or repeating the calibration.
[Overlap discriminator](../local-evidence/structural-fine/row-7-ready-lane-overlap.json)

Recurring integer-source bank preparation now leases each epoch/bit source
once, shares clock/ray plans and applies a native four-tap stencil in the worker
pool. The representative row-7 preparation fell from 6.628 s to 1.504 s with
source reuse, then to 0.490 s with the stencil. The final fresh stencil pair
was 1.434 s versus 0.490 s. Source requests fell from 1,248 to five. The final
twelve-worker observation averaged 3.51 busy cores; its six- and one-worker
observations were 0.396 s and 0.613 s. These single observations do not select
a smaller portable worker budget. All 78 workspace arrays agreed within
9.44e-16 peak-relative error, and DATA/H0 candidate identities, order,
coordinates and search work matched. A byte-equality gate failed and remains
recorded; the qualified result uses the declared numerical tolerance.
[Source reuse](../local-evidence/current-bank-lease/row7-pilot.json) ·
[Native stencil](../local-evidence/current-bank-lease/row7-native-stencil-paired.json)

A grouped NumPy projection variant increased CPU work without reducing
elapsed time and was removed. The post-reuse row-7 native source takes about
0.1 CPU-seconds, making further pulse-synthesis GPU acceleration a small target
there. A different FHSS coefficient/stencil interface could also remove
transport, but is not implemented or qualified. The earlier tensor-DFT
conclusion below remains applicable; no tensor speedup is inferred from this
CPU restructuring.

Large CPU observations also reuse the existing bounded FIR overlap-add path.
The branch depends on public sample count and backend, preserves finite tails,
global decimation phase and validity checks, and shares its actual buffer
estimate between ordinary and tiled acquisition. CUDA keeps its polyphase
filter. In a bounded row-1 public-geometry check with synthetic noise and a
tone, filtering fell from 6.715 s to 2.461 s and its modeled workspace from
2.940 GB to 0.342 GB. Projection relative error was 8.28e-16; full and tiled
search retained the same candidates and work. This is an operator/admission
check, not a native-channel communication trial or a complete capacity result.
[Filter qualification](../local-evidence/current-restructure/row1-streamed-observation-paired.json)

The fine and bank comparison receipts predate this isolated large-input FIR
routing change; their measured row-4/7 paths do not enter that branch. Keep
the scoped receipts, original failures and current integration results while
they support this qualification. Scratch implementations and copied sources
have been removed. Retire raw receipts when matched replacements supersede
the claims, retaining the concise algorithm and negative-result findings.

## Complete headless Robust and manual capacity

These estimates measure the historical Python-orchestrated complete receiver
with native kernels. They are retained with their original margins, failures
and applicability receipts, not as native GUI runtime calibration. New native
CPU work requires matched affected timings and complete same-stream accounting.


The retained complete-work measurements cover all eight manual rows on the Ryzen
5 PRO 5650U with **one 24-worker pool, 12 hardware threads and a 9072 MiB
workspace**. Every row includes complete work on the configured public calendar.
These are complete-work service estimates for that allocation, **not the
saturated ceiling of the automatic multiowner receiver**. Neither the row-7
pipeline gain nor its CPU occupancy is a conversion factor for the other rows.
Unknown epoch/ordinal/timing acquisition and physical sensitivity also remain
outside this qualification.

The audited union preserves the canonical measurements from three bounded
processes: rows 1–6 from the original run, row 7 from its deadline-limited
continuation and row 8 from one separately predeclared residual run. Completed
rows were not remeasured. Source, native artifacts, manual inputs, initial
runtime, allocation and the 72-comparison statistical family match; independent
audit verified component sums, row selection, workspace bounds and cleanup.
**Row 7 retains an explicit provenance limit:** its row was fully written before
the continuation was interrupted during row 8, but the canonical report has no
end-of-run source/runtime flags. External post-termination source/native/input
checks match; they do not manufacture the missing runtime completion flag.
[Current eight-row summary](../local-evidence/parallel-routes/all8-current-summary.json)

The shared-input revision retains these single-pool DSP and finite-controller
costs through an independently reviewed applicability receipt. Invoked globals
and initialization were checked as well as function bodies; only the two
pipeline modules changed, and the remaining 64 measured sources and native
artifacts match. Original timing reports and their source identities are intact.
The explicit forecast option `--cpu-calibration-applicability` validates the
exact report bytes, reviewed before/after source pair, native/runtime identity,
manual input, workers and workspace. It does not change `same_measured_source`
or loosen CUDA/reference/independent-holdout and measurement-quality gates.
All **26 forecast tests passed**, including actual CLI acceptance with unchanged
numerical output and rejection of altered bindings or scope.

The completed canonical reports for rows **1–6 and 8** are accepted through
that receipt. The actual interrupted row-7 report is rejected for its missing
completion flags, and the audited eight-row summary is rejected as a canonical
forecast input. Row 7 remains a reported manual estimate with the provenance
limit above; automated forecasting requires a completed canonical calibration.
This implements reuse of qualified CPU costs, not a newly qualified GPU forecast
or an automatic multiowner capacity estimate. No DSP timings were repeated.
[Reviewed applicability](../local-evidence/parallel-routes/shared-input-cpu-applicability.json) ·
[Source/initialization proof](../local-evidence/parallel-routes/shared-input-applicability-proof.json) ·
[Actual report-consumer checks](../local-evidence/parallel-routes/shared-input-applicability-consumer-check.json) ·
[Forecast test log](../local-evidence/parallel-routes/shared-input-forecast-tests.log)

Nominal work/model/interval gates pass on rows **2–8**. Conservative gates pass
on rows **3 and 5**. Row 4's conservative point is **4.111 s/s**, a numerical
real-time failure even though uncertainty prevents a stronger capacity decision.
Other unsupported models remain unresolved despite favorable numerical points.
Intervals are conditional extrapolation intervals, not communication-success
probabilities. Values below 1 s/s indicate less processing time than input time.

| Manual row | Nominal (s/s) | Nominal interval | Conservative (s/s) | Conservative interval | Conservative decision | Continued fine busy cores |
| --- | ---: | --- | ---: | --- | --- | ---: |
| 1 Sub-9kHz | 0.095588 | 0.00217337–221796 | 0.344345 | 0.288156–221796 | Unresolved | 2.263 |
| 2 Groundwave GPSDO | 0.082295 | 0.0660219–0.103034 | 0.328242 | 0.260634–0.410538 | Unresolved | 2.451 |
| 3 Groundwave Helper | 0.110476 | 0.0841488–0.169263 | 0.298940 | 0.221439–0.423049 | Conditional pass | 3.262 |
| 4 HF Helper | 0.170988 | 0.10633–0.261123 | 4.110555 | 0.697945–84.0643 | Unresolved; point fails | 3.335 |
| 5 HF encrypted GPSDO | 0.074096 | 0.0392398–0.123398 | 0.274246 | 0.222896–0.33994 | Conditional pass | 2.653 |
| 6 FHSS | 0.179261 | 0.116871–0.265807 | 0.506988 | 0.399053–0.65286 | Unresolved | 2.311 |
| 7 HF unencrypted GPSDO | 0.064076 | 0.0525413–0.080367 | 0.262868 | 0.196675–0.352477 | Unresolved | 2.842 |
| 8 EME | 0.168811 | 0.0738428–0.353889 | 0.583261 | 0.376338–1.19589 | Unresolved | 2.387 |

Row 1 stopped at the first material four-second continuation holdout drift above
30%; its enormous propagated upper endpoint is an uncertainty warning, not a
credible precise maximum. Rows 2 and 6 exhausted the bounded terminal-fine
model check; row 7 retained unsupported noise-only fine extrapolation, and row 8
retained unsupported terminal/state-ceiling models with a conservative upper
endpoint above real time. Row 4's broad acquisition remains unsupported. These
outcomes must not trigger repeated measurements to obtain favorable decisions.

Continued fine processing used **2.26–3.33 busy hardware-thread equivalents**
within the single pool; FHSS acquisition reached about 9.0–9.1. The separate
49-window measurement above establishes higher occupancy from independent
controllers, not a reduction in these per-case costs or all-row pipeline
capacity. The software-worker occupancy denominator is 24; hardware utilization
uses 12. Neither this laptop's power limits nor these single-pool observations
authorize reducing the automatic `2H` budget.

The first run completed in 813.032 s but its late per-row admission guard
rejected rows 7 and 8 because the frozen cap exceeded then-current free RAM.
Observed earlier workspace remained below the cap; allocator retention versus
external memory pressure was not separately attributed. A fresh-process
continuation preserved the same cap and completed row 7, then stopped at the
original 1,200-second deadline after 64.882 s. A separately predeclared row-8-only
run completed in 105.410 s within its 240-second bound. Total measured process
time was 983.324 s; the original deadline failure remains visible. All owned
processes were reaped. This is an audited union, not a fabricated successful
single canonical run. Preserve explicit-cap admission, unchanged sampling and
failed receipts when using this residual-process workflow.
[Original report](../local-evidence/parallel-routes/all8-cpu-capacity.json) ·
[Deadline-limited continuation](../local-evidence/parallel-routes/all8-cpu-missing-7-8.json) ·
[Residual row 8](../local-evidence/parallel-routes/all8-cpu-residual-8.json)

The final named `./build.sh test triad` passed **22/22 in 30.59 seconds**.
The canonical functional path still uses two scored quiet slots, preserves the
global public ordinal and continues receiving after message completion; EOF is
not successful physical completion. These checks do not establish blind
transmitter discovery or an all-case communication-success bound.

The preceding 12-worker/3840 MiB calibration and its conditional wrapper refresh
are historical. They remain only where they support the report's explicitly
labeled baseline and its source-applicability findings; they are not current
CPU timings. The refresh's exporter-path and H0 fixture failures remain distinct
from its accepted check. Private proof drivers, copied sources and redundant
per-row outputs were removed. Keep current raw reports and failures while they
support unresolved bounds; retire them when an equally scoped qualification
supersedes their claims.
[Historical sequential baseline](../local-evidence/continuous-receiver/calibration-all8.json) ·
[Historical refresh audit](../local-evidence/continuous-receiver/serial12-refresh-audit.json)

Development checks exposed and corrected three reception defects before the
current measurement: FP32 innovation arithmetic failed at very high SNR,
legacy 0.01 Hz final frequency snapping discarded long-duration coarse
resolution, and fixed-affine residual clock hypotheses introduced an unintended
IF-dependent frequency shift. Fine processing now declares FP64, uses the
public duration-dependent frequency grid with its full prior penalty, and
preserves acquired total CFO in the fixed-affine basis. The adaptive model
remains diagnostic and its full work is measured. Focused numerical, wide-time
phase, prior-decomposition and lifetime tests passed. Memory reviews also removed
retained session/cache cycles and included simultaneous profile-validation banks
and transient projection/control buffers.

The frozen-source native message gate passed: DATA 0, DATA 0, DATA 1 decoded
canonical `e`, followed by two fully scored quiet slots and physical completion
before transport EOF. Its captured-prefix EOF negative control stayed
incomplete. This one high-SNR case establishes a functional path, not all-case
sensitivity. The default invocation repeats this gate on current sources. The
earlier source-changing functional exercise is superseded by this frozen result.

The earlier receiver's
[sensitivity pilot](../local-evidence/robust-manual-sensitivity-current.json)
received both independent bits correctly at 8.113563 dB-Hz in case 7,
each followed by two quiet slots and a complete work receipt. A fresh,
predeclared [confirmation](../local-evidence/robust-manual-confirmation-current.json)
at 11.113563 dB-Hz received its first **5/29 pairs** correctly, then stopped at
its memory-accounting gate. These were ten successful complete single-bit
endpoints, not five full `fox` messages. The incomplete confirmation establishes
no >90% probability bound. Do not pool different SNR points or count timing
repeats as additional successes.

That confirmation's resource monitor incorrectly treated the largest reaped
child's lifetime RSS as native post-exec residency. Its five child counters
exactly matched the Python workers' peaks. A separate zero-sample discriminator
reproduced this: a 125.7 MB launcher/child high-water counter accompanied only
20.5 MB combined sampled RSS in two verified native executables after exec.
The future confirmation helper now samples owned post-exec native processes,
retains conservative workspace/runtime/decoder allowances and counts persistent
worker processes across waves. Polling does not certify every transient peak.
Four focused regression fixtures pass. The original resource stop and all five
successful pairs remain unchanged. The original cost allowance did not admit
completion after diagnosis; the experiment was not restarted or extended.
After registering those fixtures, the affected `triad_cases` check passed again.

The v3 default manual invocation used the continued-session measurement. The
promotion evidence below records that earlier phase and is historical; the full
v3 capacity tables were removed because current-source results supersede them.
The [promotion receipt](../local-evidence/robust-manual-promotion-current.json)
verifies that the promoted probe is identical and that DSP sources and the
native binary did not change. It preserves the original measurement phases:
the separate continuation phase used family size 24; the v3 default uses 64
for its then-current list. The v3 wiring and accounting were tested separately,
including acquisition gating, missing-stage subtotals, slot normalization,
workspace/RSS/utilization and propagation of supported nominal failures. The
old observations are not relabeled as a full run of v3. Its full run was
source-bound but is no longer the maintained CPU estimate.

Keep prior base, continuation, allocation, pilot, incomplete-confirmation and
promotion reports only for the stated unresolved sensitivity/resource
comparisons; retire each when equivalent current evidence supersedes its
purpose. The duplicate temporary continuation/allocation runners and earlier
exploratory message/allocation outputs have been removed.

### Retained historical coarse evidence

Earlier tests used separate coarse and abbreviated tracking stages. They do
not measure complete receiver work or qualify the current endpoint. The old
nine-entry baseline is retained in [its JSON](../local-evidence/robust-cases-current.json)
only to preserve then-current configuration/provenance and unresolved sensitivity
outcomes: nominal Sub9 had insufficient successes, and the HF/FHSS fixed-point
paired check failed on pair six. Those observations must not be relabeled as
current eight-entry coverage or complete-message outcomes.

The earlier [Groundwave JSON](../local-evidence/robust-groundwave-current.json)
records resolution of the Helper handoff/search failure. A five-point bookkeeping
bank had rejected a useful continuous interval through neighbor padding; the
corrected caller uses continuous union width, preserving weak/broad-pilot
fallbacks. Exact lag-domain ZoomFFT completed the public CFO bank. At nominal
20.0206 dB-Hz DATA / 30.0206 dB-Hz Helper, both bits produced correct coarse
proposals. Its coarse-only service was 0.006148 s/s on this Ryzen 5 PRO 5650U;
that value excludes complete fine tracking and message work.

At -8.72375 dB-Hz, all 22 independent noise pairs recovered both coarse bits
using the complete 3,619-point prior after Helper fallback. The 90% one-sided
paired-event lower bound was 0.900628, conditional on one sampled channel and
the old coarse endpoint. This is neither a 95% bound nor literal-`fox` success.
The old `--paired-followup` campaign entry was removed with the superseded staged
suite. Keep these compact historical JSON records while those sensitivity
questions remain unresolved; retire them when current complete-receiver evidence
replaces the relevant claims. Their obsolete implementations are not retained.

## GPU-assisted receiver measurement

The completed campaign below **predates the receiver restructuring**. It
demonstrates the forecasting procedure, not current-source GPU qualification.
The new local CPU calibration must not be combined with these old GPU traces.
During the restructuring, a disposable RTX PRO 6000 host never reached SSH
readiness and was stopped and deleted. Replacement RTX PRO 6000 WK, RTX 5090
and B200 deployments were refused for unavailable capacity. No new full GPU
trace or GPU-derived CPU forecast is claimed from those attempts.

The current GPU filter uploads at most 2^20 input samples per staging block,
keeps each host block alive through stream synchronization, and drains an
exceptional submission before releasing its buffers. It preserves CUDA
polyphase filtering and explicitly budgets device input, full filter output,
cropping and standalone caller-owned host input. The revised row-1 filter
estimate is about 1.020 GB with its bank; this is an allocation model, not an
observed CUDA peak. The named GPU diagnostics passed **40/40** tests,
including fake-device copy/lifetime/interruption and exact-budget boundary
checks. Actual new-source CUDA execution remains unqualified until a usable
pod is available. CPU calibration does not include this GPU-only leaf.
[GPU regression log](../local-evidence/current-restructure/gpu-tests.log)

The earlier [eight-row RTX PRO 6000 trace](../local-evidence/current-parallel/gpu-current-full.json)
took 426.221 seconds internally. Allowlisted source and matching native
artifacts were only a few megabytes and transferred over authenticated SSH/SCP;
the source archive was 1.58 MB. The three paired reference rows (1, 2 and 6)
passed their precision, decision and work gates. Tight-clock
rows 1–2 now evaluate the whole coarse grid in FP64 before pruning and use
precise fine rendering; other rows retain bulk FP32. Continued fine work was
1.76–2.44× faster than the pod's matched 12-worker CPU. Native waveform
simulation and bank preparation still consumed about 80% of end-to-end time.
Do not attribute those CPU stages to GPU acceleration or use one global
GPU-to-CPU multiplier.

The earlier [full-list forecast](../local-evidence/current-parallel/forecast-current-all8.json)
covers all eight rows with source-matched local CPU costs and
[independent CPU traces](../local-evidence/current-parallel/holdout-current-all8.json).
The eight isolated holdouts took 539.759 seconds inside the drivers. Row 5 was
once refused admission before any receiver observation; its original log is
retained, and a fresh invocation admitted the same 4608 MiB cap and 12 workers.
No memory guard or timing threshold changed.

In that earlier campaign, **all eight nominal and conservative timing checks
passed**. Its forecast reports
`complete_point_estimates_with_unresolved_support` after two bounded local
phase refreshes described below.
All nominal guarded intervals
are below 1 s/s. Conservative points for rows 4, 6 and 8 fail real time, and
conservative capacity support remains unresolved for every row after the
forecast's engineering allowance. This is complete work accounting with explicit
uncertainty, not a saturated CPU ceiling or
all-case communication qualification.

Maximum accepted no-cancellation errors are 14.57% of nominal service and
17.02% of conservative service. The
[initial forecast](../local-evidence/current-parallel/forecast-initial-all8.json)
retains the original row-2 nominal and row-7 conservative timing failures.
One [canonical row-2 nominal refresh](../local-evidence/current-parallel/forecast-refresh-row-2-nominal.json)
took 24.770 seconds, with family size 72 and at most nine looks. It sealed its
new point and interval before a fresh receiver observation; nominal error was
−2.29%. Row 7's initial maximum-state cost contributed 15.253% error against
the 15% stage limit, so it required a separate cost-class refresh. Its
[accepted maximum-state refresh](../local-evidence/current-parallel/forecast-refresh-row-7-cap-repaired.json)
took 33.057 seconds and nine canonical looks. It rechecked the retained erasure
observation against the new bound and sealed the prediction before a new capped
receiver observation. Conservative error was +4.98%. The two accepted refreshes
took 57.827 seconds in summed measured time, excluding the failed setup attempt
below. The final report recomputes the complete list with only those two fits
and observations replaced. Other component checks remain explicitly mixed-time
verification. No measured source or precision/work/timing threshold changed.

The first row-7 maximum-state attempt used the canonical sampler, but its
temporary wrapper rejected live tuple coordinates against archived JSON lists
before sealing or obtaining an independent observation. That transient fit
had not been persisted and supplies no replacement numerical result. The
[failed-attempt disposition](../local-evidence/current-parallel/forecast-refresh-row-7-cap-disposition.json)
records that limitation and the original log. An
[offline diagnosis](../local-evidence/current-parallel/cap-refresh-format-diagnosis.json)
reproduced the representation rejection on archived data and restored the exact
original forecast through ordinary JSON normalization. One explicit format-only
harness repair then normalized and persisted the fit before verification; that
run produced the accepted receipt above. This repaired an entry-format failure
before any independent accuracy test. It did not retry a failed timing result
or relax a near-threshold limit. Both temporary wrappers were removed.

The disposable RTX PRO 6000 pod was stopped and terminated after results were
verified locally. No persistent volume was created. Temporary tensor sources,
CPU experiment drivers and the upload archive were removed. Retain that
campaign’s calibration, GPU references, holdouts, original failures and refresh receipts
while they support these claims and unresolved comparisons; retire them when
matched replacements supersede their purpose.

Earlier source-bound workflow results remain useful procedural evidence, not
current CPU calibration. A [cached same-case row-7 CLI forecast](../local-evidence/robust-v2/forecast-target-7-same-row.json)
passed both timing gates in about 47.82 seconds including target GPU work,
local verification and estimator startup, after applicable calibration and
paired references already existed. A [two-job cloud comparison](../local-evidence/robust-v2/gpu-dual-5-7.json)
ran rows 5/7 in 31.01 seconds over SSH with admitted aggregate CPU and memory
budgets and identical useful work. Independent simulations accelerate cloud
preparation; they do not demonstrate single-stream CPU capacity.

The [prior source's original forecast](../local-evidence/robust-v2/forecast-all8.json)
failed six nominal timing checks and one conservative check. Separate bounded
phase refreshes then passed its full list, but that result does not apply to
the changed CPU implementation. A [matched driver crossover](../local-evidence/robust-v2/driver-crossover.json)
found only 1.58% difference between calibration and holdout drivers and did not
reproduce the historical drift or a material garbage-collection cost. No unique
cause or retrospective clock correction is justified. The earlier tight-clock
FP32 trace rejected rows 1–2 on candidate ordering, and encrypted-5→clear-7 cost
transfer remained rejected despite supplying its missing H0 class. Keep those
failures source-bound; they do not prohibit qualified lower precision elsewhere.
See the [preferred workflow](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
for commands and applicability gates.

### Tensor-core lag-search conclusion

Two direct lag-domain Fourier transforms were implemented and tested on the
RTX PRO 6000 Blackwell using Triton 3.4: TF32x3 and explicitly rounded TF32,
both with FP32 accumulation and unchanged FP64 phase setup, Gram/rank and
quadratic scoring. Each executed 135 tensor launches across the actual row-3
DATA, noise and recurring search workload. Executed-kernel PTX and disassembled
SASS included `mma.sync.aligned.m16n8k8.row.col.f32.tf32.tf32.f32` and
`HMMA.1688.F32.TF32`. The [Triton arithmetic contract](https://github.com/triton-lang/triton/blob/v3.4.0/python/triton/language/core.py#L1858-L1890)
and [NVIDIA instruction reference](https://docs.nvidia.com/cuda/parallel-thread-execution/#warp-level-matrix-instructions-mma)
describe those operations.

Both variants passed the existing paired CPU receiver gates without relaxing
thresholds: public cells, candidate order, decisions, all five fine-work strata
and their scores matched. The worst coarse-score difference used 0.40% of the
allowed tolerance for TF32x3 and 56.76% for rounded TF32. This establishes
bounded computational agreement, not independent communication sensitivity.

| Variant | Isolated transform including coefficients, relative to ZoomFFT | Whole-work finding |
| --- | ---: | --- |
| TF32x3 | 0.78× | Already slower at the isolated transform; actual H0 searches were also slower. |
| Rounded TF32 | 1.08× | Warm ABBA process medians were 32.580 s versus 31.975 s for ZoomFFT: 1.89% slower, with both paired comparisons slower. DATA acquisition also increased from 0.935–0.939 s to 1.23–1.26 s. |

The TF32 experiment met its numerical gates but failed its predeclared useful
whole-trace improvement criterion. A separate power-of-two-scaled FP16 variant
then tested the more efficient FP16 tensor instructions, with FP32 accumulation,
explicit zero/nonfinite/range guards and scaling, packing, coefficient setup
and rescaling included in its preflight timings. At the two public row-3 matrix
geometries, the isolated transforms ran 1.88× and 1.33× faster than ZoomFFT.
Preflight PTX and SASS directly showed
`mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32` and `HMMA.16816.F32`.
Relative L2 errors were at most 2.67e-4; the tested nonzero inputs did not
underflow during packing.

The paired row-3 FP16 receiver passed the unchanged CPU precision, decision
and work gates, with exact candidate coordinates/order and all five fine-work
records. Coarse-score error consumed at most 58.39% of the existing tolerance.
However, its measured acquisition stages took 4.103 s versus 3.543 s for
ZoomFFT; the sum of common measured stages was effectively unchanged
(28.248 s versus 28.314 s, −0.24%). These sums exclude CPU-reference validation
overhead and unassigned process overhead. One bounded pair establishes no
material gain here, not a stable slowdown across workloads. The receiver
wrapper's counter was attached to temporary screen copies and did not reach
the original-screen receipt: changed scores and eligible geometry corroborate
the alternate arithmetic, but direct instruction proof is confined to the
preflight. No per-receiver FP16 launch count is claimed.

All three implementations and their switches were removed; the canonical GPU
FFT path remains. Lower precision was adequate for this tested coarse workload,
but the higher arithmetic throughput did not make this algorithm replacement a
faster forecast. This concludes the tested variants; it is not a prohibition on
different tensor algorithms or a reason to force all bulk work into FP64.

The [TF32 disposition receipt](../local-evidence/current-tensor/disposition.json)
binds actual source/native hashes, runtime, unchanged inputs, paired checks,
warm timing order and instruction evidence. Its roughly 2.3 MB of constituent
receipts and the [FP16 disposition](../local-evidence/current-tensor/fp16/disposition.json)
with its three constituent receipts support these specific numerical and
performance conclusions; remove them when the conclusions are superseded or
the summary alone suffices. No experimental driver or copied source tree is
retained.

## Test timing investigation

On 8 November 2026, the dirty working tree based on `64f7feb6` was timed with
GCC 14.2, the native Release FLTK configuration and two test jobs. `test all`
stopped before testing: `test_terminal_ui` and `test_framebuffer` failed to link
native audio symbols. Separate web and build-tool runs also had preview socket
permission failures and temporary GPG-key setup failures. Retain the three logs
in `local-evidence/test-timing-2026-11-08/` to diagnose these failed checks;
remove them when the link and environment failures are resolved or superseded
by equivalent current evidence.

## Transmit key policy checks

This is the historical 7 November checkpoint; the native GUI migration is
recorded separately above.

The uncommitted policy change based on `64f7feb6f298ba844f45ecbf1c57f02219158d16`
passed `./build.sh test contract` (26/26), `./build.sh test triad` (19/19), and
`./build.sh test gui` (27/27) on 7 November 2026. These runs used the existing
Release `build/dev` profile, two test jobs and four build jobs, with
`CCACHE_DISABLE=1` because the sandbox could not write the compiler cache.
The initial compile also exposed a missing test include, corrected before
these passing runs. Documentation link checks and `git diff --check` passed.

Coverage includes ordinary private/Amateur key parity, temporary noise DATA
keys and forced encryption, public-Amateur Helper decoding, changing callsign
ciphertext across absolute dwells, unchanged Helper vectors, independent
background noise, and the unavailable Robust boundary. Noise-mode coverage is
at the native policy/generator API; no GUI reconnection, operational noise
endpoint, physical transmission, or release certification is claimed.

## Cleanup validation

This is the historical cleanup checkpoint before native GUI reconnection.

The CLI, FLTK GUI and pipe-worker applications rebuilt successfully. The FLTK
adapter and document test executables also compile and link; their native-window
runtime tests were not rerun. GUI controls remain, with Robust operation
explicitly unavailable pending connection to the canonical triad backend.

| Final affected check | Result |
| --- | --- |
| `./build.sh test contract` | 25/25 passed, 25.13 seconds |
| `./build.sh test triad` | 18/18 passed, 13.09 seconds |
| `./build.sh sanitize contract` | 25/25 passed, 37.49 seconds; ASan, UBSan and leak detection enabled |
| Final crypto/keyring fixture refresh under sanitizers | 2/2 passed, 8.69 seconds |
| Final focused crypto, keyring, Fast codec/CLI, GUI Fast, web worker/bridge | 7/7 passed, 40.40 seconds |

The triad Python aggregate ran 201 checks, with one native-dependent case
skipped there and covered by its separately registered native test. These are
small correctness checks, not full-observation or performance campaigns.
Independent source comparisons found the retained GF(256)/Reed–Solomon bodies,
Payload keystream path and prepared CBC implementation unchanged by the final
removal of unused recovery, framing and cryptographic APIs. Independent frozen
crypto vectors now exercise the retained prepared CBC API.

Earlier in the cleanup, the shared `regular` group passed 29/29 in 44.08 seconds
after removal of the two obsolete recovery/framing suites. The later crypto
change received the final affected checks above. Legacy passed 8/8 in 1.79
seconds. Web passed 13/13 in 146.47 seconds, including the retained Fast audio
case; the worker and bridge were rechecked after the final crypto change.
The initial Fast and GUI runs passed 35/37 and 26/27 respectively. Corrected
focused reruns completed every previously failing case; all 37 Fast and 27 GUI
cases have passed across those runs, but neither full group was rerun after
all cleanup changes.

The build-tool suite passed 27/32 in the sandbox; the five local socket/GPG
fixtures then passed outside it. Workflow tests passed 3/3, abbreviated-inference
tests 8/8, and certification-tool unit tests 86/86. The maintained-document
path/anchor audit and `git diff --check` pass.

Initial failures remain failed invocations: an invalid tiny channel fixture,
a CLI error exit code, obsolete GUI simulation/readiness expectations, and
missing native audio linkage for adapter test targets were corrected. The
initial sanitizer invocation could not run LeakSanitizer under the sandbox's
ptrace restrictions; the passing reruns retained leak detection outside that
restriction. No omitted check is counted as passed.

## Limits

RunPod instances supported the replay transport and the separately recorded
GPU parity investigation. The current manual capacity checks used simulated
baseband and local headless receiver work. Those measurements do not qualify the new native GUI runtime. Physical-device
and release qualification remain separate. The
[abbreviated regression harness](triad-performance.md) measures relative changes
in declared work and cannot establish absolute receiver capacity. Its historical
smoke measurement withheld an unresolved Gaussian projection and made no
comparative performance-pass claim.

Release assurance follows [the release procedure](releases.md) and binds to
exact published source and binaries. Earlier checks are not automatically
qualification for changed code, configurations or physical devices.

## Historical provenance

A local unreleased GUI review compared `52ace0a` with
`2a0f3dd142c97909f206145cd4f4eecb1cc72dcf`. Both failed native text replay
with `receiver search limited | Simulation replay 0%`: an existing 16-entry
presentation limit discarded newly accepted output, while decoder arbitration
could retire a pending identity without a separate presentation notification.
The old receiver is retired; this finding does not establish the behavior of the
current receiver. Age-based eviction and treating retirement as physical
completion would violate the historical pending/physical-completion contract.
The same review reported an independently inherited ALSA clipping failure.
Its other host and SDK checks and exact historical scope are in Git history;
they do not qualify current sources or releases.

Useful [pattern](validation-data/triad-v3/2026-11-06-independent-patterns/README.md),
[spacing](validation-data/triad-v3/2026-11-06-spacing/README.md), and
[stochastic-channel](validation-data/triad-v3/2026-11-05-stochastic/README.md)
measurements remain curated, source-bound research because their conclusions
inform the current triad. Preserve concise results, assumptions, limitations and
needed provenance; do not accumulate duplicate raw captures or parallel
implementations. The
[archive-retirement manifest](validation-data/triad-v3/archive-content-retirement.json)
records removed implementation copies and changed archive hashes; retained
triad data leaves were preserved byte-for-byte. Original source/internal hashes
remain historical provenance, not qualification of repacked archives or current
code. Consult the [evidence index](indexes/evidence.md) and Git history for
superseded results and their limits.

## Dedicated repeated-bit feature retirement — 17 November 2026

The dedicated repeated-bit mode was removed from the current GUI and native
receiver after the user preferred ordinary raw-bit transmission, shared Target
SNR settings, or external scripting. The removed path had its own opt-in,
scheduled five-attempt TX group, distinct logical event, receive schedule and
assumptions about independently distributed noise. It did not combine arbitrary
ordinary messages. Current code has no dedicated repeated-bit control or group
event; an ordinary `0000` draft comprises four separate slot decisions and does
not soft-combine their evidence. Lowering Target SNR requires matching settings
at both peers and changes the waveform duration, aperture and channel evolution.

Keep the earlier executed grouped replay as a **historical result of the
retired feature**: the same −3 dB-Hz two-hop bit-one capture that had five
ordinary attempts missed could produce the correct dedicated group event on
attempt four, while all ordinary per-slot scores and quiet decisions stayed
unchanged. That demonstrates recoverable information in that one modeled
capture under that feature's noise, calendar and key contract. It is not
evidence that the current ordinary receiver accumulates repeated bits, nor a
qualification of broad weak-channel or natural outage availability. The
[original replay](#production-repeat-receiver-on-the-frozen-weak-two-hop-bit-one-history),
[live PCM test](#calibrated-live-pcm-repeated-bit-check), and earlier
[calculation](#native-soft-evidence-and-coherent-line-probe-for-configured-repetition)
retain their original experimental scope. Timing and likelihood improvements
that apply to ordinary slot scoring remain part of the production receiver;
only the dedicated control, schedule, and event mechanism were retired.

The removal itself changes code after those historical triad/GUI/appraisal
results. They must not be cited as tests of the post-retirement source. The
current-source correctness checks for the removal are recorded with this task's
validation run when available; no new ordinary repeated-bit sensitivity or
capacity result is implied here.
