"""Quantization efficiency of Gaussian input, DFT-matrix quantization error, and losses with a strong
narrowband interferer before channel isolation, for NVFP4 (E2M1 with E4M3 block scale), INT4 and INT8.

Written by the computation literature reviewer of the Sol-6.2 coherence-limit pass (25 Nov 2026).
Approximate: quantization error is treated as white noise; real errors include spurs.
Run: python3 -I quantization_losses.py > results/quantization_losses.txt   (about a minute)
"""
import math, random
random.seed(1)
E2M1=[0,0.5,1,1.5,2,3,4,6]
def q_e2m1(v):
    s=-1 if v<0 else 1; a=abs(v)
    best=min(E2M1,key=lambda e:abs(e-a)); return s*best
def e4m3(v):
    # round positive v to E4M3 (bias 7, 3 mantissa bits), normals only approx
    if v==0: return 0
    e=math.floor(math.log2(v)); m=v/2**e
    m=round(m*8)/8
    return m*2**e
def nvfp4_block(xs,scale_mode="e4m3"):
    amax=max(abs(x) for x in xs)
    if amax==0: return [0]*len(xs)
    sc=amax/6
    if scale_mode=="e4m3": sc=e4m3(sc)
    return [q_e2m1(x/sc)*sc for x in xs]
def int_sym(xs,bits,clip=None):
    L=2**(bits-1)-1
    amax=clip if clip else max(abs(x) for x in xs)
    if amax==0: return [0]*len(xs)
    sc=amax/L
    return [max(-L,min(L,round(x/sc)))*sc for x in xs]
def eff(x,q):
    sxq=sum(a*b for a,b in zip(x,q)); sxx=sum(a*a for a in x); sqq=sum(b*b for b in q)
    return sxq*sxq/(sxx*sqq)
N=16*20000
x=[random.gauss(0,1) for _ in range(N)]
# Gaussian input: NVFP4 block16
q=[]
for i in range(0,N,16): q+=nvfp4_block(x[i:i+16])
print("Gaussian, NVFP4 block16 absmax: rho^2=%.4f"%eff(x,q))
q=[]
for i in range(0,N,32): q+=nvfp4_block(x[i:i+32],scale_mode="pow2") 
print("Gaussian, FP4 block32 (non-quantized scale approx MXFP4-like): rho^2=%.4f"%eff(x,q))
# INT4 symmetric per-block absmax 16
q=[]
for i in range(0,N,16): q+=int_sym(x[i:i+16],4)
print("Gaussian, INT4 sym block16 absmax: rho^2=%.4f"%eff(x,q))
# INT4 global optimal clip ~ 2.5 sigma? search
for c in (1.5,2.0,2.5,3.0):
    q=int_sym(x,4,clip=c); print(" INT4 sym global clip %.1f sigma: rho^2=%.4f"%(c,eff(x,q)))
for c in (3.0,4.0):
    q=int_sym(x,8,clip=c); print(" INT8 sym global clip %.1f sigma: rho^2=%.5f"%(c,eff(x,q)))
# DFT matrix quantization: relative error energy of quantized cos/sin rows for N=256
Nd=256
def rel_err(quant):
    num=0;den=0
    for k in range(0,Nd,7):
        row=[math.cos(2*math.pi*k*n/Nd) for n in range(Nd)]+[math.sin(2*math.pi*k*n/Nd) for n in range(Nd)]
        qr=[]
        for i in range(0,len(row),16): qr+=quant(row[i:i+16])
        num+=sum((a-b)**2 for a,b in zip(row,qr)); den+=sum(a*a for a in row)
    return num/den
print("DFT matrix N=256 relative error power: NVFP4 %.2e (%.1f dB), INT4 blk %.2e (%.1f dB), INT8 blk %.2e (%.1f dB)"%(
  rel_err(nvfp4_block),10*math.log10(rel_err(nvfp4_block)),
  rel_err(lambda b:int_sym(b,4)),10*math.log10(rel_err(lambda b:int_sym(b,4))),
  rel_err(lambda b:int_sym(b,8,clip=1.0)),10*math.log10(rel_err(lambda b:int_sym(b,8,clip=1.0)))))
# fp16 twiddle
def fp16(v):
    if v==0: return 0
    e=math.floor(math.log2(abs(v))); m=v/2**e; m=round(m*1024)/1024; return m*2**e
print("DFT matrix FP16 rel err power %.1f dB"%(10*math.log10(rel_err(lambda b:[fp16(v) for v in b]))))
print("--- strong narrowband interferer + unit noise; quantization error power relative to noise ---")
M=16*8000
noise=[random.gauss(0,1) for _ in range(M)]
for inr_db in (0,10,20,30,40):
    A=math.sqrt(2*10**(inr_db/10))
    xs=[noise[i]+A*math.cos(0.7316*i) for i in range(M)]
    res=[]
    for name,qf in (("NVFP4/16",lambda b:nvfp4_block(b)),("INT4/16",lambda b:int_sym(b,4)),("INT8/16",lambda b:int_sym(b,8))):
        q=[]
        for i in range(0,M,16): q+=qf(xs[i:i+16])
        err=sum((a-b)**2 for a,b in zip(xs,q))/M
        res.append("%s err/noise=%.3g (loss %.2f dB)"%(name,err,10*math.log10(1+err)))
    print("INR %2d dB: "%inr_db+"; ".join(res))
