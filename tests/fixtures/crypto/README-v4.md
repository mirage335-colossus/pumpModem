# V4 keyring and cascade test vectors

`generate-v4-vectors.py` independently generates the frozen public,
deterministic values in `v4-vectors.json`. It uses Python `hashlib` and `hmac`
for SHA-256, SHA-512, HMAC and HKDF, system libgcrypt for Serpent-256, and
Python `cryptography` for AES. It never calls the project implementation. The
roots, audio samples, nonce, header and pad are known test data and must never
be used for actual security.

Run `python3 tests/fixtures/crypto/generate-v4-vectors.py` to print JSON. The
`--write-cpp tests/v4_crypto_vectors.hpp` option regenerates the frozen
test-only C++ constants from the same independent values. The
default unpadded fixture uses a 4,096-byte `0x5a` header, entry name `Fixture`,
Serpent root bytes 128–191, AES root bytes 0–63, and nonce bytes 0–11. Pass
`--write-fixture PATH` to exclusively create that complete synthetic keyring;
the ordinary production loader rejects its reduced test-only header policy.
`--header-bytes 134217728` can instead produce a production-sized synthetic
header without storing the full header in memory.

The same JSON includes a separately generated padded fixture with 4,097
patterned header bytes `(i*37+11)&255` and 8,193 patterned pad bytes
`(i*73+5)&255`. Its odd lengths make incorrect lane partitioning and omitted
tail bytes visible. Pass both `--write-padded-fixture PATH` and
`--write-padded-pad PATH` to create those synthetic files exclusively. The
explicit test policy must allow their reduced sizes.

Stream vectors include aligned, unaligned, and final-counter positions, with
the Serpent and AES outputs shown separately. Counter blocks are constructed
explicitly and encrypted in ECB mode. The file also includes child-root,
separate HMAC components and their XOR, CBC inner and outer layers,
confidentiality and waveform identities, and one-byte lane variations. The
`entropy_combiner` vector hashes 12,000
deterministic binary32 samples, then applies the v4 Serpent-only OS/audio
combiner and independent AES derivation. It checks framing and mixing, not
microphone operation, entropy quality, or cryptographic strength.
