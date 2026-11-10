# Symmetric cryptography and keyfiles

The maintained services use OpenSSL 3 for AES, HKDF, HMAC, SHA and AES-GCM,
plus a portable Serpent-256 implementation. Robust waveform keys, dual-lane
confidentiality, keyfiles and Fast encryption are separate constructions; a
waveform-purpose stream is not message encryption.

## Canonical Robust waveform purposes

The native `IndependentApskV2` waveform uses purpose-separated AES streams.
`Crypto` derives the fixed purpose keys from a 32-byte waveform seed using the
`datapump/v1/hkdf-sha256` salt and the following labels:

| Slot | Purpose | Label | Use |
| ---: | --- | --- | --- |
| 0 | Data | `datapump/v1/data` | Reserved waveform-purpose stream |
| 1 | DSSS | `datapump/v1/dsss` | Canonical triad waveform |
| 2 | Scrambler | `datapump/v1/scrambler` | Canonical triad waveform |
| 3 | FHSS | `datapump/v1/fhss` | Reserved purpose slot |
| 4 | MAC | `datapump/v1/mac` | Waveform context derivation |

The canonical triad uses the DSSS and Scrambler slots. Keep all labels and
indices unchanged: exact sample bytes and independent vectors depend on them.
An intentional waveform change must update the canonical format and its
independent vectors together.

For stream purpose `p` and whole-second timestamp `t`, the stream key is
`HKDF(purpose_key[p], "datapump/v1/epoch/" || BE64(t))`. The AES-CTR counter
starts with eight zero bytes followed by `BE64(floor(offset/16))`; the first
`offset mod 16` bytes are skipped. The stream API takes purpose, timestamp,
byte offset, and count. Separate purposes prevent Scrambler and DSSS output
from sharing keystream; they do not provide message confidentiality,
authentication, replay prevention, or key freshness. Reusing the same key,
purpose, timestamp, and offset reproduces the same bytes.

For file-backed keys, the AES root's first 32 bytes seed those waveform
purpose keys. The independent Serpent root does not feed that AES derivation.

The waveform's keyed child sources use `Crypto::mac` on their canonical
context to derive a child waveform seed. The dual-lane message APIs are kept
separate from this AES-only waveform path. The native C++ complete receiver
and GUI session share these canonical components. Python tools test and compare
the implementation; they are not an operating receiver. GUI connection does
not add sender authentication or replay protection to waveform evidence. Short
text FEC/CRC is error correction/detection rather than authentication. Native
long text uses the separate interval tag described below.

## Dual-lane confidentiality and authentication

Current key entries hold independent 64-byte Serpent and AES roots. They are
not concatenated as input to one cipher-key KDF. For the random-access
confidentiality stream, each lane derives its own epoch key with HKDF-SHA512
and the fixed `datapump/cascade/v2/hkdf-sha512` salt. Its info is the
lane-specific `datapump/cascade/v2/{serpent|aes}/stream/` label followed by
`BE64(epoch)`. Each lane uses AES-256-CTR or Serpent-256-CTR respectively;
their output bytes are XORed. Offset and length are checked against the
64-bit byte-coordinate limit. Reusing a key/epoch/offset stream is unsafe.

`confidentiality_child(context)` derives child confidentiality roots
independently within both lanes. The Serpent and AES info domains are
`datapump/cascade/v2/serpent/child/` and
`datapump/cascade/v2/aes/child/`, each followed by the same framed context.
Use separate, stable contexts for distinct cryptographic purposes.

The stream counter prefixes are `data-s02` for Serpent and `data-a02` for
AES, followed by `BE64(floor(offset/16))`; the first `offset mod 16` bytes
are skipped. These fixed prefixes and the per-lane KDF domains are part of the
construction and are covered by the independent vectors.

`confidentiality_mac(message)` returns the XOR of full HMAC-SHA256 results
under independently derived Serpent- and AES-lane authentication keys. The
key domains are `datapump/cascade/v2/serpent/auth/` and
`datapump/cascade/v2/aes/auth/`. The protocol supplies the canonical message
context and framing. Authentication does not
replace source sanitization or establish sender identity outside that framed
construction. `mac` is the waveform-purpose HMAC utility; it is not a
confidentiality tag for user data.

