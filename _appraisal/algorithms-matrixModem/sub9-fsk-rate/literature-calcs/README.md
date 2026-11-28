# Scratch calculations of the delegated literature searches (27 November 2026)

The four literature searches behind the
[sub-9 kHz rate and spacing note](../../../../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md)
made their own small calculations ("my calc", "own calc") in a temporary directory. The programs
are kept here unchanged below a provenance header, with the output of a rerun on 28 November, so
that the numbers in the stored
[reviews](../../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/) can be reproduced.

**Purpose and retention.** Theoretical calculations, one small Monte Carlo and one transcription
of measured data. Research material under the
[research-material rule](../../../../docs/development.md#research-material). None of them is
modem code and none was reviewed line by line by the coordinating session; the main programs one
directory up are the ones the note's own figures come from.

| Program | What it computes | Quoted in |
| --- | --- | --- |
| [`coherent_fsk_calc.py`](coherent_fsk_calc.py) | Correlation of two FSK tones against spacing (0.64 at half spacing with a 90° offset); the `M/(M−1)` penalty of coherent orthogonal signalling; coherent gain against window length for a Lorentzian line and a Gaussian Doppler spectrum; the coherent window that maximises a deflection measure for a given linewidth and C/N₀ | [Coherent FSK review](../../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-coherent-fsk-rate-spacing.md), sections 1 to 3 |
| [`search_grid_calc.py`](search_grid_calc.py) | Worst and mean loss of frequency steps of 1/2 to 1/8 of the window's reciprocal and of timing steps of 1/8 to 1/40 of a tone; single-pole antenna amplitude, phase and group delay against offset; power lost to the keying transient against `B·T`; cutoff-rate limits | The same review, sections 1, 2 and 5 |
| [`trials_cost.py`](trials_cost.py) | Exact energy needed for a false-presence rate of 10⁻⁶ shared among 1 to 10¹² hypotheses at a miss rate of 10⁻³; loss of adding 1 to 1,024 segments as energies at those rates | [Long-coherence review](../../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-long-coherence-search.md), sections 1.3 and 4 |
| [`frequency_wander.cpp`](frequency_wander.cpp) | Monte Carlo, 600 trials: coherent power kept when the carrier frequency random-walks during the window, for a search over frequency alone and over frequency and drift | The same review, section 1.5 |
| [`modes_calc.py`](modes_calc.py) | Ideal-waveguide cut-off ladders for reflection heights of 70 to 90 km, phase and group velocity, differential group delay between modes, hop delays against the ground wave, interference distance, mode-conversion coefficient | [Phase-stability review](../../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-sub9khz-phase-stability.md), appendix C |
| [`phase_statistics.py`](phase_statistics.py) | Standard deviation, successive differences and measurement-noise correction of amateur phase series at 8.27 to 8.82 kHz over 881 to 7,173 km, and the group delay from a two-tone series 60 Hz apart | The same review, section 2 |

Run commands are in the head of each file; outputs are in [`results/`](results/).

Limits:

- `frequency_wander.cpp` prints nothing useful in its frequency-only column for five of its nine
  rows: its drift grid misses zero there. The review says so and quotes that column only for
  `q` = 0.25, 1 and 4. The frequency-and-drift columns are unaffected.
- `phase_statistics.py` holds the phases and bin signal-to-noise ratios as the search transcribed
  them from public mailing-list posts of 2014 to 2017 (listed in the review's sources). They were
  not re-read against the posts for this directory.
- `modes_calc.py` is the ideal waveguide with perfectly conducting walls and two cut-off rules;
  the review states where the real waveguide departs from it.
- A fetched-figure crop and the downloaded sources the searches worked from were temporary and are
  not kept; the reviews list each source with its access level.
