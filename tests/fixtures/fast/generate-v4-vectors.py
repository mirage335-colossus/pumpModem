#!/usr/bin/env python3
# Independent Fast-v4 fixture oracle: Python hashlib/HMAC RFC5869, libgcrypt
# Serpent-256-CBC, cryptography AES-CBC and independent GF(256)/K7 coding.
# Synthetic roots only; never use this deterministic generator for keyfiles.
import ctypes as ct, ctypes.util, hashlib, hmac, struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def h(key, data, alg='sha256'): return hmac.new(key, data, alg).digest()
def hkdf(ikm, salt, info, n, alg):
    prk=h(salt,ikm,alg); out=b''; block=b''; index=0
    while len(out)<n:
        index+=1; block=h(prk,block+info+bytes([index]),alg); out+=block
    return out[:n]
S256=b'datapump/v1/hkdf-sha256'; S512=b'datapump/cascade/v2/hkdf-sha512'
master=bytes(range(32)); data=hkdf(master,S256,b'datapump/v1/data',32,'sha256'); mac=hkdf(master,S256,b'datapump/v1/mac',32,'sha256')
serpent_root=bytes(range(128,192)); aes_root=bytes(range(64))
serpent_auth=hkdf(serpent_root,S512,b'datapump/cascade/v2/serpent/auth/',32,'sha512')
aes_auth=hkdf(aes_root,S512,b'datapump/cascade/v2/aes/auth/',32,'sha512')
def confidentiality_mac(message):
    return bytes(a^b for a,b in zip(h(serpent_auth,message),h(aes_auth,message)))
U64=lambda n:struct.pack('>Q',n)
def profile(depth):
    return b'datapump/fast/v1'+b''.join(U64(x) for x in (0,16,0,1,depth))+struct.pack('>ddd',15000.,9300.,.2)
def gcrypt_serpent(key,iv,data):
    lib=ct.CDLL(ctypes.util.find_library('gcrypt')); lib.gcry_check_version.restype=ct.c_char_p; assert lib.gcry_check_version(None)
    lib.gcry_cipher_map_name.argtypes=[ct.c_char_p]; lib.gcry_cipher_map_name.restype=ct.c_int
    alg=lib.gcry_cipher_map_name(b'SERPENT256'); assert alg
    handle=ct.c_void_p()
    lib.gcry_cipher_open.argtypes=[ct.POINTER(ct.c_void_p),ct.c_int,ct.c_int,ct.c_uint]
    lib.gcry_cipher_setkey.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_size_t]
    lib.gcry_cipher_setiv.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_size_t]
    lib.gcry_cipher_encrypt.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_size_t,ct.c_void_p,ct.c_size_t]
    lib.gcry_cipher_close.argtypes=[ct.c_void_p]
    assert not lib.gcry_cipher_open(ct.byref(handle),alg,3,0)
    keybuf=ct.create_string_buffer(key); ivbuf=ct.create_string_buffer(iv); inp=ct.create_string_buffer(data); out=ct.create_string_buffer(len(data))
    assert not lib.gcry_cipher_setkey(handle,keybuf,len(key))
    assert not lib.gcry_cipher_setiv(handle,ivbuf,len(iv))
    assert not lib.gcry_cipher_encrypt(handle,out,len(data),inp,len(data))
    lib.gcry_cipher_close(handle); return out.raw

def aes_cbc(key,iv,data): return Cipher(algorithms.AES(key),modes.CBC(iv)).encryptor().update(data)
def oldkeys(p,salt):
    prk=h(salt,h(mac,b'DataPump/fast/v1/root'))
    return h(prk,b'DataPump/fast/v1/AES-256-CBC'+p+b'\x01'),h(prk,b'DataPump/fast/v1/HMAC-SHA256'+p+b'\x01')
def seal(p,salt,ordinal,iv,plain,old=False):
    if old:
        enc,auth=oldkeys(p,salt); body=iv+aes_cbc(enc,iv,plain); domain=b'DataPump/fast/v1/group'
    else:
        context=b'DataPump/fast/v1/serpent-aes-v4/cbc'+p
        suffix=U64(len(context))+context+U64(len(salt))+salt
        sk=hkdf(serpent_root,S512,b'datapump/cascade/v2/serpent/cbc/'+suffix,32,'sha512')
        ak=hkdf(aes_root,S512,b'datapump/cascade/v2/aes/cbc/'+suffix,32,'sha512')
        siv=hkdf(sk,S512,b'datapump/cascade/v2/cbc/serpent-iv/'+iv,16,'sha512')
        cipher=aes_cbc(ak,iv,gcrypt_serpent(sk,siv,plain))
        auth=h(h(salt,confidentiality_mac(b'DataPump/fast/v1/serpent-aes-v4/root')),b'DataPump/fast/v1/HMAC-SHA256'+p+b'\x01')
        body=iv+cipher; domain=b'DataPump/fast/v1/serpent-aes-v4/group'
    return body+h(auth,domain+p+salt+U64(ordinal)+body)
