# Security boundaries

Data Pump transports analog-audio decisions into opaque application bytes.
No received address selects a network endpoint, filename selects a destination,
or command is executed. Files are not automatically opened. The native GUI calls
the C++ transfer service without a shell; saving requires a local explicit path.
The byte transport has no packet header, remote integer length, filename/type,
packet ID, repeat flag, bootstrap CRC, or automatic legacy parser fallback.
Existing keyfiles remain usable, but peers must use the same fixed-interval wire
format and local profile. See [protocol](protocol.md).

## Signal evidence, error correction and authentication

Pattern evidence alone establishes symbol timing and keystream coordinates.
It is a model score, not authentication or a calibrated probability for arbitrary
real interference. Fixed marker recognition follows normal Data unmasking and
cannot restart the cipher or select a packet parser.

Each marker precedes exactly 128 coded bytes. Public intervals contain only their
fixed data area and optional RS parity: no SHA-256 content digest or substitute
checksum. Anyone can generate valid public RS codewords; neither marker confidence
nor successful correction makes public content trustworthy. Public source-format
validation is likewise not proof of origin or harmlessness.

Encrypted intervals use the existing independently keyed HMAC-SHA256. Their fixed
layout is data area, 32-byte HMAC, then optional RS parity. The HMAC binds the local
FEC/source profile, a versioned domain, the canonical `(epoch, ordinal)` address
of the interval's first coded symbol, and the complete fixed data area. RS protects
the data and tag together; MAC verification follows correction. Missing timing
addresses remain unresolved, and failed authentication cannot become accepted
source content. The cipher masks markers and all other interval bits at their
normal symbol positions.

Generic RS supports known byte erasures with `2*errors+erasures <= parity`.
Unknown slots contribute no marker evidence. Their zero placeholders retain an
erasure mask through packing; they cannot silently become observed zero bits or
empty source cells. Uncorrectable intervals make exact source reconstruction
incomplete. Later markers may permit later segments to be received independently.
There is no public integrity claim merely because an error pattern was accepted
by an algebraic decoder.

The marker's independent-fair-bit model budgets at most `2^-84` false acceptance
across a drained collector, charging all bounded start/deletion/mismatch trials.
See [marker evidence](protocol.md#marker-evidence-threshold). This model does not
cover a sender constructing marker-like bytes and supplies no authentication.
Runtime derivation from a stored public label reduces literal self-recognition;
it cannot make a finite marker absent from every possible source file.

## Stream completion and post-end decoding

Only iterative search finding consecutive **fully scored failed symbols** whose
received duration covers at least six seconds ends a stream. If one symbol lasts
six seconds or more, its first completed failure suffices. Shorter symbols require
enough consecutive failures to cover six seconds. Long symbols are not preempted
by a partial six-second window.

EOF, cancellation, receiver replacement, exhausted quotas, FEC/MAC results and
codec end markers are not evidence of physical completion. They cannot bypass
the application source-decoder gate. Reception may be interrupted and buffers
released while its status remains incomplete.

Compressed mode is exactly one raw LZMA2 source with a fixed 4 MiB dictionary and
less than one fixed data area's trailing zero fill. It transmits no original
length. A bounded spool retains corrected compressed areas until physical end;
only then may the application decompress. No compressed previews or speculative
codec probes run during reception. The decoder has explicit scratch/output caps,
requires its own end, and rejects noncanonical trailing fill. Uncompressed mode
uses fixed validity/byte cells with bounded loops to retain exact trailing zeros.
Malformed occupancy, unresolved holes or failed MACs cannot be concatenated away.

LZMA2 has an internal chunk grammar; it is a separately bounded post-end application
codec, not modem framing. Public senders can construct syntactically valid
compressed data. Decoder bounds and safe output handling remain necessary even
when a source is well formed or keyed authentication succeeds. The format rewrite
removes old parser dependencies; this review did not establish a buffer-overflow
exploit in the previous implementation.

Independent MACs authenticate received intervals at their canonical positions.
They do not prove the sender intended no additional intervals. A whole lost final
interval removes the sole codec endpoint of a canonical compressed source and
therefore makes that source truncated; an independently acquired later segment,
or an uncompressed source, still carries no authenticated total transfer count.
A long fade may terminate a receiving segment rather than the intended file.

## Keys, replay and private waveforms

AES-256-CTR and HKDF purpose separation retain distinct Data, MAC, Scrambler,
DSSS and reserved FHSS roles. A symbol selects its start-second epoch for its
entire duration; same-second ordinals and fixed seek caches preserve addresses
through missing bits and interval drains. A canonical schedule address is not a
fresh nonce. Reusing key/epoch/ordinal positions exposes XOR relationships between
plaintexts and permits replay within accepted timing windows. Local cooldowns do
not coordinate separate processes or hosts; there is no durable replay database.

Every selected non-tone key enables private pattern templates. Private chips and
protected settling use capped circular I/Q values with varying amplitude/phase.
Settling uses separate preamble counter positions and supplies no acquisition
condition. Tone modes clear private protections. Bandwidth, chip cadence, capped
amplitudes and finite edges remain observable; no measured interception or
indistinguishability guarantee is made. Exact raw-bit/status signals have no MAC
and cannot authenticate identity.

The 128 MiB named-keyfile codec and optional pad remain separate from radio data.
New keyfiles use exclusive creation. Large keyfiles do not guarantee physical
SSD erasure, and possession of a complete keyfile and required pad compromises
its shared secrets. Buffers are cleansed where practical; all process memory is
not locked or scrubbed. See [cryptography](crypto.md).

## Resource and host boundary

The receiver drains decisions and fixed intervals; retained waveform/candidate
state, marker overlap, RS scratch and diagnostic prefixes are bounded. A continuous
confident signal is limited by local content, spool and output quotas, not by a
silence timeout. Received compressed bytes may occupy a capped temporary-file
spool until completion. Application caches, caller buffers, spool storage and
codec scratch are separately charged; a content quota is not a process RSS limit.
Current fixed LZMA2 scratch caps are 64 MiB for encoding and 8 MiB for decoding.
Batch PCM APIs still require a full-waveform budget; simulation work grows with
sample count. Finite key/timing/frequency banks can be refused when unaffordable.

Temporary files, OS swap/hibernation/core dumps, terminal scrollback, redirection
and clipboard managers can persist data outside the application. Terminal display
escapes control bytes; pipes preserve exact bytes. Clipboard text requires valid
UTF-8. No source bytes supply a destination path or executable file type.

The system still trusts audio hardware, firmware, OS drivers and its runtime.
Analog modulation does not establish protection against hostile peripherals,
BadUSB, electrical faults, host compromise or driver defects. Hardware isolation
and attenuation remain external engineering responsibilities. There is no serial
modem substitute, network receiver API, built-in repeater or automatic radio-control
channel. Tests and source review do not replace independent security assessment
on the intended hardware.
