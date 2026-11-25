"""Per-branch C/N0 reduction that keeps the single-antenna bound rate at -10 dB-Hz, for 2 and 4
independent or fully correlated receive branches (Gaussian Doppler spectra).

Written by the theory literature reviewer of the Sol-6.2 coherence-limit pass (25 Nov 2026).
Power-equivalent companion to the rate-scaling branch table in ../results/bound_shapes.md.
Run: python3 -I branch_power.py > results/branch_power.txt
"""
import math
def pen(P, sig, n=20001):
    L=12*sig; h=2*L/(n-1); s=0.0
    for i in range(n):
        f=-L+i*h; S=math.exp(-0.5*(f/sig)**2)/(sig*math.sqrt(2*math.pi))
        s+=(0.5 if i in (0,n-1) else 1.0)*math.log1p(P*S)
    return s*h
def rate(P,sig,nr,corr=False):
    if corr: return nr*P - pen(nr*P,sig)   # fully correlated: one fading process, nr-fold SNR
    return nr*(P-pen(P,sig))
P0=0.1
print("Per-branch C/N0 reduction (dB) needed to keep the single-antenna rate at C/N0=-10 dB-Hz")
for B in [0.1,1,10,30]:
    sig=B/2; R0=rate(P0,sig,1); out=[]
    for nr in [2,4]:
        for corr in [False,True]:
            lo,hi=1e-6,P0
            for _ in range(60):
                m=math.sqrt(lo*hi)
                if rate(m,sig,nr,corr)>R0: hi=m
                else: lo=m
            out.append(f"nR={nr} {'corr' if corr else 'indep'}: {10*math.log10(P0/hi):5.2f}")
    print(f"spread {B:5} Hz:", "; ".join(out))
