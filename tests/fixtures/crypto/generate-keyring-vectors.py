#!/usr/bin/env python3
"""Reproduce fixed *test-only* keyring vectors using Python and libgcrypt.

Requires Python cryptography and libgcrypt, independent of DataPump's cipher.
Prints prefix and ciphertext/tag; header is 4096 bytes of 0x5a. These keys
are public test data and must never be used for real communications.
"""
import ctypes as ct
import ctypes.util
import hashlib
import hmac
import struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def hkdf(ikm, salt, info, digest):
    prk = hmac.new(salt, ikm, digest).digest()
    return hmac.new(prk, info + b'\x01', digest).digest()


def serpent_ctr(key, iv, data):
    lib = ct.CDLL(ctypes.util.find_library('gcrypt'))
    lib.gcry_check_version.restype = ct.c_char_p
    assert lib.gcry_check_version(None)
    lib.gcry_cipher_map_name.argtypes = [ct.c_char_p]
    lib.gcry_cipher_map_name.restype = ct.c_int
    lib.gcry_cipher_open.argtypes = [ct.POINTER(ct.c_void_p), ct.c_int, ct.c_int, ct.c_uint]
    lib.gcry_cipher_setkey.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
    lib.gcry_cipher_setctr.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
    lib.gcry_cipher_encrypt.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.c_void_p, ct.c_size_t]
    lib.gcry_cipher_close.argtypes = [ct.c_void_p]
    handle = ct.c_void_p()
    # Libgcrypt's CTR mode is 6.
    assert lib.gcry_cipher_open(ct.byref(handle), lib.gcry_cipher_map_name(b'SERPENT256'), 6, 0) == 0
    try:
        kb, ib, source = map(ct.create_string_buffer, (key, iv, data))
        output = ct.create_string_buffer(len(data))
        assert lib.gcry_cipher_setkey(handle, kb, len(key)) == 0
        assert lib.gcry_cipher_setctr(handle, ib, len(iv)) == 0
        assert lib.gcry_cipher_encrypt(handle, output, len(data), source, len(data)) == 0
        return output.raw
    finally:
        lib.gcry_cipher_close(handle)


def main():
    salt256 = b'datapump/v1/hkdf-sha256'
    salt512 = b'datapump/cascade/v1/hkdf-sha512'
    master = bytes(range(32))
    keys = b''.join(hkdf(master, salt256, b'datapump/v1/' + purpose, 'sha256')
                    for purpose in (b'data', b'dsss', b'scrambler', b'fhss', b'mac'))
    header, nonce = bytes([0x5a])*4096, bytes(range(12))
    for version in (2, 3):
        plain = b'\x00\x07Fixture' + keys + (bytes(range(96, 160)) if version == 3 else b'')
        prefix = (b'DPMKEY0' + bytes([48 + version]) + struct.pack('>QQ', len(header), 0)
                  + bytes(4) + struct.pack('>I', 1) + nonce + struct.pack('>I', len(plain)))
        assert len(prefix) == 48
        if version == 2:
            digest = hashlib.sha256(b'datapump/v2/keyfile-hash' + prefix + header).digest()
            aes = hkdf(digest, salt256, b'datapump/v2/keyfile-wrap', 'sha256')
        else:
            digest = hashlib.sha512(b'datapump/v3/keyfile-hash-sha512' + prefix + header).digest()
            wrap = hkdf(digest, salt512, b'datapump/v3/keyfile-wrap-serpent-aes', 'sha512')
            sk, aes = wrap[:32], wrap[32:]
            iv = hkdf(sk, salt512, b'datapump/v3/keyfile-serpent-iv/' + nonce, 'sha512')[:16]
            plain = serpent_ctr(sk, iv, plain)
        wrapped = AESGCM(aes).encrypt(nonce, plain, prefix)
        print(f'v{version} prefix: {prefix.hex()}')
        print(f'v{version} ciphertext/tag: {wrapped.hex()}')


if __name__ == '__main__':
    main()
