# Statistics of amateur phase series below 9 kHz (hourly phases and bin signal-to-noise ratios transcribed by the search from public rsgb_lf_group posts of 2014 to 2017; measurement data with that provenance).
# Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
# this header. Theoretical; assumptions are in the code and in the review that quotes it:
# _ref/algorithms-matrixModem/Fable-5.2/reviews/review-sub9khz-phase-stability.md (section 2)
# Run: python3 -I phase_statistics.py > results/phase_statistics.txt

import math
def stats(name, ph, snr_db=None):
    # unwrap relative to circular mean
    c=sum(math.cos(math.radians(p)) for p in ph); s=sum(math.sin(math.radians(p)) for p in ph)
    m=math.degrees(math.atan2(s,c))
    d=[((p-m+180)%360)-180 for p in ph]
    n=len(d); sd=math.sqrt(sum(x*x for x in d)/(n-1))
    diffs=[d[i+1]-d[i] for i in range(n-1)]
    rd=math.sqrt(sum(x*x for x in diffs)/len(diffs))
    out=f"{name}: n={n} mean={m:.1f} sd={sd:.1f} deg, p-p={max(d)-min(d):.1f}, rms successive diff={rd:.1f} deg"
    if snr_db:
        # phase noise sd from SNR (amplitude SNR in the bin): sigma = 1/sqrt(2*snr)
        sig=[math.degrees(1/math.sqrt(2*10**(x/10))) for x in snr_db]
        mn=math.sqrt(sum(x*x for x in sig)/len(sig))
        intr=math.sqrt(max(sd*sd-mn*mn,0)); intrd=math.sqrt(max(rd*rd-2*mn*mn,0))
        out+=f"; meas-noise sd~{mn:.1f} deg -> intrinsic sd~{intr:.1f}, intrinsic rms diff~{intrd:.1f} deg"
    print(out)
# W4DEX 8822 Hz -> Todmorden 6194 km, 16/17 Dec 2014, hourly, 278 uHz
t=[154.4,166.4,89.9,115.7,103.2,86.3,147.1,127.1,117.3,113.2,125.5,130.0,154.7,-155.7]
sn=[4.7,9.8,11.8,11.4,9.8,11.5,8.6,10.5,13.8,13.1,11.9,14.6,9.9,7.3]
stats("Todmorden 21-10UT all",t,sn)
stats("Todmorden 23-08UT night core",t[2:12],sn[2:12])
# 1/2 Dec 2014 8820 Hz half-hour steps of 1-h windows
t2=[80.4,90.9,115.6,106.2,30.6,60.4,88.8,78.6,62.7,77.3]; sn2=[7.6,8.9,9.1,6.3,7.9,7.9,11.8,10.1,10.8,10.0]
stats("Todmorden 1/2 Dec 22:30-03:00",t2,sn2)
stats("Todmorden 1/2 Dec 01:00-03:00",t2[5:],sn2[5:])
# Cumiana 7173 km
c=[155.1,172.9,151.8,-173.8,-174.5,-174.0,-165.6,-161.3,151.3,-178.6]; sc=[5.3,4.1,6.4,6.4,4.4,6.2,8.9,8.1,6.8,9.8]
stats("Cumiana 23:00-03:30",c,sc)
# Bielefeld 6918 km 16/17 Dec
b=[114.2,60.0,84.0,127.4,138.1,68.6,175.1,92.4]; sb=[4.7,3.7,6.9,5.9,6.1,7.3,5.2,4.5]
stats("Bielefeld",b,sb)
# DF6NM 8280/8340 -> Todmorden 1028 km, 4h windows
a=[-7.4,-11.1,-13.5,-20.6,-21.2,-15.4,-17.2,-23.7,-28.7,-30.6,-27.9,-18.5,-15.2]
bq=[-84.3,-85.2,-85.6,-94.7,-102.9,-111.2,-113.3,-111.0,-101.9,-95.5,-82.2,-84.8,-88.6]
stats("DF6NM f1",a); stats("DF6NM f2",bq)
dd=[x-y for x,y in zip(a,bq)]
print("diff across 60 Hz:",[round(x,1) for x in dd], "group delay ms:",[round(x/360/60*1000,2) for x in dd])
# DK7FC 8270.1 881 km nightly mean phases
stats("DK7FC 3 nights",[101.5,92.5,100.6])
# fade period fit
for f in (10.2,8.27,5.17,2.97): print(f, "kHz fade period h:", round(10**(7-2.5*math.log10(f))/3600,1))
