# Explicit source-variant rescoring

`rescore.py` reuses existing hashed physical workspaces after an accepted
scorer change. It never exports a waveform and never edits the original plan,
receipt, or score. Use one manifest per original paired/SNR plan; the same
procedure applies to the paired20dB-Hz screen and−6/0/6/12dB-Hz plans.

This variant separates two changes:

- The negative-scientific-notation CLI fix is an argument-parser compatibility
  change; it does not change likelihood arithmetic.
- Only `shared-microwave-am65` explicitly permits0.02dB original-block
  phase-arc loss, instead of0.01dB. Its reported bound was about0.01408883dB.
  This is a changed approximation allowance, not a parser-only repair or a
  sensitivity improvement. All other cases retain0.01dB, and both reception
  gates and finite-family accounting remain unchanged.

The six original failed jobs remain baseline evidence: three zero-shift VLF
frequency-bank argument failures and three microwave mismatch-guard failures.
The manifest retains every supplied original acquisition record, its digest,
the original receipt hashes, original plan/source/executable identities and
the new scorer closure. Missing or failed exports remain explicit unavailable
dependencies in the original receiver denominator. Never count their absence
as a passed rescore.

Prepare after the new scorer has passed review and focused tests:

```sh
STUDY=docs/validation-data/triad-v3/2026-11-03-extension/physical
python3 "$STUDY/rescore.py" prepare \
  --root "$PWD" --plan ORIGINAL_PLAN.json --input-root ORIGINAL_OUTPUT \
  --prior-results ORIGINAL_CONFIGURE/results.json \
  --prior-results ORIGINAL_EXPORT/results.json \
  --prior-results ORIGINAL_ACQUIRE/results.json \
  --manifest FRESH_VARIANT.json
```

Supply all split pilot/remainder receipts with additional `--prior-results`.
Every referenced output is rehashed during preparation. No old raw I/Q is
required; the original complete grouped workspaces are the inputs.

```sh
TRIAD_PYTHON=/path/to/numerical/python
"$TRIAD_PYTHON" "$STUDY/rescore.py" run \
  --root "$PWD" --manifest FRESH_VARIANT.json --input-root ORIGINAL_OUTPUT \
  --python "$TRIAD_PYTHON" --output-root FRESH_RESCORE_OUTPUT \
  --run-dir FRESH_RESCORE_RECEIPTS --jobs 4 --global-seconds 1800
```

Both output roots must be fresh. The default executes all three methods for
every planned capture. `--only-id rescore-ORIGINAL_ACQUISITION_ID` is an
explicit bounded pilot selection; the receipt preserves both the full and
selected denominator. A later run must explicitly select its intended jobs
and use fresh outputs. No failed job is automatically retried.

The existing bounded child owner handles each300second scorer process, timeout
and reaping. Source and physical input hashes are checked before and after
the stage. Each result retains the actual command, input inventory, new score
hashes, original acquisition identity and independently recounted outcomes.
Inspect status counts and individual failures: exit0 establishes stable
inputs and complete accounting of the selected jobs, not successful reception
or success of every process. Repeated methods/SNR draws are not new independent
physical paths. No resulting small control set qualifies the operational
daily false-admission target.
