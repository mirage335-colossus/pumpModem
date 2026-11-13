# Independent Fast cascade vectors

`generate-v4-vectors.py` constructs the fixed Fast classic group, classic
physical wire, compact acoustic-short public cycles, and wire-capacity group
fingerprints for `Crypto::from_independent_roots` using **synthetic** roots:

- Serpent root: bytes 128 through 191.
- AES root: bytes 0 through 63. The waveform/MAC master is bytes 0 through 31.
- Transfer salt: bytes 32 through 63; group IV: bytes 0 through 15.

The script uses Python `hashlib`/`hmac` for HKDF and authentication,
`cryptography` for AES, the system libgcrypt implementation for Serpent, and
independent finite-field Reed–Solomon and shift-register convolutional coding.
It does not call project cipher, codec, or encoder code. Compact vectors cover
the deterministic public bootstrap and final source cycle, outer GF(65536)
parity, punctured K=7 coding, 2,048-bit whitening, and the v4 single-carrier
training profile identity. Capacity vectors cover the changed wire single-
carrier v4 identity and retain old AES-only and shared-root cascade group and
bootstrap tags as fail-closed controls. Classic/APSK vectors remain frozen
controls. The script also emits single-lane group/bootstrap forgery tags.

The generator rechecks these previous compact v2 public-cycle fingerprints
before printing the context-updated vectors:

- Rate 1/2: `a0825e08d739449dea3b1d27a5c4b8a391ebb1b8f25f08371a8a2f20f97c1614`
- Rate 3/4: `36fda4c62bc75bb013a44e8de5bca8c9f8ebd1e7072f67183792d154e0a4c820`

The independent v4 receipts pinned by `test_fast_codec` are:

- Compact public cycle pair, rate 1/2: `f48e62d7e5ebe8e8e462e30a5164ba66ffdfeec7934cab17e1dc658523562e53`
- Compact public cycle pair, rate 3/4: `28e218349eb149c8bfab04b76d99a53d5742bd94ae62c21325ba3d7718c77b10`
- Wire capacity encrypted group: `7c04297e59a0042cf1385118ddece384a73381a07d6439ac5a3f5066e47c1108`
- Previous-suite group tag under the v4 context: `e467eabbc41067fa14e7e46edd60b1f868944f5bdf1a90b0b9b52242a510f7af`
- Previous-suite bootstrap tag under the v4 context: `91c1258b2fb3a28797abaf1b312aa5bd0b514e554b70f17ac2ef74e6b494bb29`

Run `python3 tests/fixtures/fast/generate-v4-vectors.py` with Python
`cryptography` and libgcrypt installed. This deterministic generator is only for
test vectors; its inputs are public and must never be used for real keys.
