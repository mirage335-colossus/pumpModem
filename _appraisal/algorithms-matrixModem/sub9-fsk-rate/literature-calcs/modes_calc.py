# Waveguide mode arithmetic for 3 to 10 kHz: cut-off ladders, ideal phase and group velocity, differential group delay, hop delays, interference distance.
# Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
# this header. Theoretical; assumptions are in the code and in the review that quotes it:
# _ref/algorithms-matrixModem/Fable-5.2/reviews/review-sub9khz-phase-stability.md (appendix C)
# Run: python3 -I modes_calc.py > results/modes_calc.txt

import math
c=299792.458 # km/s
a=6371.0
US_PER_MM = 1e6/c*1000.0  # microseconds per Mm at c  (1000 km / c)
def fc(n_eff,h): return n_eff*c/(2*h)/1000.0  # kHz
print("us per Mm at c: %.1f"%US_PER_MM)
print("\n== cutoff ladders (kHz) ==")
for h in (70,74,85,87,90):
    ints=[fc(n,h) for n in range(1,7)]
    half=[fc(n-0.5,h) for n in range(1,7)]
    print("h=%d  n*c/2h: %s"%(h," ".join("%.2f"%x for x in ints)))
    print("      (n-1/2)c/2h: %s"%(" ".join("%.2f"%x for x in half)))
def vg(f,fcut):
    return math.sqrt(1-(fcut/f)**2) if f>fcut else float('nan')
def slow(f,fcut): # group slowness c/vg
    return 1/vg(f,fcut)
freqs=(2.97,3.0,5.0,5.17,8.0,8.27,10.0)
print("\n== ideal vp/c, vg/c, excess group delay (us/Mm) ==")
for h in (70,74,85,87,90):
    for rule,off in (("int",0.0),("half",0.5)):
        for n in range(0 if rule=="int" else 1,6):
            if rule=="int" and n==0:
                continue
            fcut=fc(n-off,h)
            row=[]
            for f in freqs:
                if f>fcut:
                    g=vg(f,fcut); row.append("%5.2f:%.4f/%.4f/%6.0f"%(f,1/g,g,(1/g-1)*US_PER_MM))
                else: row.append("%5.2f:cutoff"%f)
            print("h=%d %s n=%d fc=%.3f | %s"%(h,rule,n,fcut," | ".join(row)))
print("\n== differential group delay between ideal modes (us/Mm) ==")
def dgd(f,fa,fb):
    if f<=fa or f<=fb: return float('nan')
    return (slow(f,fb)-slow(f,fa))*US_PER_MM
for h in (70,74,85,87,90):
    for f in freqs:
        # half rule 1-2, 2-3 ; int rule TEM-1, 1-2, 2-3
        h12=dgd(f,fc(0.5,h),fc(1.5,h)); h23=dgd(f,fc(1.5,h),fc(2.5,h))
        i01=(slow(f,fc(1,h))-1)*US_PER_MM if f>fc(1,h) else float('nan')
        i12=dgd(f,fc(1,h),fc(2,h)); i23=dgd(f,fc(2,h),fc(3,h))
        print("h=%d f=%.2f  half:1-2 %7.0f  2-3 %7.0f | int: TEM-1 %7.0f  1-2 %7.0f  2-3 %7.0f"%(h,f,h12,h23,i01,i12,i23))
print("\n== intra-mode dispersion d(tau)/df (us/Mm per 100 Hz) ==")
def disp(f,fcut):
    if f<=fcut: return float('nan')
    # d/df of 1/sqrt(1-fc^2/f^2) = -fc^2/(f^3 (1-fc^2/f^2)^1.5)
    return -(fcut**2)/(f**3*(1-(fcut/f)**2)**1.5)*US_PER_MM*0.1  # per 0.1 kHz
