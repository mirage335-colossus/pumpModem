# Meteor-burst link arithmetic

Theoretical calculation for the Matrix Modem FSK appraisal: what a 2–100 mW transmitter gets from
underdense meteor trails. Formulas are from ITU-R P.843-1 and P.372; the trail rate is anchored
to one measured link (Spann 1987). Sources and the reading of the results are in
[`_ref/algorithms-matrixModem/Fable-5.2/meteor-burst.md`](../../../_ref/algorithms-matrixModem/Fable-5.2/meteor-burst.md).

```bash
g++ -O2 -std=c++20 meteor_burst.cpp -o meteor_burst
```

```bash
./meteor_burst > results/meteor_burst.md
```

Assumptions, all of which favour the link: trail above the path midpoint, trail axis normal to
the propagation plane, the trail-formation and absorption losses left out, the anchor link's
trail rate kept for every antenna. Underdense trails only (up to 2×10¹⁴ electrons/m).

## Whole shared sky

`meteor_sky.cpp` is a Monte Carlo over trail position and direction for two stations 50 to
1,300 km apart: how many trails an hour deliver a tone's or a block's worth of energy to a
100 mW or 2 mW link whose receiver has gain in every direction, and how many of those a radar at
the transmitting site would also see. Model and omissions are in the program header.

```bash
g++ -O2 -std=c++20 -fopenmp meteor_sky.cpp -o meteor_sky
```

```bash
./meteor_sky 8000000 > results/meteor_sky.md
```
