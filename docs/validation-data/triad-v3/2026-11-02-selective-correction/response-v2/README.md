# Corrected dispersive response probe, settled rerun

The first probe's `corrected_results.jsonl` is retained as an original failed
diagnostic. Its fixed 5,000-second warm-up was shorter than the 8,888.9-second
maximum branch delay at 0.000075-Hz spacing. Therefore its large error at that
rate was caused by residual prehistory, not evidence of interpolation error.
The following rerun uses a warm-up of `ceil(tail_samples/Fs) + 1000 seconds`
for every profile and replaces the invalid result.

This accepted probe is in `dispersive_probe.cpp`; its complete JSONL output is
`response-results.jsonl`. SHA-256 values in this checked copy are
`c9fed6aab032dc44425858b33f07f3f7bf0652c2fbdd29549937c0583ee5c29a` for the
source and `bf1edb64821c6c8d37e72e6e1cbfaf1f8d0611cb986672aa1cf255cfab6175d4`
for results. Build and run from the repository root:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Isrc -Iinclude \
  docs/validation-data/triad-v3/2026-11-02-selective-correction/response-v2/dispersive_probe.cpp \
  -o /tmp/triad-corrected-probe-v2
/tmp/triad-corrected-probe-v2 > /tmp/triad-corrected-response-results-v2.jsonl
```

The original transient-contaminated v1 diagnostic remains in ignored local
evidence only. Its low-rate interpolation-error figures are withdrawn; this
version uses a warm-up of `ceil(tail_samples/Fs) + 1000 seconds` for every
profile.

There are 48 target-spacing cases: spacing 0.00075 Hz; seeds A and B; Each 25%
and 65%; Fs 0.25, 0.5, 1, and 2 Hz; offsets 0, 0.025, and 0.05 Hz. Ten
rate-scaled cases add spacing 0.000075 Hz at those three offsets and both
depths, plus 0.0075 and 0.075 Hz at zero offset. The sample rates, slow and
fast modulation rates (0.005/0.05 Hz), occupied bandwidth (3× spacing), and
20,000-second measurement duration are common. Each measurement duration is
an integer 100 periods of the slow component and 1000 of the fast component.
The adaptive warm-up covers the class's complete finite FIR support plus 1,000
seconds; the longest case discards 9,890 seconds. Maximum simultaneous versus
isolated output error is 2.11e−16.

At the target spacing, Each65 seed A adjacent complex-response RMS for offsets
0/0.025/0.05 Hz is 0.9816/1.0824/1.1621 at Fs=1 Hz; seed B at 0.05 Hz is
0.9933. Each25 seed A is 0.3775/0.4163/0.4470. Across Fs 0.25–2 Hz and the
three offsets, finite-versus-ideal RMS errors for Each65 at seed A range from
8.4e−10 to 0.017; the largest is Fs=0.25 Hz, offset=0.05 Hz. At Fs=1 Hz the
Each65 errors for offsets 0/0.025/0.05 are about 4.4e−12 / 6.7e−6 /
1.04e−4. Each25's maximum over the same Fs/offset set is about 0.0065.

For the 0.000075-Hz scaling at Fs=1 Hz, offset 0/0.025/0.05, the settled
Each65 finite-vs-ideal RMS errors are approximately 1.9e−13 / 6.5e−6 /
1.03e−4; Each25 errors are about 7.2e−14 / 2.5e−6 / 4.0e−5. Thus the earlier
0.1946/0.0749 error claim is retracted. The complete machine-readable results
include per-profile warm-up, FIR tail, sideband coefficients, complex and
amplitude/phase differential RMS, and ideal-response comparison.

This isolates tone-level operator behavior and numerical approximation. It
does not establish propagation calibration or receiver sensitivity.

## Response comparison

[The three-panel plot](response-comparison.png) compares actual adjacent
complex-response, amplitude-difference and wrapped-phase RMS at spacing
0.00075 Hz and Fs=1 Hz for Each25/Each65. It shows the three common measured
offsets, 0, 0.025 and 0.05 Hz, with corrected seeds A/B and legacy seed A.
Lines only connect those points. The legacy probe did not save adjacent
amplitude-difference RMS; that panel explicitly omits legacy curves rather
than deriving them from per-tone magnitude RMS.

The plot's four inputs are maintained in this directory:

- [Corrected results](response-results.jsonl), 58 profiles, and their
  [probe source](dispersive_probe.cpp), with the hashes given above.
- [Frozen legacy results](legacy-response-results.jsonl), **30 profiles**,
  SHA-256 `89b4463a9e0003faa593d29c8727843eadbcbe6f56f3826fb3ae68435fe40a48`.
- [Frozen legacy probe source](legacy_probe.cpp), SHA-256
  `26fecc0ae5f8e9e2e3be0dc72a3d097095b45496d4214430f79a19895c362dd0`.
  This source establishes Fs=1 Hz and seed A, which its historical JSONL
  did not record.

[The plotting script](plot_response.py) verifies all four exact input hashes
and embeds their paths/hashes and its own source hash in PNG metadata. No
ignored local-evidence files or new probe executions are needed. From the
repository root, using a Python environment with Matplotlib installed:

```sh
MPLCONFIGDIR=/tmp/triad-selective-mpl python3 \
  docs/validation-data/triad-v3/2026-11-02-selective-correction/response-v2/plot_response.py
```

The study's rendering environment was `/tmp/triad-weak-env/bin/python`.
The script also accepts `--output` and explicit input-path overrides; input
overrides must preserve the frozen hashes. Regeneration is byte-identical
within that environment when input/source paths and the script are unchanged.

The legacy measurement spans 19,000 seconds after a 5,000-second discard;
the selected corrected measurement spans 20,000 seconds after a settled
1,890-second discard. Both windows contain integer slow/fast modulation
cycles, but their start times, transient treatment and phase wrapping are
not interchangeable. These illustrative one-/two-seed tone diagnostics are
neither confidence intervals nor receiver-sensitivity measurements.
