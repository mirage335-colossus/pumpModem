#!/usr/bin/env python3
"""Independent public-only DataPump v4 cipher and keyring test vectors.

Uses Python hashlib/hmac, cryptography AES and system libgcrypt Serpent. It
never loads DataPump code. The deterministic roots and file are NOT secret.
The default 4 KiB header corresponds to the explicit testing policy only.
"""
import argparse
import ctypes as ct
import ctypes.util
import hashlib
import hmac
import json
import re
import struct
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

SROOT = bytes(range(128, 192))
AROOT = bytes(range(64))
SALT = b"datapump/cascade/v2/hkdf-sha512"
EPOCH = 1720000000
NAME = b"Fixture"
NONCE = bytes(range(12))


def kdf(key, info):
    prk = hmac.new(SALT, key, hashlib.sha512).digest()
    return hmac.new(prk, info + b"\x01", hashlib.sha512).digest()


def contextual(label, context, salt=b""):
    return label + struct.pack(">Q", len(context)) + context + struct.pack(">Q", len(salt)) + salt


def xor(left, right):
    assert len(left) == len(right)
    return bytes(a ^ b for a, b in zip(left, right))


class Serpent:
    def __init__(self):
        name = ctypes.util.find_library("gcrypt")
        if not name:
            raise RuntimeError("libgcrypt is required")
        self.lib = ct.CDLL(name)
        self.lib.gcry_check_version.restype = ct.c_char_p
        if not self.lib.gcry_check_version(None):
            raise RuntimeError("libgcrypt initialization failed")
        self.lib.gcry_cipher_map_name.argtypes = [ct.c_char_p]
        self.lib.gcry_cipher_map_name.restype = ct.c_int
        self.lib.gcry_cipher_open.argtypes = [ct.POINTER(ct.c_void_p), ct.c_int, ct.c_int, ct.c_uint]
        self.lib.gcry_cipher_setkey.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
        self.lib.gcry_cipher_setiv.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
        self.lib.gcry_cipher_setctr.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
        self.lib.gcry_cipher_encrypt.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.c_void_p, ct.c_size_t]
        self.lib.gcry_cipher_close.argtypes = [ct.c_void_p]

    def encrypt(self, key, data, mode, iv=None):
        handle = ct.c_void_p()
        algorithm = self.lib.gcry_cipher_map_name(b"SERPENT256")
        if not algorithm or self.lib.gcry_cipher_open(ct.byref(handle), algorithm, mode, 0):
            raise RuntimeError("libgcrypt cannot open Serpent-256")
        try:
            keybuf = ct.create_string_buffer(key)
            if self.lib.gcry_cipher_setkey(handle, keybuf, len(key)):
                raise RuntimeError("libgcrypt Serpent key setup failed")
            if iv is not None:
                ivbuf = ct.create_string_buffer(iv)
                setter = self.lib.gcry_cipher_setctr if mode == 6 else self.lib.gcry_cipher_setiv
                if setter(handle, ivbuf, len(iv)):
                    raise RuntimeError("libgcrypt Serpent IV setup failed")
            source = ct.create_string_buffer(data)
            result = ct.create_string_buffer(len(data))
            if self.lib.gcry_cipher_encrypt(handle, result, len(data), source, len(data)):
                raise RuntimeError("libgcrypt Serpent encryption failed")
            return result.raw
        finally:
            self.lib.gcry_cipher_close(handle)


SERPENT = Serpent()


