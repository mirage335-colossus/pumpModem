# Structured wrong-key admission: bounded source and sample-domain diagnosis

The corrected Gaussian-window receiver still admits the Aurora-worst / Each65
wrong-key capture at 80 dB-Hz. This investigation found no same-key substitution
or compact-statistics extraction discrepancy. It identified a concrete limitation
of using a short-block residual noise estimate to judge a strong, structured
wrong-key signal: the local calibration span absorbs **99.8968% of its energy**.
This is separate from the repaired high-C/N₀ numerical null-admission defect.
It is not evidence that wrong-key rejection is inherently impossible.

The [compact evidence record](wrong-key-diagnosis.json) binds measurements,
commands, input hashes, source hashes and binary identity. This is one selected
failure diagnosis, not an independent reception or false-admission campaign.

## Capture and replay identity

The original workspace is
`spread-aurora-worst-wrong-key-cn80`: encrypted, rate 360 Hz, Added Symbols ×1,
30 MHz shift, 1,725 Hz nominal IF, OCXO/OCXO, margin 1, bounded 70 ms audio
uncertainty, Aurora-worst two-ray separation 55 Hz / delay 11 ms, and Each65
additional modulation. The carrier spacing is 75 Hz and nominal combined
bandwidth 225 Hz; selected channel effects broaden that spectrum. Sampling is
4,096 complex samples/s, with 16-sample blocks (3.90625 ms) and 16-block
likelihood windows (62.5 ms). There are 38,188 samples over 9.323242 s.
Gaussian coefficient renewal width is zero; this does not remove the selected
55 Hz deterministic ray separation or frequency-selective modulation.

The [replay script](../../../../../local-evidence/triad-broad-coverage/admission-diagnosis/replay_wrong_key_raw.py)
retained all original seed arguments and physical options, changed export mode
to `export-sampled`, and capped output at 128 MiB. The exact executed child and
bounded-run argument arrays are in the JSON. The 30-second bounded child
completed successfully in **0.25149 seconds**; source and binary hashes were
unchanged before/after. The local binary SHA-256 was
`ba965b7a8d844bbee67e39ff8a5b5d4292a0c44b89d0fce4aeba2c42db8d7a34`.
Its four checked modem/export source hashes match the frozen campaign plan;
the local binary is not asserted identical to the separately compiled cloud
binary. Raw artifacts and scratch scripts are ignored local evidence, not
files guaranteed present in a fresh checkout.

The inspected [exporter](../../../../../tools/triad-long.cpp) derives the
transmitter key from `key_seed` and the wrong receiver key from
`key_seed ^ 0x6a09e667f3bcc909ULL`. Its truth waveform uses the transmitter
key; candidate patterns use the receiver key. The inspected
[pattern source](../../../../../src/triad_modem_v3.cpp) and
[crypto implementation](../../../../../src/crypto.cpp) consume those supplied
keys. This is a bounded source-path audit, not an independent cryptographic
audit.

## Independent raw projection

The [raw analysis](../../../../../local-evidence/triad-broad-coverage/admission-diagnosis/wrong-key-raw-analysis.json)
uses sample-domain SVD projectors, equivalent to rank-aware QR projection, with
the same 10⁻¹⁰ relative Gram-rank policy. It computes residual norms directly.
Recomputed compact Grams, correlations and received energies agree with the
original export to maximum absolute errors **2.13×10⁻¹⁴**, **1.59×10⁻¹⁴** and
**2.84×10⁻¹⁴**, respectively. This rejects an extraction-error explanation for
this capture.

Four unique full-bank nuisance columns are fitted independently in each block.
Of total received energy 47,532.75925, the blockwise projection captures
47,483.72044 and leaves 49.03880 over 29,320 complex residual degrees of freedom.
The resulting variance UCB is **0.0017312361**, versus injected thermal variance
0.00004096. It is 42.27 times thermal noise, but observed average signal-plus-noise
power is 1.244704, about 719 times that UCB.

Repartitioning the same raw data makes the short local span's flexibility visible:

| Independently fitted span | Full-bank captured energy | Residual variance UCB |
| --- | ---: | ---: |
| 3.90625 ms | 99.8968% | 0.00173124 |
| 15.625 ms | 78.3407% | 0.295283 |
| 62.5 ms | 22.9803% | 1.00297 |
| 250 ms | 6.5833% | 1.20296 |
| Entire 9.323242 s capture | 0.221283% | 1.28017 |

These are diagnostic projections, not newly qualified receivers. A heavily
oversampled narrowband waveform is locally smooth; four freely fitted complex
columns can explain most of a short slice despite using the wrong key.
Even the more constrained bit-0 two-tap fit captures 5,816.624 energy when
repeated over 62.5 ms windows. Dividing that by the short-block UCB explains
the scale of the corrected receiver's bit scores, approximately
**3,356,432 and 3,204,572 nats**, against a 22.721-nat admission threshold.
Even the whole-capture bit-0 projection retains 66.257 energy, so a very
strong wrong-key signal can produce substantial coherent evidence.

The residual-UCB/noise-likelihood argument assumes stationary white proper-complex
Gaussian H₀ and its declared search family. A structured wrong-key transmission
is outside that null. Its residual is not a calibrated covariance model for its
remaining colored projection. The numerical repair preserves the intended
Gaussian-H₀ calculation; it does not establish key validity, correct-bit
reception, or immunity to signal/model mismatch. The nominal candidate arrival
also differs from physical arrival by about 54.93 ms; this fixed candidate bank
does not establish complete acquisition coverage.

## Existing consistency guard and remaining limits

The existing optional [pattern-consistency postprocessor](../../2026-11-02-acquisition/README.md#experimental-pattern-consistency-and-duration-followup)
compares a fixed bit across windows with a window-bit-switch proxy. Replaying
its preselected `C_winner >= ln(100)` AND guard gives
**C = −1,143,564.26**, rejecting this capture. Among 150 windows, bit-1 minus
bit-0 score differences are positive in 72, negative in 67 and zero in 11.
The [guard diagnostic](../../../../../local-evidence/triad-broad-coverage/admission-diagnosis/wrong-key-consistency-envelope.json)
retains exact scores and hashes.

This proxy is not a calibrated wrong-key Bayes factor or authentication test.
Its AND form preserves any valid existing noise-only bound by removing
admissions. Prior independently held-out high30/Each65 tests rejected all eight
wrong-key cases at each of 12 and 20 dB-Hz, but reduced legitimate 8-dB-Hz,
246.4-second reception from **16/16 to 15/16**. Those small cells do not establish
rare wrong-key rejection or 99% correct reception. No new receiver or guard was
implemented in this followup; broader validation and a defensible structured
alternative remain outstanding.
