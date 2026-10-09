# Local DSSS follow-up evidence, 2026-10-09

See [the maintained report](../../../dsss-followup-validation.md) for scope,
interpretation, baseline/final hashes, validation and open coverage.

- `performance-summary.json`: three paired timings per case, corrected metadata
  from raw rows. Short FFT hypothesis count is deliberately not inferred from its
  three frequency/clock pairs. Long completion fields come from `completions`.
- `performance-short-results.json` and `performance-long-results.json`: raw rows,
  commands, PCM hashes, errors, wall timing and instrumented component runs.
- `performance-inclusive-short-results.json`: one additional complete-lifetime
  pair per short case. `performance-inclusive-normalized.json` names its two
  omitted trailing CSV labels from the frozen driver's emit order; raw evidence
  remains unchanged. Totals include destruction already.
- `holdout-plan.json`, `holdout-family-*.json`, analyzer and statistic harness:
  320,000 fresh paired conditional factor10 draws, not full-bank qualification.
- `final-replay-summary.json`: selected actual raw/automatic PCM replays against
  the final library. This is sampled numerical agreement, not a global enclosure.
- `resolved-configurations.csv` and its probe: current logical waveform geometry
  and default-window planner references. No hardware negotiation was performed.
- `dsss-spectrum-before.csv` and its probe: finite post-limiter baseline waveform
  energy. No waveform change in this follow-up; no hardware emission claim.
- `source-evidence-manifest.json`: exact source/build/probe/evidence identities.

Large PCM files and raw holdout CSVs remain local under `.agent-work/artifacts/`
in the identified experiment directories. All keys are synthetic. The raw
reference remains available. Nothing here certifies a release or substitutes for
full regression/calibration/platform/SDK/packaging qualification.
