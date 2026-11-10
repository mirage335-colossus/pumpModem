# Security boundaries

Data Pump does not execute received data or use it to choose a file path,
network endpoint, or command. Saving original received bytes requires an
explicit local path. Current format, authentication, and completion guarantees
are specific to each maintained service; see [Fast](fast-mode.md),
[Legacy](legacy-modem.md), and [cryptography](crypto.md). Robust transmission
and configured-calendar reception use the native C++ GUI session and retain
these shared presentation and input bounds. See [current scope](robust-current.md).

## Received-content presentation

The shared receive presentation policy restricts bytes before text reaches the
GUI, clipboard, transmit draft, terminal, pipe, or JSON text field. The allowed
bytes are English ASCII letters and digits, comma, period, at sign, space,
hyphen, underscore, slash, equals, and line-feed (`LF`, `0x0a`). Every other
byte is displayed as one underscore. UTF-8 is not decoded; CR and tab become
placeholders. JSON output escapes the filtered LF as `\n`.

**Shellcode mode** is an opt-in GUI exception for copying bootstrap commands.
It is hidden unless Developer mode is enabled and permits printable ASCII
`0x20`–`0x7e` plus LF. Turning Developer mode off disables Shellcode mode and
reapplies the default policy to received views and derived drafts/history. It
cannot revoke bytes already copied outside the application.

Paste and export use the filtered presentation for text. Raw-bit presentation
contains only `0` and `1`; original completed file bytes remain available only
through explicit saving. The policy changes presentation, not the underlying
wire format or cryptographic guarantees. See the
[GUI contract](gui-contract.md#received-content-and-drafts) for adapter behavior.

## Cryptography and format limits

Fast has encrypted authenticated operation and a separate public checksum
mode; checksums detect accidental changes but do not authenticate a sender.
Authentication failures do not fall back to public mode. Legacy follows its
own maintained format and security limits. See [Fast integrity and physical
completion](fast-mode.md#integrity-physical-completion-and-bounded-storage),
the [Legacy reference](legacy-modem.md), and [cryptographic constructions](crypto.md).

The native Robust waveform uses purpose-separated keyed streams, and its data
confidentiality uses independent Serpent and AES lanes. Reusing the same key
and canonical stream address reproduces bytes; key/address reuse is unsafe.
Purpose separation and waveform evidence do not authenticate a sender or
provide replay protection. The native Robust session integrates transmission
and reception, but that connection does not establish a physical-link security
guarantee. Configured text FEC uses a public CRC for error detection, not sender
authentication. Public AMATEUR key material and unencrypted operation provide
no confidentiality; applicable tests and limits remain in [validation](validation.md#native-gui-integration).

Keyfiles use the current `DPMKEY04` format; older versions are rejected. Audio
collected during key generation is supplementary input, not a source of
measured entropy. A delay between operating-system random collections does
not prove independence or hardware RNG use. A complete unpadded keyfile
contains the material needed to recover its keys; it is not password
protection. See [keyfile construction and limits](crypto.md#keyfile-version-4).

## Input bounds and host boundary

Decoders and parsers must validate lengths, indices, and allocation extents
before using received-derived values. Current component limits and selected
speculative-execution mitigations are described in
[receive-processing hardening](receive-processing-hardening.md); they are
defense in depth, not protection against every CPU, compiler, operating-system,
or hardware vulnerability.

No process or VM isolation is credited. Audio hardware, firmware, drivers,
operating systems, swap, crash dumps, terminal scrollback, clipboard managers,
and explicitly saved files can persist or expose information outside the
application's control. Source review and tests do not replace independent
security assessment on the intended deployment hardware. See the
[validation record](validation.md) for the actual test coverage and limits.
