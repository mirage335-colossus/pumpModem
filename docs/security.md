# Security boundaries

Data Pump decodes analog audio into deliberately limited application content.
No decoded address selects a network endpoint. No received command is executed,
no received filename selects a write path, and no received file is auto-opened.
The native GUI calls the C++ transfer service directly without a subprocess or
shell. File saves require a user-selected path and exclusive creation.
Release 0.5 uses bandwidth-derived clocks with adaptive differential APSK;
packet integrity/authentication and existing keyfile formats are unchanged.
Its public audio whitening mask reduces symbol bias, but is reversible without a
key and supplies no confidentiality or additional key-reuse protection. Keyfile
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

The live ticker can display provisional text before the footer arrives. This is
explicitly unvalidated: partial bootstrap correction and metadata checks do not
establish integrity or authentication. Only a complete verified frame enters the
received-message cache. Verified text is eligible for strict UTF-8 clipboard
copy; only verified files and screenshots enter the explicit-save list. A file
containing valid text is still a file. Diagnostic CLI previews
likewise carry `validated:false` and are base64 encoded.

Text written directly to a terminal escapes control characters that could
otherwise manipulate terminal state or clipboard selections. Redirected stdout
and pipes preserve exact bytes. Binary packet output requires a pipe/redirection
or an explicit output path. Metadata must be well-formed UTF8; GUI clipboard copy
also requires valid UTF8 and refuses to silently replace binary bytes.

Encrypted on-air ordering is:

```
AES256-CTR(known training || RS-protected bootstrap ||
           interleaved RS(plaintext content || HMAC(context || metadata || content)))
```

The exact packet layout is in `protocol.md`. Wrong keys, modified authenticated
frames, uncorrectable FEC, unsupported versions, and malformed lengths fail
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
bounded chunks/integrals, with a default 64 MiB DSP budget distinct from the
256 MiB content/cache limits. Packet coding scratch is checked separately from
DSP. A long symbol does not require retaining its whole PCM duration; legacy
batch PCM/WAV calls still have full-waveform allocation limits. These bounds are
not a total process resource sandbox or a guarantee against CPU exhaustion.

Actual acquisition evaluates a finite timing/key/epoch bank at one carrier.
Too many candidate receivers can exceed the configured workspace and fail
explicitly. Successful symbol correlation is never sufficient to accept content.
Simulation supplies receiver-clock PCM with arbitrary startup timing and phase,
relative crystal error and Wiener phase noise. It uses the same finite blind
acquisition and spreading correlation as audio reception, without giving the
receiver transmitter timing, length or epoch. Successful simulation does not
establish calibrated hardware sensitivity or oscillator tracking. Blind
protected-bootstrap acquisition avoids requiring detectable five-second training
when payload symbols are much longer. Bootstrap correction remains provisional
until complete packet digest/MAC verification succeeds.

Continuous reception is local audio only; there is no network API, built-in
repeater, or automatic radio-control channel.

Automated tests and review do not substitute for an independent security audit.
The reference release should be evaluated on the intended hardware before
being relied on as a security boundary or for sensitive communications.
