import ctypes as ct, ctypes.util, hashlib, hmac, struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def hkdf(ikm,salt,info,n,alg):
    prk=hmac.new(salt,ikm,alg).digest(); t=b''; out=b''; i=0
    while len(out)<n:
        i+=1; t=hmac.new(prk,t+info+bytes([i]),alg).digest(); out+=t
    return out[:n]
U64=lambda n:struct.pack('>Q',n)
S256=b'datapump/v1/hkdf-sha256'; S512=b'datapump/cascade/v2/hkdf-sha512'
master=bytes(range(32))
data=hkdf(master,S256,b'datapump/v1/data',32,'sha256')
mac=hkdf(master,S256,b'datapump/v1/mac',32,'sha256')
serpent_root=bytes(range(64,128))
aes_root=master*2
context=b'datapump/helper-aux/v4/confidentiality'
child_s=hkdf(serpent_root,S512,b'datapump/cascade/v2/serpent/child/'+U64(len(context))+context+U64(0),64,'sha512')
child_a=hkdf(aes_root,S512,b'datapump/cascade/v2/aes/child/'+U64(len(context))+context+U64(0),64,'sha512')
lib=ct.CDLL(ctypes.util.find_library('gcrypt'));lib.gcry_check_version.restype=ct.c_char_p; assert lib.gcry_check_version(None)
lib.gcry_cipher_map_name.argtypes=[ct.c_char_p];lib.gcry_cipher_map_name.restype=ct.c_int
lib.gcry_cipher_open.argtypes=[ct.POINTER(ct.c_void_p),ct.c_int,ct.c_int,ct.c_uint]
lib.gcry_cipher_setkey.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_size_t]
lib.gcry_cipher_encrypt.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_size_t,ct.c_void_p,ct.c_size_t]
lib.gcry_cipher_close.argtypes=[ct.c_void_p]
def serpent(key,block):
    handle=ct.c_void_p(); assert not lib.gcry_cipher_open(ct.byref(handle),lib.gcry_cipher_map_name(b'SERPENT256'),1,0)
    k=ct.create_string_buffer(key); x=ct.create_string_buffer(block); out=ct.create_string_buffer(16)
    assert not lib.gcry_cipher_setkey(handle,k,len(key));assert not lib.gcry_cipher_encrypt(handle,out,16,x,16)
    lib.gcry_cipher_close(handle); return out.raw

def aes_ctr(key,iv,data): return Cipher(algorithms.AES(key),modes.CTR(iv)).encryptor().update(data)
def pack(text):
    bits=''.join(f'{ord(c):07b}' for c in text.ljust(16,'\0'))
    return int(bits,2).to_bytes(14,'big')
def symbols(ticks,body):
    bits=''.join(f'{v:08b}' for v in body)
    return [(ticks%532480)*2//65]+[int(bits[i:i+14],2) for i in range(0,112,14)]
plain=pack('N0CALL')
old_expected=[0,1491,5608,132,9865,9169,8719,2903,15422]
oldkey=hmac.new(mac,b'datapump/helper-aux/v3/ctr-key','sha256').digest()
assert symbols(0,aes_ctr(oldkey,bytes(16),plain))==old_expected
for dwell,ticks in [(0,0),(14,367002),(15,393216),(162,4246733),(4500000000,117964800000000)]:
    serpent_key=hkdf(child_s,S512,b'datapump/cascade/v2/serpent/stream/'+U64(dwell),64,'sha512')[:32]
    aes_key=hkdf(child_a,S512,b'datapump/cascade/v2/aes/stream/'+U64(dwell),64,'sha512')[:32]
    s=serpent(serpent_key,b'data-s02'+U64(0))[:14]
    a=aes_ctr(aes_key,b'data-a02'+U64(0),bytes(14))
    out=bytes(x^y^z for x,y,z in zip(plain,s,a))
    print(dwell,ticks,','.join(map(str,symbols(ticks,out))),out.hex())
print('legacy independent control passed; libgcrypt',lib.gcry_check_version(None).decode())
