# Security boundaries

Data Pump transports analog-audio decisions into opaque application bytes.
No received address selects a network endpoint, filename selects a destination,
or command is executed. Files are not automatically opened. The native GUI calls
the C++ transfer service without a shell; saving requires a local explicit path.
The byte transport has no packet header, remote integer length, filename/type,
packet ID, repeat flag, bootstrap CRC, or automatic legacy parser fallback.
Existing keyfiles remain usable, but peers must use the same fixed-interval wire
format and local profile. See [protocol](protocol.md).

## Received-text boundary

Robust, Fast and Legacy Modem apply the same restricted receive character policy
before received content reaches native GUI text, clipboard requests or a
transmit draft. The CLI applies it to received content on terminals, in pipes
and in JSON text fields. The default allowed bytes are exactly English ASCII
`a-z`, `A-Z`, `0-9`, comma (`,`), period (`.`), at sign (`@`), space, hyphen (`-`),
underscore (`_`), slash (`/`), equals (`=`), and line-feed newline (`LF`, `0x0a`).

Every other source byte becomes one ASCII underscore. This includes parentheses,
semicolon, backslash, ampersand, every control byte except LF, and every byte
outside ASCII. Carriage return (`CR`) and tab remain placeholders; CRLF is
therefore displayed as an underscore followed by a newline.
UTF-8 is not decoded: a two-byte UTF-8 character becomes two underscores.
`received_text.hpp` implements this boundary with byte comparisons and a
same-size output buffer, without locale handling, Unicode parsing or escape
expansion. CLI JSON escapes the already-filtered LF as `\n` to preserve valid
JSON; plain-text output and GUI message text retain actual newlines.
Received filenames use the default policy even in Shellcode mode.
Application-owned labels and diagnostics are distinct from received content.

**Shellcode mode** is a GUI exception for copying bootstrap commands. It starts
unchecked, is hidden unless **Developer mode** is checked, and permits only
printable English ASCII bytes `0x20` through `0x7e` in addition to LF. Other
controls and non-ASCII bytes still become underscores. Turning Developer mode
off also unchecks and hides Shellcode mode. Turning either permission off
reapplies the default policy to received views and received-derived drafts/history,
refreshes their QR previews,
and revokes pending copies made under the withdrawn exception. It cannot revoke
content already copied to an external program or clipboard manager.

**Paste as message** copies the permitted presentation, not the original rejected
bytes. Received raw-bit pastes preserve exact `0`/`1` bits, but any accompanying
decoded text or expected-text preview obeys the same character policy. Edits and
previous-message actions cannot restore forbidden received bytes after the
exception is disabled. Fresh locally entered transmit text retains its existing
input rules; its QR generator may encode locally typed or pasted punctuation
without either mode being enabled. QR generators consume the transmit draft,
never original received buffers.

Received-content presentation and export have three forms: retained diagnostic
bits are rendered as `0` and `1`, rejected byte values have the fixed placeholder,
and completed original payloads stay in bounded receive memory for direct saving
to a user-chosen file. Attachment saves and explicit CLI `--save PATH` preserve
the original bytes and never open or execute them. Ordinary CLI output does not
provide an exact-byte pipe bypass or a Base64 payload field. The policy changes
presentation and export behavior, not modem framing, source decoding quotas,
physical completion, authentication or pending bit progress.

## Speculative execution: current coverage and limits

Selected receive indices now retain a bounds dependency through their memory
access, and selected validation transitions have a target-specific speculation
barrier. Coverage includes short-dictionary reads, portions of Regular/Fast
Reed–Solomon correction, retained-bit recovery, acoustic interpolation, selected
LZMA dictionary/property operations, the ASCII output boundary and explicit
payload writes. The LZMA changes are applied to a verified generated copy of the
pristine pinned source. Full-domain field tables and reviewed Legacy/LDPC tables
and arithmetic remain unchanged; no global DSP serialization is enabled.

These are targeted bounds-check-bypass mitigations, not protection against all
Spectre variants, Meltdown or other CPU weaknesses. Ordinary C++, printable ASCII,
authentication and an explicit file save are not security proofs. Compiler-target
support is recorded in `build-info.txt`; unsupported targets retain architectural
checks without a claim of speculative protection. Generated-code witnesses check
specific instruction dependencies, not the behavior of every caller or CPU.

The application-controlled receive paths contain no JavaScript engine, embedded
browser, received-content JIT/interpreter or automatic execution of received
files. No process or VM isolation is added or credited. CPU, firmware, operating
system and library behavior still require separate deployment assessment; this
change neither configures nor verifies host mitigations. See the
[technical design and audit scope](receive-processing-hardening.md) for precise
coverage, architecture limits, direct-save handling and performance validation.

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

The optional additional post-end search consumes only retained hard decisions,
missing-slot masks and already established symbol addresses. It enumerates
assignments for unknown bits to free RS capacity, and tests fixed-size interval
alignments. Analog observations and symbol-confidence scores are not inputs.
Assignments retain their original unknown status for evidence and diagnostics;
the received bits inside a partially missing byte must remain consistent with
any recovered byte. No trial changes timing, cipher position, key, local FEC
profile or source codec. Every keyed candidate still passes the complete
HMAC-SHA256 verification after ordinary RS correction.

This search has an explicit finite domain and local storage, wall-clock and
worker limits. Completing only a prefix of that domain cannot establish
uniqueness: deadline or cancellation retains an unfinished result for resumption,
without releasing speculative source bytes. Competing credible reconstructions
are rejected before source parsing. Neither successful authentication nor
readable/decompressible content supplies missing alignment evidence. See the
[recovery scope and evidence accounting](protocol.md#exhaustive-hard-bit-recovery).

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

For interval-coded sources, compressed mode is exactly one raw LZMA2 source with a fixed 4 MiB dictionary and
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
silence timeout. Received compressed areas remain in bounded RAM until physical
completion; they are not written to temporary files. Application caches, caller
buffers, spool storage and codec scratch are separately charged; a content quota
is not a process RSS limit.
Current fixed LZMA2 scratch caps are 64 MiB for encoding and 8 MiB for decoding.
Additional recovery defaults to five wall-clock minutes per run and at most the
available CPU cores. It separately retains up to 65,536 hard-bit slots; the
displayed diagnostic prefix remains limited to 4,096 bits. Recovery retains no
analog samples and does not expand a quota according to received lengths.
An unfinished job remains resumable while its owning result is retained; this
is not a persistent checkpoint or an authenticated total message-length claim.
Live reception admits at most eight active, queued or suspended recovery jobs
within a separate 16 MiB recovery pool. Queue admission reserves the job's
bounded workspace; overflow is reported as unavailable. One coordinator runs
jobs using their parallel workers, independently of the capture/DSP budget.
Batch PCM APIs still require a full-waveform budget; simulation work grows with
sample count. Finite key/timing/frequency banks can be refused when unaffordable.

OS swap/hibernation/core dumps, explicit saved files, terminal scrollback,
redirection and clipboard managers can persist data outside the application.
The receive character policy applies before text display, pipe output and
clipboard export; exact saved payloads remain opaque. No source bytes supply a
destination path or executable file type.

The system still trusts audio hardware, firmware, OS drivers and its runtime.
Analog modulation does not establish protection against hostile peripherals,
BadUSB, electrical faults, host compromise or driver defects. Hardware isolation
and attenuation remain external engineering responsibilities. There is no serial
modem substitute, network receiver API, built-in repeater or automatic radio-control
channel. Tests and source review do not replace independent security assessment
on the intended hardware.
