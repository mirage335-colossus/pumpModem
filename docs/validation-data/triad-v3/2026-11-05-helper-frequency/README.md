# Helper frequency-only narrowing study

This study tests whether Helper pilot spectra can narrow the DATA receiver's
frequency bank **without estimating or using cross-frequency pilot phase**. The
frequency estimator and acquisition adapter are isolated tools; no ordinary
Robust receiver integration was changed. The current Helper integration entry
point is the [Helper integration guide](../../../helper-integration.md).

The main result is bounded: in the independent 103-capture cohort, the adaptive
frequency regions covered the actual nearest original frequency-bank coordinate
in 103/103 trials and reduced a 365-frequency bank to 10–22 candidates (median
11). The complete adaptive receiver call was 1.832× faster than the paired
full-bank call on this CPU for that batch. However, this did **not** establish a
high reception rate or operational false-admission rate: adaptive reception
correctly admitted 25/32 signals, missed seven, and admitted two of 32 nulls.
The method is a useful calibrated search-space reduction under the stated
simulated distribution, not a 99% reception qualification.

## Algorithm and phase guard

[`triad_dynamic_frequency.py`](../../../../tools/triad_dynamic_frequency.py)
profiles the low and high Helper pilot spectra separately, maps their
competitive frequency regions onto the original DATA CFO bank with declared
channel, modulation, oscillator and future-aperture allowances, and selects the
union of covered candidates. It does not apply pilot phase correction, combine
pilots coherently, or narrow the clock bank. If pilots are unusable, an interval
is too broad, a gate fails, or the estimated region expires before the scored
capture ends, the receiver falls back to the complete frequency bank.

The historical `triad_dynamic_acquire.py` driver applied that per-capture
subset while retaining the full-bank public projection, all clock candidates,
and the independent timing/phase guard. In the tested cases,
the Helper phase/timing relation was unresolved, so the guard selected the
DATA-only mode (`helper_phase_timing_unresolved`) for every capture. Frequency
narrowing still ran and retained the correct bank coordinate; it did not rely
on phase-guard success. This separates frequency assistance from phase
assistance and avoids claiming coherent phase authority from spectral evidence.

The two initial diagnostic captures also narrowed the bank (365 to 10 and 11)
and decoded both bits correctly in the raw bit decision, but these were
known-arrival/epoch/clock diagnostics without admission gates. They are not
independent reception qualification.

## Calibration and independent validation

The predeclared HF scenario used 30 MHz shift, free-running crystal base
oscillator and IC-7100 shift oscillator, 4 W DATA / 40 W Helper, 180 dB path
loss, 1.725 kHz DATA IF, 36 Hz inner rate with Added Symbols ×10, encrypted
DATA with Each65 channel modulation, HF-worst spread at margin 3 (90 Hz
Doppler, 21 ms delay), a 0.5 Hz random-ray width, and 0.05 Hz selective
modulation. DATA occupied 23.2 seconds; the full capture calendar
lasted 62.267 seconds and the Helper calendar extended to 61.6 seconds. The
native physical link budget was
20.02 dB-Hz DATA C/N₀, but this deliberate stress test injected DATA C/N₀ of
8 dB-Hz and Helper C/N₀ of 18 dB-Hz. It is therefore incorrect to read the
8 dB-Hz injected case as the nominal 4 W/180 dB physical link result.

The original finite frequency bank had 365 candidates over approximately
±90.522 Hz at 0.5 Hz spacing. Search scoring conditioned on known arrival
0.013 seconds, epoch zero, and a singleton 0 ppm clock bank; oscillator effects
were enabled. The campaign does not qualify unknown arrival, epoch, clock, or
full joint acquisition.

