`actual_window_information.py` connects the **public sampled waveform design** to
the declared windowed Gaussian receiver model. It reads only public fields from
`manifest.json`, the full `gram.c128`, `intervals.u64` and `valid_rows.u64`.
It never reads observed correlations, simulator means, signal energies or bit
truth. Candidate subsets are selected by the number of declared tap powers.

For each candidate, the existing `PreparedWindowedGaussian` module forms the
within-window coefficient covariance using physical block centers and returns
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

Example (declare the C/N0 grid before inspecting receiver outcomes):

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 /tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-02-spread/theory/actual_window_information.py \
  --workspace local-evidence/triad-spread-gaussian-pilot/paths/path-00000 \
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

Focused tests compare the eigenvalue method against a raw-observation covariance
oracle, check the tiny-signal cancellation formula and monotone inversion, verify
that absent or corrupt oracle/noisy files are irrelevant, and reject bad public
calendars, non-PSD Grams and insufficient array budgets:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 /tmp/triad-weak-env/bin/python -m unittest \
  discover -s docs/validation-data/triad-v3/2026-11-02-spread/theory \
  -p test_actual_window_information.py
```
