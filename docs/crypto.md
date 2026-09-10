# Symmetric cryptography and keyfile format, version 1

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

## Keyfile binary layout

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
