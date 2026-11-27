# OTHR interference calculations and tolerance simulation (Fable-5.2 pass, 26 November 2026)

Standalone programs behind the OTHR requirement of the [FSK specification draft](../../matrixModem-FSK.md).
They support the [interference note](../../../_ref/algorithms-matrixModem/Fable-5.2/othr-and-interference-tolerance.md)
and the [OTHR literature review](../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-othr-interference.md).
They are analysis aids, not modem code, and use only the standard library.

**Purpose and retention.** Both are theoretical calculations on model radars; their outputs are the
reference for the specification's margin and conditioning statements. Keep them as research
material under the [research-material rule](../../../docs/development.md#research-material). The
radars are built from published and monitored parameters. No recording of a real radar was used.

| Program | What it computes | Output | Runtime (12-thread 5650U) |
| --- | --- | --- | --- |
| `othr_interference_calc.py` | Written by the literature reviewer. Duty, line spacing, time-bandwidth product and notch fractions for the documented radar classes; fraction of 6.25 Hz by 160 ms cells hit for rectangular and Hann windows; blanking duty needed for 20, 30 and 40 dB of suppression after a passband filter; comb-notch residuals with Doppler spread; clipped-metric and erasure losses; worst-case partial-band jamming; regular against pseudo-random interleaver hit fractions | `othr_interference_calc.out.txt` | about a minute |
| `othr_tolerance.cpp` | Time-domain simulation of the 0.4 s lattice receiver against nine model radars at 0–60 dB above the passband noise, for six receiver chains from a naive energy sum to comb excision plus inverse-power weighting plus the 4-bit erasure code. Reports the weak-signal deflection of the 4-bit evidence as received-power margin consumed | `results/othr_tolerance.md` | about 15 min at 160 scenes |

```bash
cd _appraisal/algorithms-matrixModem/othr-interference
python3 -I othr_interference_calc.py > othr_interference_calc.out.txt
g++ -O3 -march=native -std=c++20 -fopenmp othr_tolerance.cpp -o /tmp/othr_tolerance
/tmp/othr_tolerance 160 > results/othr_tolerance.md
```

## Model of `othr_tolerance.cpp`

- One scene is 96 consecutive 0.4 s dwells (1,280 samples at 3,200 Hz, 1,024 active) sharing one
  radar: a periodic linear-FM sweep generated at 12.8 kHz, passed through a 255-tap Kaiser filter
  (−6 dB at ±1,400 Hz) and decimated, so each crossing has the tails of a steep receive filter.
  The line comb sits at a random offset from the tone grid.
- `INR` is the radar power while in band over the noise power in 3.2 kHz.
- Each dwell carries a wanted tone at −6 dB per dwell on a fresh random 6.25 Hz position in the
  central 2.2 kHz. The tone is added before the front end in every chain.
- Result per chain: `d² = (E1[g] − E0[g])² / Var0[g]` of the evidence `g`, relative to `γ²` for an
  unquantized receiver without radar. `10·log10(γ²/d²)` is extra transmit time; the printed value
  is half of it, the equivalent received-power margin.
- 160 scenes of 96 dwells per point give a scatter of about ±0.2 dB. One generator per thread, so
  values move within that scatter with the thread count.

## History of the model

Two earlier versions of `othr_tolerance.cpp` were run during the pass and replaced:

1. An ideal gate on instantaneous frequency instead of a receive filter. It made time gating look
   sufficient: an erasure code alone held the loss near the nominal duty at every level. The
   literature review showed that a steep filter leaves long tails on each crossing, and the
   filtered model confirmed it (the erasure code alone fails above 40 dB).
2. Comb lines on bin centres (50 sweeps/s against 3.125 Hz bins). Per-position normalisation alone
   then looked sufficient at any level, an artefact of zero leakage. A random comb offset removed
   it.

A first excision stage placed after the sample weighting also re-estimated the noise scale on the
weighted stream and collapsed; the stored version excises first and re-estimates on the excised
stream.

## Limits

One radar at a time; coherent or random-phase sweeps only, with no Doppler spread on the radar's
path; one dwell length; medians taken over the whole scene; a weak-signal deflection, not a coded
error rate. The comb stage must not be applied to dense combs (the 40 kHz at 10 sweeps/s case).
