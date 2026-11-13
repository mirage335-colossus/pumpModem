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
anchor, initial waveform ordinal, key and waveform/FEC settings. **Robust setup**
exposes the calendar and a declared local live input-noise density in dBFS/Hz.
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
| Clock sync, region, offset, audio error | Public arrival bounds and sampled timing model; live capture also needs actual provider timestamps and uncertainty. |
| Base/Shift oscillator, Margin | Always-active declared oscillator bounds and public receiver search multiplier; synthetic effects are injected only in Simulation and explicit loopback. |
| Doppler/delay spread, selective amplitude | Declared receiver assumptions in every mode; synthetic effects are injected only in Simulation and explicit loopback. Off remains explicit. |
| Rate, Shift | Rate is native inner rate; audio IF is Carrier minus Shift. Shift also contributes to the oscillator error model. |
| Additional Symbols, Spreading | Off uses one lane; Interleave with `N×` selects `N` native added-symbol lanes. Nominal occupied DATA bandwidth is `0.625 × Rate × N`. |
| Symbol burst, Random extra | Shared whole-bit calendar waits; see [burst scheduling](symbol-burst.md). |
| FHSS | Canonical envelope and illustrative frequency schedule; no external radio retuning. |
| Target SNR dB/1Hz | One shared C/N0 target, default 32 dB-Hz, for provisional duration planning; separate from actual simulated C/N0. |
| Error correction | Configured Off, RS20 or RS60 native short/long text profiles. Raw bits bypass text coding and FEC exactly. |
| Search arithmetic | Default selects qualified automatic routing; FP32 minimum selects FP32 products/FFT with higher-precision guards; FP64 force selects its native CPU reference policy. Sensitive stages retain higher precision. |
| GPU | Optional native CUDA or Vulkan DATA processing, shared by live reception and simulation; the compiled selector prefers usable CUDA, then a compatible physical Vulkan device. GPU remains off by default. |
| Simulation | Native emitted waveform through the selected continuous sampled channel and complete native receiver. |
| Audio device, channel routing, Live/duplex, TX volume, Exclusive | Existing native device service, selected channel layout, capture/playback policy and output gain. |
| Waterfall, Waveform, Receiver input I/Q | Bounded actual session observations; receiver I/Q is not a substitute TX trace. |
| Pattern evidence, Pattern steps | Native public-slot classifications, scores, completeness, resource and stage diagnostics. |

The shared **GPU** checkbox sits immediately left of **Developer**. It defaults
off, persists with the launch settings as `--gpu yes|no`, and is disabled with
an explanation outside Robust mode, while busy or closing, or when the build
has no native CUDA or Vulkan receiver backend. Enabling it initializes the
selected physical GPU on the native session manager, away from the UI thread.
The selector tries usable CUDA first when compiled, then a compatible physical
Vulkan device. A requested-device failure is reported and prevents that session
from starting; there is no CPU-as-GPU fallback. The selected device and bounded
selection/fallback reason are exposed in diagnostics. Turning GPU off explicitly
returns to the native CPU path.

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
native CPU processing. The CUDA automatic policy also routes qualified small public
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
