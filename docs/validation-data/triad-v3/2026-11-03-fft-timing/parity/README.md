# Native sampled parity receipt

This is a bounded parity check for the isolated FFT timing implementation, not a modem sensitivity trial. The actual native binary was reused without rebuilding: `build/dev-cli/triad_long`, SHA-256 `9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47`.

## Inputs and scope

Paired native sampled observations use identical public arrival prior `[-4/1024, +4/1024]` seconds for the zero-lag base and direct-arrival reference banks. The short cases use 8 s requested active time; the burst case uses 40 s. All use Fs=1024 Hz, total C/N0=12 dB-Hz, a two-ray basis with a 0.5 ms fractional tap and 0.1 Hz Doppler, fixed public epoch/clock/frequency, and identical legacy/key/channel/oscillator/noise/frontend/path seed roots within each pair. Both direct and FFT receive the same observation and validity mask, checked by byte hash.

- `flat-fractional-ray`: rate 36, channel preset none, selective modulation off.
- `hf-Each65-FHSS-burst-rate1200`: rate 1200, HF-best, Each 65% at 0.1 Hz, FHSS envelope, 3 internal burst wait tiles, and missed receive dwells `[0,11,21]`.
- `hf-Each65-FHSS-burst-rate1200-wait40-wait40/`: a 40-second request at rate1200 with the same stress settings and missed dwells `[0,187,373]`. The repeated `wait40` directory suffix is retained from the completed export wrapper.

The second fixture uses rate 1200 because native configure rejects rate 36 with FHSS (`triad v3 FHSS has no protected dwell cells`). The same rejection was observed at rates 72 and 360. A rate640 stress capture was generated before root settled the rate1200 fixture; it is retained as exploratory evidence under `hf-Each65-FHSS-burst/` and excluded from the accepted parity comparison.

The original rate1200 8-second fixture did not actually cross a private-tile boundary: configure reports 8.8 seconds elapsed with either 0 or 3 internal wait tiles. It is therefore labeled configured-but-inactive for burst waits. For the 40-second request, wait0 gives 44 seconds elapsed and wait3 gives 149.6 seconds, a 105.6-second increase. The actual native-shaped basis has five 8.8-second active tiles separated by four groups of three 8.8-second wait tiles. In each wait interior (excluding one second on each transition edge), both complex template columns are exactly zero: max magnitude 0, energy 0, and zero nonzero samples over 24,984 or 24,985 samples. Details are in the 40-second fixture's `burst-evidence.json`.

## Results

`parity-result.json` compares the FFT full four-column Gram and correlation banks to independent direct complex128 dot products formed from separately exported native direct-arrival templates, using actual received samples and mask at all nine integer lags. It also compares every group and total calibrated likelihood to the native `export-group-stats --stats-noisy` direct receiver with the same nine timing hypotheses and whole-observation block.

- Flat: maximum absolute Gram error `1.82e-11`; correlation error `1.11e-11`; per-group log-likelihood difference `2.82e-12`.
- HF stress: Gram error `2.73e-12`; correlation error `9.17e-13`; per-group log-likelihood difference `2.27e-13`.
- 40-second burst stress: Gram error `2.55e-11`; correlation error `1.12e-11`; per-group log-likelihood difference `2.16e-12`. This run used the zero-support repaired FFT source SHA-256 `415d624bbd44dd593dc29bebc2f511d4b3c4df8f716d50256e1f21d89768d77e`; the two short fixture FFT runs used prior source `2eb4c48dfae832e748ab687b2a8da60acc41e560f2a3c9adb40cf84b6bad1e29`.
- Both paths select bit 1 and pass strict and relaxed gates for these realized captures. This is a deterministic parity outcome, not a probability estimate.
- HF stress validity mask has 1,229 erased samples of 9,177 in three separate runs (indices 99–507, 4604–5013, 8700–9109). Flat has no erasures.

## Reproduction

The recorded fixture-generation commands are `python local-evidence/triad-fft-timing/native-parity/run_parity.py` (flat and exploratory rate640), `python local-evidence/triad-fft-timing/native-parity/run_rate1200.py` (8-second stress fixture), and `python local-evidence/triad-fft-timing/native-parity/run_rate1200_wait40.py` (40-second burst fixture). The longer-capture receiver commands and exact accepted source hash are in `receiver-run-receipt.json`; the original invocations are also retained in their logs. The raw verification command is:

```sh
/tmp/triad-weak-env/bin/python local-evidence/triad-fft-timing/native-parity/verify_parity.py
```

The scripts use one CPU thread and cap each child at 60 seconds, with native exports capped at 512 MiB and the FFT workspace at 512 MiB. The parity does not qualify fractional timing loss, nonzero residual CFO/drift search, clock/epoch acquisition, long-term phase tracking, device acceleration, physical RF retuning, or correct reception rates. The grouped/FFT score model is the fixed whole-observation two-ray Gaussian case only.

## Provenance and reproduction limits

`source-hashes.json` binds the FFT and native binary versions to each result. The copied Python scripts are exact evidence copies from the ignored local run directory; their original invocations refer to `local-evidence/triad-fft-timing/native-parity/`. The scripts are not standalone from this promoted documentation directory because their generated capture arrays were intentionally excluded. To reproduce, rebuild the three sampled workspaces with `build/dev-cli/triad_long` using the recorded `--mode export-sampled` and `--mode export-group-stats` argument arrays in the original native status/log receipts, preserving each fixture's seed roots and explicit arrival prior. Then run the scoring and raw-product verifier with the listed source hashes. The promoted result records observation/mask/template hashes and all parity metrics; it does not claim a portable capture archive.

## Independent final-source review replay

`reviewer-final-source-replay.json` records the extension receiver reviewer's replay on the same declared 149.6-second workspace using source SHA `f91265b9…b94b` and Gaussian helper SHA `b22fe002…f883`. It reports three focused tests passing and maximum group-score difference `2.16005e-12` versus the native grouped receiver. The file is explicitly a **post-execution transcription of retained tool stdout**, not a prebound runner receipt or independent input-byte attestation. It is separate from the owner's 40-second parity run bound to source `415d624b…d77e`; do not merge their score values. The reviewer confirms acceptance only for the stated fixed clock/epoch, single fractional anchor, whole-observation two-tap scope.
