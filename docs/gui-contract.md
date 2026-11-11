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

| Shared control | Native meaning |
| --- | --- |
| Encryption | Off preserves unencrypted DATA; keyfile and AMATEUR select the effective key. AMATEUR is public key material, not confidentiality. |
| Helper power, ratio limit, callsign | Requested Helper watts, native power allocation/ratio clamp and Helper metadata. Zero power disables Helper; these settings do not control radio hardware. |
| Transmit power, path loss | DATA power and simulated link attenuation, separate from playback volume. |
| Clock sync, region, offset, audio error | Public arrival bounds and sampled timing model; live capture also needs actual provider timestamps and uncertainty. |
| Base/Shift oscillator, Margin | Declared oscillator errors/phase noise and public search-bound multiplier. |
| Doppler/delay spread, selective amplitude | Selected canonical sampled-channel effects and corresponding declared receiver assumptions. Off remains explicit. |
| Rate, Shift | Rate is native inner rate; audio IF is Carrier minus Shift. Shift also contributes to the oscillator error model. |
| Additional Symbols, Spreading | Off uses one lane; Interleave with `N×` selects `N` native added-symbol lanes. Nominal occupied DATA bandwidth is `0.625 × Rate × N`. |
| Symbol burst, Random extra | Shared whole-bit calendar waits; see [burst scheduling](symbol-burst.md). |
| FHSS | Canonical envelope and illustrative frequency schedule; no external radio retuning. |
| Target SNR dB/1Hz | One shared C/N0 target, default 32 dB-Hz, for provisional duration planning; separate from actual simulated C/N0. |
| Error correction | Configured Off, RS20 or RS60 native short/long text profiles. Raw bits bypass text coding and FEC exactly. |
| Search arithmetic | Default selects qualified automatic routing; FP32 minimum selects FP32 products/FFT with higher-precision guards; FP64 force selects its native CPU reference policy. Sensitive stages retain higher precision. |
| GPU | Optional native Vulkan DATA coarse processing, shared by live reception and simulation, with a scoped native chirp-Z crossover in Default mode; GPU remains off by default. |
| Simulation | Native emitted waveform through the selected continuous sampled channel and complete native receiver. |
| Audio device, channel routing, Live/duplex, TX volume, Exclusive | Existing native device service, selected channel layout, capture/playback policy and output gain. |
| Waterfall, Waveform, Receiver input I/Q | Bounded actual session observations; receiver I/Q is not a substitute TX trace. |
| Pattern evidence, Pattern steps | Native public-slot classifications, scores, completeness, resource and stage diagnostics. |

The shared **GPU** checkbox sits immediately left of **Developer**. It defaults
off, persists with the launch settings as `--gpu yes|no`, and is disabled with
an explanation outside Robust mode, while busy or closing, or when the build
has no Vulkan receiver backend. Enabling it initializes an actual GPU on the
native session manager, away from the UI thread. A requested device failure
is reported and prevents that session from starting. Once initialized, Default
may choose the documented native chirp-Z route on the calibrated geometry; its
actual routing is reported. Turning GPU off explicitly returns to the native CPU path.

The [Search arithmetic preference](search-arithmetic.md) persists as
`--search-arithmetic default|fp32-min|fp64-force`. Automatic native CPU processing
can use Q15 spectral products with FP32 FFTs, FP32 products or a qualified FP64
chirp-Z route; sensitive stages retain FP64. FP32 minimum keeps native spectral
products at FP32 and selects the FP32 Vulkan FFT when GPU is enabled. Combining
GPU with FP64 force is rejected. The displayed policy is distinct from actual
per-slot routing and precision in receiver diagnostics/receipts.

GPU readiness identifies the device; positive GPU scored-row evidence identifies
executed work. Helper, FIR, template generation, native lag/tight-clock routes
and fine tracking retain their documented CPU processing. One backend and its
bounded program cache survive repeated transmissions within a settings
generation; reconfiguration or close releases that generation. The fixed
256 MiB per-owner GPU ceiling and persistent cache are charged to native
admission, including shared RAM on an APU. The default `W=2*H` worker budget
remains intact. This control does not promise a speedup for every geometry; see
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

## Presentation and native adapters

Pages declare controls and actions once. Adapters present the same text, values,
availability, and state, subject to native glyph metrics, popup placement, and
platform dialogs. Shared colors and application chrome preferences come from
the shared theme/layout declarations. Full-window plots and bitmap content are
produced by shared code; native adapters do not compute modem or receive state.

Features using existing controls should change shared declarations and
application mapping only. Preserve selection and draft state when changing tabs
or switching active modems. Keep unsupported or unconfigured actions explicitly unavailable with their
reason; implementation coverage and validation limits must remain visible.