for h in (70,87,90):
    for f in freqs:
        print("h=%d f=%.2f half n=1: %8.2f n=2: %8.2f n=3: %8.2f | int n=1: %8.2f n=2: %8.2f n=3: %8.2f n=4 %8.2f n=5 %8.2f"%(h,f,disp(f,fc(0.5,h)),disp(f,fc(1.5,h)),disp(f,fc(2.5,h)),disp(f,fc(1,h)),disp(f,fc(2,h)),disp(f,fc(3,h)),disp(f,fc(4,h)),disp(f,fc(5,h))))
print("\n== interference distance D = lambda/|S1-S2| (km), S=c/vp=sqrt(1-fc^2/f^2) ==")
def S(f,fcut): return math.sqrt(1-(fcut/f)**2) if f>fcut else float('nan')
for h in (70,74,85,87,90):
    for f in (5.0,5.17,8.0,8.27,10.0,18.0,19.8):
        lam=c/(f*1000.0)
        d12h=lam/abs(S(f,fc(0.5,h))-S(f,fc(1.5,h)))
        try: d12i=lam/abs(S(f,fc(1,h))-S(f,fc(2,h)))
        except: d12i=float('nan')
        d01i=lam/abs(1-S(f,fc(1,h)))
        print("h=%d f=%.2f lam=%.1f km  half 1-2: %7.0f km | int 1-2: %7.0f km  TEM-1: %7.0f km"%(h,f,lam,d12h,d12i,d01i))
print("\n== hop geometry delays relative to ground wave (ms) ==")
def hop(d,h,nh,sph=True):
    if sph:
        th=d/(2*nh*a)
        leg=math.sqrt(a*a+(a+h)**2-2*a*(a+h)*math.cos(th))
        L=2*nh*leg
        # incidence angle at ionosphere (from vertical): sin i = a sin(th)/leg
        i=math.degrees(math.asin(a*math.sin(th)/leg))
        # elevation at ground
        el=math.degrees(math.acos(min(1,(a+h)*math.sin(th)/leg)))
        # check below horizon: elevation sign
        cosz=((a+h)*math.cos(th)-a)/leg
        el=90-math.degrees(math.acos(cosz))
    else:
        leg=math.sqrt((d/(2*nh))**2+h*h); L=2*nh*leg; i=math.degrees(math.atan(d/(2*nh)/h)); el=90-i
    return L,(L-d)/c*1000.0,i,el
for h in (70,90):
    for d in (100,300,500,1000,2000):
        o=[]
        for nh in (1,2,3):
            L,dt,i,el=hop(d,h,nh)
            Lf,dtf,_,_=hop(d,h,nh,False)
            o.append("%d-hop: %.3f ms (flat %.3f) i=%.1f el=%.1f"%(nh,dt,dtf,i,el))
        print("h=%d d=%d km: %s"%(h,d," ; ".join(o)))
print("\n== group velocity from TN300 phase-velocity curve readings (mode 1, beta=0.5, sigma=inf) ==")
# (v/c-1)*100 readings at 8,10 kHz
rd={60:(1.27,0.67),70:(0.80,0.38),80:(0.47,0.18),90:(0.23,0.02)}
for h,(p8,p10) in rd.items():
    n8=1/(1+p8/100); n10=1/(1+p10/100)
    sg=(10*n10-8*n8)/2
    print("h=%d: n(8)=%.5f n(10)=%.5f  c/vg(9kHz)=%.5f vg/c=%.4f excess %.0f us/Mm ; +-0.05%% reading error -> +-%.0f us/Mm"%(h,n8,n10,sg,1/sg,(sg-1)*US_PER_MM, 0.0005*9/2*2**0.5*US_PER_MM))
print("\n== Wait 1962 conversion coefficient |a2| = 3 k h psi/(4 pi^2) ==")
for f in (3,5,8,10,20):
    for hh,dh,Lt in ((80,15,300),(80,15,1000)):
        k=2*math.pi*f*1000/c
        psi=dh/Lt
        a2=3*k*hh*psi/(4*math.pi**2)
        print("f=%d kHz, h=%d, dh=%d over %d km: kh=%.1f psi=%.3f |a2|=%.3f (%.1f dB)"%(f,hh,dh,Lt,k*hh,psi,a2,20*math.log10(a2)))