Frequency coverage and reception gating used separate, disjoint native seed
sets. The frequency-region policy was calibrated on 99 DATA-absent captures;
its conformal rank was 99 at `β=0.01`. All 99 calibration CFO coordinates were
covered, and every measured nonconformity `R` was zero. The empirical
nonconformity quantile was therefore zero, and the configured engineering
competitive cutoff remained 18. A separate 39-capture DATA-absent
set calibrated each receiver arm's per-capture admission gate at `α=0.05`.
Then 32 independent signal and 32 independent null captures were scored by
both arms with separate frozen gates. The frequency/admission/validation phases
used distinct seeds but the same encryption key (2409); the eight earlier
diagnostics used key 2403. The coverage calibration's 99% statement is a
marginal split-conformal target under exchangeability and the reviewed complete
selection/fallback procedure, not a conditional-on-narrowing or instantaneous
Doppler guarantee.

The exact paired-procedure gate identities are adaptive
`b74362cffe69a74f72e7ab31e47dcd0f52e482980661fd7b35f1c56bd341075d` and
full-bank `bc5e425bcafa937d1cbd31e39ead85878f6d0ff6d8150d83c4162725e268ba8b`.
Each gate has 39 calibration scores, `α=0.05`, and minimum attainable rank
`p=0.025`; neither is a daily false-admission guarantee.

| Held-out result | Adaptive Helper frequency | Full frequency bank |
| --- | ---: | ---: |
| Signals | 32 | 32 |
| Correctly admitted | 25 | 24 |
| Wrong-bit admissions | 0 | 1 |
| Misses | 7 | 7 |
| Raw bit errors before gate | 4 | 4 |
| Nulls admitted | 2/32 | 0/32 |
| Actual nearest CFO coordinate covered | 103/103 | — |
| Frequency candidates per adaptive capture | 10–22; median 11 | 365 |

The adaptive balanced correct-admission fraction was 25/32 = 0.781; its
pointwise 95% interval was `[0.4746, 0.9549]`. The full-bank interval was
`[0.4414, 0.9409]`. These intervals are conditional on independent
representative trials within the two fixed bit strata and their frozen gate;
they are not simultaneous comparisons. For 103/103 observed CFO-coordinate
coverage, the one-sided exact 95% upper bound on the miss probability is
2.87% (lower coverage bound 97.13%), so this held-out count alone does not
establish a 99% population-coverage bound. The null samples also leave broad
uncertainty: adaptive 2/32 has one-sided 95% upper false-admission bound 18.4%;
full-bank 0/32 has upper bound 8.94%. Neither is a rare-event or daily
false-admission qualification.

## Processing cost and precision

On the recorded CPU, the two-capture diagnostic scored in 9.140 seconds
adaptively versus 135.240 seconds full-bank (14.80×). That tiny batch had very
expensive one-time full-bank preparation. For the 103-capture cohort, that
preparation was amortized: adaptive complete scoring took 159.244 seconds
(88.122 frequency estimation, 47.710 tile preparation, 23.124 candidate
scoring), versus 291.703 seconds full-bank (125.691 preparation, 164.798
candidate scoring), a 1.832× batch speedup. The 103-capture end-to-end receiver
campaign took 453.098 seconds including input loading/hashing and both arms;
native capture generation was external. Thus the two-capture speedup should not
be extrapolated to batches where preparation is shared.

The spectral front end uses complex FP32 FFT data; the Whittle profiles/fits
and reductions used CPU FP64. The 88.122 seconds of adaptive frequency
estimation across 103 captures was material after batching. A whole-estimator
diagnostic call took about 0.8 seconds per capture; no isolated FFT-library
timing was measured. This work ran on a
Ryzen 5 PRO 5650U with NumPy 2.5.4, SciPy 1.18.1 and CPU-only PyTorch 2.8.0.
CUDA was unavailable; there is no GPU throughput or precision-dropdown claim.
GPU-batched profile fitting remains unqualified.

## Strong selective-modulation diagnostic

