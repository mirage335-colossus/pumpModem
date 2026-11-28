"""Theoretical receiver bounds. Run with Python 3.8+ and no dependencies."""

from math import log10


def losses(kept):
    # Coherent signal energy is proportional to the number of retained looks.
    # In independent low-SNR energy looks, deflection squared scales as n * SNR^2.
    return -10 * log10(kept), -5 * log10(kept)


def integer_bounds(tones, symbols=32):
    repeats = (tones + symbols - 1) // symbols
    table_max = 15 * repeats
    energy_max = 2 * (7 * tones) ** 2
    return repeats, table_max, table_max.bit_length(), energy_max.bit_length()


def main():
    print("keep    extra received power (dB)         speedup if tone-dependent")
    print("        coherent   weak independent        share=0.1   0.5   1.0")
    for kept in (0.63, 0.5, 0.4):
        coherent, diffuse = losses(kept)
        speedups = [1 / (1 - share + share * kept) for share in (0.1, 0.5, 1)]
        print(f"{kept:.2f}      {coherent:5.2f}          {diffuse:5.2f}          "
              + "   ".join(f"{value:4.2f}" for value in speedups))

    print("tones     repeats   maximum symbol sum   unsigned bits (sum, |z|^2)")
    for tones in (64, 512, 420_000, 42_000_000):
        repeats, maximum, sum_bits, square_bits = integer_bounds(tones)
        print(f"{tones:8} {repeats:9} {maximum:20} {sum_bits:12} {square_bits:7}")

    # Equal-amplitude IQ (7+7i) reaches the bound with quarter-turn de-keying.
    assert integer_bounds(512) == (16, 240, 8, 25)
    assert integer_bounds(420_000) == (13_125, 196_875, 18, 44)
    # Constructed mask: all received energy on the part erased by the receiver.
    power = (0, 1) * 16
    assert sum(power) == 16 and sum(power[::2]) == 0


if __name__ == "__main__":
    main()
