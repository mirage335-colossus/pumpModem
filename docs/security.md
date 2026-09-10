# Security boundaries

Data Pump decodes analog audio into deliberately limited application content.
No decoded address selects a network endpoint. No received command is executed,
no received filename selects a write path, and no received file is auto-opened.
The GUI subprocess invocation uses an argument array without a shell. File saves
require a user-selected path and exclusive creation.

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

Text written directly to a terminal escapes control characters that could
otherwise manipulate terminal state or clipboard selections. Redirected stdout
and pipes preserve exact bytes. Binary packet output requires a pipe/redirection
or an explicit output path. Metadata must be well-formed UTF8; GUI clipboard copy
also requires valid UTF8 and refuses to silently replace binary bytes.

Encrypted on-air ordering is:

```
AES256-CTR(balanced training || RS-protected bootstrap ||
           interleaved RS(plaintext content || HMAC(context || metadata || content)))
```

The exact packet layout is in `protocol.md`. Wrong keys, modified authenticated
frames, uncorrectable FEC, unsupported versions, and malformed lengths fail
closed. The modem's acquisition correlation is only a signal-detection heuristic;
it is never treated as payload authenticity. Few-bit status is explicitly
unauthenticated and cannot establish identity.

Repeating a timestamp under the same shared key repeats stream positions. This
can expose matching portions of plaintexts but does not replace the independent
MAC. Users sharing a key must coordinate large sends; GUI cooldown does not
coordinate separate hosts. Replay within the clock window remains possible.
Packet IDs allow external scripts to deduplicate repeat requests but do not
constitute durable anti-replay state.

The cache is RAM-only at the application level. OS swapping, hibernation, core
dumps, terminal scrollback, explicit redirection, and clipboard managers may
persist content outside the application. Key buffers are cleansed where
practical; the entire process memory is not locked or scrubbed. Very large
captures or inputs are bounded and can be rejected before processing. The
configured workspace limits are conservative per operation, not a total process
resource sandbox. This release has no receiving daemon, network API, built-in
repeater, or automatic radio-control channel.

Automated tests and review do not substitute for an independent security audit.
The reference release should be evaluated on the intended hardware before
being relied on as a security boundary or for sensitive communications.
