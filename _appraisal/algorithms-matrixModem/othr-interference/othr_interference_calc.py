#!/usr/bin/env python3
"""THEORETICAL calculation: periodic FM radar interference seen by a narrowband
noncoherent tone receiver (6.25 Hz bins, 160 ms symbols, 2.2-2.8 kHz passband).

Supports _ref/algorithms-matrixModem/Fable-5.2/reviews/review-othr-interference.md.
Standard library only.  Run:   python3 -I othr_interference_calc.py > othr_interference_calc.out.txt

Everything printed here is DERIVED (formulas or small simulations), not measured.
Assumptions are printed with each section.  Radar parameter sets are the ones
reported in the review (monitoring reports, ITU-R M.1874-1, task examples).
"""
import cmath
import math
import random
import sys
import time

DF = 6.25          # tone/bin spacing, Hz
TSYM = 0.16        # symbol, s (DF * TSYM = 1)
W = 2800.0         # receiver passband, Hz (2200 also printed where it matters)


def db(x):
    return 10.0 * math.log10(x) if x > 0 else float("-inf")


def loss_db(frac_lost):
    return -db(1.0 - frac_lost) if frac_lost < 1.0 else float("inf")


# ---------------------------------------------------------------------------
# A. Analytic table
# ---------------------------------------------------------------------------
RADARS = [
    # name, swept bandwidth Hz, repetition Hz, source tag
    ("task: 10 kHz @ 50 Hz (CHN 10K0 50 sps)", 10e3, 50.0),
    ("Foghorn burst 10 kHz @ 66.67 Hz", 10e3, 66.67),
    ("29B6 Container 12 kHz @ 40 Hz", 12e3, 40.0),
    ("PLUTO 20 kHz @ 50 Hz", 20e3, 50.0),
    ("PLUTO 20 kHz @ 25 Hz", 20e3, 25.0),
    ("PLUTO 40 kHz @ 12.5 Hz", 40e3, 12.5),
    ("task: 20 kHz @ 10 Hz", 20e3, 10.0),
    ("task: 40 kHz @ 25 Hz", 40e3, 25.0),
    ("CHN 40 kHz @ 10 Hz", 40e3, 10.0),
    ("CHN wideband 160 kHz @ 10 Hz", 160e3, 10.0),
    ("JORN-as-logged 10 kHz @ 7 Hz", 10e3, 7.0),
    ("ROTHR-as-logged 8 kHz @ 32 Hz", 8e3, 32.0),
    ("unid 4 kHz @ 25 Hz", 4e3, 25.0),
    ("Iran 45 kHz @ 150 Hz", 45e3, 150.0),
    ("Iran 45 kHz @ 313 Hz", 45e3, 313.0),
    ("CODAR 25.6 kHz @ 1 Hz (M.1874)", 25.6e3, 1.0),
    ("CODAR 49.4 kHz @ 2 Hz (M.1874)", 49.4e3, 2.0),
    ("CODAR 101 kHz @ 2 Hz (M.1874)", 101e3, 2.0),
    ("WERA-class 100 kHz @ 4 Hz (0.25 s)", 100e3, 4.0),
    ("slow unid 4 kHz @ 0.25 Hz", 4e3, 0.25),
]


