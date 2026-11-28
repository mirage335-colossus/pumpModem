# Coherent FSK: tone correlation against spacing, M/(M-1) penalty, coherent gain against window length for Lorentzian and Gaussian spectra, optimum coherent window.
# Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
# this header. Theoretical; assumptions are in the code and in the review that quotes it:
# _ref/algorithms-matrixModem/Fable-5.2/reviews/review-coherent-fsk-rate-spacing.md (sections 1 to 3)
# Run: python3 -I coherent_fsk_calc.py > results/coherent_fsk_calc.txt

import math
# 1. coherent binary FSK correlation Re rho = sin(2 pi h)/(2 pi h)
best=None
for i in range(1,20001):
    h=i/10000.0
    r=math.sin(2*math.pi*h)/(2*math.pi*h)
    if best is None or r<best[1]: best=(h,r)
print("min Re rho at h=%.4f rho=%.4f gain over orth %.3f dB, vs antipodal %.3f dB"%(best[0],best[1],10*math.log10(1-best[1]),10*math.log10((1-best[1])/2)))
for h in (0.25,0.5,0.715,0.75,1.0):
    re=math.sin(2*math.pi*h)/(2*math.pi*h); ab=abs(math.sin(math.pi*h)/(math.pi*h))
    print("h=%.3f Re rho=%.4f |rho|=%.4f  coherent d2/2E=%.4f (%.2f dB vs orth)  noncoh |rho|^2=%.3f"%(h,re,ab,1-re,10*math.log10(1-re),ab*ab))
# 2. simplex-to-orthogonal penalty
for M in (2,4,8,16,32,64,256):
    print("M=%d penalty %.3f dB; brickwall Eb/N0 %.3f dB; lowSNR noncoh fraction coefficient (M-1)/(2M)=%.4f"%(M,10*math.log10(M/(M-1)),10*math.log10(M*math.log(2)/(M-1)),(M-1)/(2*M)))
# 3. coherent integration gain: Lorentzian (Wiener) linewidth dnu (FWHM): R(tau)=exp(-pi dnu |tau|)
def gl(x): # x = dnu*T
    a=math.pi*x
    return 2*(a-1+math.exp(-a))/a/a
print("Wiener/Lorentzian: dnu*T, gain dB")
for x in (0.01,0.02,0.05,0.1,0.2,0.3,0.5,1,2,5):
    print("  %.2f  %.3f dB"%(x,10*math.log10(gl(x))))
# Gaussian Doppler spectrum rms sigma: R=exp(-2 pi^2 sigma^2 tau^2)
def gg(s): # s = sigma*T
    b=math.pi*s*math.sqrt(2)
    return math.sqrt(math.pi)*math.erf(b)/b-(1-math.exp(-b*b))/(b*b)
print("Gaussian: sigma*T, FWHM*T, gain dB")
for s in (0.02,0.053,0.1,0.15,0.2,0.25,0.3,0.4,0.425,0.5,0.75,1.0):
    print("  %.3f  %.3f  %.3f dB"%(s,2.3548*s,10*math.log10(gg(s))))
# 4. optimum window for Wiener: maximise post-detection SNR proxy for noncoherent combining of K=Ttot/T windows
#   deflection per window ~ (g*rho)^2/(1+2 g rho) with rho=C/N0*T ; total ~ (Ttot/T)* that
print("optimal coherent window (Wiener linewidth dnu) for given C/N0: maximise (g*r)^2/(1+2 g r)/T")
for cn0_db in (-10,-20,-30):
    cn0=10**(cn0_db/10)
    for dnu in (0.001,0.003,0.01):
        best=None
        T=1.0
        while T<1e6:
            g=gl(dnu*T); r=cn0*T
            d=(g*r)**2/(1+2*g*r)/T
            if best is None or d>best[1]: best=(T,d,g,r)
            T*=1.02
        T,d,g,r=best
        # compare with coherent ideal rate: per unit time deflection = cn0/2 at high window SNR -> efficiency
        print("  C/N0=%d dBHz dnu=%.3f Hz: Topt=%.0f s (dnu*T=%.2f) window SNR g*r=%.2f (%.1f dB), coherent loss %.2f dB, efficiency vs fully coherent %.1f dB"%(cn0_db,dnu,T,dnu*T,g*r,10*math.log10(g*r),10*math.log10(g),10*math.log10(d/(cn0/2))))
