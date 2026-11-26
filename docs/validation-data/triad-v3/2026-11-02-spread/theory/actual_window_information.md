`actual_window_information.py` connects the **public sampled waveform design** to
the declared windowed Gaussian receiver model. It reads only public fields from
`manifest.json`, the full `gram.c128`, `intervals.u64` and `valid_rows.u64`.
It never reads observed correlations, simulator means, signal energies or bit
truth. Candidate subsets are selected by the number of declared tap powers.

For each candidate, the standalone [window-covariance helper](window_covariance.py)
forms the within-window coefficient covariance using physical block centers and returns
the eigenvalues of `Q^(1/2) G Q^(1/2)`. The diagnostic uses unnormalized sampled
Grams and evaluates

\[
D(P_1\Vert P_0)=\sum_{w,j}\left[
\lambda_{w,j}/\sigma_n^2-\log(1+\lambda_{w,j}/\sigma_n^2)\right],
\qquad \sigma_n^2=F_s/10^{(C/N_0)_{\rm dB-Hz}/10}.
\]

This is the exact formula for that **assumed independent-window, block-center,
proper-complex Gaussian signal law**, evaluated numerically in FP64. It is not
the covariance law of the sampled physical FIR channel: that channel evolves
inside blocks and across window boundaries. Known stationary noise and known
candidate synchronization make this a presence oracle. Correct-bit decisions,
unknown-noise estimation, synchronization search and mismatched modeling can
lose further information. No simulation false-admission qualification follows.

The binary data-processing inequality requires `D >= d(p || alpha)` for a
presence decision with detection probability at least `p` and false probability
at most `alpha`. The global strict/relaxed profiles apply to one completion with
the whole daily/12-hour false-admission budget. The separately reported minute,
16-model, 8-duration, 2-bit, half-budget allocation is conditional on using that
allocation; it is not required for every family-wise detector. Inverting this
inequality yields a necessary C/N0 crossing, **not an achievable threshold or
recommended duration**. An optional external comparison C/N0 merely subtracts
that crossing; it does not establish that the resulting gap is recoverable.

**25 November recovery.** The calculation retains the covariance mathematics
from the historical `f13e3bc9` model. Its helper no longer imports the retired
Python receiver or Torch; only the public-Gram eigensolves, PSD checks and
explicit array bounds are retained. No observation scorer or receiver state is
restored. See [source identity and reproduction checks](README.md#reproduction-and-source-identity).

## Retained designs and reproduction coverage

The original information reports for [G001](fixtures/information/g001.json),
[G01](fixtures/information/g01.json) and [G1-short](fixtures/information/g1.json)
are retained byte-for-byte. Each records four public designs, their input and
calculation hashes, and the J=16/32/64 results. They preserve the evidence behind
the [parent study's necessary-crossing table](../README.md#theoretical-limits-and-remaining-gaps),
including cases whose raw public arrays no longer survive.

The four required input files survive for G001 `path-00000` and `path-00001` in
`fixtures/screen/g001/`. No noisy observations, receiver source or campaign
drivers are included. Complete original manifests are retained for hash identity;
the calculation ignores their unused private/oracle fields. These minimal
research inputs are needed to regenerate the sampled-Gram results; the summary
crossings alone cannot reconstruct them.

All eleven retained files were extracted without modification from
`local-evidence/triad-spread-final/evidence.tar.gz` (139,317,355 bytes,
SHA256 `0993791313d687d2742495260266b330ef6f4b7177d4ffa31f687402c06906f0`).
Each archive member path is its path below `fixtures/`: `information/g001.json`,
`information/g01.json`, `information/g1.json`, and each of `manifest.json`,
`gram.c128`, `intervals.u64`, `valid_rows.u64` under the two named G001 paths.
The original reports contain the exact input hashes. Their own SHA256 values are:

| Original report | SHA256 |
| --- | --- |
| `information/g001.json` | `ab9ba250494daa98b7d55d8971795f558edfbe7c97b7b97ec2d1992f94467298` |
| `information/g01.json` | `512a33d3a55dd95614b43b602a0ce80728bdcda2796c9de5c9db4f405e3a20a2` |
| `information/g1.json` | `1f173db4b57190e37f9c4bdf6fd1be1d020de92460b3730d384eed94c6950475` |

On 25 November, a bounded J64 replay of both surviving designs reproduced all
four candidates' KL evaluations and all sixteen necessary crossings within
relative tolerance `5e-12`. The largest crossing difference was
`3.6e-15` dB. The strict global crossings span −31.032896 to −31.030193 dB-Hz;
the strict allocated crossings span −28.836051 to −28.833411 dB-Hz, consistent
with the parent table's approximate G001 values. All eight input hashes match
the original report. The [retained-fixture check](test_retained_windows.py)
repeats these comparisons without generating a waveform or running a receiver.

**Unreplayed original cases:** G001 `path-00002` and `path-00003`, and all four
`path-00000`–`path-00003` designs for each of G01 and G1-short. Their original
reports remain available, but the ten required public Gram/calendar datasets
were not found in the retained archive or the bounded local search. J16/J32
results are also preserved rather than newly replayed here. This recovery is
not a fresh reproduction of the entire crossing table or a receiver
qualification; it does not substitute newly generated inputs for missing ones.

To repeat the bounded retained-input comparison:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 -m unittest \
  discover -s docs/validation-data/triad-v3/2026-11-02-spread/theory \
  -p test_retained_windows.py
```

## Additional public designs

The example below requires an existing exported public-design directory with
the four files named above; the old pilot directory is not a bundled fixture.
Replace `PUBLIC_DESIGN` with that directory, and declare the C/N0 grid before
inspecting receiver outcomes. Use Python 3 with NumPy, SciPy and mpmath:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 \
  docs/validation-data/triad-v3/2026-11-02-spread/theory/actual_window_information.py \
  --workspace PUBLIC_DESIGN \
  --cn0-grid=-60,-50,-40,-30,-20 --window-blocks 8 16 32 \
  --doppler-width .001 --tap-powers .5,.5 --memory-mib 256 \
  --output /tmp/triad-window-information.json
```

Repeat `--workspace` for separate physical designs. Window factorizations are
computed once per workspace/window size and reused for every C/N0 and inversion.
The report includes hashes of the public inputs and implementation dependencies,
candidate eigenvalue hashes, preparation diagnostics and explicit array bounds.
Framework/LAPACK/Python overhead is outside those bounds. The manifest hash is
whole-file provenance; unused private fields cannot affect calculated values.

The focused tests create their own small public-design fixture. They compare the eigenvalue method against a raw-observation covariance
oracle, check the tiny-signal cancellation formula and monotone inversion, verify
that absent or corrupt oracle/noisy files are irrelevant, and reject bad public
calendars, non-PSD Grams and insufficient array budgets:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 -m unittest \
  discover -s docs/validation-data/triad-v3/2026-11-02-spread/theory \
  -p test_actual_window_information.py
```