def section_a():
    print("=" * 100)
    print("A. ANALYTIC TABLE (derived).  One propagation mode, ideal sawtooth LFM of total")
    print("   received power P, passband W = 2800 Hz wholly inside the sweep, bins DF = 6.25 Hz,")
    print("   symbol T = 160 ms.")
    print("   k        = B*fr                      sweep rate")
    print("   t_cross  = W/k                       time each sweep spends in the passband")
    print("   d_t      = min(1, W/B)               time duty of the chirp pulses in the passband")
    print("   d_t+g    = min(1, (W/k + 2 ms)*fr)   same with 2 ms guard (filter tails, delay spread)")
    print("   n_sw     = fr*T                      sweeps per symbol")
    print("   L_bins   = fr/DF                     comb line spacing in bins")
    print("   line     = 10log(fr/B)               power of one comb line re P (coherent sweeps)")
    print("   avgbin   = 10log(DF/B)               mean in-bin power re P (any coherence)")
    print("   pk1      = 10log(min(1, DF^2/k))     peak response of one bin to one sweep re P")
    print("   d_f(w)   = min(1, w/fr)              spectrum fraction removed by a comb notch of width w")
    print("   d_dc     = min(d_t, 2*fr/W)          T-F fraction removed by per-sweep dechirp-and-notch")
    print("                                        (2 resolution cells of k/W Hz per crossing; idealised")
    print("                                        lower bound: one mode, known sweep timing and rate)")
    print("   Ntb      = W^2/k                     time-bandwidth product of one in-band crossing; the")
    print("                                        crossing is a well-localised pulse only when Ntb >> 1")
    print("   native   = min(1,(t_cross+T)*fr) * min(1,(k*T+2*DF)/W)")
    print("                                        fraction of native (6.25 Hz x 160 ms) cells the chirp")
    print("                                        passes through when nothing finer is done")
    print()
    hdr = ("%-38s %8s %8s %6s %6s %6s %6s %7s %7s %7s %6s %6s %6s %7s %6s"
           % ("radar", "k kHz/s", "t_x ms", "d_t", "d_t+g", "n_sw", "Lbins", "line", "avgbin", "pk1",
              "d_f2", "d_f4", "d_dc", "Ntb", "native"))
    print(hdr)
    rows = []
    for name, B, fr in RADARS:
        k = B * fr
        tx = W / k if B > W else 1.0 / fr
        dt = min(1.0, W / B)
        dtg = min(1.0, (W / k + 2e-3) * fr) if B > W else 1.0
        nsw = fr * TSYM
        lb = fr / DF
        line = db(fr / B)
        avg = db(DF / B)
        pk1 = db(min(1.0, DF * DF / k))
        df2 = min(1.0, 2.0 / fr)
        df4 = min(1.0, 4.0 / fr)
        ddc = min(dt, 2.0 * fr / W)
        native = min(1.0, (tx + TSYM) * fr) * min(1.0, (k * TSYM + 2 * DF) / W)
        rows.append((name, B, fr, dt, dtg, df2, df4, ddc))
        print("%-38s %8.1f %8.2f %6.3f %6.3f %6.2f %6.2f %7.1f %7.1f %7.1f %6.3f %6.3f %6.4f %7.1f %6.3f"
              % (name, k / 1e3, tx * 1e3, dt, dtg, nsw, lb, line, avg, pk1, df2, df4, ddc, W * W / k, native))
    print()
    print("   Loss in dB of collected signal energy if the stated fraction is removed with perfect")
    print("   knowledge (loss = -10log10(1-d)).  'practical' = smaller of blank+guard and comb(4 Hz);")
    print("   the dechirp column is an idealised bound and is not used for 'practical'.")
    print("%-38s %9s %9s %9s %9s %9s  %s" % ("radar", "blank", "blank+g", "comb 2Hz", "comb 4Hz", "dechirp", "practical (which)"))
    for name, B, fr, dt, dtg, df2, df4, ddc in rows:
        cands = [("blank+g", dtg), ("comb4", df4)]
        bname, bval = min(cands, key=lambda c: c[1])
        print("%-38s %9.2f %9.2f %9.2f %9.2f %9.2f  %.2f dB (%s, d=%.3f)"
              % (name, loss_db(dt), loss_db(dtg), loss_db(df2), loss_db(df4), loss_db(ddc),
                 loss_db(bval), bname, bval))
    print()
    print("   Comb line width caused by ionospheric Doppler spread (Gaussian spectrum, 2-sigma = nu):")
    print("   full width at -L dB = nu*sqrt(2*L*ln10/10).  Fraction of spectrum above -L dB = width/fr.")
    for nu in (0.1, 0.5, 1.0, 3.0, 10.0, 30.0):
        s = []
        for L in (20.0, 30.0, 40.0):
            s.append("%.2f" % (nu * math.sqrt(2.0 * L * math.log(10.0) / 10.0)))
        print("     nu = %5.1f Hz : width at -20/-30/-40 dB = %s Hz" % (nu, " / ".join(s)))
    print()


# ---------------------------------------------------------------------------
# F. Unhandled interference: noise-floor rise
# ---------------------------------------------------------------------------
def section_f():
    print("=" * 100)
    print("F. UNHANDLED INTERFERENCE (derived).  rho_W = radar power during its in-band pulse divided")
    print("   by the noise power in W = 2.8 kHz (what a fast S-meter shows above the band noise).")
    print("   Mean in-bin interference-to-noise ratio = rho_W * W/B (for B > W; = rho_W for B <= W).")
    print("   Noise-floor rise = 10log10(1 + rho_W*W/B) dB, for any sweep coherence, if nothing is done.")
    print()
    Bs = (4e3, 10e3, 20e3, 40e3, 160e3)
    print("   rho_W dB |" + "".join(" B=%5.0fk" % (b / 1e3) for b in Bs))
    for rw in (-10, -5, 0, 5, 10, 20, 30, 40, 50, 60):
        line = "   %8d |" % rw
        for b in Bs:
            inr = 10 ** (rw / 10.0) * min(1.0, W / b)
            line += " %8.2f" % db(1.0 + inr)
        print(line)
    print()
    print("   Largest rho_W (dB above the 2.8 kHz noise floor) whose unhandled rise fits inside a margin M:")
    print("   rho_W,max = 10log10((10^(M/10)-1) * max(1, B/W))")
    print("   margin dB |" + "".join(" B=%5.0fk" % (b / 1e3) for b in Bs))
    for m in (3, 6, 10, 13, 16, 20):
        line = "   %9d |" % m
        for b in Bs:
            line += " %8.1f" % db((10 ** (m / 10.0) - 1.0) * max(1.0, b / W))
        print(line)
    print("   (S-units are nominally 6 dB; a radar logged at S9+20 dB on a band whose noise reads S3")
    print("    has rho_W about 56 dB.)")
    print()


