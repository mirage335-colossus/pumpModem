"""Finite rational calculations for noiseless tone labels and lost hops."""

from fractions import Fraction
from math import comb, prod


def expected_distinct(positions, repeats):
    # For each output tuple, apply inclusion/exclusion to the 256 input bytes.
    group_size = 256 // positions
    occupied = Fraction(0)
    for size in range(1, group_size + 1):
        subsets = comb(256, size)
        in_group = Fraction(comb(group_size, size), subsets)
        occupied += (-1) ** (size + 1) * subsets * in_group**repeats
    return positions**repeats * occupied


def expected_rivals(bits, positions, repeats_per_symbol):
    distinct_bytes_match = Fraction(256 // positions - 1, 255)
    pair_matches = prod(
        Fraction(1, 256) + Fraction(255, 256) * distinct_bytes_match**repeats
        for repeats in repeats_per_symbol
    )
    return (2**bits - 1) * pair_matches


def binomial_tail(hops):
    cutoff = 77 * hops // 100
    return Fraction(sum(comb(hops, k) for k in range(cutoff + 1, hops + 1)), 2**hops)


def persistent_tail(hops, switch):
    # Joint distribution of (hops lost, last hop bad), stationary before hop 1.
    states = {(0, False): Fraction(1, 2), (1, True): Fraction(1, 2)}
    for _ in range(1, hops):
        updated = {}
        for (lost, bad), prob in states.items():
            for next_bad, transition in ((bad, 1 - switch), (not bad, switch)):
                key = (lost + int(next_bad), next_bad)
                updated[key] = updated.get(key, 0) + prob * transition
        states = updated
    assert sum(states.values()) == 1
    return sum(prob for (lost, _), prob in states.items() if 100 * lost > 77 * hops)


def main():
    print("P  L   expected distinct/256   optimal uniform byte error")
    for bits in range(1, 9):
        positions = 2**bits
        repeats = (8 + bits - 1) // bits
        distinct = expected_distinct(positions, repeats)
        assert 1 <= distinct <= 256
        assert expected_distinct(positions, 1) == positions
        print(f"{positions:3} {repeats}   {float(distinct):10.6f}            {float(1 - distinct / 256):.6f}")
    # For P=16 with two maps, the second group must miss all 16 first-group bytes.
    assert expected_distinct(16, 2) == 256 * (1 - Fraction(comb(240, 16), comb(256, 16)))

    print("\n24-bit payload, P=16, expected number of other matching messages")
    for label, repeats in (("32 symbols twice", [2] * 32),
                           ("6 symbols once", [1] * 6), ("3 symbols twice", [2] * 3)):
        print(f"{label:17} {float(expected_rivals(24, 16, repeats)):.8g}")

    print("\nIndependent loss, probability of losing >77%")
    for hops in (32, 33, 34):
        print(f"{hops} hops: {float(binomial_tail(hops)):.9f}")

    print("\nStationary symmetric loss, 32 hops, probability of losing >77% and all 32")
    assert persistent_tail(32, Fraction(1, 2)) == binomial_tail(32)
    for run_mean in (2, 4, 10, 50):
        tail = persistent_tail(32, Fraction(1, run_mean))
        all_lost = Fraction(1, 2) * (1 - Fraction(1, run_mean))**31
        print(f"mean bad run {run_mean:2} hops: {float(tail):.9f}  {float(all_lost):.8g}")


if __name__ == "__main__":
    main()
