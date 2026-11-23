# Shared GUI contract

The FLTK and Rev adapters consume the same toolkit-neutral application state,
field declarations, actions, document records, and presentation data. Application
code owns modem workflows and declares each control's value, options, and
availability; adapters own native widgets, drawing, file choosers, and platform
services. Keep user-visible values and actions consistent across adapters.
See [GUI architecture](gui-architecture.md) for shared-code structure.

## Modems and current availability

The Modem dropdown retains **Fast Modem**, **Robust Modem**, and **Legacy Modem**;
ordinary launches default to Fast. Each modem retains its own controller and
settings. Robust connects the shared controls to the native C++ session linked
into the application. Starting Fast does not start a Robust receiver or acquire
its audio device. Mode changes and pending key loads preserve active intent
without starting a session with a different key.

Robust live operation requires both peers to configure the same public calendar
anchor, initial waveform ordinal, DATA cadence, key and waveform/FEC settings.
**Robust setup** exposes the calendar, DATA cadence, minimum DATA carrier
spacing, and a declared local live input-noise density in dBFS/Hz.
An unconfigured calendar or noise model keeps the affected live action unavailable
with an explanation. Simulation uses its own agreed local calendar. These
settings do not implement unknown-start acquisition or measure the audio device.
See [current architecture](robust-current.md) and
[current validation](validation.md#native-gui-integration).

Fast Modem's **Simulation** page offers **Live audio** (the default),
**Simulation**, and **Live loopback test**. Ordinary Live uses the selected audio
device and clean application PCM. The compact Console row declares Baseband and
Shift oscillator bounds, Doppler/delay spread, selective AM, receiver search
margin, and RF Shift coordinate. These declarations always bound the receiver
and inform deterministic Auto profile selection, regardless of mode. They are
not physical measurements or radio controls. The Simulation page configures
assumed transmit power (W), path loss (dB), and receiver noise density (dBm/Hz
or Off); only software Simulation injects those link-budget effects. In
Simulation, the locally emitted Fast waveform passes through the Fast native
sampled channel and Fast receiver. The six Console declarations are injected
as waveform effects only for Simulation or an explicitly started loopback test;
ordinary Live and idle loopback monitoring remain clean.
The Shift frequency is an RF coordinate for the oscillator model; it leaves the
Fast audio carrier and passband fixed. Simulation results are labeled separately
from live audio results. A simulation begins from a local transmission and
retains the channel's delayed tail and scored quiet completion without opening
hardware audio. Mode changes alter effect injection and audio ownership, not the
transmitted format; peers must match the resolved local waveform settings.
Power, path loss and noise define the modeled signal-to-noise ratio in normalized
receiver-input units: link power and path loss set the equivalent received level
relative to independent receiver noise. Noise is added at the simulated receiver
boundary after propagation. These values do not attenuate audio-DAC playback or
change a physical receiver's gain.
Profile and waveform changes revalidate the retained receiver declarations.
Invalid receiver declarations keep their fields editable but block new listening,
transmission, Simulation, and loopback start in every mode. Separately, invalid
synthetic channel or link-budget settings disable Simulation and explicit
loopback only. Audio-band diagnostics state the required frequency range,
available sampled band, and oscillator/channel allowances.
An invalid declaration or waveform draft preserves the current monitor and
any already-admitted restart using its last validated settings. Its PCM and
plots continue while the draft error remains visible. Valid edits update that
snapshot through the normal serial handoff; device, key, mode and cancellation
changes retain their explicit audio-ownership behavior.

**Run loopback test** sends the selected nonempty Console text or file with the
selected profile, key/encryption, device, and output routing. It copies the
declared oscillator, spread and selective-AM effects into the test waveform;
the copied search margin is fixed at one because margin is receiver-only.
It prepares the complete source and checks encoded airtime before opening audio;
an oversized source/profile combination is rejected without truncating or
replacing the source. It acquires the shared audio service, starts capture before
playback, and sends the selected waveform impairments through the physical
loopback path into the native Fast receiver. Transmit power, path loss and noise
apply only to software Simulation; they are disabled and retained in Live
loopback mode. Oscillator, spread and selective-AM effects remain available as
synthetic waveform stress before playback. Playback uses the selected TX volume,
with no mixer change or automatic level adjustment. The test retains its
75-second audio and 110-second overall elapsed maxima, including source
preparation. Capture must begin within 5 seconds. After the complete source and
canonical quiet tail are produced, up to 2 seconds of captured audio are allowed
for receiver completion; this grace can declare failure only and cannot
manufacture successful completion. Silence continues while the receiver
finishes to avoid an audio-server drain wait. Progress distinguishes audio
samples queued for playback from receiver samples captured. The queued count
includes silence and does not confirm speaker output. Shared audio handoff waits
at most 2 seconds and remains cancellable. Draft text/file selections remain
intact after sending.
A pass requires exact selected text bytes or file bytes and filename, integrity
and scored physical absence; cancellation, limits and audio discontinuities
remain incomplete. A verified result is added once to the ordinary Signals or
Files list with **LOOPBACK TEST** provenance. It uses the shared restricted text
view and copy/paste behavior; completed attachments remain available only by an
explicit Save action. Public mode checks integrity but does not authenticate a
sender. Encrypted mode requires successful authentication. Failed, cancelled or
mismatched tests do not publish a completed result.

When Fast is selected, **Live loopback test** also runs the normal receive-only
session while idle. The bounded duplex test takes over only after that session
closes, then receive-only monitoring resumes when the test completes or is
cancelled. Only one Fast session owns audio at a time. Status retains the last
test outcome alongside current monitor state. Tab changes do not stop capture;
hiding Fast for another modem stops idle monitoring for the shared audio
handoff, while an active bounded test can finish. Both native frontends use this
same in-process workflow.
See [Fast mode](fast-mode.md#fast-simulation) for operation and qualification limits.

Fast Live and loopback waveform/waterfall snapshots follow captured PCM on a
bounded display path independent of receiver acquisition and decoding. Loopback
never substitutes generated playback samples for received plots. Input I/Q and
equalized constellation points come from receiver progress and may lag capture;
old constellation points are identified by their receiver-sample age. During or
after an eligible loopback test, **Last loopback signal SNR** compares a payload
signal estimate near decoded receiver progress with microphone noise captured during the 250 ms
before playback. The baseline uses detrended 2,048-sample Hann windows and is
reported with its reference bandwidth and observation age. A bounded history
matches completed captured-audio windows against native receiver payload sample
ticks. The estimate is omitted when the baseline is unstable, clipped, too quiet, lacks
frequency resolution or has no comparable payload observation. It measures
signal-to-noise power, not EVM, and is never measured during physical absence.
Ordinary Live and software Simulation do not show a calibrated physical SNR;
Live input level and plots remain available. Expected SNR is a fixed local
preset assumption that selects waveform settings, including the fade-capped
acoustic symbol rate and coding rate; both peers must use matching updated
geometry. It is not a measured value or automatic transmit-feedback control.
Display timing never controls receiver decisions or completion.

| Shared control | Native meaning |
| --- | --- |
| Encryption | Off preserves unencrypted DATA; keyfile and AMATEUR select the effective key. AMATEUR is public key material, not confidentiality. |
| Helper power, ratio limit, callsign | Requested Helper watts, native power allocation/ratio clamp and Helper metadata. Zero power disables Helper; these settings do not control radio hardware. |
| Transmit power, path loss | DATA power and simulated link attenuation, separate from playback volume. |
| Clock sync, region, offset, audio error | Public arrival bounds and sampled timing model. Audio error starts at **Default** and saved settings preserve that choice. Live capture also needs actual provider timestamps and uncertainty. |
| Base/Shift oscillator, Margin | Always-active declared oscillator bounds and public receiver search multiplier; synthetic effects are injected only in Simulation and explicit loopback. |
| Doppler/delay spread, selective amplitude | Declared receiver assumptions in every mode; synthetic effects are injected only in Simulation and explicit loopback. Off remains explicit. |
| Rate, Carrier, Shift | Rate is the native inner rate. Carrier accepts a custom absolute frequency or a computed preset. Bare numbers are Hz. The baseband frequency is Carrier minus Shift. Configuration edits preserve a valid baseband frequency, including custom values, and translate it back to an absolute Carrier. Runtime FHSS hops keep the same baseband plan. |
| Additional Symbols, Spreading | Off uses a factor of 1. Interleave uses the selected `N×` factor. The nominal bandwidth of each DATA carrier is `1.25 × Rate × N / 6`. |
| Minimum carrier spacing | Robust setup offers 0, 3.5, 125, and 265 Hz, with 265 Hz as the startup default. Adjacent centers use the greater of the selected minimum and the nominal single-carrier bandwidth. The triplet spans twice the center spacing plus one carrier bandwidth before shaping allowances. Select 0 Hz to use the original rate-derived spacing. Both peers must use matching spacing. The launch option accepts `--minimum-carrier-spacing 3.5` and the other listed values. |
| DATA cadence | Scheduled + short transitions is selected at startup and stays selected with Helper or FHSS. It fills the DATA gaps at the DATA gain and follows the original FHSS envelope. Scheduled + gap fill protects eligible same-band fill with a guard and requires Helper and FHSS Off. Both filled cadences keep the temporary key at the transmitter. Continuous DATA places successive gates on adjacent samples and requires Helper, FHSS and both Symbol Burst waits Off. Match cadence and settings at both endpoints. Explicit saved or launch choices are preserved. `--data-cadence` accepts `scheduled`, `scheduled-fill`, `scheduled-bridge`, or `continuous`. Older commands using `default` load `scheduled-bridge`. [Burst scheduling](symbol-burst.md) |
| Symbol burst, Random extra | Shared whole-bit calendar waits; see [burst scheduling](symbol-burst.md). |
| Wrong Key Admission | Reversible Robust history/browser filter. Unfiltered shows the baseline; stronger choices hide completed records when fewer than half their admitted DATA bits pass. Pattern evidence marks the selected filter's pass/reject results for rated slots. The original likelihoods, decisions and decoded content are retained. The dB labels are approximate HF reference visibility losses. Strong wanted signals can also be hidden. |
| FHSS | Canonical envelope and illustrative frequency schedule. Short transitions applies the original FHSS envelope to filler and preserves the guards and ramps. The illustration leaves external radio tuning fixed. |
| Target SNR dB/1Hz | One shared C/N0 target, default 32 dB-Hz, for provisional duration planning and live reference DATA amplitude relative to declared input noise. The adjacent Budget target is a starting value from the nominal DATA link budget, rounded down to a whole dB-Hz within the supported range before fades. It leaves the selected target in place. |
| Live input noise dBFS/Hz | Local receiver noise-model reference, separate from link-budget noise in dBm/Hz. At fixed Target SNR, changing it scales receiver variance and reference DATA amplitude together; correlated or impulsive noise is not qualified by mean power alone. See the [calibration scope](robust-current.md#configured-calendar-reception). |
| Error correction | Configured Off, RS20 or RS60 native short/long text profiles. Raw bits bypass text coding and FEC exactly. |
| Search arithmetic | Default (fastest) selects the fastest supported complete receiver route for the hardware and workload. FP32 minimum selects FP32 products/FFT with higher-precision guards. FP64 force selects its native CPU reference policy. Sensitive stages retain higher precision. |
| GPU | Runtime choice for native CUDA or Vulkan DATA processing, shared by live reception and simulation. The selector prefers usable CUDA when included, then a compatible physical Vulkan device. It starts off on the desktop and selected on a zLab GPU allocation. |
| Simulation | The native emitted waveform runs through the continuous sampled channel and complete receiver without real-time pacing. The console shows modeled receiver-input time and compute wall time. The waterfall, waveform and I/Q preview the newest accepted post-channel samples. The preview stops when input ends. The receiver can still be finishing queued scoring and quiet slots, as shown by the status. A three-second excerpt of signal activity replays at its original rate after finite processing. Live audio follows the device clock. |
| Audio device, channel routing, Live/duplex, TX volume, Exclusive | Existing native device service, selected channel layout, capture/playback policy and output gain. |
| Waterfall, Waveform, Receiver input I/Q | Bounded actual session observations; receiver I/Q is not a substitute TX trace. |
| Pattern evidence, Pattern steps | Native public-slot classifications, scores, completeness, resource and stage diagnostics. |

**Robust setup** samples the compute host twice per second and retains up to
one minute for its CPU, RAM, GPU and VRAM charts. CPU is the worker process
share of its effective CPU-time capacity. On Linux the caption distinguishes
a limiting cgroup quota from a wider CPU affinity mask. For example, a
27.20-core quota permits 27.20 CPU seconds per wall second, even with 32 CPUs
in the affinity mask. The active receiver's software worker ceiling appears
separately. The default in this example is 54 workers for 27 whole effective
CPUs. RAM shows usage against the visible container limit or physical system
RAM. On Linux, the NVIDIA driver provides
one GPU and VRAM lane per visible device, with the receiver's selected device marked
when its UUID matches. The Vulkan workspace display reports temporary
receiver leases and their peaks. The Console reports actual Vulkan search
arithmetic and the sum of completed device-command intervals when timestamps
are available. Command intervals can overlap. Device-wide VRAM usage and GPU
busy time appear in the setup charts. Blank traces indicate unavailable
observations.

**Replay sim** beside **Clear waterfall** displays the previous completed
simulation's retained activity again at its original frequency and scale.
Retained Pattern evidence advances in public-slot order across the replay.
The caption shows slots revealed and their modeled elapsed time. Replay leaves
decoded reception and the public calendar in place. **Cancel TX** can stop it.
Explicit raw simulations display the received bits even when the same bits
also form a short-codec character.

While computing, the worker retains one latest receive window for the preview.
Waterfall rows can skip modeled time when the receiver advances between screen
updates. Adjacent rows retain their actual sample offsets. The preview adds no
silence between them. The waveform fills its viewport with captured receiver
samples. The Pattern evidence axis uses a signed logarithmic scale for log
evidence and its threshold, with exact values in **Pattern steps**.

Carrier presets use the native occupied DATA bandwidth, including spreading,
pulse shaping and gating, plus the declared oscillator and channel allowances.
The Link Planner reports the same planned band. Half the inner Rate is offered
when it fits the waveform, sampled band and any enabled Helper. This choice
is intended for radio hardware with suitable baseband conversion and filtering.
Higher recommendations leave a lower edge of at least 375 Hz without Helper
or 1400 Hz with effective Helper power. The preferred higher carrier is
1725 Hz if it fits. With Helper it tries 1775 Hz next, then searches upward
in 5 Hz steps. Without Helper it searches upward in 5 Hz steps from 1725 Hz.
With Helper the list also offers 1525, 1575 and 1775 Hz when they fit.
When the current baseband frequency becomes invalid, the higher recommendation
is used if it fits. The half-rate preset is the fallback when it is the only
recommendation that fits.

The shared **GPU** checkbox sits immediately left of **Developer**. It defaults
off on the desktop, persists with the launch settings as `--gpu yes|no`, and is disabled with
an explanation outside Robust mode, while busy or closing, or when the build
has no native CUDA or Vulkan receiver backend. Enabling it initializes the
selected physical GPU on the native session manager, away from the UI thread.
The selector tries usable CUDA first when compiled, then a compatible physical
Vulkan device. A requested-device failure is reported and prevents that session
from starting; there is no CPU-as-GPU fallback. The selected device and bounded
selection/fallback reason are exposed in diagnostics. Clear **GPU** between
runs to use the native CPU path in the same application, including after a
loader or driver error.

The [Search arithmetic preference](search-arithmetic.md) persists as
`--search-arithmetic default|fp32-min|fp64-force`. Automatic native CPU processing
can use Q15 spectral products with FP32 FFTs, FP32 products or a qualified FP64
chirp-Z route; sensitive stages retain FP64. With GPU enabled, FP32 minimum
selects FP32 CUDA bulk work (the current CUDA automatic FFT path already uses
FP32), with higher precision retained for sensitive stages. Vulkan FP32 minimum
selects its FP32 search path and disables the Default-only native chirp-Z
crossover. Combining GPU with FP64 force is explicitly unavailable and rejected.
The displayed policy is distinct from actual per-slot routing and precision in
receiver diagnostics/receipts.

GPU readiness identifies the device; positive GPU scored-row evidence identifies
executed work. CUDA performs GPU coarse and bulk fine work when selected by its
automatic route. Vulkan performs GPU
frequency-grid search while Helper processing, FIR/template preparation,
native lag/tight-clock routes, fine tracking and sensitive host scoring retain
native CPU processing. On an NVIDIA Blackwell device with the OpenCL module,
eligible searches use the dense FP4 search with its device template bank and
qualified fine statistics use resident FP4 projection instead. The CUDA automatic policy also routes qualified small public
fine blocks through canonical native fine processing while retaining CUDA
coarse work; its native-fine session count and fallback reason are shown
separately from arithmetic precision. The supported geometry and bounded
[complete-stream measurements](validation.md#nvidia-and-intel-gpu-toggle-integration)
define its qualification; routing alone does not prove acceleration. Receipts and
diagnostics distinguish backend, executed stage work and fallback/selection
reason. CUDA workspace admission is shared
across sessions, bounded by available device memory with runtime headroom, and
cancellable; status polling uses a cheap receipt query. Automatic Vulkan
admission discovers a backend-wide workspace budget from available physical
device memory after runtime headroom, and leases each whole coarse session from
that shared pool. Diagnostics show current and peak leases, the largest owner
lease, waiting sessions and any native coarse fallback count. If a geometry
cannot fit even the smallest admissible Vulkan shape, its coarse work falls
back to the native CPU path and is reported as `native-vulkan-workspace`, never
as GPU work. Explicit pinned Vulkan probes may still use their configured
per-owner limit. Vulkan's bounded persistent program cache remains separately
charged; on an APU, device allocations count against shared host RAM. Both
routes preserve the default `W=2*H` worker budget. This control does not promise
a speedup for every geometry. Actual device and performance qualification is
recorded separately in [validation](validation.md#native-gui-integration) and
[measured scope and limits](triad-performance.md#local-radeon-vulkan-processing).

Robust keeps one Target SNR shared by Console, Link Planner and saved
`--target-snr`. Waveform selection uses canonical automatic defaults. Helper
placement uses the fixed high/low pilots and separate auxiliary tone; an invalid
selected IF/rate fails configuration explicitly. Pattern/tone and Helper
frequency controls and their launch overrides remain removed.

The Robust **Wrong Key Admission** choice is immediately left of Encryption key
entry and persists in launch settings as
`--wrong-key-admission`. It defaults to `current`. GUI reception always uses
the unfiltered baseline for acquisition and decoding, retaining compact counts
for the stronger choices. Changing this dropdown refreshes Signals and Files without
reconfiguring the receiver, including during reception/transmission. It leaves
the transmitted waveform, duration and original decoded content intact and
remains retained while Fast or Legacy is selected.

Choices show approximate HF reference visibility losses:

| Choice | Estimated loss shown |
| --- | --- |
| Unfiltered | No added filter loss |
| 2/3 reduction | ~1 dB |
| 1/3 reduction | ~2 dB |
| 1/10 reduction | ~3 dB |
| 1/100 reduction | ~6 dB* |
| 1/1000 reduction | ~6 dB* |

The compact labels omit the space before `dB`; `*` identifies an extrapolated
model target, as explained in the control's help.
The rounded estimates summarize coarse tested grids, not confidence bounds or
universal sensitivity limits. The deeper choices share an estimate because the
measurements do not resolve distinct dB costs. These estimates concern visibility; the
original unfiltered reception is preserved.
Here a fraction means wrong-key DATA bits **remaining relative to Unfiltered**
in the static-HF reference: 1/10 means 90% filtered. The two deepest choices
are extrapolated model targets. Saved tokens are respectively `current`,
`guarded`, `strict`, `very-strict`, `extreme`, `maximum`; existing settings
remain valid. The numeric target is distinct from the multi-bit row rule.

Completed rated Robust records are visible when at least half their baseline
DATA bits pass the chosen filter; ties stay visible. The view reports passing
bits over admitted DATA bits, excluding physical erasures and quiet slots.
Pending records remain visible until completion. Fast, Legacy, Helper and
unrated records stay visible; missing Robust evidence is identified rather than
assigned confidence. Hidden selections cannot supply copy, paste or save
actions. Selecting Unfiltered restores hidden records still retained in memory;
already exported files are unchanged. This is a browser visibility policy,
not bit erasure followed by FEC re-decoding or a message-level probability.
The display reports the hidden count. Strong wanted signals can also be hidden,
and higher SNR need not restore visibility; see the measured
[tradeoffs](validation.md#reversible-wrong-key-admission-history--15-november-2026).
No calibrated wrong-key/day rate, universal dB loss or authentication guarantee
is provided.

Robust text accepts up to 1 MiB of decoded bytes, with up to 4 MiB of escaped
editor representation. Local edit frames allow up to 4 MiB for this declared
Message control; the shared bridge enforces each control's published limit. Other
event kinds and ordinary fields retain their 1 MiB ceiling. A 6 MiB JSON-text
expansion preflight preserves the 8 MiB snapshot/frame envelope and rejects
drafts whose escaped presentation would exceed it. Text of at most
16 bytes keeps its short profile; longer
text uses fixed marker/codeword intervals. Attachments remain unsupported by
Robust. Exact raw-bit drafts remain 1–208 bits. Long compression and wire-length
preparation run off the UI thread on Transmit; the active transmission retains
its own count while the user edits the next draft. Received long messages retain
bounded diagnostic bits, so exact-raw copy is available only when the complete
bit sequence was retained. Source limits and authentication caveats are in
[protocol](protocol.md#native-long-message-intervals).

Transmit, Cancel TX, Transmit noise and Helper-only transmission dispatch to the
same native session. Noise uses fresh independent DATA keys and encryption;
Helper metadata uses the public Amateur key in that mode. **Force next
transmission** appears only for the same-key waveform-address reuse lock. It is
a one-shot override of that lock and separation wait, preserves reuse history,
and retains draft, key, device and resource checks. Cancellation does not reset
the public calendar or erase reuse history.

Detailed transmission inspection remains deferred. This is separate from the
connected live Pattern evidence and Pattern steps views, which show native
receiver observations and score records.

The **Link Planner** retains its controls, presets, command editor, general
link-budget arithmetic, and generic plots. Robust receive-probability, CPU-pace,
clock-search, and workspace forecasts are unavailable; see the concise
[Link Planner status](link-planner.md).

## Received content and drafts

Apply the same received-text restrictions before content appears in a GUI field,
clipboard request, draft, or preview. Use the sanitized presentation, not
original received bytes; preserve the original payload only for bounded in-memory
storage and an explicit operator-selected save. Raw-bit views may retain exact
bits without turning rejected source bytes into text. See the
[received-text policy](security.md#received-content-presentation) and
[receive processing boundaries](receive-processing-hardening.md).

Keep draft edits separate from received records. A receive result must retain its
identity and status while an adapter redraws or changes pages; stale asynchronous
updates must not overwrite a newer selection. Pending content, when supplied by
an active modem, must remain visibly pending and must not gain completed-message
copy or save actions prematurely. Shared rows, selection, and safe copy behavior
belong in the application layer, not separate toolkit-specific modem logic.

An abandoned pending Robust episode is marked rejected before its display
identity is released. While boundary recovery is active, its status explains
that the receiver is waiting for two scored quiet slots. Raw Pattern steps
continue during recovery, and a later episode gets a fresh identity. Rejection
does not grant completed-message actions or transfer a later episode's evidence
to the old row.

Reception history remains RAM-only. Signals retain at most 4,096 records
(formerly 64), with the existing bounded text/raw-bit previews. The GUI applies
one combined 256 MiB retained-workspace budget to cached content and charged
history/metadata, evicting old records/content as necessary. This is not raw
audio/IQ retention or persistent storage across restarts. File filter evidence
shares the cached payload's identity/lifetime, even if its signal row expires.
Changing a filter never itself evicts or supersedes an underlying reception.
The lists show bounded pages of 64 records, with Older/Newer navigation and
stable reception identities; all retained matching records remain reachable.
Navigation rows cannot supply content actions. This keeps dense history within
the shared 8 MiB frontend snapshot envelope even with a large editor draft.
Retained-workspace charging is distinct from total process or transient RSS.

## Audio and lifecycle

Audio-device selection, channel routing, playback volume, and format labels are
shared application settings. Platform audio adapters report failures through
the owning service response, preserve the selected mode, and do not silently
replace user settings. Starting or stopping one modem must not dispatch stale
work to another. Cancellation and close operations stop only the work owned by
the active application generation; delayed replies from an earlier generation
are ignored.

Hiding Fast preserves an active transmission or duplex loopback test and its
audio ownership until it finishes or is cancelled. Receive-only Fast listening,
including idle monitoring in loopback mode, stops when another modem is
selected, allowing the shared device handoff.

## Presentation and native adapters

Pages declare controls and actions once. Adapters present the same text, values,
availability, and state, subject to native glyph metrics, popup placement, and
platform dialogs. Shared colors and application chrome preferences come from
the shared theme/layout declarations. Full-window plots and bitmap content are
produced by shared code; native adapters do not compute modem or receive state.

Fast uses an 800-logical-pixel default height and a 720-logical-pixel minimum.
Its compact layout places the six receiver-declaration fields together in the
Console row and covers Console, Simulation and Modem details, with profile,
audio and action controls accessible across tabs. Both adapters apply the same
content-size limits through their native window sizing APIs.

Features using existing controls should change shared declarations and
application mapping only. Preserve selection and draft state when changing tabs
or switching active modems. Keep unsupported or unconfigured actions explicitly unavailable with their
reason; implementation coverage and validation limits must remain visible.
