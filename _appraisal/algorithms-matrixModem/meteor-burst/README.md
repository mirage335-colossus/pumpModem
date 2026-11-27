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
