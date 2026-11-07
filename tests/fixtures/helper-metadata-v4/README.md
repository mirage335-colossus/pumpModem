# Independent Helper metadata vectors

`generate-vectors.py` generates the five frozen `N0CALL` vectors in
`test_helper_metadata.cpp` from synthetic, distinct 64-byte Serpent and AES
roots. It implements HKDF and bit packing with Python `hashlib`/`hmac`, uses
libgcrypt for Serpent-256 ECB counter blocks, and uses Python `cryptography`
for AES-256-CTR. It does not import or execute DataPump code. As an independent
control it also reproduces the historical AES-only Helper vector.

Run with Python 3, `cryptography`, and an installed `libgcrypt`:

```sh
python3 tests/fixtures/helper-metadata-v4/generate-vectors.py
```

The roots and outputs are public deterministic test fixtures, not key material.