def stream_components(sroot, aroot, epoch, offset, count):
    if count < 0 or offset < 0 or (count and offset + count - 1 > (1 << 64) - 1):
        raise ValueError("stream byte offset out of range")
    skip = offset % 16
    blocks = (skip + count + 15) // 16
    s_key = kdf(sroot, b"datapump/cascade/v2/serpent/stream/" + struct.pack(">Q", epoch))[:32]
    a_key = kdf(aroot, b"datapump/cascade/v2/aes/stream/" + struct.pack(">Q", epoch))[:32]
    def counters(prefix):
        return b"".join(prefix + struct.pack(">Q", offset // 16 + i) for i in range(blocks))
    # Use ECB over explicitly constructed counters, independent of project CTR.
    s = SERPENT.encrypt(s_key, counters(b"data-s02"), 1)[skip:skip + count]
    a = Cipher(algorithms.AES(a_key), modes.ECB()).encryptor().update(counters(b"data-a02"))[skip:skip + count]
    return s, a


def mac_components(sroot, aroot, message):
    sk = kdf(sroot, b"datapump/cascade/v2/serpent/auth/")[:32]
    ak = kdf(aroot, b"datapump/cascade/v2/aes/auth/")[:32]
    return (hmac.new(sk, message, hashlib.sha256).digest(),
            hmac.new(ak, message, hashlib.sha256).digest())


def child_roots(context, sroot=SROOT, aroot=AROOT):
    return (kdf(sroot, contextual(b"datapump/cascade/v2/serpent/child/", context)),
            kdf(aroot, contextual(b"datapump/cascade/v2/aes/child/", context)))


def entropy_combiner_vector():
    # Synthetic sample source for testing only; these values carry no entropy.
    # Values are exact binary fractions, hence their IEEE-754 encoding is
    # portable and independent of a host audio device or project callback.
    rate, frames = 4000, 12000
    audio_os = bytes(range(128))
    aes_os = bytes(range(128, 256))
    def transcript(change_one=False):
        digest = hashlib.sha512(b"datapump/v4/serpent/physical-audio/float32-mono-be/")
        digest.update(struct.pack(">QQ", rate, 1))
        for i in range(frames):
            sample = (((i * 73) % 201) - 100) / 128
            if change_one and i == 1234:
                sample = 0.125 if sample != 0.125 else 0.25
            digest.update(struct.pack(">f", sample))
        digest.update(struct.pack(">Q", frames))
        return digest.digest()
    os_key = kdf(audio_os, b"datapump/v4/serpent/os-entropy")
    def serpent_root(digest, used):
        return hmac.new(os_key, b"datapump/v4/serpent/audio-mix/" + bytes([used]) + digest,
                        hashlib.sha512).digest()
    digest, altered = transcript(), transcript(True)
    return {"sample_rate": rate, "frames": frames,
            "sample_rule": "(((i*73)%201)-100)/128, binary32",
            "serpent_os": audio_os.hex(), "aes_os": aes_os.hex(),
            "audio_digest": digest.hex(), "serpent_os_key": os_key.hex(),
            "serpent_audio_used": serpent_root(digest, 1).hex(),
            "serpent_audio_changed": serpent_root(altered, 1).hex(),
            "altered_audio_digest": altered.hex(),
            "serpent_audio_unavailable": serpent_root(bytes(64), 0).hex(),
            "aes_root": kdf(aes_os, b"datapump/v4/aes/keyfile-entropy").hex()}


def cbc_vector(sroot=SROOT, aroot=AROOT):
    context, salt = b"v4 CBC context", bytes(range(32, 64))
    iv, plain = bytes(range(16)), bytes(range(48))
    sk = kdf(sroot, contextual(b"datapump/cascade/v2/serpent/cbc/", context, salt))[:32]
    ak = kdf(aroot, contextual(b"datapump/cascade/v2/aes/cbc/", context, salt))[:32]
    siv = kdf(sk, b"datapump/cascade/v2/cbc/serpent-iv/" + iv)[:16]
    inner = SERPENT.encrypt(sk, plain, 3, siv)  # Libgcrypt CBC
    outer = Cipher(algorithms.AES(ak), modes.CBC(iv)).encryptor().update(inner)
    return {"context": context.hex(), "salt": salt.hex(), "iv": iv.hex(),
            "plaintext": plain.hex(), "serpent_key": sk.hex(), "aes_key": ak.hex(),
            "serpent_iv": siv.hex(), "inner": inner.hex(), "ciphertext": outer.hex()}


def keyring(header_bytes, output, *, patterned=False, pad=b"", pad_output=None):
    if header_bytes <= 0 or header_bytes > 128 * 1024 * 1024:
        raise ValueError("header must be in 1..128 MiB")
    plain = struct.pack(">H", len(NAME)) + NAME + SROOT + AROOT
    prefix = (b"DPMKEY04" + struct.pack(">QQ", header_bytes, len(pad)) + bytes([2 | bool(pad), 0, 0, 0])
              + struct.pack(">I", 1) + NONCE + struct.pack(">I", len(plain)))
    assert len(prefix) == 48
    sd = hashlib.sha512(b"datapump/v4/serpent/keyfile-hash-sha512" + prefix)
    ad = hashlib.sha512(b"datapump/v4/aes/keyfile-hash-sha512" + prefix)
    # Explicit even/odd partition; patterned inputs make mistaken contiguous
    # halves or dropped odd tails detectable. Avoid huge in-memory headers.
    def header_blocks():
        for start in range(0, header_bytes, 65536):
            size = min(65536, header_bytes - start)
            yield (bytes(((start + i) * 37 + 11) & 255 for i in range(size))
                   if patterned else b"\x5a" * size)
    for block in header_blocks():
        sd.update(block[::2])
        ad.update(block[1::2])
    for digest, part in ((sd, pad[::2]), (ad, pad[1::2])):
        digest.update(b"pad/" + struct.pack(">Q", len(pad)) + part)
    shash, ahash = sd.digest(), ad.digest()
    sk = kdf(shash, b"datapump/v4/serpent/keyfile-wrap")[:32]
    ak = kdf(ahash, b"datapump/v4/aes/keyfile-wrap")[:32]
    iv = kdf(sk, b"datapump/v4/serpent/keyfile-iv/" + NONCE)[:16]
    inner = SERPENT.encrypt(sk, plain, 6, iv)  # Libgcrypt CTR
    outer = AESGCM(ak).encrypt(NONCE, inner, prefix)
    auth_key = kdf(shash, b"datapump/v4/serpent/keyfile-auth")[:32]
    transcript = b"datapump/v4/keyfile-auth/" + prefix + struct.pack(">Q", len(outer)) + outer
    tag = hmac.new(auth_key, transcript, hashlib.sha256).digest()
    if output:
        with Path(output).open("xb") as dest:
            dest.write(prefix)
            for block in header_blocks():
                dest.write(block)
            dest.write(outer + tag)
    if pad_output:
        if not pad:
            raise ValueError("pad output requested without a pad")
        with Path(pad_output).open("xb") as dest:
            dest.write(pad)
    return {"header_bytes": header_bytes, "name": NAME.decode(),
            "header_pattern": "(i*37+11)&255" if patterned else "constant 0x5a",
            "pad_bytes": len(pad),
            "prefix": prefix.hex(), "serpent_hash": shash.hex(), "aes_hash": ahash.hex(),
            "serpent_wrap_key": sk.hex(), "aes_wrap_key": ak.hex(), "serpent_iv": iv.hex(),
            "serpent_auth_key": auth_key.hex(),
            "outer_ciphertext_and_gcm_tag": outer.hex(), "serpent_auth_tag": tag.hex()}


def write_cpp(path, values):
    leaves = {}
    def visit(parts, value):
        if isinstance(value, dict):
            for key, nested in sorted(value.items()):
                visit(parts + [key], nested)
        elif isinstance(value, str):
            name = re.sub(r"[^a-zA-Z0-9_]", "_", "_".join(parts))
            if name in leaves:
                raise ValueError("C++ vector name collision: " + name)
            leaves[name] = value
    visit([], values)
    lines = ["// Generated by tests/fixtures/crypto/generate-v4-vectors.py; public test data.",
             "#pragma once", "#include <string_view>", "", "namespace v4_vectors {"]
    lines.extend("inline constexpr std::string_view " + name + " = " + json.dumps(value)
                 + ";" for name, value in leaves.items())
    lines.extend(["} // namespace v4_vectors", ""])
    Path(path).write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header-bytes", type=int, default=4096)
    parser.add_argument("--write-fixture", help="exclusively create full synthetic keyring file")
    parser.add_argument("--write-padded-fixture", help="exclusively create the patterned test keyring")
    parser.add_argument("--write-padded-pad", help="exclusively create its synthetic test pad")
    parser.add_argument("--write-cpp", help="write a C++ header containing all string vector leaves")
    args = parser.parse_args()
    if bool(args.write_padded_fixture) != bool(args.write_padded_pad):
        parser.error("--write-padded-fixture and --write-padded-pad must be used together")
    pad = bytes((i * 73 + 5) & 255 for i in range(8193))
    result = {"description": "public deterministic v4 fixtures; never use as real keys",
              "serpent_root": SROOT.hex(), "aes_root": AROOT.hex(), "epoch": EPOCH,
              "keyring": keyring(args.header_bytes, args.write_fixture),
              "patterned_padded_keyring": keyring(4097, args.write_padded_fixture, patterned=True,
                                                   pad=pad, pad_output=args.write_padded_pad),
              "entropy_combiner": entropy_combiner_vector(), "streams": {}}
    for offset, count in ((0, 64), (1, 31), (15, 34), (39, 24), ((1 << 64) - 32, 32)):
        s, a = stream_components(SROOT, AROOT, EPOCH, offset, count)
        result["streams"][f"{offset}:{count}"] = {"serpent": s.hex(), "aes": a.hex(), "cascade": xor(s, a).hex()}
    message = b"v4 independent dual lane authentication test"
    s, a = mac_components(SROOT, AROOT, message)
    result["mac"] = {"message": message.hex(), "serpent": s.hex(), "aes": a.hex(), "combined": xor(s, a).hex()}
    different_s = bytes([SROOT[0] ^ 1]) + SROOT[1:]
    different_a = bytes([AROOT[0] ^ 1]) + AROOT[1:]
    ss, sa = stream_components(different_s, AROOT, EPOCH, 0, 32)
    aa_s, aa_a = stream_components(SROOT, different_a, EPOCH, 0, 32)
    sm_s, sm_a = mac_components(different_s, AROOT, message)
    am_s, am_a = mac_components(SROOT, different_a, message)
    result["lane_variations"] = {
        "changed_serpent_first_byte": different_s[0],
        "changed_aes_first_byte": different_a[0],
        "serpent_changed_stream": {"serpent": ss.hex(), "aes": sa.hex()},
        "aes_changed_stream": {"serpent": aa_s.hex(), "aes": aa_a.hex()},
        "serpent_changed_mac": {"serpent": sm_s.hex(), "aes": sm_a.hex()},
        "aes_changed_mac": {"serpent": am_s.hex(), "aes": am_a.hex()}}
    context = b"v4 child context"
    s, a = child_roots(context)
    result["child"] = {"context": context.hex(), "serpent_root": s.hex(), "aes_root": a.hex()}
    result["cbc"] = cbc_vector()
    for name, sroot, aroot in (("serpent_changed", different_s, AROOT),
                               ("aes_changed", SROOT, different_a)):
        s, a = child_roots(context, sroot, aroot)
        result["lane_variations"][name + "_child"] = {"serpent_root": s.hex(), "aes_root": a.hex()}
        result["lane_variations"][name + "_cbc"] = cbc_vector(sroot, aroot)
    identity_context = b"v4 identity context"
    identity_message = contextual(b"datapump/v4/identity/confidentiality/", identity_context)
    s, a = mac_components(SROOT, AROOT, identity_message)
    wave_prk = hmac.new(b"datapump/v1/hkdf-sha256", AROOT[:32], hashlib.sha256).digest()
    wave_key = hmac.new(wave_prk, b"datapump/v1/mac\x01", hashlib.sha256).digest()
    wave_message = contextual(b"datapump/v4/identity/waveform/", identity_context)
    result["identity"] = {"context": identity_context.hex(), "serpent": s.hex(), "aes": a.hex(),
                          "confidentiality": xor(s, a).hex(),
                          "waveform": hmac.new(wave_key, wave_message, hashlib.sha256).hexdigest()}
    if args.write_cpp:
        write_cpp(args.write_cpp, result)
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
