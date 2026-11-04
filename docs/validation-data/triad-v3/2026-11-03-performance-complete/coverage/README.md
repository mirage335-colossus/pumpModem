# Python coverage evidence

This directory preserves separate measurements:

- [CPU Python runtime branch coverage](python/README.md), including the original ray-frequency failure and its focused corrected follow-up.
- [GPU phase-family functional coverage](gpu/README.md), collected in a separate Python 3.12 CUDA environment.
- [Fresh CPU capture with declared dependency binding](python/attempt-04-dependency-closed/README.md): a single 16-file passing run after the earlier split evidence.
- [Native follow-up with CLI coverage](native/followup-001/README.md): five production sources, the all-invalid receiver and high-rank Gamma fallback, and guarded atomic counters.

Python reports do not cover native C++ branches. Python coverage arcs are interpreter control-flow arcs; they are not semantic decision, receiver-behavior, or numerical-library implementation coverage. The GPU report demonstrates selected functional checks and Python source arcs, not CUDA instruction coverage or performance qualification. Native counters and their limitations are documented separately.

Each evidence folder includes raw `coverage.py` data, JSON output, logs, source snapshots where the reported hash was available, and `SHA256SUMS`. See the individual READMEs for run identity and limits.