# ---------------------------------------------------------------------------
# C. Tone loss under comb notch and under blanking
# ---------------------------------------------------------------------------
def sinc2(x):
    if abs(x) < 1e-12:
        return 1.0
    y = math.pi * x
    return (math.sin(y) / y) ** 2


def tone_energy_fraction(f_lo, f_hi, n=4000):
    """fraction of the energy of a rectangular T=160 ms tone that lies in [f_lo, f_hi] (Hz offsets)."""
    h = (f_hi - f_lo) / n
    acc = 0.0
    for i in range(n + 1):
        f = f_lo + i * h
        wgt = 0.5 if i in (0, n) else 1.0
        acc += wgt * TSYM * sinc2(f * TSYM)
    return acc * h


def section_c():
    print("=" * 100)
    print("C. EFFECT OF EXCISION ON THE WANTED 160 ms TONE (derived).")
    print("   Comb notch of full width w centred on every multiple of fr.  A rectangular 160 ms tone has")
    print("   energy spectrum T*sinc^2(fT).  Loss for a tone exactly on a comb line (worst), midway")
    print("   between lines (best) and averaged over a uniform tone-to-line offset (= w/fr).")
    print("   %6s %6s | %10s %10s %10s | %10s" % ("fr Hz", "w Hz", "worst dB", "best dB", "mean dB", "mean frac"))
    for fr in (10.0, 25.0, 40.0, 50.0, 66.67):
        for w in (2.0, 4.0, 8.0):
            if w >= fr:
                continue
            def removed(offset):
                tot = 0.0
                m_max = int(60.0 / fr) + 2
                for m in range(-m_max, m_max + 1):
                    c = offset + m * fr
                    tot += tone_energy_fraction(c - w / 2, c + w / 2, 400)
                return tot
            worst = removed(0.0)
            best = removed(fr / 2.0)
            nn = 40
            mean = sum(removed((i + 0.5) * fr / nn - fr / 2.0) for i in range(nn)) / nn
            print("   %6.2f %6.1f | %10.2f %10.3f %10.3f | %10.4f"
                  % (fr, w, loss_db(worst), loss_db(best), loss_db(mean), mean))
    print()
    print("   Time blanking with duty d spread over each symbol (fr*T >= 1): tone amplitude in its own")
    print("   bin scales by (1-d), noise power by (1-d): SNR loss = -10log10(1-d).  The gate also")
    print("   creates images of the tone at +-n*fr with power (sin(pi n d)/(pi n))^2 re the unblanked tone.")
    print("   %6s | %8s | %s" % ("d", "loss dB", "image n=1,2,3 re blanked main tone (dB)"))
    for d in (0.02, 0.07, 0.14, 0.23, 0.28, 0.40, 0.56, 0.70):
        ims = []
        for n in (1, 2, 3):
            ims.append("%.1f" % db((math.sin(math.pi * n * d) / (math.pi * n)) ** 2 / (1 - d) ** 2))
        print("   %6.2f | %8.2f | %s" % (d, loss_db(d), " / ".join(ims)))
    print()


# ---------------------------------------------------------------------------
# D. Classic FH/MFSK worst-case partial-band noise (uncoded, noncoherent)
# ---------------------------------------------------------------------------
def pb_mfsk(x, K):
    """bit error probability of noncoherent orthogonal MFSK in AWGN, x = Eb/N0 (linear).
    Alternating binomial sum evaluated in 80-digit decimal arithmetic (it cancels badly in
    double precision for M = 64)."""
    from decimal import Decimal, getcontext
    getcontext().prec = 80
    M = 1 << K
    xs = Decimal(repr(x))
    s = Decimal(0)
    for n in range(1, M):
        term = Decimal(math.comb(M - 1, n)) / Decimal(n + 1) * (-(Decimal(n * K) * xs) / Decimal(n + 1)).exp()
        s += term if n % 2 == 1 else -term
    return float(s * Decimal(M) / Decimal(2 * (M - 1)))


