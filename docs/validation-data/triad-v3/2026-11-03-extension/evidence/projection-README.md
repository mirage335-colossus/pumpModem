# Why one 6-dB-Hz missed-hop capture was missed

The independent `loss-random25-validation-11-b1-signal` capture selected the
correct bit but scored **9.067**, below the strict **22.721** and relaxed
**17.423** gates. This evaluation used its already scored 455 timing groups and
three public CFO alternatives; it introduced no receiver hypotheses or truth
refinement. [Raw results and input hashes](projection-result.json) bind the
[diagnostic source](projection-diagnostic.py) and receiver version.

| Evaluation of clean signal after erasures | Retained energy | Energy/noise, ρ |
| --- | ---: | ---: |
| Complete captured signal | 100% | 69.8894 |
| Best global two-tap fit, true bit | 42.009% | 29.3601 |
| Independent two-tap gains every 62.5 ms, true bit | 62.153% | 43.4386 |

The actual receiver chose the same timing/CFO group as the best clean-signal
global projection: group 256, zero CFO. Thus choosing a different existing
group does not recover the missing energy. The selected arrival differs from
truth by 0.159 ms and CFO by 0.001228 Hz. Finite-grid mismatch, phase evolution,
selective modulation and channel-model mismatch remain unseparated; the loss
cannot be attributed solely to added modulation.

Global representation retains **3.767 dB less energy**. For comparison, the
favorable known-waveform, known-phase/channel/noise presence calculation is
`Pd = Φ(sqrt(2ρ) − Φ⁻¹(1−α))`. At the same component allocation
`α = exp(−22.7210634) = 1.35634e−10`, 99% requires `ρ = 37.331`.
Complete energy gives `Pd ≈ 0.999999982`; inserting the retained global energy
gives approximately **91.1%**. These are conditional genie comparisons, not
achievable performance estimates or a bound for the entire hypothesis bank.

The segmented fit is an **oracle flexibility diagnostic**, not a demonstrated
improvement: its wrong-bit retained fraction also rises from **0.606% to
5.718%**, and fitting many unknown gains costs noisy detection sensitivity.
Neither these projections nor one missed reception determine a duration target.

Reproduction requires the retained workspace and its original score JSON:

```sh
python projection-diagnostic.py --tools-root /path/to/bound/tools \
  --workspace /path/to/loss-random25-validation-11-b1-signal \
  --score-report /path/to/rescore-global-frequency-loss-random25-validation-11-b1-signal.json \
  --window-blocks 2 --output /fresh/result.json
```

The copied script preserves SHA256
`2efc92fc99bc4c1e4c4408e301e78118abe2b97cd4a255a82233d0f3d7555630`.
Its identity/rank-deficient/zero sanity checks passed. The remote diagnostic
took **1.282 s internally / 2.232 s including launch**, but peak RSS was
**747,552 KiB (730 MiB)**: the estimated 512-MiB resource target was **not met**.
Allow at least 1 GiB for reproducing this capture; no memory optimization or
additional campaign was performed.