A separate frequency-only AM90 diagnostic checked the default policy and an
opt-in zero-modulation-allowance policy using a unity-core working model. In the bounded-arrival geometry,
the default policy fell back to all 365 candidates on 8/8 captures because the
frequency allowance was too broad; the opt-in zero allowance narrowed all 8/8
to 10–11 candidates and covered the nearest original coordinate in 8/8. This
is only a small diagnostic (95% exact interval 63.1–100%); its calibration
under the actual AM90 background mismatch remains unqualified, and no DATA
receiver was run.

With the broad arrival geometry, both policies fell back to all 365 candidates
on 8/8 because no Helper pilot was usable: the guaranteed 36-second Helper
window overlapped DATA support and both pilot fit windows overlapped the public
DATA envelope. This is a conservative estimator guard, not a demonstrated
physical impossibility. Broad-geometry AM90 narrowing remains unqualified.

## Identity, evidence and limits

The compact evidence in [`evidence/`](evidence/) contains exact protocols,
calibration and gate receipts, paired summary, independent audit, compressed
trial rows, environment metadata, and source snapshots. The 47,909,809-byte
raw paired-row JSON was compacted to 3,838,814 bytes before gzip (749,008 bytes
archived) by removing only the `grid_hz` and `profile_scores` arrays in each
pilot diagnostic (412 arrays, each 2,895–2,899 values). The remaining rows keep
selected frequencies, raw bit likelihoods, truth, candidate work and coverage
indicators; the raw original remains in ignored `local-evidence` and its hash
and size are recorded. Raw I/Q, validity, templates and the full calibration
profile archives are not copied into this evidence directory; their hashes
remain in the receipts.

The 61-test suite completed in 10.507 seconds: 59 passed, two CUDA-dependent
tests skipped because CUDA was unavailable. After the focused result-identity
fix, 18 acquisition tests passed in 1.002 seconds; 17 overlap the prior suite.
The current expanded 62-test set was not rerun wholesale. The fix changes the
default full-bank result identity (omitting repeated full-bank lists from the
hash input) while retaining explicit adaptive subset identity; focused tests
confirmed one- versus two-capture default identity and scores. The reception
gates and procedure IDs below are bound to the archived pre-fix acquisition
source snapshot. They are **not** implicitly current-source gates after the
identity change; any use under the new procedure identity needs fresh gate
calibration.

The measured base revision was `fefa8ce0c081b66d4075624f72326fad1373966d`;
the exact Python sources/tests are included under
[`evidence/source-snapshot/`](evidence/source-snapshot/). Native source and
binary hashes remain in the preserved study protocols. The environment and
raw-input hashes are listed in the same evidence directory.

## Minimal checks and diagnostic invocation

Use the captured numeric Python environment (Python 3.13.5, NumPy 2.5.4,
SciPy 1.18.1, CPU PyTorch 2.8.0). The system Python attempt lacked NumPy and
failed before running this algorithm; that was an environment setup failure.
The three focused test modules can be run with:

```sh
TRIAD_PY=/tmp/triad-weak-env/bin/python
"$TRIAD_PY" -m unittest discover -s tests -p 'test_triad_dynamic_frequency.py'
"$TRIAD_PY" -m unittest discover -s tests -p 'test_triad_dynamic_acquire.py'
"$TRIAD_PY" -m unittest discover -s tests -p 'test_triad_dynamic_frequency_study.py'
```

The saved schedule and compact evidence package retain the historical inputs,
source identities and calibration/validation provenance needed to interpret
the reported result. The copied recipe programs and archived protocols are
historical artifacts, not current checkout tooling; do not restore them or use
them to launch another cohort. Any necessary compute-heavy Robust check follows
the [hierarchical abbreviated method](../../../triad-performance.md), with
temporary evidence governed by the [material lifecycle](../../../development.md#material-lifecycle).

This study does not show 99% correct reception, a rare false-admission rate,
all-arrival acquisition, continuous-frequency worst-case coverage, physical
HF radio performance, sustained performance, or GPU acceleration. The
frequency-only result isolates a computational benefit while preserving
important search dimensions and fallback behavior; reception quality and
operational calibration remain separate requirements.