def section_d():
    print("=" * 100)
    print("D. CLASSIC WORST-CASE PARTIAL-BAND NOISE, UNCODED NONCOHERENT FH/MFSK (derived here; it")
    print("   reproduces the textbook result P_b = beta/(Eb/NJ) for Eb/NJ >= gamma, no thermal noise,")
    print("   one hop per symbol, jammer fills a fraction rho = gamma/(Eb/NJ) of the hop band).")
    print("   %3s %4s | %8s %9s | %14s %14s %9s" % ("K", "M", "beta", "gamma dB", "Eb/NJ@1e-5 wc", "Eb/N0@1e-5 bb", "loss dB"))
    for K in range(1, 7):
        # maximise y*Pb(y)
        best_y, best_v = 0.0, 0.0
        y = 0.05
        while y < 6.0:                      # coarse scan
            v = y * pb_mfsk(y, K)
            if v > best_v:
                best_v, best_y = v, y
            y += 0.02
        lo_y, hi_y = best_y - 0.02, best_y + 0.02
        for _ in range(60):                 # golden-section refinement
            a = lo_y + 0.381966 * (hi_y - lo_y)
            b = lo_y + 0.618034 * (hi_y - lo_y)
            if a * pb_mfsk(a, K) < b * pb_mfsk(b, K):
                lo_y = a
            else:
                hi_y = b
        best_y = 0.5 * (lo_y + hi_y)
        best_v = best_y * pb_mfsk(best_y, K)
        wc = best_v / 1e-5
        lo, hi = 0.1, 100.0
        for _ in range(80):
            mid = 0.5 * (lo + hi)
            if pb_mfsk(mid, K) > 1e-5:
                lo = mid
            else:
                hi = mid
        print("   %3d %4d | %8.4f %9.2f | %14.2f %14.2f %9.2f"
              % (K, 1 << K, best_v, db(best_y), db(wc), db(hi), db(wc) - db(hi)))
    print()


# ---------------------------------------------------------------------------
# E. Robust nonlinearities: clean-noise efficiency and pulsed-interference loss
# ---------------------------------------------------------------------------
def section_e():
    print("=" * 100)
    print("E. ROBUST NONLINEARITIES (derived; standard locally-optimum efficacy algebra).")
    print()
    print("E1. Time-domain envelope nonlinearity g(r) before the transform, complex Gaussian noise with")
    print("    per-component sigma = 1 (E r^2 = 2), weak narrowband signal.  Efficiency relative to a")
    print("    linear receiver: eta = (1/2) * (E[g'(r) + g(r)/r])^2 / E[g(r)^2].")
    print("    signum (hard limiter): eta = pi/4 -> %.2f dB" % db(math.pi / 4))
    print("    %8s | %12s | %12s" % ("c/sigma", "clipper dB", "blanker dB"))
    for c in (1.0, 1.5, 2.0, 2.5, 3.0, 4.0):
        e = math.exp(-c * c / 2.0)
        p_in = 1.0 - e
        # clipper g = min(r, c)
        num = 2.0 * p_in + c * math.sqrt(math.pi / 2.0) * math.erfc(c / math.sqrt(2.0))
        den = 2.0 * (1.0 - e)
        eta_clip = 0.5 * num * num / den
        # blanker g = r for r < c else 0
        numb = 2.0 * p_in - c * c * e
        denb = 2.0 - (c * c + 2.0) * e
        eta_blank = 0.5 * numb * numb / denb
        print("    %8.1f | %12.2f | %12.2f" % (c, db(eta_clip), db(eta_blank)))
    print()
    print("E2. Strong in-band pulses of time duty d (ITU-R M.2030 forms, published model, applied here):")
    print("    blanker with perfect pulse detection : N0eff/N0 = 1/(1-d)")
    print("    saturating/limiting receiver         : N0eff/N0 = (1 + NLIM^2 * d/(1-d)) / (1-d),")
    print("    NLIM = limit level / 1-sigma noise voltage (NLIM = 1 for a hard limiter).")
    print("    %6s | %8s | %10s %10s %10s %10s" % ("d", "blank dB", "NLIM=1", "NLIM=2", "NLIM=3", "NLIM=5"))
    for d in (0.0175, 0.07, 0.14, 0.23, 0.28, 0.40, 0.56):
        row = "    %6.3f | %8.2f |" % (d, loss_db(d))
        for nl in (1.0, 2.0, 3.0, 5.0):
            row += " %10.2f" % db((1.0 + nl * nl * d / (1.0 - d)) / (1.0 - d))
        print(row)
    print("    (add the clean-noise loss of E1 for the limiter that is always in circuit.)")
    print()
    print("E3. Per-cell energy metric clipped at L (units of the bin's mean noise energy), summed over n")
    print("    symbols at low per-symbol SNR.  A fraction rho of cells is hit by strong interference at")
    print("    random (hits independent between candidate tones, as a keyed hop schedule makes them);")
    print("    hit cells saturate at L.  Deflection^2 relative to the clean unclipped energy detector:")
    print("      R = (1-rho)^2 Cov^2 / [ (1-rho) V + rho(1-rho)(L-mu)^2 ],")
    print("      mu = 1-e^-L, Cov = 1-(L+1)e^-L, V = 1-2Le^-L-e^-2L.")
    print("    'time dB' = -10log10(R) = extra integration time; power equivalent is half of it in the")
    print("    square-law (very low SNR per symbol) regime.  Perfect side information: R = 1-rho.")

    def r_clip(rho, L):
        e = math.exp(-L)
        mu = 1 - e
        cov = 1 - (L + 1) * e
        v = 1 - 2 * L * e - e * e
        return (1 - rho) ** 2 * cov * cov / ((1 - rho) * v + rho * (1 - rho) * (L - mu) ** 2)

    def r_unclipped(rho, J):
        # hit cells: exponential with mean 1+J; all cells keep the (swamped) signal
        var = (1 - rho) * 2.0 + rho * 2.0 * (1 + J) ** 2 - (1 + rho * J) ** 2
        return 1.0 / var

    Ls = (1.0, 2.0, 3.0, 5.0, 8.0)
    print("    %6s | %9s |" % ("rho", "side-info") + "".join(" L=%-6.0f" % L for L in Ls)
          + " | %10s %10s %10s" % ("raw J=10dB", "raw J=20dB", "raw J=40dB"))
    for rho in (0.0, 0.02, 0.05, 0.10, 0.25, 0.40, 0.60):
        row = "    %6.2f | %9.2f |" % (rho, -db(1 - rho))
        for L in Ls:
            row += " %8.2f" % (-db(r_clip(rho, L)))
        row += " |"
        for jdb in (10.0, 20.0, 40.0):
            row += " %10.2f" % (-db(r_unclipped(rho, 10 ** (jdb / 10.0))))
        print(row)
    # numeric cross-check of Cov and V for L = 3
    L = 3.0
    n = 200000
    h = 40.0 / n
    eg = eg2 = ege = 0.0
    for i in range(n):
        x = (i + 0.5) * h
        w = math.exp(-x) * h
        g = min(x, L)
        eg += g * w
        eg2 += g * g * w
        ege += g * x * w
    print("    numeric check L=3: mu=%.5f (closed %.5f)  Cov=%.5f (closed %.5f)  V=%.5f (closed %.5f)"
          % (eg, 1 - math.exp(-L), ege - eg, 1 - (L + 1) * math.exp(-L), eg2 - eg * eg,
             1 - 2 * L * math.exp(-L) - math.exp(-2 * L)))
    print()
    print("E4. Per-bin noise-level estimate from n exponential (energy) samples: sample mean has relative")
    print("    variance 1/n; median/ln2 has relative variance 1/(n ln^2 2) = 2.08/n (asymptotic) but")
    print("    tolerates up to 50 % contaminated samples.")
    for n in (25, 100, 400, 2000, 20000):
        print("      n = %6d : rms error mean %.1f %%, median %.1f %%"
              % (n, 100 / math.sqrt(n), 100 * math.sqrt(1 / (n * math.log(2) ** 2))))
    print()


