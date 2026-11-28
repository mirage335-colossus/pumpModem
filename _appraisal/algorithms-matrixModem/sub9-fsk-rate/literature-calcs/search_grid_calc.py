# Search-grid losses (frequency and timing steps), single-pole antenna response, keying transient loss, cutoff-rate limits.
# Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
# this header. Theoretical; assumptions are in the code and in the review that quotes it:
# _ref/algorithms-matrixModem/Fable-5.2/reviews/review-coherent-fsk-rate-spacing.md (sections 1, 2 and 5)
# Run: python3 -I search_grid_calc.py > results/search_grid_calc.txt

import math
def sinc(x): return 1.0 if x==0 else math.sin(math.pi*x)/(math.pi*x)
# frequency search scalloping: step = k/Tw, max offset = k/(2 Tw)
for k in (0.5,0.25,0.16,0.125):
    worst=sinc(k/2)**2
    n=2000; avg=sum(sinc((i+0.5)/n*k/2)**2 for i in range(n))/n
    print("freq step %.3f/Tw: worst %.2f dB, mean %.2f dB"%(k,10*math.log10(worst),10*math.log10(avg)))
# timing: step s*T, max offset s/2; amplitude (1-|tau|/T) when neighbours orthogonal/different
for s in (1/8,1/16,1/40):
    worst=(1-s/2)**2
    n=2000; avg=sum((1-(i+0.5)/n*s/2)**2 for i in range(n))/n
    print("time step T/%.0f: worst %.2f dB, mean %.2f dB (neighbour tone different)"%(1/s,10*math.log10(worst),10*math.log10(avg)))
# single-pole antenna: 3 dB bandwidth B, tau = 1/(pi B)
print("single-pole: |H| and phase and group delay vs offset/B")
for x in (0,0.1,0.25,0.254,0.5,1.0):
    u=2*x
    print("  df/B=%.3f |H|=%.2f dB phase=%.1f deg group delay=%.3f/(pi B)"%(x,-10*math.log10(1+u*u),-math.degrees(math.atan(u)),1/(1+u*u)))
print("transient power loss for step (amplitude loss a*tau/T): BT, 90deg/wide-FSK (a=1), 180deg (a=2)")
for BT in (2,5,10,20,50,100):
    r=1/(math.pi*BT)
    print("  B*T=%d: tau/T=%.4f  a=1: %.2f dB  a=2: %.2f dB"%(BT,r,20*math.log10(1-r),20*math.log10(1-2*r)))
# CPFSK continuous-phase small jump: amplitude loss = (tau/T)*Re[ j w tau/(1+j w tau) ], w=2 pi df
for x in (0.05,0.1,0.25,0.5,1.0):  # df/B
    wt=2*math.pi*x/math.pi  # 2 pi df tau = 2 df/B
    re=(wt*wt)/(1+wt*wt)
    print("  CPFSK jump df/B=%.2f: fraction of tau/T lost = %.3f"%(x,re))
# binary CPFSK distance bound
h=0.715; print("d2 bound binary CPFSK h=0.715: %.3f -> %.2f dB vs d2=2"%(2*(1-math.sin(2*math.pi*h)/(2*math.pi*h)),10*math.log10((1-math.sin(2*math.pi*h)/(2*math.pi*h)))))
# R0 low-rate Eb/N0 for coherent orthogonal and antipodal
for M in (2,4,8,16): print("R0-limit Eb/N0 coherent orthogonal M=%d: %.2f dB"%(M,10*math.log10(2*math.log(2)*M/(M-1))))
print("R0-limit antipodal/unconstrained: %.2f dB"%(10*math.log10(2*math.log(2))))
