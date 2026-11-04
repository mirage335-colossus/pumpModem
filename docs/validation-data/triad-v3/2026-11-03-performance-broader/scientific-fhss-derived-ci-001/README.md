# Derived FHSS CI assessment

This case-specific assessment reuses the 33 paired observations from GPU complete
002. Its normal-policy interval remains
`[1.2852248297332862, 1.3068551011650873]`: normal is inconclusive at threshold
1.30. The retained stronger upper bound is below the previously declared CI
threshold 1.70, supporting the derived CI pass criterion.

There are no new measurements, no overwritten normal decision, no measured CI
stopping latency, and no complete-suite CI claim. The exact assessment is retained
with the unchanged adaptive-policy source. `source-context.json` binds that
source to the original measured normal report, which remains in the immutable
GPU complete 002 bundle.

Run `python3 verify.py` to check the original report hash, interval, policy source,
and preserved assessment. This verifier does not run receiver code or retime the
case.
