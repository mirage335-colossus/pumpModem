import hashlib,struct
MASK=(1<<64)-1

def splitmix(seed, count):
    out=b''
    while len(out)<count:
        seed=(seed+0x9e3779b97f4a7c15)&MASK
        z=seed
        z=((z^(z>>30))*0xbf58476d1ce4e5b9)&MASK
        z=((z^(z>>27))*0x94d049bb133111eb)&MASK
        z^=z>>31
        out+=z.to_bytes(8,'little')
    return out[:count]

def multiply(a,b):
    out=0
    while b:
        if b&1:out^=a
        b>>=1;a<<=1
        if a&0x10000:a^=0x1100b
    return out

def outer(data):
    words=[int.from_bytes(data[i:i+2],'big') for i in range(0,len(data),2)]
    work=words+[0,0]
    for i in range(len(words)):
        factor=work[i]
        work[i+1]^=multiply(factor,3)
        work[i+2]^=multiply(factor,2)
    return b''.join(w.to_bytes(2,'big') for w in words+work[-2:])

def code(data,mask):
    state=0;phase=0;bits=[]
    incoming=[int(b) for byte in data for b in f'{byte:08b}']+[0]*6
    for bit in incoming:
        state=(state<<1)|bit
        for generator in [0o171,0o133]:
            if mask[phase%len(mask)]:bits.append((state&generator).bit_count()%2)
            phase+=1
        state&=63
    return bits

def cycle(data,rate,ordinal):
    bits=code(outer(data),[1,1] if rate==0 else [1,1,1,0,0,1])
    print('codedbits',len(bits))
    bits+=[0]*(2048-len(bits))
    whitening=splitmix((0x44504d2f76322f77+ordinal*0xd1342543de82ef95)&MASK,256)
    return bytes(bit^((whitening[i//8]>>(i%8))&1) for i,bit in enumerate(bits))

for rate in [0,1]:
    u=lambda n:n.to_bytes(8,'big')
    profile=b'datapump/fast/v2/compact/v1'+b''.join(u(n) for n in [4,4,rate,0,1])+b'/short/v1'+u(16200)+u(16)+struct.pack('>ddd',2000.,4000.,.2)+u(1)+u(32)
    salt=splitmix(0x636f6d7061637421,32)
    size=122 if rate==0 else 186
    boot=salt+hashlib.sha256(b'DataPump/fast/capacity/v2/public/bootstrap'+profile+salt).digest()
    boot+=bytes(size-len(boot))
    source=b'\x01\0\x80\xff\0\x80';source+=bytes(size-32-len(source))
    area=source+hashlib.sha256(b'DataPump/fast/capacity/v2/public/group'+profile+salt+u(0)+source).digest()
    wire=cycle(boot,rate,0)+cycle(area,rate,1)
    print('rate',rate,'context',hashlib.sha256(profile).hexdigest(),'wire',hashlib.sha256(wire).hexdigest())
