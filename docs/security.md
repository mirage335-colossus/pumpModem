# Security boundaries

Data Pump decodes analog audio into deliberately limited application content.
No decoded address selects a network endpoint. No received command is executed,
no received filename selects a write path, and no received file is auto-opened.
The native GUI calls the C++ transfer service directly without a subprocess or
shell. File saves require a user-selected path and exclusive creation.
Automatic transport uses binary pattern evidence for acquisition, followed by
bounded byte-boundary recovery for compact packets. Packet integrity and
authentication and existing keyfile formats are unchanged. Legacy APSK and its
public audio whitening layer have been removed. Keyfile
generation in the GUI uses the same exclusive creation and 128 MiB keyring codec
as the CLI. Show in folder passes an encoded parent-directory URI to the OS;
it does not open or execute received content.

The protected computer still trusts its audio/ADC hardware, firmware, operating
system drivers, and application runtime. Audio modulation does not prove the
absence of BadUSB, malicious peripheral firmware, electrical fault injection,
bus corruption, host malware, or driver vulnerabilities. Hardware attenuation
and isolation are external engineering responsibilities. This application does
not accept raw digital modem/serial data as an analog-device substitute and does
not implement a shielded serial randomizer.

Packet parsers validate fixed metadata, checked lengths, kind/flags, filename
boundaries, CRC, FEC syndromes, and whole-content integrity before exposing a
download. Unencrypted SHA256/CRC/FEC protect against accidental corruption; an
attacker can generate new valid unencrypted packets. HMAC-SHA256 with a separate
key authenticates encrypted frame metadata and payload. The CLI includes a
versioned domain and candidate timestamp in HMAC input. Physical training is not
part of the authenticated user message and is not signed.

Pattern-only raw bits and short text can be displayed without packet
authentication. Only a complete verified packet releases a received file or
screenshot. Packet content is interpreted after pattern acquisition and
whole-extent recovery; no provisional APSK bootstrap/packet ticker remains.

Text written directly to a terminal escapes control characters that could
otherwise manipulate terminal state or clipboard selections. Redirected stdout
and pipes preserve exact bytes. Binary packet output requires a pipe/redirection
or an explicit output path. Metadata must be well-formed UTF8; GUI clipboard copy
also requires valid UTF8 and refuses to silently replace binary bytes.

Encrypted compact-packet processing on the pattern transport is:

```
protected bootstrap || interleaved RS(metadata || content || HMAC)
  -> fixed recovery marker initially and after every 256 encoded bytes
  -> Data-stream AES-256-CTR over every bit, including markers
  -> configured pattern mapping and Scrambler/DSSS layers
```

The HMAC covers the canonical bootstrap, metadata and encoded content with the
existing local context. A separate hardware-settling prefix precedes payload.
Raw bits and short dictionary text have neither packet integrity nor recovery
markers. Byte packet APIs retain their existing formats.

Recovery runs on plaintext after the existing whole-stream Data decryption.
Both repeated 96-bit words must match exactly. The initial marker is searched
at offsets 0 through 7 from the burst origin; subsequent markers are searched
within seven bits of an expected boundary. A damaged initial marker consumes
its nominal 192-bit slot when available. Before each periodic marker, the
plaintext interval is trimmed or zero-filled to 256 bytes. Marker positions
consume Data-stream positions and are stripped before deinterleaving and FEC.
Thus every transmitted marker bit is ciphertext when
encryption is enabled. Recovery changes only downstream byte grouping; it does
not select crypto offsets, reset counters or reseed streams. The marker supplies
no data length, command, packet identity or new parser entry point. A recovered
burst is eligible for a single packet parse
from its original beginning, with exact whole-extent validation. Failure never
triggers inner-packet scanning, an unstripped-stream retry or fallback to older
packets without the initial marker. Short dictionary interpretation is bounded
to 195 acquired bits, so a long failed packet cannot
become dictionary text. Raw diagnostics remain available independently.

