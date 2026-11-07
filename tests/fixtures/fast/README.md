# Independent Fast cascade vectors

`generate-v4-vectors.py` constructs the fixed Fast classic group, the default
capacity group fingerprint, and the full classic physical wire fingerprint for
`Crypto::from_independent_roots` using **synthetic** roots:

- Serpent root: bytes 128 through 191.
- AES root: bytes 0 through 63. The waveform/MAC master is bytes 0 through 31.
- Transfer salt: bytes 32 through 63; group IV: bytes 0 through 15.

The script uses Python `hashlib`/`hmac` for HKDF and authentication,
`cryptography` for AES, the system libgcrypt implementation for Serpent, and
independent finite-field Reed–Solomon and shift-register convolutional coding.
It does not call project cipher, codec, or encoder code. The original AES-only
group, obsolete shared-root cascade group, and public-mode full-wire fingerprints are frozen independent controls and
are checked each time before emitting the new values. The script also emits
old capacity group and bootstrap tags, plus single-lane group/bootstrap
forgery tags, for explicit fail-closed tests.

Run `python3 tests/fixtures/fast/generate-v4-vectors.py` with Python
`cryptography` and libgcrypt installed. This deterministic generator is only for
test vectors; its inputs are public and must never be used for real keys.
