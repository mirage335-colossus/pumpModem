# Postselected weak-signal precision check

CPU FP64 and CUDA FP32 preserved the original CPU FP32 winning bits and both
admission decisions on three selected observations near the strict threshold.
The maximum change in either bit's mixture log-likelihood was **6.94927e−6**.
This is a precision diagnostic on existing observations, not three new
independent receptions or a replacement for the frozen cohort decisions.

The driver selected the completed signal closest to the strict threshold in
each of three followup families, after seeing the original results. It then
replayed each observation once with CPU FP64 and once with CUDA FP32. All six
runs retained the entire six-group timing/CFO bank, fixed clock/epoch/drift,
noise calibration, priors and thresholds. Only device, precision, memory
allowance, output location and retention of group scores changed.

| Selected observation | Original winning log-likelihood | Original strict margin, nats | Largest mixture change across its two replays | Decisions in all three implementations |
| --- | ---: | ---: | ---: | --- |
| A850-best, trial 27 | 21.37730265 | −1.34376074 | 6.94927e−6 | Strict miss; relaxed correct |
| B425-best, trial 1 | 22.22419494 | −0.49686845 | 5.78204e−6 | Strict miss; relaxed correct |
| C850-moderate, trial 2 | 22.81187971 | +0.09081632 | 6.81548e−6 | Strict and relaxed correct |

The strict gate is 22.7210633896 and the relaxed gate is 17.4227460230.
The smallest tested strict margin is still much larger than the observed
arithmetic changes. These three postselected signals do not establish behavior
arbitrarily close to a threshold, rare noise-only admission calibration, or
hardware portability. No null or wrong-key precision controls were included.
No performance comparison is claimed from these six sequential jobs.

Independent read-only review verified the selection against the frozen
followup results; all six commands against the original plan; every score and
status hash; unchanged source and input bindings; all six normalized group
mixtures; and both bits/admission outcomes. All jobs exited zero with their
process groups empty. Raw input bytes were not rehashed during this review:
input identities were matched to the original capture receipts. Full score
reports and statuses remain in
`local-evidence/triad-fft-timing/gpu/weak-remote/`; their hashes and embedded
completion statuses are retained in the byte-identical [summary](summary.json).

The scientific receiver remains SHA256
`f91265b9d0a6915848be62e2b687982b2b07109d534a8487e137c73377d5b94b`,
with Gaussian helper
`b22fe002864922e44bc7e10000672a163353818f610c4882bae5d62cfe1cf883`.
The later `d4a3c2cc…` entrypoint adds arithmetic-report wording only; it was
not substituted into these receipts. CUDA FP32 is mixed precision: Gram FFTs
and small preparation remain CPU FP64, selected-device whitening and evidence
reductions retain complex128/float64, and FP32 applies to observation FFTs and
the bounded mode multiplication. TF32 was disabled. See the
[parent precision description](../README.md) for these implementation limits.

The byte-identical [driver](weak_precision.py) is bound by SHA256
`95b2510d998dfc50278c2bb5e98500f0d763b5f46e90327c593e65d3f31c60a5`.
It requires the exact frozen plan/source closure, original followup results and
retained workspaces, a CUDA environment, and a fresh output directory. Its
120-second child limit uses the bounded process-group runner. Reproduction is
optional research, not a routine regression prerequisite; it does not generate
new waveforms. The [parent study](../../README.md) retains the physical scope
and the unchanged reception results.
