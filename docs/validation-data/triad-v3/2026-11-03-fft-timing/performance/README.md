# FFT score batching: bounded CPU pilot

This is one measured CPU run on one retained sampled workspace. It is evidence that a score-batched implementation can reduce elapsed work in this case while preserving the reported likelihoods; it is not a sensitivity result, general speed guarantee, GPU result, or throughput qualification.

## Matched result

The bound and batched score calls used the same 95,656-sample, 4,096-Hz capture and the same predeclared bank: 57,632 integer arrival lags over 14.07 seconds, crossed with three fixed CFO alternatives (172,896 total groups). The four input files have identical SHA-256 hashes in `performance-result.json`.

- Bounded wrapper wall time: 17.54985 s → 7.23079 s (2.43×).
- Receiver-reported time: 15.83016 s → 5.45470 s (2.90×).
- Child maximum RSS: 318,236 → 318,152 KiB (difference −84 KiB).
- Both report exactly `[−7.137976509904455, 409.9511665205666]` bit log likelihoods, identical strict/relaxed gates, and identical geometry, calibration and resource metadata.

The proof receipts bind the two runs separately to source SHA `98d619f9…dac6` plus the earlier Gaussian-window helper for the bound run, and source SHA `f91265b9…b94b` plus helper SHA `b22fe002…f883` for the batched run. The other listed dependencies match; both proofs report stable before/after hashes. Full compact hashes and exact invocation arrays are in `score-bound-proof.json`, `score-batched-proof.json`, and `performance-result.json`.

Reproduction uses the proof's argv after restoring the same hashed workspace and source/dependency revisions. The recorded input workspace is held in ignored local evidence and is not copied here. This directory omits the large score arrays and input capture.

The original unbound score attempt is excluded because it cannot establish the exact loaded source. Earlier pre-batch numerical variants that failed edge cases are not treated as a general accepted baseline. CUDA remains untested pending the separate root campaign. Nothing here estimates correct-reception reliability or false-admission probability.
