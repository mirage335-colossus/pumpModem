"""Low-energy-coherence (deflection) losses of unweighted or mis-weighted tone-energy sums.

Written by the computation literature reviewer of the Sol-6.2 coherence-limit pass (25 Nov 2026).
Deflection efficiency eta = (sum w s)^2 / (sum w^2 sum s^2); required-power loss = -5 log10(eta).
Valid for many weak bins; not detection probability at very low false-alarm rates.
Run: python3 -I deflection_losses.py > results/deflection_losses.txt
"""
import math
# LEC deflection efficiency of quadratic detector with weights w vs optimal (w=S)
# eta = (sum w s)^2 / (sum w^2 * sum s^2); required-power loss dB = -5 log10(eta)
def erf(x): return math.erf(x)
# Gaussian spectrum, box of half-width x*sigma (continuous approx)
best=(0,0)
for i in range(1,4000):
    x=i/1000
    eta=math.sqrt(math.pi)*erf(x/math.sqrt(2))**2/x
    if eta>best[0]: best=(eta,x)
print("Gaussian PSD, best box: eta=%.4f halfwidth=%.3f sigma, loss=%.3f dB"%(best[0],best[1],-5*math.log10(best[0])))
for x in (1.0,1.5,2.0,3.0):
    eta=math.sqrt(math.pi)*erf(x/math.sqrt(2))**2/x
    print(" box half-width %.1f sigma: eta=%.4f loss=%.3f dB"%(x,eta,-5*math.log10(eta)))
# Gaussian weights with wrong width ratio r
for r in (2**0.25,2**0.5,2.0,4.0):
    eta=2*r/(1+r*r)
    print("Gaussian weight width mismatch ratio %.3f: eta=%.4f loss=%.3f dB"%(r,eta,-5*math.log10(eta)))
# Flat (rectangular) PSD of width B, box of width W: eta = min(W,B)^2/(W*B)
for q in (1.0,1.5,2.0,4.0):
    eta=1/q
    print("flat PSD, box q times too wide: eta=%.3f loss=%.3f dB"%(eta,-5*math.log10(eta)))
# Jakes (classical) Doppler PSD, unweighted box over full spread: S(f)=1/sqrt(1-f^2) |f|<1 -> sum s^2 diverges (log), compute discretized with N bins across 2fd
for N in (8,16,32,64):
    # bin-integrated Jakes PSD: P_k = (asin(f_{k+1})-asin(f_k))/pi
    edges=[-1+2*k/N for k in range(N+1)]
    P=[(math.asin(edges[k+1])-math.asin(edges[k]))/math.pi for k in range(N)]
    s2=sum(p*p for p in P)
    eta=(sum(P))**2/(N*s2)
    print("Jakes PSD over %d bins, unweighted: eta=%.3f loss=%.3f dB"%(N,eta,-5*math.log10(eta)))