# ---------------------------------------------------------------------------
# G. Interleaver resonance with a periodic interferer
# ---------------------------------------------------------------------------
def section_g():
    print("=" * 100)
    print("G. INTERLEAVER RESONANCE (derived, illustrative).  A codeword of n = 64 channel symbols is")
    print("   sent with regular spacing D symbols (D*0.16 s apart).  An interferer with period P and a")
    print("   given on-time erases every symbol it overlaps (p_hit = (on + 0.16 s)/P).  Only spacings")
    print("   whose codeword span is at least 10 interferer periods are examined (so plain 'burst longer")
    print("   than the codeword' is excluded); for each such D the worst interferer phase is taken.")
    print("   'random' is the same codeword placed pseudo-randomly: hit count ~ Binomial(64, p_hit).")
    n = 64
    cases = [
        ("CODAR 1 Hz sweep, 109 ms in band", 1.0, 0.109),
        ("burst radar 3.8 s on / 20 s (illustrative)", 20.0, 3.8),
        ("CHN wideband 50 s on / 90 s (IARU wiki)", 90.0, 50.0),
        ("slow radar 120 s on / 240 s (IARU 2024-07)", 240.0, 120.0),
        ("chirpsounder 28 ms / 300 s", 300.0, 0.028),
    ]
    rng = random.Random(7)
    nD = 2000
    for name, P, on in cases:
        hit_win = on + TSYM
        p_hit = min(1.0, hit_win / P)
        Dmin = int(math.ceil(10.0 * P / (n * TSYM)))
        ge90 = ge_mid = 0
        worst_overall, worst_D = 0.0, 0
        fracs = []
        for D in range(Dmin, Dmin + nD):
            step = (D * TSYM) % P
            pos = sorted((j * step) % P for j in range(n))
            ext = pos + [q + P for q in pos]
            best = 0
            j2 = 0
            for j1 in range(n):               # sliding window over sorted positions
                if j2 < j1:
                    j2 = j1
                while j2 + 1 < j1 + n and ext[j2 + 1] - ext[j1] < hit_win:
                    j2 += 1
                c = j2 - j1 + 1
                if c > best:
                    best = c
            frac = best / n
            fracs.append(frac)
            if frac >= 0.9:
                ge90 += 1
            if frac >= min(1.0, p_hit + 0.3):
                ge_mid += 1
            if frac > worst_overall:
                worst_overall, worst_D = frac, D
        fracs.sort()
        med = fracs[len(fracs) // 2]
        trials = 20000
        cnts = sorted(sum(1 for _ in range(n) if rng.random() < p_hit) for _ in range(trials))
        q999 = cnts[int(0.999 * trials)] / n
        print("   %-44s p_hit=%.3f | regular D=%d..%d: median worst-phase frac %.2f, max %.2f (D=%d), "
              "D with >=0.9: %d, D with >=p_hit+0.3: %d of %d | random: mean %.3f, 99.9%% point %.3f"
              % (name, p_hit, Dmin, Dmin + nD - 1, med, worst_overall, worst_D, ge90, ge_mid, nD, p_hit, q999))
    print()


# ---------------------------------------------------------------------------
# B. Simulation: spectrum and time structure in the passband
# ---------------------------------------------------------------------------
def fft(a, inverse=False):
    n = len(a)
    a = list(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    sign = 1.0 if inverse else -1.0
    while length <= n:
        ang = sign * 2.0 * math.pi / length
        wlen = cmath.exp(1j * ang)
        half = length >> 1
        ws = [1.0 + 0j] * half
        w = 1.0 + 0j
        for k in range(half):
            ws[k] = w
            w *= wlen
        for i in range(0, n, length):
            for k in range(half):
                u = a[i + k]
                v = a[i + k + half] * ws[k]
                a[i + k] = u + v
                a[i + k + half] = u - v
        length <<= 1
    if inverse:
        inv = 1.0 / n
        a = [x * inv for x in a]
    return a


FS = 51200.0
NSYM_FFT = 8192            # 160 ms at 51.2 kHz -> 6.25 Hz bins


def make_fading(rng, ntot, nu, step=64):
    """Rayleigh fading with Gaussian Doppler spectrum, 2-sigma width nu (Hz); unit mean power."""
    nsin = 24
    comps = [(rng.gauss(0.0, nu / 2.0), rng.uniform(0, 2 * math.pi)) for _ in range(nsin)]
    npts = ntot // step + 2
    pts = []
    for i in range(npts):
        t = i * step / FS
        z = 0j
        for f, ph in comps:
            z += cmath.exp(1j * (2 * math.pi * f * t + ph))
        pts.append(z / math.sqrt(nsin))
    out = [0j] * ntot
    for n in range(ntot):
        i = n // step
        fr = (n - i * step) / step
        out[n] = pts[i] * (1 - fr) + pts[i + 1] * fr
    return out


def make_radar(rng, B, fr, ntot, mode, gate=None, delay=0.0, f_shift=0.0):
    """Unit-power sawtooth LFM, baseband -B/2..+B/2, repetition fr.
    mode: 'coh' identical sweeps; 'incoh' random start phase per sweep.
    gate: (period_s, duty) on/off gating (FMICW)."""
    Tr = 1.0 / fr
    k = B * fr
    out = [0j] * ntot
    phases = {}
    for n in range(ntot):
        t = n / FS - delay
        m = math.floor(t / Tr)
        tau = t - m * Tr
        ph = 2 * math.pi * (-0.5 * B * tau + 0.5 * k * tau * tau + f_shift * t)
        if mode == "incoh":
            if m not in phases:
                phases[m] = rng.uniform(0, 2 * math.pi)
            ph += phases[m]
        if gate is not None:
            gp, gd = gate
            if (t % gp) >= gd * gp:
                continue
        out[n] = cmath.exp(1j * ph)
    return out


def analyse(label, sig, B, fr, w_hz=W, f_lo=300.0, notch_centres=None):
    ntot = len(sig)
    nsym = ntot // NSYM_FFT
    b_lo = int(round(f_lo / DF))
    nb = int(round(w_hz / DF))
    m0 = FS / B               # expected mean of |X|^2/sum(w^2) for PSD 1/B
    res = {}
    for wname in ("rect", "hann"):
        if wname == "rect":
            win = [1.0] * NSYM_FFT
        else:
            win = [0.5 - 0.5 * math.cos(2 * math.pi * (i + 0.5) / NSYM_FFT) for i in range(NSYM_FFT)]
        sw2 = sum(x * x for x in win)
        cells = []
        for s in range(nsym):
            seg = [sig[s * NSYM_FFT + i] * win[i] for i in range(NSYM_FFT)]
            X = fft(seg)
            for b in range(b_lo, b_lo + nb):
                cells.append((X[b].real ** 2 + X[b].imag ** 2) / sw2 / m0)
        ncell = len(cells)
        mean = sum(cells) / ncell
        fr_above = {}
        for thr in (6.0, 0.0, -10.0, -20.0, -30.0):
            lim = 10 ** (thr / 10.0)
            fr_above[thr] = sum(1 for c in cells if c > lim) / ncell
        res[wname] = (mean, max(cells), fr_above)
    # whole-record processing: band-limit to the passband, two filter shapes:
    #   'steep'  raised-cosine transition bands of 200 Hz (an SSB-like filter)
    #   'taper'  raised-cosine over the whole passband (transition = W/2), the smoothest case
    nbig = 1
    while nbig * 2 <= ntot:
        nbig *= 2
    Xb = fft(sig[:nbig])
    dfb = FS / nbig
    tails = {}
    for fname, edge in (("steep", 200.0), ("taper", w_hz / 2.0)):
        Y = [0j] * nbig
        for i in range(nbig):
            f = i * dfb if i < nbig // 2 else (i - nbig) * dfb
            if f_lo <= f <= f_lo + w_hz:
                g = 1.0
                if f < f_lo + edge:
                    g = 0.5 - 0.5 * math.cos(math.pi * (f - f_lo) / edge)
                elif f > f_lo + w_hz - edge:
                    g = 0.5 - 0.5 * math.cos(math.pi * (f_lo + w_hz - f) / edge)
                Y[i] = Xb[i] * g
        y = fft(Y, inverse=True)
        env = [v.real * v.real + v.imag * v.imag for v in y]
        tot = sum(env)
        srt = sorted(env, reverse=True)
        acc = 0.0
        need = {}
        targets = [0.90, 0.99, 0.999, 0.9999]
        ti = 0
        for i, e in enumerate(srt):
            acc += e
            while ti < len(targets) and acc >= targets[ti] * tot:
                need[targets[ti]] = (i + 1) / nbig
                ti += 1
            if ti >= len(targets):
                break
        tails[fname] = (tot / nbig, need)
    # comb notch on the Hann-windowed whole record
    notch = {}
    if notch_centres is not None:
        hw = [0.5 - 0.5 * math.cos(2 * math.pi * (i + 0.5) / nbig) for i in range(nbig)]
        Xh = fft([sig[i] * hw[i] for i in range(nbig)])
        i_lo = int(f_lo / dfb)
        i_hi = int((f_lo + w_hz) / dfb)
        pw = [Xh[i].real ** 2 + Xh[i].imag ** 2 for i in range(i_lo, i_hi)]
        ptot = sum(pw)
        off = notch_centres
        for wn in (2.0, 4.0, 8.0):
            rem = 0.0
            for idx, p in enumerate(pw):
                f = (i_lo + idx) * dfb
                d = (f - off) % fr
                d = min(d, fr - d)
                if d > wn / 2.0:
                    rem += p
            notch[wn] = rem / ptot if ptot > 0 else 0.0
    print("  %s" % label)
    for wname in ("rect", "hann"):
        mean, mx, fa = res[wname]
        print("    %-4s window: mean cell power re P*DF/B = %+6.2f dB, max %+6.1f dB; cells above "
              "+6/0/-10/-20/-30 dB: %.3f / %.3f / %.3f / %.3f / %.3f"
              % (wname, db(mean), db(mx), fa[6.0], fa[0.0], fa[-10.0], fa[-20.0], fa[-30.0]))
    for fname in ("steep", "taper"):
        frac, need = tails[fname]
        print("    %-5s filter: in-band power fraction %.4f (W/B = %.4f); smallest time fraction holding "
              "90/99/99.9/99.99 %% of the in-band interference energy: %.3f / %.3f / %.3f / %.3f"
              % (fname, frac, min(1.0, w_hz / B), need.get(0.90, 1), need.get(0.99, 1), need.get(0.999, 1),
                 need.get(0.9999, 1)))
    if notch:
        print("    comb notch on a %.2f s Hann record: residual in-band interference after notch of "
              "2/4/8 Hz = %.1f / %.1f / %.1f dB"
              % (nbig / FS, db(notch[2.0]) if notch[2.0] > 0 else -999, db(notch[4.0]) if notch[4.0] > 0 else -999,
                 db(notch[8.0]) if notch[8.0] > 0 else -999))
    sys.stdout.flush()


def section_b():
    print("=" * 100)
    print("B. SIMULATION (derived).  Complex baseband at 51.2 kHz, 16 symbols (2.56 s), 8192-point")
    print("   symbol transforms (6.25 Hz bins), passband 300..3100 Hz above the sweep centre, radar")
    print("   total power P = 1, no noise.  A random sub-bin frequency offset is applied.  'cells' are")
    print("   (bin, symbol) pairs in the passband; powers are relative to the all-coherence mean P*DF/B.")
    print("   The 'time fraction holding x %' lines give the blanking duty needed to remove x % of the")
    print("   in-band interference energy (99 % = 20 dB, 99.9 % = 30 dB, 99.99 % = 40 dB suppression)")
    print("   when the gate is applied after the passband filter ('steep': 200 Hz raised-cosine edges;")
    print("   'taper': raised cosine over the whole passband).  The comb-notch line assumes the line")
    print("   frequencies are known exactly and uses one 2.56 s Hann-windowed record.")
    print("   Faded cases use one finite fading realisation, so their mean power differs from the nominal.")
    print("   For a noise-like interferer the expected fractions above +6/0/-10/-20/-30 dB are")
    print("   %.3f / %.3f / %.3f / %.3f / %.3f." % (math.exp(-10 ** 0.6), math.exp(-1), math.exp(-0.1),
                                                   math.exp(-0.01), math.exp(-0.001)))
    print()
    rng = random.Random(20261126)
    ntot = 16 * NSYM_FFT
    cases = [
        ("10 kHz @ 50 Hz, coherent sweeps", 10e3, 50.0, "coh", None, None, 0.0),
        ("10 kHz @ 50 Hz, random phase per sweep", 10e3, 50.0, "incoh", None, None, 0.0),
        ("10 kHz @ 50 Hz, coherent, Doppler spread 1 Hz", 10e3, 50.0, "coh", 1.0, None, 0.0),
        ("10 kHz @ 50 Hz, coherent, Doppler spread 10 Hz", 10e3, 50.0, "coh", 10.0, None, 0.0),
        ("10 kHz @ 50 Hz, coherent, two modes 1.5 ms apart (equal, 0.5 Hz spread each)", 10e3, 50.0, "coh", 0.5, None, 1.5e-3),
        ("12 kHz @ 40 Hz, coherent sweeps", 12e3, 40.0, "coh", None, None, 0.0),
        ("20 kHz @ 10 Hz, coherent sweeps", 20e3, 10.0, "coh", None, None, 0.0),
        ("20 kHz @ 10 Hz, coherent, Doppler spread 1 Hz", 20e3, 10.0, "coh", 1.0, None, 0.0),
        ("40 kHz @ 25 Hz, coherent sweeps", 40e3, 25.0, "coh", None, None, 0.0),
        ("40 kHz @ 25 Hz, coherent, Doppler spread 1 Hz", 40e3, 25.0, "coh", 1.0, None, 0.0),
        ("CODAR 25.6 kHz @ 1 Hz, FMICW gate 1946 us 50 %, coherent", 25.6e3, 1.0, "coh", None, (1946e-6, 0.5), 0.0),
        ("CODAR 25.6 kHz @ 1 Hz, ungated FMCW, coherent", 25.6e3, 1.0, "coh", None, None, 0.0),
    ]
    for label, B, fr, mode, nu, gate, delay2 in cases:
        t0 = time.time()
        f_shift = rng.uniform(0.0, DF)
        sig = make_radar(rng, B, fr, ntot, mode, gate=gate, f_shift=f_shift)
        if gate is not None:
            # normalise gated signal to unit mean power
            p = sum(v.real * v.real + v.imag * v.imag for v in sig) / ntot
            sc = 1.0 / math.sqrt(p)
            sig = [v * sc for v in sig]
        if nu is not None:
            fad = make_fading(rng, ntot, nu)
            sig = [sig[i] * fad[i] for i in range(ntot)]
        if delay2 > 0.0:
            sig2 = make_radar(rng, B, fr, ntot, mode, gate=gate, delay=delay2, f_shift=f_shift)
            fad2 = make_fading(rng, ntot, nu if nu else 0.5)
            s2 = math.sqrt(0.5)
            sig = [s2 * sig[i] + s2 * sig2[i] * fad2[i] for i in range(ntot)]
        # comb line positions: multiples of fr shifted by (-B/2 + f_shift) mod fr
        centre = (-0.5 * B + f_shift) % fr
        analyse(label, sig, B, fr, notch_centres=(centre if fr >= 8.0 else None))
        print("    [%.0f s]" % (time.time() - t0))
    print()


def main():
    print("OTHR / periodic-FM interference versus a 6.25 Hz, 160 ms noncoherent tone receiver")
    print("THEORETICAL / DERIVED values.  python3 -I othr_interference_calc.py")
    print()
    section_a()
    section_f()
    section_c()
    section_d()
    section_e()
    section_g()
    if "--no-sim" not in sys.argv:
        section_b()


if __name__ == "__main__":
    main()
