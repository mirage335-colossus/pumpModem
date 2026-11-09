# Protocol status

The former single-carrier fixed-interval Robust protocol is retired and is not a
current interoperability or compatibility contract. Its short dictionary,
marker/interval, recovery, and GUI-progress specifications no longer define the
maintained Robust implementation. Protocol breaks are handled by evolving the
canonical source and publishing a separately versioned binary; the repository
does not retain parallel Robust receivers for older wire formats.

The current native Robust path is documented in
[robust-current.md](robust-current.md). It defines a canonical native
`IndependentApskV2` waveform, shaping, streaming sampled channel, and Helper
Generator. A headless `CompleteStreamReceiver` also processes captured streams
against public slot plans, runs complete fine tracking, and completes a message
only after two fully scored quiet slots following DATA. Received length and
transport EOF do not choose the physical endpoint; codec output is filtered
text. Quiet evidence uses complete orthogonal likelihood blocks wholly inside
the native public aperture: current DATA support intersected with the search
window and bounded by neighboring latest/earliest DATA slots. All received
samples remain in fine likelihoods. A single functional case result does not
establish all-case, operational, physical-channel or message-success
qualification. The headless path is not connected to the Robust GUI, which
remains unavailable pending reconnection.
See the distinct [manual capacity](robust-manual-capacity.md) and
[relative regression](triad-performance.md) policies.

Fast has its own maintained wire formats and codecs; see the [Fast format](fast-mode.md)
and [capacity codec](fast-capacity-codec.md). Legacy has its own modem behavior
in the [Legacy reference](legacy-modem.md). Shared cryptographic and
received-content boundaries remain in [crypto.md](crypto.md) and
[security.md](security.md).

## Retained shared components

The application continues to maintain shared crypto, key-selection, source
sanitization, and codec components required by the active services. Their
contracts are defined by their current headers and focused tests, not by the
retired Robust framing below. Keyfiles may contain named entries; a receive
operation selects one entry, by name or the documented default.

A historical protocol or fixture in Git history is evidence about that exact
revision only. It is not a supported fallback, current test requirement, or
qualification of the canonical path.
