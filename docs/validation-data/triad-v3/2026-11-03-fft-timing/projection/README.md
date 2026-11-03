# Clean-signal representation in two −60 dB-Hz missed receptions

The original static two-tap representation retained **56.81% and 56.42%** of
clean signal energy in two selected A850-best observations. The corresponding
representation losses are **2.455 dB and 2.486 dB**. These are measurements at
one original public base group, not an achievable receiver gain or an energy
bound for the complete six-group acquisition bank.

Both observations already belonged to the frozen followup cohort. Trial 2
missed both gates; trial 27 passed the relaxed gate and missed the strict gate.
The diagnostic was selected afterward and changes none of those decisions.

| Observation | Total clean E/N₀ | Retained E/N₀, true bit | Retained fraction | Representation loss | Original winning log-likelihood |
| --- | ---: | ---: | ---: | ---: | ---: |
| A850-best trial 2, bit 0 | 58.0771 | 32.9956 | 56.8135% | 2.45548 dB | 14.41852 |
| A850-best trial 27, bit 1 | 84.2684 | 47.5438 | 56.4196% | 2.48570 dB | 21.37730 |

The unchanged strict and relaxed gates are 22.72106 and 17.42275. The other
bit retained only 0.001396% and 0.003936% of clean energy, respectively, at
this same base group. That separation is diagnostic evidence, not a noisy
correct-reception or wrong-key guarantee.

## What was measured

The fixed native binary regenerated paired clean/noisy grouped statistics
using the original keys, seeds, physical channel and calendar. The receiver
itself was not rerun. This A850-best case uses configured rate 0.001 Hz,
Added Symbols ×1, −60 dB-Hz, Sub9-best 0.001-Hz/0.005-ms spread, Each65,
GPSDO OCXO base clock, zero RF shift and 0.0005-Hz nominal IF, margin 1,
and the tight clock/audio policy. Each original observation contains 576,800
complex samples at 1/128 sample/s. This retains the original sampled-model
limitations; it is not a field measurement.

For the two public columns of a bit, let `G = AᴴA` and `r = Aᴴs`, where `s`
is the simulated clean received signal. Observable eigenspace projection gives
`E_projection = rᴴ G⁺ r`. Both measured bit subspaces have rank two; the
relative rank tolerance is 1e−10. Retained fraction is `E_projection/E_clean`,
and representation loss is `−10 log10(retained fraction)`.

The diagnostic uses the original fractional base arrival, zero common CFO,
zero clock hypothesis, zero drift and the original keyed epoch. It does not
maximize or mix over the two timing lags and three CFO alternatives used by
the operational score. Clean simulator means and truth appear only in this
postdecision evaluation; they never select receiver hypotheses or alter gates.

The missing representation energy cannot be assigned solely to additional
modulation: timing/CFO mismatch, oscillator phase evolution and channel-model
mismatch remain unseparated. Recovering that energy may require more unknown
parameters, whose estimation or marginalization can lose sensitivity. Thus
neither loss is a promised improvement from a more elaborate decoder, an
explanation of the entire score shortfall, or a transmit-duration multiplier.
No new detection probability or minimum-duration estimate is inferred.

## Bindings and independent review

The byte-identical [result](result.json), [plan](plan.json) and
[diagnostic](diagnostic.py) retain the native binary identity, original capture
and score hashes, physical configuration and one-group scope. Export receipts,
grouped manifests and original score reports are in [provenance](provenance).
The [review record](review.json) binds those compact copies.

Independent review rehashed all 16 available paired-stat files (54,966 bytes)
and checked both export commands against the plan. Full-rank complex linear
solves independently reproduced both bits' projected energies and the stated
losses. Original score and capture-record file hashes matched the frozen plan;
original outcomes remain unchanged.

The original raw sampled basis, observation and validity arrays were not
available to this local review. The executed remote diagnostic reports their
hash verification, original-versus-paired Gram relative errors at most
5.85e−14, normalized noisy-correlation errors at most 5.25e−18, and noisy-energy
relative errors at most 3.60e−15. Those remote raw comparisons are distinct from
this review's grouped-byte audit. The subsequently completed
[archive preservation check](../preservation/README.md) independently verified
the original raw bytes; matching metadata alone is not a byte audit.
Clean raw signal samples are not retained.

The original scientific receiver is `f91265b9…`, its Gaussian helper is
`b22fe002…`, and the native binary is `9cdf82f3…`; full hashes are retained in
the plan. The current `d4a3c2cc…` receiver adds reporting text only and does not
replace historical bindings. The diagnostic has SHA256
`ad0fe717665d79b4271aee893a3d96912bd7206e694aa270f9c058c48dff5b16`.

## Optional recount

Restore both original sampled workspaces and paired grouped statistics from
the evidence archive, then analyze into a fresh result path:

```sh
python docs/validation-data/triad-v3/2026-11-03-fft-timing/projection/diagnostic.py analyze \
  --plan docs/validation-data/triad-v3/2026-11-03-fft-timing/projection/plan.json \
  --output-root /restored/clean-projection \
  --original-root /restored/followup/workspaces \
  --output NEW/projection-result.json
```

This performs byte checks and projection analysis, without running a receiver
or regenerating a waveform. Native export is a separate explicit mode requiring
the exact bound binary and fresh outputs. Neither this recount nor the original
long exports are routine regression prerequisites. Ordinary Robust integration
remains outside scope.
