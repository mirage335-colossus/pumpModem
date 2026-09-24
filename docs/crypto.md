# Symmetric cryptography and keyfiles

The implementation uses OpenSSL 3 for all cryptographic primitives: HKDF-SHA256,
AES-256-CTR, AES-256-CBC, HMAC-SHA256, AES-256-GCM, SHA256, and operating-system-seeded private
random generation.

The time-indexed streams below belong to regular mode and are unchanged by
Fast. [Fast cryptography](fast-mode.md#cryptography-and-exact-source-bytes) uses
separately derived AES-256-CBC/HMAC keys, a per-transfer random salt and fresh
per-group IVs when encryption is enabled. It reuses the keyfile loader, never
the regular epoch keystream. Fast encryption is optional; its public SHA-256
checksum mode provides neither secrecy nor authentication. Both peers choose
the same mode locally, with no automatic fallback on authentication failure.

## Time-indexed streams

All text labels below are ASCII bytes without a terminating zero. `BE64(x)` is
exactly eight bytes in network order. `HKDF(ikm, info)` means RFC 5869 HKDF with
SHA256, extract-and-expand, output length 32, and salt
`datapump/v1/hkdf-sha256`.

The master secret is exactly 32 random bytes. Independent purpose keys are:

| Purpose | HKDF info |
| --- | --- |
| Data encryption | `datapump/v1/data` |
| DSSS | `datapump/v1/dsss` |
| Scrambler | `datapump/v1/scrambler` |
| Reserved keyfile purpose (FHSS) | `datapump/v1/fhss` |
| Authentication | `datapump/v1/mac` |

Each stream purpose derives `epoch_key = HKDF(purpose_key,
"datapump/v1/epoch/" || BE64(timestamp))`. A timestamp is an unsigned whole-second
coordinate, normally a Unix-time approximation of absolute time. The
`Crypto::stream` primitive accepts an explicit timestamp and byte offset;
it does not read the system clock. Pattern transport selects a new timestamp
at symbol boundaries using the sample schedule below. Data, Scrambler and
DSSS use that same symbol-start timestamp under each clock hypothesis.

For byte offset `o`, the AES-256-CTR initial 128-bit counter is
`domain_pad || BE64(floor(o / 16))`. The optional final `Crypto::stream`
argument selects `StreamDomain::Payload` by default, whose eight-byte pad is
all zero, preserving existing payload output. `StreamDomain::Preamble` uses
the eight ASCII bytes `preamble`; `StreamDomain::Suppression` uses `suppress`.
All three domains use the same purpose and epoch
key; the counter pad is not another key derivation. Discard the first `o % 16`
keystream bytes and produce the requested count. OpenSSL increments the whole
counter in big-endian order. The API rejects an offset/count combination whose
final byte exceeds `2^64-1`, so no domain can carry into the high counter
bytes or overlap another domain.
Changing the whole-second anchor derives a fresh epoch key; counter positions
therefore do not collide merely because adjacent transmissions cross a second.
Within one anchor, purpose and domain, random access and sequential generation
agree.

### Binary pattern chip addressing

Every selected key on a non-tone transfer enables private Scrambler templates
as well as Data encryption. DSSS remains an independently selectable private
purpose. Tone modes force the key, Data mask, Scrambler and DSSS off; they are
unencrypted modes for ordinary communications or local experiments and provide
no LPI claim. Explicit old APSK profiles are rejected.

Transfer derives each stable 32-byte waveform seed from its purpose's original
key stream at epoch zero and byte offset zero. `PatternCode` constructs a
`Crypto` instance from that seed and selects the purpose and each symbol's
start epoch for chip generation. Raw payload Data masking uses the original
selected key. A later symbol therefore does not require the original message's
epoch to reconstruct its streams. Both peers must use this addressing scheme;
it is incompatible with the earlier transmission-wide epoch mapping and the
former real-sign waveform.

Let `Fs` be the sample rate, `N` the samples per symbol, and
`C = ceil(N / chip_samples)`. `stream_epoch` is the whole second of symbol
zero; `stream_phase_samples` is its sample offset within that second. For
symbol `j`, integer arithmetic evaluates:

```text
start_samples = stream_phase_samples + j*N
epoch         = stream_epoch + floor(start_samples / Fs)
within_second = start_samples mod Fs
ordinal       = floor(within_second / N)
chip_position = ordinal*C + chip_in_symbol
```

The selected epoch stays fixed through the entire symbol, including an
hours-long pattern. Successive symbols beginning in one second consume
successive positions; the first symbol beginning in a later second selects
that newer epoch and resets the local positions. Long symbols skip intervening
seconds. Partial final chips consume a full position. The helpers avoid
overflowing intermediate sample products and reject true epoch/address overflow.

A private template consumes eight bytes at `8*chip_position`. If Scrambler is
enabled its bytes supply the row; otherwise a public Scrambler stream supplies
the base. Enabled DSSS bytes at the same position and symbol epoch XOR into
that row. DSSS currently follows the same symbol-boundary schedule. Two
big-endian 32-bit words determine a circular
I/Q sample: uniform words `u,v` in `(0,1)` produce phase `2*pi*v` and radius
`min(1.75, sqrt(-log(u))) / sqrt(1-exp(-1.75^2))`. This caps input-chip peaks and
normalizes expected complex power to one. Both amplitude and
phase now depend on the private stream. This removes the former invariant
where squaring real PCM canceled all private +/- signs and exposed a fixed
squared carrier.

The public seek coordinate for symbol `j` starts at chip `start_chip + j*C`;
it indexes this schedule rather than one transmission-wide cryptographic byte
stream. All addresses are checked, including the eight-byte expansion. Fixed
512-byte seek caches are indexed by epoch and offset, retain no
duration-proportional keystream history, and are cleansed on release.

Receiver search state is separate from these caches. Live keyed symbols lasting at
least 60 seconds use compact scalar correlation rather than a symbol-sized
FFT history: all configured timing/frequency/rate/phase hypotheses remain,
with at most 32 diagnostic evidence records and 64 constellation points.
Failed symbol results and expired epoch searches are discarded. The number
of still-running epoch hypotheses remains subject to the aggregate DSP budget;
compact caches do not imply unlimited search or guaranteed real-time operation.

Pulse shaping occurs after this mapping. Eligible profiles use finite 25% RRC
pulses, followed by a circular radial PCM limiter for overlapping peaks. Neither
operation changes plaintext-to-ciphertext encryption, AES/HKDF, purpose keys,
CTR domains, byte XOR mixing, bit masks or absolute chip positions. The two
filter tails consume no new chips or stream bytes. Fixed-size mapped-chip caches
avoid repeating the same seek/map work and are cleansed on release. The receiver
uses linear shaped candidate templates and treats limiter/neighbor tails as
model mismatch, while retaining pattern evidence as its sole synchronization
criterion. See [pulse shaping](modem.md#pulse-shaping) for bandwidth, power loss,
short-pattern exceptions and peer compatibility.

The two legal bit alternatives multiply that position's circular private row
by different public internal-transition masks. Only the selected alternative
is emitted at each position. The receiver regenerates both alternatives for
its candidate key, epoch and stream position, fitting unknown common gain and
phase. FFT correlation divides by the actual template energy; the bounded
clock-window correlator retains the full two-quadrature Gram matrix. Signal
start, continuation, end and time/key alignment still come from pattern
evidence alone, never a preamble, coding interval, FEC or MAC. Ending requires
consecutive fully scored failed symbols whose durations cover six seconds;
an individual symbol lasting at least six seconds ends the stream on its first
completed failure. A partial long-symbol window cannot end reception.

Unkeyed public patterns use the same circular I/Q mapping with the public
Scrambler seed, epoch zero and symbol-local chip positions. Amplitude and phase
both vary, but their rows restart each symbol and are intentionally recognizable.
Two bit alternatives mean two complete pattern templates, not a two-point
constellation. Short public templates legitimately yield sparse plots because
later symbols reuse the same finite chip values. All templates use complex
`value` samples; the former real `sign`/`fill` API has been removed.

### Protected hardware settling

The hardware-settling prefix uses the same bounded circular-noise mapping and
one update per chip as private payload templates. Public prefix bytes are
XORed with the selected Data-purpose bytes and every enabled Scrambler/DSSS
stream before amplitude and phase mapping. Each layer uses the existing key
and transmission epoch with `StreamDomain::Preamble`, the separate counter
range whose high eight bytes are ASCII `preamble`. This pad is local state,
never an on-air field; no preamble-specific key is generated.

After the full payload waveform and its final filter tail, every nonempty
transmission emits exactly three seconds of suppression noise. It uses the same
noise mapping and enabled Data/Scrambler/DSSS mixture, but a third counter
domain, `StreamDomain::Suppression`, whose high eight bytes are ASCII
`suppress`. This separates its bytes from both the prefix and every payload
stream without deriving another key. The same transmission epoch anchors
these bounded noise caches. There is no payload bit, symbol slot, transmitted
metadata or change to the Data counter schedule associated with this tail.
Its duration is independent of symbol duration, including when the rounded
prefix is absent. The tail is a guard against weaker delayed echoes, not a
proof of echo cancellation or a receiver ending condition.

The prefix's input chips are generated independently of both payload codewords and carry no
acquisition marker. It uses no payload stream positions; payload starts at
position zero afterward. Continuous pulse shaping overlaps those independent
prefix and payload contributions near their boundary; this overlap does not
change either stream's source bytes or addresses.
For automatically timed hardware output, the epoch is the scheduled whole
second of the first payload symbol; playback begins earlier by the settling
duration and leading pulse tail. The sample schedule advances later epochs,
so buffer generation never polls the clock to change a pattern in progress.
The output device is opened before scheduling. This is a nominal audio sample
schedule; unknown hardware/output-buffer latency is not measured or compensated.
Explicit-timestamp output and simulation remain deterministic; capture timing
hints account for their prefix offset. No epoch, prefix length, chip count, nonce
or sender identity is transmitted as an additional field. There is no legacy
APSK training prefix or post-encryption symbol padding.

Regular chip timing, finite bandwidth, burst edges and bounded sample amplitudes
remain physical characteristics of the waveform. RRC pulses can expose periodic
second-order statistics at the chip clock, and concentrating signal energy can
aid a keyless energy detector. Removing the squared-carrier
invariant is not proof of indistinguishability from arbitrary background noise
or a measured probability of interception.

### Continuous tuning noise

The GUI's **Transmit noise** action continuously modulates dummy bits through
the regular encrypted pattern transmitter. Each start obtains fresh temporary
master key material through `Crypto::random()` (OpenSSL `RAND_priv_bytes`).
The usual purpose-separated Data, Scrambler and DSSS streams use the same
derivation and addressing as ordinary encrypted transmission. Saved key
material is not a noise input; temporary keys
are never registered with reception, saved or shown in the transmission trace,
and their owned cryptographic storage is cleansed on release.

Noise keeps the selected carrier, chip rate, waveform shaping and normal signal
level, using encrypted pattern modulation even when the saved mode is Tone.
A bounded source masks all-zero dummy bits with the usual Data stream and feeds
the resulting bits into the ordinary `PatternCode` and `PatternTransmitter`.
This preserves the two pattern alternatives, symbol boundaries, private
Scrambler/DSSS mapping, pulse shaping and normal settling waveform. It does not
replace the payload with the preamble's noise distribution. No user content,
dictionary or interval framing is added. Each symbol uses its own normal
epoch/ordinal address; no finite buffer loops or stream positions restart on
successive reads. Finite coordinate limits stop generation before exhaustion
rather than wrapping. This prevents intentional periodic reuse; it does not
forbid chance repetition in random output values.

The independent receiver retains its ordinary local key bank and admission
thresholds. Noise is intended to present unrelated random evidence, like
background noise at comparable received power and spectrum, rather than a
recognizable public or saved-key message template. This is not an absolute
false-detection guarantee: raw-bit reception is unauthenticated, finite random
observations can correlate by chance, and transmitted power can raise the
receiver's noise floor. The bounded Gaussian mapping, chip clock, filtering and
hardware remain distinguishable physical characteristics; no thermal-noise
indistinguishability or RF interference measurement is claimed.

### Data, integrity and reuse

`xor_data` XORs arbitrary bytes at an explicitly selected epoch with the Data
stream. On-air pattern transport uses `xor_binary_bits`: each symbol consumes
Data bit `ordinal` from its symbol-start epoch, most-significant bit first
within each byte. Data and
pattern addressing advance on the sample schedule whether preceding symbols
decoded or not. A 512-byte cache bounds Data mask scratch storage.
`mac` computes the full 32-byte HMAC-SHA256 using its independent key;
verification requires all 32
bytes and compares with `CRYPTO_memcmp`.

Every source interval carries exactly 128 coded bytes. A 24-byte marker made
from two copies of the 96-bit recovery word precedes each interval; there is no
terminal marker or whole-stream footer. FEC and compression profiles are agreed
locally. The data area, optional keyed HMAC and optional RS parity occupy fixed
positions:

| FEC | Public data bytes | Keyed data bytes | Keyed HMAC bytes | Parity bytes |
| --- | ---: | ---: | ---: | ---: |
| Off | 128 | 96 | 32 | 0 |
| RS20 | 106 | 74 | 32 | 22 |
| RS60 | 80 | 48 | 32 | 48 |

Only encrypted intervals contain an integrity tag. For each keyed interval,
transfer computes the HMAC over the following exact concatenation:

```text
ASCII("DP-INTERVAL") || 0x02 || U8(fec) || U8(compressed)
    || BE64(epoch) || BE64(ordinal) || fixed_data_area
```

Here `fec` is 0, 1 or 2 for Off, RS20 or RS60; `compressed` is 0 or 1. The
canonical `(epoch, ordinal)` comes from `symbol_stream_address` for the first
coded symbol after that interval's marker. It is not a received-interval count,
measured carrier frequency, transmitted identifier or original-stream origin.
A later acquisition can verify a surviving interval if it acquires the same
canonical address. The full tag is appended before RS encoding, so RS protects
data and tag together. Data encryption then masks all marker, data, tag and
parity bits. Reception reverses this order and verifies the tag after RS.

Public intervals contain no content digest, checksum or MAC. Raw bits bypass
markers, the source codec, FEC and MAC altogether, even when Data encryption is
selected. Pattern evidence and successful public RS correction provide no
cryptographic authentication or replay protection. The fixed short-text
dictionary adds no authentication, transmitted metadata or length field. There
is no packet parser. Short dictionary interpretation runs only after physical end.

Marker recovery runs after Data decryption and cannot change a crypto offset or
reset a counter. Its [evidence threshold](protocol.md#marker-evidence-threshold)
accounts for tested marker hypotheses under an independent-fair-bit model,
including bounded leading and interior missing runs. Missing leading positions
come from the physical receiver's symbol clock. Marker search cannot select a
new Data-stream alignment or turn its model bound into authentication.

An established pattern track can retain unknown interior symbol slots until a
later confident observation confirms their extent. Data decryption processes
the known spans at their original symbol addresses; a missing slot consumes
its position without supplying a ciphertext decision. Marker matching excludes
these unknowns from its evidence. Any coded byte containing an unknown bit is
passed to RS as a declared erasure; its zero filler is not an observed value.
At physical completion, a partially observed final interval can supply its
remaining fixed positions as erasures. No entirely unobserved interval is
manufactured from a marker alone. Keyed intervals must still pass HMAC after
correction. This preserves alignment across a detection gap; it does not
recover from an unknown change in the capture clock or keystream origin.

The bounded receiver spools corrected data areas during reception. Only the
physical six-second ending event permits the raw LZMA2 source decoder or exact
uncompressed validity-cell decoder to run. Raw LZMA2 uses a fixed dictionary;
its endpoint and final zero padding are checked after physical completion.
Codec success does not provide public integrity. Independent interval MACs do
not authenticate a total stream length or prove that no later interval was
intended. Losing the final interval of one canonical LZMA2 source removes its
codec endpoint and causes post-end source decoding to fail. Whole-interval loss
can remain undetected for an uncompressed source or independently valid suffix.

Reusing the same key, timestamp, purpose, domain and stream positions repeats
CTR output. It can expose plaintext XORs and allow correlation between repeated
private waveforms; the new mapping cannot repair stream reuse. Independent
MAC keys remain separate. The live hardware sender keeps an in-memory maximum
used epoch for each key, identified by a dedicated local MAC label. It includes
the first epoch used by surrounding noise and every payload symbol whose shaped
pulse can contribute to generated output, even before that symbol's nominal
start. Reservations are retained before audio delivery and survive cancellation,
profile changes and reloading the same key within the session.

An ordinary subsequent payload must start beyond that maximum. The selected
new profile's settling/pulse prefix determines the corresponding playback wait.
For maximum used whole-second epoch `H` and prefix duration `P` seconds, the
conservative automatic-playback deadline is `H + 1 - P`; the countdown is
`max(0, H + 1 - P - current_time)`. It is recomputed from the clock, rather than
decremented independently of clock adjustments. A fixed caller-supplied epoch
at or below `H` stays locked until changed or explicitly forced.
The shared GUI shows **TX lock** with a countdown and enforces the same condition
for ordinary button, keyboard and raw-bit sends. A backward clock correction
can extend the wait. Backward jumps during preparation or playback waiting,
and forward jumps that miss the scheduled start, abort that attempt. Normal
hardware completion retains its separate symbol-duration-aware receive-separation
wait; cancellation retains its existing separation behavior.

The explicit **Force next transmission** action bypasses these transmit waits
for one valid request. It can repeat CTR masks and private waveforms, but does
not erase the maximum used epoch or leave protection disabled for later requests.
It does not alter the receiver's physical-absence completion rule. No keyfile
flags, transmitted fields or extra receive hypotheses implement this override.
All history is memory-only and closing the program forgets it. Simulation stays
separate from hardware history. Standalone CLI/file transmitter timestamps remain
caller-controlled, and separate devices sharing a key still require coordination.
A surviving reception uses its acquired epoch,
subsecond phase and stream-symbol index to recover the matching Data positions.
Recovering later bits does not reconstruct wholly missing intervals or prove
that reception includes the complete original source.

## Named key sets: keyfile version 2

New CLI keyfiles use `DPMKEY02`. The GUI and CLI load all named sets from one
file. Each set stores the complete five 32-byte purpose keys, in the order data,
DSSS, scrambler, FHSS, MAC; restoring a set reproduces those exact keys.

```sh
pump keygen --output shared.key --key-names 'Home,Portable,Emergency'
pump keys --keyfile shared.key
pump simulate --text 'hello' --keyfile shared.key --key-name Portable --json
```

The 48-byte prefix retains the version-1 header length, optional pad length and
flag, and nonce positions. Bytes 25–27 are zero; bytes 28–31 contain the number
of entries (BE32, 1–128); bytes 44–47 contain the encrypted payload length
(BE32). The prefix is followed by exactly 128 MiB of random header, the encrypted
key-set payload, and a 16-byte GCM tag. Each plaintext entry is a BE16 name length,
1–64 bytes of printable UTF-8 name, then 160 bytes of purpose keys. Names must be
unique. Parsing rejects impossible counts/lengths before allocation, and releases
keys only after authentication and complete validation.

```
digest = SHA256("datapump/v2/keyfile-hash" || prefix48 || random_header || pad)
wrapping_key = HKDF(digest, "datapump/v2/keyfile-wrap")
```

HKDF retains the salt defined above. AES-256-GCM authenticates the entire prefix
as associated data and encrypts the complete named collection. Temporary
plaintext key-set buffers are cleansed even when loading fails. The optional
external pad is retained for old workflows through the CLI; ordinary keyfiles
need only the single file. The GUI has no separate pad field.

Version-1 keyfiles remain readable as one set named `Default`. The legacy C++
`load_keyfile` entry point selects the first set from either version. New code
can use `load_keyring` to receive all sets. Creation remains exclusive: a new
collection is written to a new pathname; there is no in-place keyfile editor.

## Legacy keyfile binary layout (version 1)

Integer fields are big-endian. No padding or trailing bytes are allowed.

| Offset | Length | Contents |
| --- | ---: | --- |
| 0 | 8 | ASCII `DPMKEY01` |
| 8 | 8 | Random header length, exactly 134217728 for production |
| 16 | 8 | Pad length, zero when absent |
| 24 | 1 | Pad flag: 0 absent, 1 required |
| 25 | 7 | Reserved, all zero |
| 32 | 12 | Random AES-GCM nonce |
| 44 | 4 | Reserved, all zero |
| 48 | 134217728 | Cryptographically random header |
| 134217776 | 32 | Encrypted master key |
| 134217808 | 16 | AES-GCM tag |

The wrapping key derivation is:

```
digest = SHA256("datapump/v1/keyfile-hash" || prefix48 || random_header || pad)
wrapping_key = HKDF(digest, "datapump/v1/keyfile-wrap")
```

AES-256-GCM uses that wrapping key, the stored 12-byte random nonce, and the
complete 48-byte prefix as associated data. Its plaintext is the 32-byte master
secret; its tag is 16 bytes. A pad, when supplied, must contain at least
1073741825 bytes, and its entire contents are hashed with a 64KiB working
buffer. A file created with a pad cannot be opened without that pad or with a
different length/content. Providing a pad for an unpadded file also fails.
Malformed flags, changed headers, ciphertext/tag corruption, truncation, and
trailing data are rejected.

The explicit `datapump::testing` helpers use smaller fixture policies. Production
entry points always enforce the 128MiB header and greater-than-1GiB pad sizes;
they reject reduced test keyfiles. No normal CLI setting lowers these limits.

Creation uses exclusive file creation and refuses existing paths and symlinks.
POSIX creation uses mode 0600. Windows uses a protected owner-only DACL and
`CREATE_NEW`; Windows builds link the system Advapi32 library. Data is flushed
and synchronized before successful return. A failed write can leave an
incomplete, restricted file, which will fail to load; automatic deletion is
avoided because a pathname could have been replaced concurrently. Directory
entry durability across a power loss is not guaranteed by this library.

## Boundaries

* An unpadded keyfile contains everything needed to recover its master key.
  This format spreads the key's storage dependency across a large random file;
  it is not password protection or encryption against someone holding the file.
  A pad must also remain secret. Neither file should contain signing keys or
  secrets for unrelated applications.
* Header and pad size improve the opportunity to remove necessary entropy;
  they do **not** guarantee secure erasure on SSDs, snapshots, journaling file
  systems, backups, or wear-levelled devices. Deleting a few predictable bytes
  does not guarantee cryptographic erasure either.
* Sensitive internal key arrays, hash inputs, and temporary secret buffers are
  cleansed where practical. Memory is not locked, and swap, crash dumps, compiler
  copies, and caller-owned plaintext/keystream buffers remain outside this
  guarantee.
* No replay protection is implemented. A bounded clock search can limit the
  accepted time range; it does not prevent a replay within that range.
* Cryptography authenticates bytes after decoding. It cannot establish hardware
  isolation or guarantee that an audio interface, USB device, driver, or host
  cannot be compromised.
* This code and the integrated modem require independent review before relying
  on them for sensitive communications. Test coverage is not a security audit.

## Primary references

* [RFC 5869: HKDF](https://www.rfc-editor.org/rfc/rfc5869)
* [RFC 2104: HMAC](https://www.rfc-editor.org/rfc/rfc2104)
* [NIST SP 800-38A: block cipher modes](https://csrc.nist.gov/pubs/sp/800/38/a/final)
* [OpenSSL HKDF API](https://docs.openssl.org/3.0/man7/EVP_KDF-HKDF/)
* [OpenSSL HMAC API](https://docs.openssl.org/3.0/man7/EVP_MAC-HMAC/)
* [OpenSSL EVP encryption API](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/)
