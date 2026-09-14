# Symmetric cryptography and keyfiles

The implementation uses OpenSSL 3 for all cryptographic primitives: HKDF-SHA256,
AES-256-CTR, HMAC-SHA256, AES-256-GCM, SHA256, and operating-system-seeded private
random generation. It implements no asymmetric cryptography or key exchange.

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
| FHSS | `datapump/v1/fhss` |
| Authentication | `datapump/v1/mac` |

Each stream purpose derives `epoch_key = HKDF(purpose_key,
"datapump/v1/epoch/" || BE64(timestamp))`. A timestamp is an unsigned whole-second
transmission anchor, normally a Unix-time approximation of absolute time. Each
purpose uses the **same timestamp** when searching a candidate clock offset.
The caller maps its symbol/sample schedule to byte positions; this library does
not convert fractional seconds or automatically advance the anchor during a
transmission.

For byte offset `o`, the AES-256-CTR initial 128-bit counter is
`domain_pad || BE64(floor(o / 16))`. The optional final `Crypto::stream`
argument selects `StreamDomain::Payload` by default, whose eight-byte pad is
all zero, preserving existing payload output. `StreamDomain::Preamble` uses
the eight ASCII bytes `preamble`. Both domains use the same purpose and epoch
key; the counter pad is not another key derivation. Discard the first `o % 16`
keystream bytes and produce the requested count. OpenSSL increments the whole
counter in big-endian order. The API rejects an offset/count combination whose
final byte exceeds `2^64-1`, so neither domain can carry into the high counter
bytes or overlap the other domain.
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

Transfer derives each 32-byte waveform seed from its purpose's original key
stream at the selected epoch and byte offset zero. `PatternCode` constructs a
`Crypto` instance from that seed and selects the same purpose and epoch for
chip generation. Raw payload Data masking uses the original selected key.
This waveform convention is incompatible with the former real-sign waveform.

For absolute chip position `k`, a private template consumes eight bytes at
`8*k`. If Scrambler is enabled its bytes supply the row; otherwise a public
Scrambler stream supplies the base. Enabled DSSS bytes at the same absolute
position XOR into that row. Two big-endian 32-bit words determine a circular
I/Q sample: uniform words `u,v` in `(0,1)` produce phase `2*pi*v` and radius
`min(1.75, sqrt(-log(u))) / sqrt(1-exp(-1.75^2))`. This caps input-chip peaks and
normalizes expected complex power to one. Both amplitude and
phase now depend on the private stream. This removes the former invariant
where squaring real PCM canceled all private +/- signs and exposed a fixed
squared carrier.

Let `C = ceil(symbol_samples / chip_samples)`. Symbol `j` starts at chip
`start_chip + j*C`; a partial final chip consumes its entire stream position.
All addresses are checked, including the eight-byte expansion. Every enabled
private purpose advances through fresh positions across symbols. Fixed
512-byte seek caches retain no duration-proportional keystream history and
are cleansed on release.

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
evidence alone, never a preamble, packet header, FEC or MAC.

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

The prefix's input chips are generated independently of both payload codewords and carry no
acquisition marker. It uses no payload stream positions; payload starts at
position zero afterward. Continuous pulse shaping overlaps those independent
prefix and payload contributions near their boundary; this overlap does not
change either stream's source bytes or addresses.
The epoch is fixed before the prefix and receiver clock hypotheses account
for the rounded settling duration. No epoch, prefix length, chip count, nonce
or sender identity is transmitted as an additional field. There is no legacy
APSK training prefix or post-encryption symbol padding.

Regular chip timing, finite bandwidth, burst edges and bounded sample amplitudes
remain physical characteristics of the waveform. RRC pulses can expose periodic
second-order statistics at the chip clock, and concentrating signal energy can
aid a keyless energy detector. Removing the squared-carrier
invariant is not proof of indistinguishability from arbitrary background noise
or a measured probability of interception.

### Data, integrity and reuse

`xor_data` XORs arbitrary bytes with the Data stream. `mac` computes the full
32-byte HMAC-SHA256 using its independent key; verification requires all 32
bytes and compares with `CRYPTO_memcmp`. Packet callers authenticate metadata,
content and local epoch context before FEC and whole-stream encryption.

Raw bits and dictionary-coded short text carry no MAC or checksum. Their
pattern evidence supplies neither cryptographic authentication nor replay
protection. Compact packets insert their repeated 96-bit recovery word after
every complete 256 encoded bytes, before Data encryption. Every marker,
header, integrity and FEC bit is therefore masked. Recovery runs only after
ordinary Data decryption and never changes a crypto offset, resets a counter,
or creates another packet parser entry point.

Reusing the same key, timestamp, purpose, domain and stream positions repeats
CTR output. It can expose plaintext XORs and allow correlation between repeated
private waveforms; the new mapping cannot repair stream reuse. Independent
MAC keys remain separate. Automatically timed live keyed bursts wait for a
fresh whole-second epoch, and encrypted output has its existing cooldown.
Explicit timestamps are caller-controlled, and separate devices sharing a key
still require coordination. A cropped reception uses the acquired absolute
symbol index for Data decryption rather than restarting at zero.

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