# Multiply field elements using polynomial reduction directly (no log tables).
def gf(a,b):
    z=0
    while b:
        if b&1:z^=a
        a<<=1
        if a&256:a^=0x11d
        b>>=1
    return z
def rs(data,parity=16):
    poly=[1]; root=1
    for _ in range(parity):
        nxt=[0]*(len(poly)+1)
        for i,c in enumerate(poly):nxt[i]^=c;nxt[i+1]^=gf(c,root)
        poly=nxt;root=gf(root,2)
    work=list(data)+[0]*parity
    for i in range(len(data)):
        c=work[i]
        for j in range(1,len(poly)):work[i+j]^=gf(poly[j],c)
    return bytes(data)+bytes(work[-parity:])
def coded_cycle(systematic):
    assert len(systematic)==224
    rows=[rs(systematic[:112]),rs(systematic[112:])]
    interleaved=bytes(rows[r][column] for column in range(128) for r in range(2))
    state=0; bits=[]
    for bit in [((v>>s)&1) for v in interleaved for s in range(7,-1,-1)]+[0]*6:
        state=((state<<1)|bit)&127
        bits.extend((state&g).bit_count()&1 for g in (0o171,0o133))
    bits.extend([0]*((-len(bits))%2048));return bytes(bits)
salt=bytes(range(32,64));iv=bytes(range(16)); p16=profile(16); p1=profile(1);plain=bytes(range(176))
# Cross-check independent oracle with pre-upgrade group frozen in test.
old=seal(p16,salt,7,iv,plain,True)
expected=('000102030405060708090a0b0c0d0e0f1e03c7f5409c338d0198fd87e3fffc4691e3b226d5c20647f492e61bd1b833a5'
'1ecfb2010b4917049caec766959b2c3d5b2eb9ab03ce8caec1f67f5dda0d4dabb5f1b1e9645fa737da7e4809f01d9165'
'8d43c52c440eb2a6073dd344e2e30582d8f22b0c79ff98200e5230b67707c79e38920bc6cbbb907444df1f55539556db69'
'7bda7137b963b932229faad89a5486185e22bec842ae8138a56c41d8d4bd85f828a6f048fa25d698e9c0295e75cc294965e'
'8c7d6c8815c1f3001358aa670dcd4beda505f2f8769fb5af14116fd25b1')
assert old.hex()==expected, 'legacy independent group control failed'
# Reproduce the pre-v4 shared-root cascade group as a further cipher/KDF
# control. This is an obsolete format and is not a valid v4 encrypted group.
old_salt=b'datapump/cascade/v1/hkdf-sha512'
old_root=hkdf(data+mac,old_salt,b'datapump/cascade/v1/legacy-root',64,'sha512')
old_context=b'DataPump/fast/v1/serpent-aes-cbc/v1'+p16
old_suffix=U64(len(old_context))+old_context+U64(len(salt))+salt
old_material=hkdf(old_root,old_salt,b'datapump/cascade/v1/cbc/'+old_suffix,64,'sha512')
old_sk,old_ak=old_material[:32],old_material[32:]
old_iv=hkdf(old_sk,old_salt,b'datapump/cascade/v1/cbc/serpent-iv/'+iv,16,'sha512')
old_body=iv+aes_cbc(old_ak,iv,gcrypt_serpent(old_sk,old_iv,plain))
old_auth=h(h(salt,h(mac,b'DataPump/fast/v1/serpent-aes/root')),b'DataPump/fast/v1/HMAC-SHA256'+p16+b'\x01')
old_group=old_body+h(old_auth,b'DataPump/fast/v1/serpent-aes/group'+p16+salt+U64(7)+old_body)
assert old_group.hex()==(
    '000102030405060708090a0b0c0d0e0f16360a0b50ac0054bc0d09be5752d9c396f192763c6bcb1dddb313b54430bfa6'
    '7ade3c569bc97161c14bce9b8c755297a3cc55f55486487cde5d285ca77aa6c10554940e8acd3feb4fac12703088e67a'
    '44ac4188fa483e325280a674fd6702c9b024a09e805e7c5def7b84448a39d3a7e702e7802fe8710aa1c9b305503fa06'
    '57394f1e27c355e21b66c6b6e74b5e6cd328f6c466b16b6d148dfb6b4bae46018a6f392d84a864b3c1e1b57f8af28c'
    '817f85d9be0f529d8576c83107db53472defbca4b69258eb6a68814413aff8e2ba7'), 'old cascade independent control failed'
