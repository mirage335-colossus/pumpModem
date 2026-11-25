"""Plausible HF skywave transmit time for a 100-bit message at 2-100 mW and >= 150 dB path loss.

For each transmit power, total signal loss and receiver-site noise density (the link-budget
review's scenario inputs, unity combined antenna gain), C/N0 = P_tx - L - N0 in dB-Hz. The time for
100 bits at the Doppler-spread bound (Gaussian spectrum, Rayleigh, constant envelope, one receive
branch) is then scaled by two stated allowances:

  RECEIVER_DB  extra time of a practical receiver over the bound: 100-bit, 1%-failure finite-message
               penalty (about 1.8 dB), finite-band adaptive MFSK receiver (about 1.1 dB) and
               unknown start time and frequency (about 1 dB), taken together as 4 dB of time.
  AVAILABILITY fraction of wall-clock time the configured frequency carries the path at the stated
               C/N0; 2/3 corresponds to about 8 h a day of closure, fadeout or deep slow fade.

Each value is wall-clock transmit time = bound time * 10^(RECEIVER_DB/10) / AVAILABILITY. The
stationary bound at the median C/N0 is applied to the whole message; see the study for why that is
pessimistic in the quadratic regime when the receiver keeps evidence across varying conditions, and
optimistic where outages or a non-tracking receiver dominate.

Run: python3 -I scenario_airtime.py > results/scenario_airtime.md
"""

import math

BITS = 100
RECEIVER_DB = 4.0
AVAILABILITY = 2 / 3
SPREADS = [None, 0.05, 0.1, 1.0, 10.0]   # None = no fading (coherent AWGN reference)
SCENARIOS = [  # (label, P_tx dBm, loss dB, N0 dBm/Hz)
    ("100 mW, 150 dB, quiet rural −144", 20.0, 150, -144),
    ("100 mW, 150 dB, rural −130", 20.0, 150, -130),
    ("2 mW, 150 dB, quiet rural −144", 3.0, 150, -144),
    ("100 mW, 170 dB, −140", 20.0, 170, -140),
    ("2 mW, 150 dB, rural −130", 3.0, 150, -130),
    ("100 mW, 180 dB, −140", 20.0, 180, -140),
    ("2 mW, 165 dB, −140", 3.0, 165, -140),
    ("100 mW, 180 dB, rural −130", 20.0, 180, -130),
    ("2 mW, 180 dB, −140", 3.0, 180, -140),
    ("2 mW, 180 dB, rural −130", 3.0, 180, -130),
]


def rate(p, two_sigma, n=20001, span=10.0):
    if two_sigma is None:
        return p
    sigma = two_sigma / 2
    lo, hi = -span * sigma, span * sigma
    h = (hi - lo) / (n - 1)
    acc = 0.0
    for i in range(n):
        f = lo + i * h
        s = math.exp(-0.5 * (f / sigma) ** 2) / (sigma * math.sqrt(2 * math.pi))
        c = 1 if i in (0, n - 1) else (4 if i % 2 else 2)
        acc += c * math.log1p(p * s)
    return p - acc * h / 3


def fmt_time(t):
    for unit, size in (("yr", 365.25 * 86400), ("mo", 30.44 * 86400), ("d", 86400),
                       ("h", 3600), ("min", 60)):
        if t >= 2 * size:
            v = t / size
            return f"{v:.0f} {unit}" if v >= 10 else f"{v:.1f} {unit}"
    return f"{t:.0f} s"


def main():
    scale = 10 ** (RECEIVER_DB / 10) / AVAILABILITY
    print(f"# Plausible transmit time for {BITS} bits over HF skywave\n")
    print(f"Wall-clock transmit time = time at the bound × {10 ** (RECEIVER_DB / 10):.2f} "
          f"(receiver allowance {RECEIVER_DB:g} dB of time) / {AVAILABILITY:.2f} (path availability). "
          "Gaussian Doppler spectra (`2σ`), Rayleigh fading, constant envelope, one receive branch, "
          "stationary bound at the stated C/N0.\n")
    names = ["no fading" if w is None else f"{w:g} Hz" for w in SPREADS]
    print("| scenario | C/N0 | " + " | ".join(names) + " |")
    print("| --- | ---: |" + " ---: |" * len(SPREADS))
    for label, ptx, loss, n0 in SCENARIOS:
        cn0 = ptx - loss - n0
        p = 10 ** (cn0 / 10)
        cells = [fmt_time(BITS * math.log(2) / rate(p, w) * scale) for w in SPREADS]
        print(f"| {label} | {cn0:+.0f} dB-Hz | " + " | ".join(cells) + " |")
    print("\nTime at the bound alone (no receiver or availability allowance):\n")
    print("| scenario | C/N0 | " + " | ".join(names) + " |")
    print("| --- | ---: |" + " ---: |" * len(SPREADS))
    for label, ptx, loss, n0 in SCENARIOS:
        cn0 = ptx - loss - n0
        p = 10 ** (cn0 / 10)
        cells = [fmt_time(BITS * math.log(2) / rate(p, w)) for w in SPREADS]
        print(f"| {label} | {cn0:+.0f} dB-Hz | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
