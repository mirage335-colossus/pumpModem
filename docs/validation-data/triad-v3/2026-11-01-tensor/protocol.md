# Tensor frequency contraction experiment

This completed follow-up used source `ebfe649c1f9c0a5bace48594c982111f64c28f41` and the retained 2026-11-01 optimization inputs. The ordinary Robust receiver was outside scope. The fixed physical design, complete candidate bank, phase/frequency/gain grids, encryption, clock-derived keystream, waveform, channel, and admission targets were unchanged. Tensor arithmetic remained opt-in; strict FP32 and FP64 reference paths remained available.

## Frozen screening plan, before GPU results

The study variable was the frequency-transition contraction of the finite-state phase receiver. The comparisons included the graph receiver with FP64 posterior/evidence reductions, a tiled strict-FP32 organization control, explicit rounded TF32, three-product TF32, BF16, column-scaled FP16, and a two-component BF16 representation with three products. All multiplication modes accumulated in FP32. Phase advection, emissions, candidate search and FP64 likelihood accumulation remained unchanged. Quantizing the transition matrix perturbed the stochastic model; no hidden transition-row renormalization was used. Scaling/conversion/decomposition was included in measured kernels. This changed state arithmetic, not ADC observations, and did not introduce a nonlinear observation projection shortcut.

The historical study used the 43-frequency, 512-phase, 24-quadrature, two-candidate workload and screened batch/tile shapes against FP64 calculations on the same represented operands. Its 216 microcases varied posterior patterns and dynamic range; these were arithmetic diagnostics, not reception trials. API acceptance alone did not establish tensor instruction execution, and hardware counters were unavailable.

Complete 929-update hour workloads compared the reference with tensor organizations. The retained results show TF32x3 and split BF16 passing the development numerical gate, while BF16, scaled FP16 and TF32 failed it; complete parent-job medians remained near parity. The 300 physical paths used in earlier work became development evidence once inspected. Later 128-path and conditional-noise comparisons did not qualify either operational tail or the 0.1/0.25/0.5/1 dB loss budgets. See the [tensor study record](README.md) for results and provenance. Do not repeat these full workloads as routine tests; use the maintained [abbreviated triad harness](../../../triad-performance.md) and [material lifecycle policy](../../../development.md#material-lifecycle) for new questions.

## Staged amendment: first-advection fusion

The first-advection fusion experiment followed the initial screen. It preserved index maps and FP32 interpolation order; the original profile suggested at most about a 5% ideal gain from removing that gather alone. The later measured pipeline did not demonstrate a material complete-job improvement, as recorded in the study report.

## Frozen validation policies

The frozen development policies were fused `bf16-split2` and `tf32x3` with 64-column tiles, compared with graph FP64 reductions. Three repeats showed small resident inference changes (about 3.5% and 0.7%) and near-parity complete parent jobs. Subsequent conditional-noise extensions and 128 independent HF-colored paths were paired/reused as documented in the results; they did not qualify operational rare-admission rates or the stated sensitivity-loss budgets. The long-duration linear diagnostic and nonlinear frontend checks had distinct assumptions and were not pooled with reception evidence. These outputs are historical records, not instructions to regenerate or extend the campaign.
