# Fast coding study evidence — 2026-09-19

Read the [study and development priorities](../../../fast-coding-study.md) for
interpretation. These are archived research results and standalone diagnostics;
they do not add a production LDPC/RS mode or change the current Fast format.

## Inventory

| Artifact | Contents and limits |
|---|---|
| [capacity.csv](capacity.csv) | 120 ideal-AWGN constellation points, 30,000 symbol observations each. CM, matched bit GMI, grid-scaled max-log estimates, standard errors and Gaussian references; not decoded files. |
| [gmi-crosscheck.csv](gmi-crosscheck.csv) | Independent 200,000-observation NumPy check of the two 256-point mappings at 20 dB. Supporting results only: the original cross-check script was not archived. |
| [capacity.png](capacity.png), [plot_capacity.py](plot_capacity.py) | Reviewed plot derived from capacity.csv. Uses ReportLab for a temporary PDF and Poppler for PNG rendering; neither is a modem dependency. |
| [ldpc-all.csv](ldpc-all.csv) | 33 pilot, supplemental and extended decoder runs. Each row preserves code, modulation, Es/N0, frame count, seed and geometry. Pilot and longer validation runs remain separate rows. |
| [ldpc-candidates.csv](ldpc-candidates.csv) | Three 800-word extensions at rate 7/9, one each for 256/1024/4096-QAM. A subset of ldpc-all.csv, not additional independent trials. |
| [ldpc-benchmark.cpp](ldpc-benchmark.cpp), [ldpc-method.md](ldpc-method.md) | Standalone encoder/channel/decoder harness, pinned upstream dependency, commands, likelihood and decoder assumptions, CPU timing limits. No outer RS, integrity field or PCM framing in these trials. |
| [ldpc-upstream-license.txt](ldpc-upstream-license.txt) | License of the external LDPC implementation. Its library headers are not vendored here. |
| [sampled-channel.csv](sampled-channel.csv) | 32 current-receiver PCM points, 64 physical intervals each. Contains acquisition/alignment/completion checks, erasures and empirical soft-bit metrics; not LDPC decoding or hardware qualification. |
| [sampled-channel.cpp](sampled-channel.cpp), [sampled-channel-method.md](sampled-channel-method.md) | Production-modem-linked diagnostic and reproduction instructions, including AWGN normalization and the synthetic acoustic echo. |
| [rs-model.csv](rs-model.csv) | Independent-erasure model applied to ldpc-candidates.csv for 5,000,000 source bytes, eight modeled integrity bytes/word and a minimum repair/data fraction of 0.003. |
| [rs-model-100kb.csv](rs-model-100kb.csv) | Same calculation for 100,000 source bytes, permitting zero outer repairs. |

## Units and interpretation

- File sizes are decimal bytes, not KiB/MiB. Code lengths n and k are bits;
  whole-word FER is distinct from bit error rate. RS repairs are whole shards,
  with parallel 16-bit RS stripes inside each shard.
- The capacity CSV labels SNR in the occupied band. The LDPC CSV records
  Es/N0; subtract `10*log10(1.2) = 0.79181246 dB` to compare with the study's
  in-band SNR because these reference profiles have B/Rs=1.2.
- LDPC `geometry=qam_sep` uses exact separable I/Q likelihoods for the same
  Gray square QAM as `qam`. It does not mean a different constellation.
- The RS model's `coded_payload_bits_per_symbol` is the LDPC input rate,
  before modeled integrity, outer repairs, padding or waveform overhead.
  `fer_upper95` is a one-sided confidence bound, not the measured FER.
  Optimizing at that bound is a sensitivity calculation, not proof that such
  parity is necessary. Zero observed failures do not establish zero risk.
- The selected 0.003 repair/data floor is configurable. It is not itself a
  validated two-second interruption guarantee. Do not combine independent
  erasure predictions with a burst budget by counting the same repair twice.
- The approximately 0.89 dB shaped-code result has no measured CSV here: it is
  a literature-based extrapolation, with formulas and assumptions in the study.

## Reproduction and future additions

The study gives the build/run command for
[fast_capacity.cpp](../../../../tools/fast_capacity.cpp). The two method files
give the LDPC and PCM commands. The LDPC dependency is pinned to upstream commit
`32357d8ad55a6a302c34e093759f0454e45cca56`; the PCM/capacity diagnostics link the
repository's built modem libraries and therefore change when that code changes.
Record the modem commit and build options when producing new results.

From the repository root, reproduce the RS models without overwriting evidence:

```sh
python3 tools/fast_coding_analysis.py \
  docs/validation-data/fast/coding-study-20260919/ldpc-candidates.csv \
  --bytes 5000000 --integrity-bytes 8 --min-repair-fraction 0.003 \
  > /tmp/fast-rs-5mb.csv
python3 tools/fast_coding_analysis.py \
  docs/validation-data/fast/coding-study-20260919/ldpc-candidates.csv \
  --bytes 100000 --integrity-bytes 8 --min-repair-fraction 0 \
  > /tmp/fast-rs-100kb.csv
```

Use a new dated directory for new channel/code experiments. Retain the exact
settings, source size, seed/sample count, hardware/channel description, SNR
definition and uncertainty. Preserve unfavorable and failed points alongside
winners. Identify selection runs versus independent validation; do not pool
duplicate rows or infer full-file reliability from a bit-metric estimate.
