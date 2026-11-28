"""Finite probability calculations for the Matrix FSK presence tests."""

import math


def gamma_survival(shape, threshold):
    """Tail probability for an integer-shape, unit-scale Gamma variate."""
    if threshold == 0:
        return 1.0
    log_terms = [i * math.log(threshold) - math.lgamma(i + 1) for i in range(shape)]
    peak = max(log_terms)
    return math.exp(peak - threshold) * math.fsum(math.exp(x - peak) for x in log_terms)


def gamma_threshold(shape, probability):
    low, high = 0.0, float(shape)
    while gamma_survival(shape, high) > probability:
        high *= 2
    for _ in range(64):
        mid = (low + high) / 2
        if gamma_survival(shape, mid) > probability:
            low = mid
        else:
            high = mid
    return high


def energy_survival(shape, threshold, energy):
    """Poisson-Gamma mixture. The excluded Poisson mass is below 1e-14."""
    k = 0
    weight = math.exp(-energy)
    terms = []
    while True:
        terms.append(weight * gamma_survival(shape + k, threshold))
        next_weight = weight * energy / (k + 1)
        next_ratio = energy / (k + 2)
        if next_ratio < 1 and next_weight / (1 - next_ratio) < 1e-14:
            return math.fsum(terms)
        k += 1
        weight = next_weight


def median_reference_tail(bins, threshold, power_ratio):
    """Test exponential vs independent sample median divided by log(2)."""
    rank = (bins + 1) // 2
    s = threshold / (power_ratio * math.log(2))
    return math.prod(rate / (rate + s) for rate in range(bins - rank + 1, bins + 1))


def premature_completion(length, marginal_miss, persistence):
    """Stationary two-state Markov misses. Accumulate first adjacent miss pair."""
    if length < 2:
        return 0.0
    on_entry = marginal_miss * (1 - persistence) / (1 - marginal_miss)
    hit, miss = 1 - marginal_miss, marginal_miss
    ended = 0.0
    for _ in range(1, length):
        ended += miss * persistence
        hit, miss = hit * (1 - on_entry) + miss * (1 - persistence), hit * on_entry
    return ended


def main():
    n = 64
    alpha = 1e-6
    assert abs(gamma_threshold(1, alpha) + math.log(alpha)) < 1e-12
    assert math.isclose(energy_survival(n, n, 0), gamma_survival(n, n))
    assert math.isclose(median_reference_tail(1, math.log(2), 1), 0.5)
    raw_threshold_floor = -n * math.log(alpha)
    # At t=1/2, Chernoff for a sum of independent |CN(mean,1)|^2.
    log_energy_bound = -raw_threshold_floor / 2 + n * math.log(2) + n
    print(f"raw common threshold >= {raw_threshold_floor:.8f}")
    print(f"raw-max Pd <= {alpha:g} + {math.exp(log_energy_bound):.3g}")
    threshold = gamma_threshold(n, alpha / 2)
    assert math.isclose(gamma_survival(n, threshold), alpha / 2, rel_tol=1e-12)
    print(f"separate per-tone threshold {threshold:.8f}")
    print(f"two-branch Pd >= {energy_survival(n, threshold, n):.8f}")
    for bins in (31, 63):
        for ratio in (1, 2):
            p = median_reference_tail(bins, -math.log(alpha), ratio)
            print(f"median reference {bins:2d}, ratio {ratio}, Pfa {p:.9g}")
    holds = math.ceil(math.log(0.05) / math.log1p(-alpha))
    print(f"zero-event iid trials for 95% upper bound <= {alpha:g}: {holds}")
    for persistence in (0.001, 0.5, 0.95):
        p = 0.001
        pair = premature_completion(2, p, persistence)
        assert math.isclose(pair, p * persistence)
        print(f"persistence {persistence:g}, pair {pair:.8g}, "
              f"any pair in 100 active blocks {premature_completion(100, p, persistence):.8g}")


if __name__ == "__main__":
    main()