The marker is derived at runtime from a stored label rather than embedded as
literal wire bytes. This reduces self-recognition in program/source transfers,
but cannot exclude accidental or deliberate collisions in arbitrary content.
The fixed cadence and narrow search window bound a collision's effect to the
candidate data; they do not authenticate it. Missing bits can still cause
corruption or rejection, and FEC can repair only errors within its capacity.
Only a complete SHA-256/HMAC check releases validated packet content. Neither
markers nor FEC prove a received file harmless or prevent host vulnerabilities.

The exact packet layout is in [protocol.md](protocol.md). Wrong keys, modified
authenticated frames, uncorrectable FEC, unsupported field values, and malformed lengths fail
closed. The modem's acquisition correlation is only a signal-detection heuristic;
it is never treated as payload authenticity. Few-bit status is explicitly
unauthenticated and cannot establish identity.

Repeating a timestamp under the same shared key repeats stream positions. This
can expose matching portions of plaintexts but does not replace the independent
MAC. The GUI's six-second cooldown applies only to actual encrypted output;
simulation and unencrypted output retain the one-active-TX rule without this
delay. Users sharing a key must coordinate large sends; GUI cooldown does not
coordinate separate hosts. Replay within the clock window remains possible.
Packet IDs allow external scripts to deduplicate repeat requests but do not
constitute durable anti-replay state.

The cache is RAM-only at the application level. OS swapping, hibernation, core
dumps, terminal scrollback, explicit redirection, and clipboard managers may
persist content outside the application. Key buffers are cleansed where
practical; the entire process memory is not locked or scrubbed. Very large
inputs are bounded and can be rejected before processing. Continuous TX/RX uses
bounded chunks/integrals, with a default DSP budget of 50% of available RAM,
resolved once at startup and distinct from the 256 MiB content/cache limits. Packet coding scratch is checked separately from
DSP. A long symbol does not require retaining its whole PCM duration;
batch PCM/WAV calls still have full-waveform allocation limits. These bounds are
not a total process resource sandbox or a guarantee against CPU exhaustion.

Actual acquisition evaluates a finite timing/key/epoch bank at one carrier.
Too many candidate receivers can exceed the configured workspace and fail
explicitly. Successful symbol correlation alone never validates packet content;
raw bits and short dictionary text are separately available without that claim.
Simulation supplies receiver-clock PCM with arbitrary startup timing and phase,
relative crystal error and Wiener phase noise. It uses the same finite blind
acquisition and spreading correlation as audio reception, without giving the
receiver transmitter timing, length or epoch. Successful simulation does not
establish calibrated hardware sensitivity or oscillator tracking. Pattern
acquisition does not require training or a valid bootstrap; byte-boundary markers
are used only after bits are acquired and decrypted in the same burst. Pattern
constellation decoding remains the sole source of timing and keystream alignment.
Markers cannot recover an unknown absolute offset, whole lost blocks or lost
Data-stream/Scrambler/DSSS alignment.
No legacy APSK receiver or protected-bootstrap acquisition fallback remains.

Continuous reception is local audio only; there is no network API, built-in
repeater, or automatic radio-control channel.

Automated tests and review do not substitute for an independent security audit.
The reference release should be evaluated on the intended hardware before
being relied on as a security boundary or for sensitive communications.

## Private waveform boundary

Every selected non-tone transfer key enables Data encryption and private
Scrambler templates. Private Scrambler/DSSS bytes map to capped circular I/Q
noise with varying amplitude and phase, correcting the old invariant where
squaring PCM canceled every private sign. Hardware settling combines Data and
all enabled private streams before the same mapping, at the same chip cadence
in separate preamble counter positions. No fixed APSK prefix or post-encryption
symbol padding is transmitted. Tone modes clear all private protections and
make no LPI claim.

Acquisition still compares patterns alone. FFT evidence uses actual template
energy; long clock-window fits retain the full quadrature Gram matrix. These
changes do not conceal every physical property: bandwidth, regular chip timing,
capped amplitudes and finite burst edges remain, and key/epoch reuse can repeat
waveforms. No measured probability of interception or indistinguishability
from arbitrary ambient noise is established.
