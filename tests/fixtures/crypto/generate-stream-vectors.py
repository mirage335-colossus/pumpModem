"""Independent, deterministic and public test-only confidentiality vectors."""
import ctypes as C, ctypes.util, hashlib, hmac
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes

def kdf(ikm,salt,info,hash):
    prk=hmac.new(salt,ikm,hash).digest()
    return hmac.new(prk,info+b'\x01',hash).digest()

g=C.CDLL(ctypes.util.find_library('gcrypt')); g.gcry_check_version.restype=C.c_char_p; assert g.gcry_check_version(None)
g.gcry_cipher_map_name.argtypes=[C.c_char_p];g.gcry_cipher_map_name.restype=C.c_int
g.gcry_cipher_open.argtypes=[C.POINTER(C.c_void_p),C.c_int,C.c_int,C.c_uint]
g.gcry_cipher_setkey.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t]
g.gcry_cipher_encrypt.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t]
g.gcry_cipher_close.argtypes=[C.c_void_p]
def serpent(key,counters):
    h=C.c_void_p(); assert g.gcry_cipher_open(C.byref(h),g.gcry_cipher_map_name(b'SERPENT256'),1,0)==0
    assert g.gcry_cipher_setkey(h,key,len(key))==0
    output=C.create_string_buffer(len(counters)); assert g.gcry_cipher_encrypt(h,output,len(counters),counters,len(counters))==0
    g.gcry_cipher_close(h); return output.raw
master=bytes(range(32)); salt=b'datapump/v1/hkdf-sha256'
data=kdf(master,salt,b'datapump/v1/data','sha256'); mac=kdf(master,salt,b'datapump/v1/mac','sha256')
salt2=b'datapump/cascade/v1/hkdf-sha512'
root=kdf(data+mac,salt2,b'datapump/cascade/v1/legacy-root','sha512')
material=kdf(root,salt2,b'datapump/cascade/v1/stream/'+(1720000000).to_bytes(8,'big'),'sha512')
def stream(offset,count):
    skip=offset%16; n=(skip+count+15)//16
    ctr=lambda prefix: b''.join(prefix+(offset//16+i).to_bytes(8,'big') for i in range(n))
    s=serpent(material[:32],ctr(b'data-s01'))
    a=Cipher(algorithms.AES(material[32:]),modes.ECB()).encryptor().update(ctr(b'data-a01'))
    return bytes(x^y for x,y in zip(s,a))[skip:skip+count]
for offset,count in [(0,64),(1,31),(15,34),(39,24),((1<<64)-32,32)]: print(offset,count,stream(offset,count).hex())
print('data',data.hex()); print('mac',mac.hex()); print('legacy_root',root.hex());print('material',material.hex())
root = bytes(range(96, 160))
material = kdf(root, salt2, b'datapump/cascade/v1/stream/' + (1720000000).to_bytes(8, 'big'), 'sha512')
print('v3 root stream', stream(0, 64).hex())
