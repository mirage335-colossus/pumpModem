"""Finite ten-bit AWGN counterexample. See the linked fault-envelope derivation."""

from math import exp, factorial, log, log10, sqrt
from statistics import NormalDist


def main():
    words = 1024
    error = 0.10
    looks = 16
    frequencies = 355
    threshold = 44.52

    # For one noise-only column, the sum of independent unit exponential
    # powers has an integer-shape gamma upper tail.
    tail = exp(-threshold) * sum(threshold**i / factorial(i) for i in range(looks))
    family_bound = frequencies * tail
    assert family_bound < 1e-4

    # A real projection along the true mean gives a sufficient condition on
    # the norm. The tested detector itself needs neither phase nor true bin.
    line_energy = (sqrt(threshold) + NormalDist().inv_cdf(1 - error) / sqrt(2)) ** 2
    required_information = (log(words) + error * log(error)
                            + (1 - error) * log(1 - error) - error * log(words - 1))
    print(f"False waterfall detection per quiet block <= {family_bound:.9g}")
    print(f"Sufficient line energy/N0 for 90% detection = {line_energy:.6f}")
    print(f"Necessary modulation energy/N0 for 90% correct = {required_information:.6f}")
    print("Modulation share | Visibility sufficient (dB) | Word recovery necessary (dB) | Gap lower bound (dB)")
    for fraction in (0.1, 0.01, 0.001, 0.0001):
        visible = 10 * log10(line_energy / (1 - fraction))
        recovery = 10 * log10(required_information / fraction)
        print(f"{fraction:>10.4g} | {visible:.4f} | {recovery:.4f} | {recovery - visible:.4f}")


if __name__ == "__main__":
    main()
