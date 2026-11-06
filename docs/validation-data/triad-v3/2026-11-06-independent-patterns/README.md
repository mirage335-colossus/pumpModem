# Independent bit patterns and power-balanced APSK spreading

The current default is `independent-apsk-v2`. See the [format and development guide](../../../triad-patterns.md) for exact construction, replay compatibility and cheap commands. This is an isolated triad change; ordinary Robust framing/receiver code was not integrated or qualified.

## Implementation and security scope

Amplitude and phase domains are independently keyed for each bit alternative. Added Symbols now uses independent two-radius/two-phase 4-APSK factors, followed by common three-carrier coefficient-power normalization. This preserves relative amplitudes and phases while keeping coefficient power three. The final per-carrier absolute alphabet has more than four points because the common normalization varies. Depth defaults to 0.25; 0.10/0.25/0.50 were compared before the independent cohort.

The old fixed cyclic amplitude transform was reproduced: its best fixed permutation mean-square error was approximately 6e-33. With the new codewords it was 0.145 in the initial fixture. The new amplitude-power correlation between alternatives was 0.0365 rather than the old deterministic cyclic relationship. Tests also check phase/sign/conjugation relationships, exact legacy goldens, valid constellation factors, protected pulse support, chunk boundaries and multi-day random access. These are implementation diagnostics; cryptographic security rests on the keyed primitives and domain separation, not those few statistics. Replay authentication remains outside this waveform change.

The source duration, rate/spacing, pulse shape, interleaving boundaries, dwell calendar, protected allocation, Helper lead/waveform and power split are retained. New work occurs at tile generation, not per received/transmitted sample. Plans, exports, power references and stream descriptors carry revision/depth identity. Old self-contained basis recordings remain readable; regeneration requires the explicit matching revision.

## Actual composite-envelope measurements

The main source campaign rendered 28 source probes covering 14 paired configuration/seed combinations across two new seeds, in addition to four initial depth/revision probes. Both alternatives were measured from the actual native samples. The strict engineering screen is maximum window-RMS departure from its observed median no greater than 0.5103 dB: one quarter of the smallest base amplitude step, `10*log10(1.6)`. Sliding windows are correlated; these are observed extrema, not population confidence bounds.

For FHSS and Symbol Burst, window energy is compared with the known squared-envelope duty exposure. This separates intentional silence/ramping from unintended modulation without altering emitted samples. Uncorrected gaps still exist. The 800-ms FHSS result does not claim continuous emission across a hop gap.

| New waveform configuration | Maximum departure at 100 ms | At 800 ms | Smallest tested window passing the quarter-step screen |
|---|---:|---:|---:|
| Rate 36, Added 10, interleaved; two independent seeds, both bits | 0.666 dB | 0.081 dB | 150 ms |
| Rate 36, Added 1 | 10.294 dB | 0.842 dB | None through 800 ms |
| Rate 3.6, Added 1000; two seeds | 0.083 dB | 0.010 dB | 50 ms |
| Rate 36, Added 10, interleave off | 0.904 dB | 0.113 dB | 200 ms |
| Rate 360, Added 1, microwave-style FHSS; two seeds | 4.308 dB | 0.152 dB | 400 ms |
| Rate 36, Added 10, three wait tiles | 1.072 dB | 0.242 dB | 400 ms |
| Rate 24000, Added 1; two seeds | 0.012 dB | 0.002 dB | 50 ms |
| Rate 0.1, Added 1 | 49.81 dB | 35.65 dB | None |
| Rate 0.001, Added 1000 | 33.81 dB | 21.92 dB | None |

The initial depth screen reached 0.709 dB at 100 ms and 0.0785 dB at 800 ms for depth 0.25; it is not counted as independent validation. Coefficient summed-power errors stayed around 1e-15. That does **not** imply that composite envelope is constant.

For the concrete 0.00075-Hz spacing case, a reciprocal-rate long source window cannot resolve 100 ms. A separate test therefore rendered 24 predeclared dense windows at 100 samples/s across 800,000 seconds of calendar, under both revisions. The new source's 100/800-ms mean power reached about −28.3 dB relative to its sampled median; the old source also had deep nulls. These are actual short-window samples at different long-duration coordinates, not a continuous 100-Hz recording of those days.

This failure has a physical explanation. Local three-tone power retains cross terms scaled by `sinc(delta_f*W)`. At 0.00075 Hz, 100 ms retains 0.999999991 of the adjacent beating term; 800 ms retains 0.999999408. The outer-carrier term cannot vanish identically while both outer carriers remain nonzero. **The universal low-rate sub-800-ms requirement is not achieved.** Increasing modulation rate/spacing or changing the waveform substantially is necessary for such a guarantee. This implementation neither clips/normalizes the composite sample nor silently expands bandwidth.

## Spectrum, energy and receiver measurements

Continuous source windows typically put around 1–3e-5 of Hann-periodogram energy outside the nominal three-carrier band, with no systematic new increase. The FHSS cases retained their real ramp sidebands: roughly 0.0025–0.0033 outside the *nominal* band, which is narrower than the shaping planner's placement bandwidth. Neither figure is a hard mask or a Helper interference guarantee.

