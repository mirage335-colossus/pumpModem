# Explicit source-variant rescoring

The archived `rescore.py` reused hashed physical workspaces after an accepted
scorer change; it did not export waveforms or edit the original plan, receipts
or scores. It used separate manifests for the paired/SNR plans, the paired
20dB-Hz screen and the −6/0/6/12dB-Hz plans.

This historical variant separated two changes:

- The negative-scientific-notation CLI fix is an argument-parser compatibility
  change; it does not change likelihood arithmetic.
- Only `shared-microwave-am65` explicitly permits0.02dB original-block
  phase-arc loss, instead of0.01dB. Its reported bound was about0.01408883dB.
  This is a changed approximation allowance, not a parser-only repair or a
  sensitivity improvement. All other cases retain0.01dB, and both reception
  gates and finite-family accounting remain unchanged.

One manifest was used per original paired/SNR plan, including the paired
20dB-Hz screen and −6/0/6/12dB-Hz plans. The six original failed jobs remain
baseline evidence: three zero-shift VLF
frequency-bank argument failures and three microwave mismatch-guard failures.
The manifest retains every supplied original acquisition record, its digest,
the original receipt hashes, original plan/source/executable identities and
the new scorer closure. Missing or failed exports remain explicit unavailable
dependencies in the original receiver denominator; their absence was not
counted as a passed rescore.

This source-variant rescore is recorded as completed historical analysis. Its
six failed baseline jobs, changed phase-arc allowance, scorer/source identities
and missing dependencies remain explicit evidence above and in the retained
receipts; a rescore cannot turn unavailable exports into successes or repeated
scores into independent physical paths. The archived scripts are not a current
replay workflow. For any necessary current compute-heavy Robust check, follow
the [hierarchical abbreviated method](../../../../triad-performance.md) and
the [material lifecycle](../../../../development.md#material-lifecycle).
