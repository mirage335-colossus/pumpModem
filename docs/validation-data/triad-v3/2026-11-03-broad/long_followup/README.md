# Long observation followup

This plan separates resource recovery, compute throughput and fresh physical
validation. It inherits the accepted fixed long-rescore manifest, original
850-day native configurations, original binary/archive and all prior workspace
hashes. It does not modify the frozen reproduction or rescore studies.

| Arm | Jobs | Explicit change |
| --- | ---: | --- |
| `resource` | 8 | Same repaired Gaussian observations/arguments; memory cap0.5→2GiB,600s child bound |
| `benchmark` | 4 | One retained rate.001/Sub9-worst/bit0 path; static FFT, CPU/CUDA×FP32/FP64,32 conditional draws/batch, C/N₀−66/−63/−60/−57, signal+null,8GiB |
| `configure` | 136 | Native preflight for every fresh physical export |
| `export` | 136 | Four physical configurations×32 fresh signal paths, plus one fresh null and wrong-key control per configuration |
| `actual` | 136 | Fixed static FFT, original gates, FP32/CUDA, one thread,2GiB, actual sampled observations |

Resource jobs require the preceding fixed Gaussian result to contain a
`MemoryError`. A successful2GiB run establishes recovery from the smaller
declared resource cap; it is not an algorithmic sensitivity improvement.
The four throughput jobs run sequentially and share a physical path/noise
address. They measure computation, not independent-channel reception.

Each fresh signal cell has16 bit0 and16 bit1 paths. Case indices follow the
original long plan's four-configuration order. For signal trial `t=0..31`,
the seed address is `30000 + 100*case_index + t`; null and wrong-key controls
use reserved trial40 and41. All seven source/key/channel/oscillator/noise/
frontend/path seed domains remain distinct. Apart from these seeds, bit,
control and output path, every native physical argument matches the original
long export. Calendar duration is genuinely850 days, not an energy-scaled
short capture. No early stopping or outcome-driven method selection is added.

Preparation validates all original and fixed receipt identities/source hashes,
all original workspace bytes and inventories, original native build provenance,
and the accepted fixed scorer import closure. The followup adds its own frozen
script hashes. Existing sources and outputs are read-only inputs. A changed
input invalidates a stage.

Run the small structural/atomic-output checks locally:

```sh
/tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-03-broad/long_followup/test_plan.py -v
```

After the accepted fixed source tree contains these new files, freeze remotely
without launching a receiver:

```sh
triad_py=/opt/venv/bin/python
triad_fixed=/workspace/triad-fixed
triad_follow=docs/validation-data/triad-v3/2026-11-03-broad/long_followup
triad_manifest="$triad_fixed/local-evidence/triad-broad-coverage/long-followup/manifest.json"
triad_out="$triad_fixed/local-evidence/triad-broad-coverage/long-followup-results"
"$triad_py" "$triad_fixed/$triad_follow/prepare.py" \
  --fixed-manifest "$triad_fixed/local-evidence/triad-broad-coverage/rescore/long-manifest.json" \
  --fixed-receipts "$triad_fixed/local-evidence/triad-broad-coverage/long-fixed-results/actual-run/results.json" \
  --output-root "$triad_out" --output "$triad_manifest"
```

Use the verified numerical interpreter path; the runner preserves its virtual
environment invocation rather than resolving its executable symlink.

Begin independent validation with the four predeclared trial0 paths, one per
configuration. Their IDs are printed by preparation and recorded in
`pilot_export_ids`. The configure IDs prepend `configure-`; actual score IDs
prepend `actual-` and append `-static-fft-ucb`. For example:

```sh
"$triad_py" "$triad_fixed/$triad_follow/run.py" --manifest "$triad_manifest" \
  --stage configure --python "$triad_py" --jobs 4 --global-seconds 120 \
  --run-dir "$triad_out/pilot-configure" \
  --only-id configure-validation-long-rate-0.001-sub9-best-t00-signal \
  --only-id configure-validation-long-rate-0.001-sub9-worst-t00-signal \
  --only-id configure-validation-long-rate-0.01-sub9-best-t00-signal \
  --only-id configure-validation-long-rate-0.01-sub9-worst-t00-signal
```

Then select the corresponding four export IDs, pass the
configure receipt with `--prior-results`, use a fresh run directory and allow
at least620 seconds of stage budget for a600-second child. Score those four
export workspaces using their completed export receipt. The pilot measures
actual export cost; it does not select or prune subsequent physical paths.
For the remainder, select only unexecuted IDs and provide all earlier receipts.
An already accounted job cannot be implicitly retried.

Resource and benchmark stages have only original export dependencies already
bound through the fixed manifest. They use their own fresh run directories:

```sh
"$triad_py" "$triad_fixed/$triad_follow/run.py" --manifest "$triad_manifest" \
  --stage resource --python "$triad_py" --jobs 8 --global-seconds 3600 \
  --run-dir "$triad_out/resource-run"
"$triad_py" "$triad_fixed/$triad_follow/run.py" --manifest "$triad_manifest" \
  --stage benchmark --python "$triad_py" --jobs 1 --global-seconds 3000 \
  --run-dir "$triad_out/benchmark-run"
```

Do not overlap export and score stages. Configure/export worker caps are20;
actual/resource caps are8; benchmark cap is1. Every export/score child has a
600-second bound, configure has10 seconds, and a stage has at most7200 seconds
of dispatch budget. The owned child runner verifies exit status and complete
process-group cleanup. All jobs, including dependency failures, numerical
errors, timeouts and deadline skips, receive atomic per-job receipts and appear
in the atomic final stage receipt. Partial data files from failed children are
retained for diagnosis, but never accepted as completed dependency inputs.

The136 workspaces should occupy roughly4.4GB before manifests/tail variation,
using about31MB per long statistics export. The source export limits remain
128 million rows and2GiB. The four-case pilot determines whether the expected
roughly20-minute parallel export estimate is realistic on this host; it is not
a measured promise. Numerical/framework memory exceeds explicit array caps,
so worker limits remain material even on a large-memory GPU.

Report correct admissions, wrong admissions, missed opportunities, null
admissions and wrong-key admissions separately, with32 signal paths/model and
per-bit16-path results visible. Failed signal processing stays in attempted
denominators; failed controls remain unresolved. In the all-success case,
32/32 yields a one-sided95% lower bound of approximately0.9106 for the equal-bit
mean under independent paths (the all-success probability is at most the mean
success probability raised to32). This does not establish99%, and requires
keeping the fixed-size independent-path design. For other outcomes, use an
explicitly justified stratified/binomial interval and report its assumptions.
The four null and four wrong-key controls are diagnostics, not rare-event
qualification. Preserve raw scores to compare both predeclared gates, and keep
partial completed conditions separate from later failures in throughput jobs.