Native Robust long intervals use `confidentiality_mac` with ASCII
`DP-TRIAD-INTERVAL`, version byte 1, FEC and compression bytes, big-endian
64-bit epoch/ordinal of the actual first coded slot, and the source area.
The full 32-byte tag precedes RS parity. This domain is not the retired long
keyed format. Verification binds an individual word to its configured native
calendar context; it does not authenticate the total number of words or the
physical endpoint. A valid uncompressed prefix ending at any complete word can
remain a valid shorter stream if all later words are absent. Public AMATEUR
material cannot establish sender authenticity. The short profile is unchanged;
see [native long intervals and limits](protocol.md#native-long-message-intervals).

Fast uses a prepared per-transfer `ConfidentialityCbc` context. Its
`prepare_confidentiality_cbc` setup derives Serpent and AES CBC keys from
separate roots, context, and random transfer salt; each group uses fresh IV
material. The Fast layer authenticates IV, salt, local profile, ordinal and
ciphertext before decryption. The exact bootstrap and group framing, public
checksum mode, bounded storage, and failure behavior are in
[Fast integrity and completion](fast-mode.md#integrity-physical-completion-and-bounded-storage).
There is no automatic fallback from an authentication failure to public mode.

## Keyfile version 4

New keyfiles use `DPMKEY04`; versions 01–03 are rejected. Each named entry
stores separate 64-byte Serpent and AES roots. A waveform-only seed cannot
provide the independent confidentiality roots, and an older keyfile cannot
reconstruct a missing lane. Both peers must use matching current key material
and the same locally selected cryptographic mode.

Keyfile generation gathers separate 64-byte operating-system random values for
the Serpent and AES lanes in each of two rounds, separated by at least one
monotonic second. It can also collect three seconds of selected mono audio.
The incrementally hashed audio digest is mixed only into the Serpent lane;
the AES lane does not depend on audio or Serpent material. Audio is
supplementary input, not an entropy estimate. An OS-only fallback is permitted
only when explicitly enabled and is reported to the caller. A delay between
random collections does not prove independent entropy or hardware RNG use.

With HKDF-SHA512 and salt `datapump/cascade/v2/hkdf-sha512`, the two rounds
`S0,S1` and `A0,A1` are conditioned as:

```text
S_os = HKDF512(S0 || S1, "datapump/v4/serpent/os-entropy")
S    = HMAC-SHA512(S_os, "datapump/v4/serpent/audio-mix/" || U8(audio_used) || audio_digest64)
A    = HKDF512(A0 || A1, "datapump/v4/aes/keyfile-entropy")
```

When audio fallback is used, `audio_used` is zero and its digest field is
all zero; lane-specific domains then derive the indexed entry roots.

The fixed keyfile prefix records format, lengths, entry count, nonce and
whether audio contributed. The encrypted payload contains unique named entries
with both roots. AES-GCM authenticates the public prefix as associated data;
a separately keyed Serpent HMAC authenticates the framed prefix and GCM
ciphertext before keys are released. The optional pad is additional storage
protection, not password-derived encryption. A complete unpadded file contains
the material needed to recover its keys.

The 48-byte prefix contains the `DPMKEY04` magic, header and optional-pad
lengths, flags, entry count, a random 12-byte nonce, and payload length. Each
plaintext entry is a big-endian 16-bit name length, 1–64 printable UTF-8 name
bytes, and the 64-byte Serpent and AES roots. The prefix is followed by the
random header, ciphertext, a 16-byte AES-GCM tag, and a 32-byte Serpent HMAC.
The optional pad must contain at least 1,073,741,825 bytes.

Even-indexed header and pad bytes feed the Serpent wrapping derivation;
odd-indexed bytes feed the AES derivation, with pad lengths framed in both.
Separate SHA-512/HKDF-SHA512 domains derive the wrapping keys. Serpent-CTR is
the inner layer, AES-256-GCM the outer layer, and the prefix is GCM associated
data. A separately derived Serpent HMAC covers the framed prefix and outer
ciphertext including the GCM tag; the loader verifies that tag before opening
either cipher layer or releasing key entries.

Production creation uses exclusive file creation, owner-only permissions and
bounded working storage. The 128 MiB random header and optional pad must not be
represented as guarantees of secure deletion or resistance to someone holding
the complete keyfile. Secret buffers are cleansed where practical, but process
memory, swap, crash dumps, caller-owned buffers, audio providers, operating
systems and hardware remain outside that guarantee.

## Security limits and references

Cryptographic correctness does not establish fresh keys, prevent address
reuse, secure an audio device, or protect a compromised host. Tests and
independent vectors verify specified constructions; they are not a security
audit or physical-link qualification. See [security boundaries](security.md),
the [current validation record](validation.md), and the
[evidence index](indexes/evidence.md) for current limits and source-bound
measurements.

Primary references: [RFC 5869: HKDF](https://www.rfc-editor.org/rfc/rfc5869),
[RFC 2104: HMAC](https://www.rfc-editor.org/rfc/rfc2104),
[NIST SP 800-38A: block cipher modes](https://csrc.nist.gov/pubs/sp/800/38/a/final),
[OpenSSL HKDF](https://docs.openssl.org/3.0/man7/EVP_KDF-HKDF/), and
[OpenSSL EVP encryption](https://docs.openssl.org/3.0/man3/EVP_EncryptInit/).
