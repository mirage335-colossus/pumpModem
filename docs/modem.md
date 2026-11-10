# Modem architecture and shared boundaries

[Robust](robust-current.md) has one compiled C++ three-carrier stack for source,
Helper, channel simulation, complete receiver processing and audio. The shared
GUI session links that implementation and uses in-process owners and shared
buffers. Python is a testing/benchmarking tool and temporary numerical reference,
not an operating receiver. Live operation uses the configured public calendar,
keyed ordinal and declared local noise model. Unknown-start acquisition and
native real-time capacity remain unqualified; see
[current validation](validation.md#native-gui-integration).

Robust GUI operation and CLI availability are separate: the former Robust audio
CLI commands remain retired. Old single-carrier protocol, planner and command
recipes are not compatibility requirements for the native stack.

[Fast](indexes/fast.md) and [Legacy](legacy-modem.md) are independent maintained
modems with their own formats, operating paths and validation. Do not apply an
old Robust wire count, completion rule or receiver estimate to either of them.
The [protocol guide](protocol.md) covers shared stream boundaries; the
[GUI contract](gui-contract.md) covers controls and presentation.

## Source, audio and receiver boundaries

Native Robust geometry defines carrier spacing, fine-cell timing, pulse support,
source/sample coordinates and Helper composition. [Shaping](triad-shaping.md)
and [channel simulation](channel-models.md) must preserve delayed tails and
continuous state. A coefficient plot, source duration or nominal power setting
is not a measurement of the complete emitted waveform or accepted receiver bits.

Physical audio timing and device routes are shared infrastructure. Preserve
[provider timestamp uncertainty](clock-sync.md), report discontinuities, and
keep source coordinates distinct from playback queue position. The optional
[background-noise stage](triad-background-noise.md) belongs at final audio rate;
a narrow I/Q simulation does not replace it. GUI frequency and oscillator
settings describe inputs and do not themselves retune or discipline hardware.

## Data and validation boundaries

Apply the [received-content policy](security.md) and [input hardening](receive-processing-hardening.md)
before presentation, independent of modem. Parsing, clipboard access, display
text and saving original payloads have distinct authority and bounds. A
successful waveform experiment does not relax those contracts.

New Robust protocol changes evolve the canonical source. Use tags and separate
binary releases for old compatibility. Computationally intensive work follows
[abbreviated hierarchical testing](triad-performance.md), with cheap correctness
checks first and explicit uncertainty. Historical research in the documentation
index supplies ideas and negative results, not additional supported backends.
