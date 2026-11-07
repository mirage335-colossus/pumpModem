# Keyring test-vector oracle

`generate-keyring-vectors.py` constructs public deterministic, unpadded v2/v3
keyrings for test use only. It uses Python's `hashlib`/`hmac` for HKDF,
`cryptography` AES-GCM, and the system libgcrypt Serpent-256 CTR implementation.
It does not call DataPump cryptographic code. It prints the 48-byte prefix and
encrypted payload plus tag; insert 4,096 bytes of `0x5a` between them. Input
is master bytes 0..31 and entry name `Fixture`, with v3 independent
confidentiality material 96..159. Nonce bytes are 0..11. These are public,
synthetic test values and must never be used for actual security.

`generate-stream-vectors.py` independently derives the legacy confidentiality
root and produces aligned, unaligned, and near-maximum-offset stream vectors.
It evaluates the two ciphers in ECB mode over explicitly constructed counters;
this keeps its counter handling independent of DataPump's CTR code.
