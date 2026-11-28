# Statistical price of searching: energy needed against the number of hypotheses, and the loss of adding M segments as energies.
# Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
# this header. Theoretical; assumptions are in the code and in the review that quotes it:
# _ref/algorithms-matrixModem/Fable-5.2/reviews/review-long-coherence-search.md (sections 1.3 and 4)
# Run: python3 -I trials_cost.py > results/trials_cost.txt

import math
def logsumexp(v):
    m=max(v); return m+math.log(sum(math.exp(x-m) for x in v))
def log_Q(m,y):   # log upper regularized gamma Q(m,y), integer m>=1 : e^-y sum_{i<m} y^i/i!
    return -y + logsumexp([i*math.log(y)-math.lgamma(i+1) for i in range(m)])
def P_low(m,y):   # lower regularized gamma P(m,y) = e^-y sum_{i>=m} y^i/i!
    terms=[]; i=m
    while True:
        t=-y+i*math.log(y)-math.lgamma(i+1); terms.append(t); i+=1
        if i>y+m and t<max(terms)-60: break
    return math.exp(logsumexp(terms))
def thr(M,p):     # y=x/2 threshold with Q(M,y)=p
    lo,hi=1e-9,2000.0; lp=math.log(p)
    for _ in range(200):
        mid=0.5*(lo+hi)
        if log_Q(M,mid)>lp: lo=mid
        else: hi=mid
    return 0.5*(lo+hi)
def miss(M,y,lam): # P(noncentral chi2_{2M}(lam) <= 2y) ; lam=2E/N0
    a=lam/2; js=[]; tot=0.0
    j0=int(a); 
    for j in range(max(0,j0-int(12*math.sqrt(a+1))-5), j0+int(12*math.sqrt(a+1))+30):
        w=-a+(j*math.log(a) if a>0 else (0 if j==0 else -1e300))-math.lgamma(j+1)
        tot+=math.exp(w)*P_low(M+j,y)
    return tot
def req(M,p,pm):
    y=thr(M,p); lo,hi=0.0,5000.0
    for _ in range(80):
        mid=0.5*(lo+hi)
        if miss(M,y,mid)>pm: lo=mid
        else: hi=mid
    return y,0.5*(lo+hi)
PFA=1e-6; PM=1e-3
print("Coherent (M=1), P_FA=1e-6 overall, P_miss=1e-3")
base=None
for Nh in [1,1e3,1e6,1e9,1e12,1.7e7,1.7e7*1e3,1.7e7*1e6,1.7e7*1e9]:
    y,lam=req(1,PFA/Nh,PM); e=lam/2
    if base is None: base=e
    print(f"  N_h={Nh:9.3g}  P_th={y:6.2f}  E/N0={e:7.2f} ({10*math.log10(e):5.2f} dB)  +{10*math.log10(e/base):4.2f} dB vs 1 trial")
print("Same at P_FA=1e-6, P_miss=0.1 (90% detection)")
base=None
for Nh in [1,1e3,1e6,1e9]:
    y,lam=req(1,PFA/Nh,0.1); e=lam/2
    if base is None: base=e
    print(f"  N_h={Nh:9.3g}  E/N0={e:7.2f} ({10*math.log10(e):5.2f} dB)  +{10*math.log10(e/base):4.2f} dB")
print("Non-coherent sum of M segments (total E/N0 required), per-trial p=1e-6/N_h, P_miss=1e-3")
for Nh in [1,1e6,1e9]:
    b=None
    for M in [1,2,4,8,16,64,256,1024]:
        y,lam=req(M,PFA/Nh,PM); e=lam/2
        if b is None: b=e
        print(f"  N_h={Nh:7.1g} M={M:5d}  E_tot/N0={e:8.2f} ({10*math.log10(e):5.2f} dB) loss vs coherent {10*math.log10(e/b):5.2f} dB ; 5log10(M)={5*math.log10(M):5.2f}; alpha_eff={1-math.log10(e/b)/math.log10(M) if M>1 else 1:5.3f}")
