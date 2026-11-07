#!/usr/bin/env python3
"""Reproduce test_interval_auth_v4's independent HKDF/HMAC vectors.

Uses only Python's standard library. The source-area bytes are fixed test
input, independent of the implementation under test; the C++ test separately
checks that the message produces those bytes.
"""
import hashlib
import hmac


def h(key, message, digest):
    return hmac.new(key, message, digest).digest()


def lane(root, name, message):
    salt = b"datapump/cascade/v2/hkdf-sha512"
    prk = h(salt, root, hashlib.sha512)
    info = b"datapump/cascade/v2/" + name + b"/auth/"
    key = h(prk, info + b"\x01", hashlib.sha512)[:32]
    return h(key, message, hashlib.sha256)


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b, strict=True))


source = bytes.fromhex("a4dbac965b8596dd64b2dbae920bb4d2414da0d0e4174b2dcee8").ljust(96, b"\x00")
context = (b"DP-INTERVAL" + bytes([4, 0, 0])
           + (1800000192).to_bytes(8, "big") + (0).to_bytes(8, "big") + source)
serpent, aes = bytes(range(64)), bytes(range(64, 128))
s = lane(serpent, b"serpent", context)
a = lane(aes, b"aes", context)
print("serpent lane", s.hex())
print("aes lane", a.hex())
print("combined", xor(s, a).hex())
print("changed serpent", xor(lane(serpent[:17] + bytes([serpent[17] ^ 1]) + serpent[18:],
                                  b"serpent", context), a).hex())
print("changed aes", xor(s, lane(aes[:17] + bytes([aes[17] ^ 1]) + aes[18:],
                                b"aes", context)).hex())