Ordinary interleaved continuous fourth moments were about 1.66–1.71 versus 2 for ideal proper complex Gaussian noise. Interleave-off and intentional gaps change those statistics. APSK is not demonstrated to improve LPI; exact power balancing introduces dependence, and finite source spectra/moments do not establish an adversary's detection probability. The coefficient amplitude-ratio separation is lower than the deliberately cyclic legacy code in some fixtures; no universal amplitude-only sensitivity equivalence is claimed.

The receiver protocol was frozen after cheap boundary pilots. Complete sampled flat-channel captures used Rate 36/Added 10, fixed public alignment, unchanged receiver thresholds and 16 independent paired seeds per C/N0. Requested 16-second bits round to 23.2 seconds. Both bit values were included.

| C/N0 | Legacy correct / wrong / missed | New correct / wrong / missed |
|---|---|---|
| 2 dB-Hz | 9 / 0 / 7 | 13 / 0 / 3 |
| 4 dB-Hz | 16 / 0 / 0 | 16 / 0 / 0 |

At 2 dB-Hz, 95% Wilson intervals for correct admission are 33.2–76.9% and 57.0–93.4%; five pairs improved and one worsened. This does not establish superiority. Paired full-source energies differed by less than 0.0005 dB. Native CPU cost was about 4 CPU seconds per 16-frame cohort under either revision. Eight noise-only captures per revision produced no admission; their one-sided 95% upper bound is still 31.2%, so they are only controls, not a rare-tail qualification.

Two additional independent fully random HF/Each 65%/Helper realizations used 120 seconds requested, 189.070 seconds observed,8 dB-Hz,21 ms / 90 Hz nominal ray structure, random ray variation and random selective coefficients. Both revisions chose the correct bit and passed the working-model gate in both cases. Both Helper-only controls failed that gate. Operational admission remained withheld because the Helper null is unqualified. New source energy differed by −0.014/−0.029 dB; new combined processing cost was 0.130–0.134 CPU seconds per input second on the local Ryzen 5 PRO 5650U.

The strong-Helper microwave pair used15 kW Helper / 40 W DATA,200 dB loss,5.8 GHz,6 Hz / 0.5 ms spread,Each 65%,FHSS,Rate 360/Added 1, and imposed 1.5 dB-Hz DATA C/N0. Both revisions processed all 709,439 samples (118.240 seconds), chose bit1 and passed the working-model gate. New/legacy CPU cost was 0.07847/0.08227 CPU seconds per input second. DATA energy changed +0.0682 dB; Helper energy was unchanged. DATA 1725 Hz / high pilot 2400 Hz and 6.5 ms ramp remained feasible. However, the bit-score margin fell from 10.375 to 4.446 in this one pair: it does **not** establish unchanged microwave sensitivity or pilot frequency-estimation precision. The model-gate result is not a physical/Helper operational admission.

An illustrative DATA-only, high-SNR AGC diagnostic applied a one-pole squared-envelope detector to the actual composite before measuring normalized template overlap. At 0.1/0.8-second time constants, the largest new-waveform shape loss was 0.00066 dB for Rate 36/Added 10,0.0481 dB with Added 1, and0.7915 dB for the very-low-rate/Added 1000 case. This is neither a full noisy AGC likelihood nor a calibrated IC-7100 model; the complete sampled receiver trials above did not substitute this simplified detector for the shared channel.

## Cable and regression checks

A fresh actual loopback run used the new waveform, Helper, 14-bit-equivalent full-audio-rate AES noise and a predeclared 13-position timing bank. It delivered 369,600 transmit and 381,600 capture frames with no reported discontinuity/error, and selected the correct bit. Combined CPU was 16.468 seconds over 63.6 seconds of logical capture, or 0.2589 CPU seconds/input second; wall time was 63.943 seconds. Concurrent experiments and a single run preclude a timing confidence claim. It is a high-SNR flow check, not a radio AGC, marginal sensitivity, replay-security or physical false-admission certification.

Passed checks include native source, shaping, channel, receiver, stream/probe, long-CLI/configuration and finite Helper export; 39 existing focused Python tests plus 3 new envelope-metric tests. The repaired source/stream/probe CTest group took 4.49 seconds. Long-CLI/configuration/finite-Helper checks took 52.91 seconds combined; do not repeat those for unrelated report edits. The native probe's five tests separately passed.

Original failures were retained: a source test included an intentionally zero rounded end cell, and the stream power object initially omitted revision metadata. Both were corrected without weakening active-cell or identity checks. EME at 6 kHz also hit an existing pre-planner default 4000-Hz IF check; explicitly requesting the intended 1725 Hz IF passed without raising sample rate. The first random-HF legacy row preceded a metadata-only Helper rebuild; per-run hashes disclose this boundary.

Use the [short commands in the guide](../../../triad-patterns.md#noise-like-appearance-and-cheap-development-checks). `summary.json`, `evidence.tar.gz` and `SHA256SUMS` retain compact results, commands, fixture hashes and source identities. Binary probe arrays are reproducible and omitted from the archive. No RunPod resources were needed for these bounded CPU and cable checks. General blind multi-day acquisition, full parameter-cross-product qualification and physical LPI/AGC guarantees remain outside the demonstrated scope.