# Independently reproduce public bootstrap and wire fingerprints to control RS/trellis.
source=bytes([0,255,66]); srcbits=[b for v in source for b in [1]+[(v>>s)&1 for s in range(7,-1,-1)]]
def area(size):
    out=bytearray(size)
    for i,b in enumerate(srcbits):out[i//8]|=b<<(7-i%8)
    return bytes(out)
pubboot=salt+hashlib.sha256(b'DataPump/fast/v1/public/bootstrap'+p1+salt).digest();pubboot=pubboot.ljust(224,b'\0')
pubplain=area(192);pubgroup=pubplain+hashlib.sha256(b'DataPump/fast/v1/public/group'+p1+salt+U64(0)+pubplain).digest()
assert hashlib.sha256(coded_cycle(pubboot)+coded_cycle(pubgroup)).hexdigest()=='82a64d0004b9588298479e8901f8bcd83008ec16644219d6ca6e11a09999c9fa','public wire oracle control failed'
boot=salt+confidentiality_mac(b'DataPump/fast/v1/serpent-aes-v4/bootstrap'+p1+salt);boot=boot.ljust(224,b'\0')
newgroup=seal(p16,salt,7,iv,plain)
wire=coded_cycle(boot)+coded_cycle(seal(p1,salt,0,iv,area(176)))
print('GROUP',newgroup.hex())
print('WIRE_SHA256',hashlib.sha256(wire).hexdigest())
print('controls: prior AES group, prior cascade and independent public wire passed')
# Frozen capacity v2 group: default wire profile and independently calculated
# fixed geometry, including its canonical zero alignment fill.
capinfo=4*57600//8; capsymbols=capinfo//2; capparity=2*((3*capsymbols+2005)//2006)
caparea=capinfo-2*capparity; capsiz=((caparea-48)//16)*16
p2=b'datapump/fast/v2'+b''.join(U64(x) for x in (0,4194304,4,0,4))+struct.pack('>ddd',18000/1.02,9300.,.02)+U64(16)+U64(256)
capcontext=b'DataPump/fast/capacity/v2/serpent-aes-v4/cbc'+p2
capsuffix=U64(len(capcontext))+capcontext+U64(len(salt))+salt
capsk=hkdf(serpent_root,S512,b'datapump/cascade/v2/serpent/cbc/'+capsuffix,32,'sha512')
capak=hkdf(aes_root,S512,b'datapump/cascade/v2/aes/cbc/'+capsuffix,32,'sha512')
capsiv=hkdf(capsk,S512,b'datapump/cascade/v2/cbc/serpent-iv/'+iv,16,'sha512')
capplain=bytes(i%256 for i in range(capsiz)); capcipher=aes_cbc(capak,iv,gcrypt_serpent(capsk,capsiv,capplain))
capauth=h(h(salt,confidentiality_mac(b'DataPump/fast/capacity/v2/serpent-aes-v4/root')),b'DataPump/fast/capacity/v2/HMAC-SHA256'+p2+b'\x01')
capbody=iv+capcipher
captag=h(capauth,b'DataPump/fast/capacity/v2/serpent-aes-v4/group'+p2+salt+U64(7)+capbody)
capgroup=(capbody+captag).ljust(caparea,b'\0')
print('CAPACITY_INFO',capinfo,'PARITY_SYMBOLS',capparity,'AREA',caparea,'PLAINTEXT',capsiz,'FILL',len(capgroup)-len(capbody)-len(captag))
print('CAPACITY_GROUP_SHA256',hashlib.sha256(capgroup).hexdigest())
# Synthetic pre-v4 capacity failures: correctly authenticated old suite
# records must not pass v4's new group/bootstrap domain verification.
old_capacity_auth=h(h(salt,h(mac,b'DataPump/fast/capacity/v2/serpent-aes/root')),b'DataPump/fast/capacity/v2/HMAC-SHA256'+p2+b'\x01')
old_capacity_body=iv+bytes(capsiz)
old_capacity_tag=h(old_capacity_auth,b'DataPump/fast/capacity/v2/serpent-aes/group'+p2+salt+U64(7)+old_capacity_body)
old_capacity_bootstrap=h(mac,b'DataPump/fast/capacity/v2/serpent-aes/bootstrap'+p2+salt)
print('OLD_CAPACITY_GROUP_TAG',old_capacity_tag.hex())
print('OLD_CAPACITY_BOOTSTRAP_TAG',old_capacity_bootstrap.hex())
# One-lane forgeries: a party that knows only either auth lane cannot replace
# the XOR-of-two-lane bootstrap or derive its group key alone.
root_label=b'DataPump/fast/v1/serpent-aes-v4/root'
boot_message=b'DataPump/fast/v1/serpent-aes-v4/bootstrap'+p1+salt
new_body=newgroup[:-32]
group_message=b'DataPump/fast/v1/serpent-aes-v4/group'+p16+salt+U64(7)+new_body
for name,lane in [('SERPENT',serpent_auth),('AES',aes_auth)]:
    lane_base=h(lane,root_label)
    lane_group_key=h(h(salt,lane_base),b'DataPump/fast/v1/HMAC-SHA256'+p16+b'\x01')
    print('ONE_LANE_'+name+'_GROUP_TAG',h(lane_group_key,group_message).hex())
    print('ONE_LANE_'+name+'_BOOTSTRAP_TAG',h(lane,boot_message).hex())
