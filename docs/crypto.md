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

For byte offset `o`, the AES-256-CTR initial 128-bit counter is the big-endian
integer `floor(o / 16)`. Discard the first `o % 16` keystream bytes and produce
the requested count. OpenSSL increments the whole counter in big-endian order.
The API rejects an offset/count combination whose final byte exceeds `2^64-1`.
Changing the whole-second anchor derives a fresh epoch key; counter positions
therefore do not collide merely because adjacent transmissions cross a second.
Within one anchor and purpose, random access and sequential generation agree.

### Binary pattern chip addressing

The planned Auto Pattern and fixed-length pattern modes enable private
Scrambler fragments whenever a key is selected. Public and different-key
receive hypotheses therefore have different acquisition patterns; Data-only
masking would leave them indistinguishable at the pattern decoder.
The binary `PatternCode` waveform addresses Scrambler and DSSS independently.
Transfer first derives each 32-byte waveform seed from that purpose's original
key stream at the selected epoch and byte offset zero. `PatternCode` constructs
its own `Crypto` instance from the supplied seed and then selects the same
purpose and epoch for chip generation. This second derivation layer is part of
the waveform convention; direct `PatternCode` callers supply those seeds
themselves. Raw payload masking continues to use the original Data-purpose key.
For absolute chip position `k`, it reads byte `floor(k/8)` and bit `k % 8`
(least-significant bit first) from that purpose's epoch stream. This bit order
is a waveform convention; packed application bytes still carry payload bits
most-significant bit first.

Let `C` be `ceil(symbol_samples / chip_samples)`. Symbol `j` starts at chip
`start_chip + j*C`; chip `i` within it uses the absolute address
`start_chip + j*C + i`. A partial final chip consumes its entire bit position.
Consequently adjacent symbols never restart a private prefix, and a long
symbol never repeats the legacy 16,384-chip period. Every multiply/add used to
form a transmitted stream range is checked before waveform generation.

The two legal bit patterns share that time's private row and mix it with
different public internal-transition masks. Only one alternative is
transmitted at that time. DSSS, when enabled, applies signs from its separate
purpose and seed. Small fixed caches support both sequential output and
receiver seeks without storing a keystream proportional to hours of airtime.
Cache contents are cleansed when released.

The hardware-settling prefix uses a separate derivation domain and an
independent stream, seeded privately when keyed pattern or DSSS spreading is
active and publicly otherwise. It does not consume Data, Scrambler or DSSS payload
positions. The epoch is fixed at transmission start, before that prefix;
payload positions begin at zero afterward. Clock-start hypotheses add the
rounded prefix duration when predicting the first payload symbol. No epoch,
prefix length or stream index is transmitted as a field.

The epoch and chip coordinates are local transmitter state and receiver clock
hypotheses. They add no sender identifier, nonce, slot, symbol index or chip
counter to the transmitted raw bits. A Unix-time anchor approximates absolute
time; the implementation does not claim a native TAI clock. Acquisition must
find the corresponding pattern evidence within its implemented clock search.
The bounded long-symbol correlator can derive the surviving stream-symbol
index from a negative clock-relative start offset. The raw Data-stream decoder
uses that same bit index when unmasking a cropped reception; it must not restart
the data stream at zero merely because the captured fragment begins there.

Public unkeyed patterns restart their deterministic row for every symbol, so
they do not require a secret symbol index. That public mode does not provide
the nonrepeating private waveform of the keyed mode. Manual legacy APSK keeps
its earlier repeated sign-template behavior and should not be described as
the new nonrepeating pattern waveform.

`xor_data` XORs that stream with arbitrary bytes. `mac` computes the complete
32-byte HMAC-SHA256 over exactly the supplied bytes with the separate MAC key.
Frame callers must include all metadata that requires integrity in an
unambiguous encoding. Verification requires a 32-byte tag and compares it with
`CRYPTO_memcmp`. HMAC does not require a unique message nonce. Packet framing
decides where the tag is placed and which surrounding coding is applied.

Reusing the same key, timestamp, purpose, and byte positions repeats CTR output
and exposes the XOR of the plaintexts. Known plaintext reveals those reused
positions. It does not directly reveal other CTR positions or the independent
MAC key. HMAC still rejects altered authenticated content. This is not a claim
that CTR reuse preserves confidentiality. Use transmission spacing and coordinate
large transfers sharing a key.

The live session waits for a fresh whole-second epoch before preparing another
automatically timed keyed pattern transmission on that device. An explicitly
supplied timestamp remains a caller-controlled override. This local guard does
not coordinate independent devices sharing a key or replace operational
transmission spacing.

Raw-bit bursts and dictionary-coded short text carry no MAC or checksum. Their
pattern confidence can reject noise and mismatched time/key hypotheses, but
does not supply cryptographic message authentication or replay protection.
The packet path retains its existing keyed MAC when enabled. No extra
authentication field is silently appended to a three-bit raw transmission.

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
