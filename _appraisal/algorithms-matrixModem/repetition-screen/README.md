# Two-look self-reference screen


This bounded Monte Carlo asks whether correlating one noisy tone projection
with a recent projection can help when the propagation coefficient persists.
It isolates two complex observations at one already-known frequency and timing.
It does not implement or measure a modem, search, FHSS schedule, physical
channel, or GPU kernel.

## Model

Under the null, `z1` and `z2` are independent circular complex Gaussian noise
with unit variance per look. Under the signal hypothesis, each look has
independent unit-variance noise plus a jointly circular Gaussian fading signal
of power `rho` per look. The two fade coefficients have correlation
`gamma * exp(j phi)`. The run fixes `phi=0` and uses `gamma` values 0, 0.5,
and 1, while holding total average received signal energy at `2*rho`.
Consequently, `gamma=1` is a persistent, phase-aligned fade model, `gamma=0`
has independent looks, and intermediate gamma values have partial persistence.
This is a deliberately small stationary Gaussian reference model. It omits
frequency-dependent hopping effects, delay spread, Doppler trajectories,
oscillator and timing error, interference, bursty noise, code structure, and
nonstationarity.

Four statistics are separately thresholded. The ordinary sum of the two
projection energies discards their phase relation. The real cross product
assumes the relative phase is known and aligned. Its absolute complex magnitude
does not need a known phase but is not a likelihood ratio. The fourth statistic
is the likelihood ratio for the stated, fully known two-look covariance model.
Training null, held-out null, and signal trials use distinct seeded random
streams. Each statistic receives its own empirical 1% threshold on the training
null, followed by a separate held-out false-alarm estimate and Wilson 95%
interval. Each table row uses 250,000 trials in each group. The implementation
also checks noise-only analytic moments before producing results.

## Reproduction and interpretation

From this directory, run:

```sh
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic repetition_screen.cpp -o repetition_screen
./repetition_screen results.csv
```

The fixed `rho` and `gamma` grids, seeds, statistical definitions, and model
are in the source. The CSV is an appraisal result for those finite trials.
Finite Monte Carlo intervals quantify only binomial sampling error within this
model. They do not represent uncertainty in an HF channel population.

The test can reveal whether two-look energy, phase relation, or the matched
covariance score gains power under particular fade persistence at the same
total received signal energy. A large gain at `gamma=1` would support testing
history-informed projections on measured persistent channels. It cannot show
that the receiver can identify which history entry to compare, know its phase,
frequency, timing, hop, or symbol label, or maintain that relationship after a
frequency change. For an unknown frequency and timing, every history cross
product adds competing null hypotheses, so a single-look false-alarm threshold
does not control the whole search. Noise-only training in this toy also assumes
the observer or receiver has a correct null calibration.

Self-reference therefore cannot replace a static or publicly specified
template bank on this evidence. A receiver still needs an acquisition path to
find candidate frequency and timing, and a way to disambiguate the desired
label from noise, interference, and other tones. History is a possible
additional likelihood term after candidate association, subject to measured
channel coherence and a whole-search false-alarm calibration. These results
make no sensitivity, LPI, bitrate, RF energy, or Tensor Core utilization claim.

## Observed toy-model results

At `rho=1` per look, held-out detection probability for summed energy was
0.157 (95% interval 0.155–0.159) at `gamma=0` and 0.165 (0.164–0.166) at
`gamma=1`. The correctly specified covariance LLR gave 0.157 (0.155–0.159)
and 0.218 (0.216–0.220), respectively. At `rho=4`, energy gave 0.620
(0.618–0.621) for independent looks and 0.539 (0.537–0.541) for fully
persistent looks. The known covariance LLR gave 0.620 (0.618–0.621) and 0.598
(0.596–0.600). At the same `rho=4`, the phase-aided real cross product alone
gave 0.230 (0.228–0.231) for independent looks, then 0.581 (0.579–0.582)
for fully persistent looks. A cross-product-only receiver throws away useful
energy evidence and performs poorly when persistence assumptions are wrong.

Empirical held-out null rates span 0.0097–0.0107 across all 48 rows. Their
Wilson intervals generally contain the target 0.01; the source's broad
0.2-percentage-point sanity check rejects gross calibration failures, not
subtle deviations. The unadjusted binomial intervals do not account for
choosing thresholds on a separate empirical training sample. The toy
differences are statistically resolvable at this simulation size but do not
show their practical size for real Matrix waveforms.

`results.csv` is retained only to support this current research conclusion.
Remove it after its numerical findings are reconciled into the maintained
Matrix research synthesis. The small executable source remains the reproducible
test, not a production implementation.
