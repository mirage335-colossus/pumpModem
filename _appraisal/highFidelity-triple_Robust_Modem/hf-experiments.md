# Adverse HF skywave experiments

These experiments ask when additional physical observation and historical
processing recover useful information, and when changing ionospheric phase,
single-antenna cancellation, or a missing propagation mode prevent that gain.
They run the canonical native Robust receiver. They do not require a favorable
answer or turn a completed experiment into successful communication.

The [research reconciliation](hf-research.md) connects the model choices to
the existing `_ref` collection and newly inspected primary literature. Four
evidence classes remain separate: deposited measured envelopes, historical
statistical fits, analytic propagation models, and constructed sensitivity
controls. None is a calibrated worldwide disturbed-day distribution.
Actual positive, negative and conditional outcomes are preserved in the
[HF validation record](hf-validation.md).
The later [public phase-search continuation](hf-public-search.md) tests the
private phase-history finding under full public acquisition priors and, through
an explicit option, inside the same native complete receiver.
The [bounded refinement continuation](hf-phase-refinement.md) adds an
evaluator-only known-physical-template comparison and a predeclared,
fixed-prior proposal/refinement candidate. Its native focused test initially
failed the noiseless blind off-grid recovery assertion; subsequent bounded
repairs and message campaigns are recorded in
[HF validation](hf-validation.md). That implementation counterexample is not
evidence of ionospheric infeasibility.

The [independent trajectory ensemble](hf-ensemble.md) now extends this with
two-scale critical-frequency motion, reproducible C2 random knots, independent
channel placement, fading/recovering effective rays, and canonical-only
Helper/nonwhite-noise combinations. Its generator is independent of the
receiver phase bank. Frozen screens, full messages, private bit-distance
diagnostics and operational SNR/time brackets have separate endpoints.

## Mechanisms and independent checks

| Cases | What changes | Evidence and numerical boundary |
| --- | --- | --- |
| `hf-ensemble-tid`, `hf-ensemble-changing-rate`, `hf-ensemble-modes` | Multiscale/nonperiodic density, independent launch position and competing effective rays | Shared native eikonal with global rate/cutoff/delay admission; constructed cases, not measured events. Full definitions and fixed selection rules are in [the ensemble protocol](hf-ensemble.md). |
| `hf-ensemble-modes-noisy`, `hf-ensemble-ox-helper` | Actual multipath plus colored/shot noise, or Jones O/X with Helper and common frequency offset | Canonical receiver interactions. They do not bypass the experimental phase supplement's proper-white-noise and Helper-off restrictions. |
| `hf-watterson-i1`, `hf-watterson-i3` | Unequal component powers, O/X spectral splitting, mode delays and different Doppler widths | Literal historical fitted scalar components; received-terminal powers already include the original antennas. They are quiet selected intervals, not a storm limit. |
| `hf-ox-split`, `hf-ox-cancellation` | Relative phase of characteristic modes after one transmit and one receive polarization projection | Constructed coherent Jones comparison with lossless vector closure and single-port cancellation checks. It is not an ionogram reconstruction or a critical-frequency ray solver. |
| `hf-trough-static`, `hf-trough-phase` | Slow common phase on coherent and diffuse contributions | Matched source-conditioned sensitivity construction. The coherent contribution is not exempt from the changing phase; identical latent fading is retained in the pair. |
| `hf-vertical-eikonal-static`, `hf-vertical-eikonal-motion` | A changing critical frequency in a vertical parabolic plasma layer | Phase, group delay and local dispersion derive from the same analytic eikonal. Independent refractive-index quadrature and frequency/time derivatives test the implementation. This unmagnetized model stays below cutoff and is not a full oblique magnetoionic ray solution. |
| `hf-vertical-eikonal-holdout` | Critical-frequency variation of ±8.5 kHz, period 137 s and initial phase .37 rad | The same analytic physical model, with a trajectory absent from the frozen phase dictionary. It is a constructed mismatch control, not an independently measured disturbed day. The complete-message failure is retained in [HF validation](hf-validation.md). |
| `hf-eikonal-holdout-a`, `hf-eikonal-holdout-b` | Independently seeded constructed layer trajectories: ±7 kHz/149 s/1.13 rad and ±8 kHz/107 s/−.61 rad | Same vertical parabolic eikonal; these expose unseen phase/period combinations. They are synthetic holdouts, not measured events or a day-frequency distribution. |
| `hf-eikonal-compound` | Two unresolved equal-power arrivals: a 149 s layer trajectory plus a separate 75 ns geometric delay oscillation at 43 s | Per-ray continuous laws preserve their channel state; no received-energy renormalization. It tests single-tap public-model mismatch and possible vector cancellation, not recovery of all multipath. |
| `hf-eikonal-cfo-edge` | Existing ±8.5 kHz/137 s/0.37 rad layer plus +0.98 Hz residual center offset | Keeps the public center-frequency prior at ±1 Hz; tests phase-model proposal behavior close to its CFO boundary. |
| `hf-mode-transition` | Smooth loss, absence and recovery of a mode | Deliberately constructed control informed by observed mode changes. Its schedule is not relabeled as a measured sunrise, flare or storm. Latent path state advances while the mode is absent. |
| `hf-ding-measured` | Deposited coupled complex envelope from a selected 12.7-MHz direct-skywave record | Exact 2,048 complex values, one full-record RMS normalization, bounded interpolation and no wrapping. Unknown instrument/filter calibration prevents interpreting it as an absolute channel gain or broadband impulse response. |
| `hf-ding-amplitude`, `hf-ding-phase` | Remove either phase or amplitude variation from the same record | Explicit counterfactuals, not additional measurements. Their differences help identify the mechanism of a receiver failure. |
| `hf-ding-measured-late`, `hf-ding-clock41` | A nonwrapped 20-second offset; an alternative duration convention | The same event, not independent channel realizations. Both retain the original full-record normalization. |

The dispersive operator respects the local transfer-phase derivative. At the
chosen 3.4-MHz parabolic reference, group delay is about 2.099 ms and its slope
about `5.21e-10 s/Hz`; the corresponding phase curvature across this narrow
signal is small. The original moving case instead produces about 12.6 radians
of carrier phase excursion while group delay moves by only a few microseconds. Applying
the vacuum moving-path formula directly to that group-delay change would give
the wrong Doppler. That distinction is part of the numerical tests.

Finite-support tests preserve source silence, interpolation halos, mode state,
delayed tails and cancellation. The complete receiver receives only samples,
validity and declared public bounds. It receives no realized path, measured
trace, noiseless waveform, channel inverse or evaluator decision.

## Measured replay has a real endpoint

The original Ding deposit has no sample clock. Nominal 50 Hz gives a last knot at **40.94 s**;
the separate `2048/41 Hz` convention interprets the paper's rounded duration.
Neither is asserted to be the missing clock metadata. The replay rejects
out-of-support requests and the frequency-separated Helper composite.

The reference waveform's first complete public observation is **19.4375 s**.
Its three-bit message plus two quiet observations requires **259.4375 s**
because the canonical public cadence is at least 60 s. Accordingly, measured
replay trials score a complete first observation, not a complete message.
The late-offset case covers a different portion of this finite event without
repeating or accelerating it. Long episodes and physical message completion
are tested with the separately labeled analytic and statistical models.

Helper qualification is separate. The O/X paths apply their delays and Jones
projections across the full emitted band, so two additional full-message
controls exercise actual separated-frequency Helper acquisition and DATA.
The local-dispersion eikonal approximation does **not** admit the current
Helper composite: its sample-band/phase bound rejects that geometry. The
measured scalar trace also remains Helper-ineligible. These restrictions are
preserved rather than relaxed to obtain a broader-looking test result.

Use explicit `capture_lead_seconds: 2` when checking positive Helper acquisition
with these narrow arrival priors. The historical 0.25-second default does not
leave enough filtered leading-noise support for the onset estimator; its
attempt/fallback outcomes remain useful but are not positive Helper controls.
The override preserves transmitter UTC and records actual capture geometry.
See the [support investigation](hf-validation.md#separated-frequency-helper-coverage).

Native least-squares linear detrending gives phase standard deviation
**0.884792 rad**, whereas endpoint detrending gives **1.134374 rad**. Neither
reproduces the paper's **1.02 rad**. The deposited filename/readme and exact
sample hashes are preserved; no estimator was fitted to obtain the published
number. See the [fixture provenance](fixtures/hf-ding-2021/README.md).

### Measured-record availability audit

The earlier targeted primary-source/data-catalog check on 14 November 2026 found useful
additional observables, but did not establish a second long complex HF channel
fixture suitable for phase-sensitive modem replay. No new capture was imported
or counted as communications evidence in this audit.

| Source | Available observable and scope | Admissible use and remaining prerequisite |
| --- | --- | --- |
| [Chartier et al. (2020), McMurdo–South Pole](https://doi.org/10.5194/amt-13-3023-2020), [public data](https://doi.org/10.5281/zenodo.3478267) | Two weeks in March 2019 on a 1,356 km polar path; 12 frequencies per minute with 5 s dwell. The instrument returned sparse range–Doppler–intensity detections above 6 dB while retaining raw DigitalRF I/Q on site. The catalog lists 31.9 MB, including 30.2 MB of oblique-sounding data. | A promising independent check of event durations, delays, mode changes and detection censoring. Nondetection is not an observed channel zero. The public summary matrices do not supply complex phase/noise for waveform replay; a curated raw-I/Q subset and its clocks/calibration would be required for that use. |
| [Cerwin et al. (2026), HamSCI HF TDOA](https://doi.org/10.3389/fspas.2026.1723511), [v1.0.3 deposit](https://doi.org/10.5281/zenodo.18394461) | Received amateur-radio audio and analysis of 2023/2024 eclipse-era paths. Manual analysis selected a favorable chirp per record; subsequent automated analyses used all chirps and exposed additional modes. The January 2026 versioned archive is 2.1 GB. | Useful for a separately qualified radio/audio-chain delay or mode-observable check. These received waveforms are not a calibrated, arbitrary-waveform complex channel. Inspect a selected file's clock, filtering, station metadata and data license before import; the whole archive is unnecessary for a small fixture. |
| [Johnk (2026), citizen-science sounder](https://usncursi.org/archive/nrsm/2026/papers/1011.pdf) | The two-page paper constructs a complex impulse-response estimate from KiwiSDR audio using a Hilbert transform and correlation. It describes 60 m tests, then mentions a 50.313 MHz opening before the example capture; that frequency/context linkage needs clarification, given the stated receiver range of 10 kHz–30 MHz. | A methods lead. The inspected paper supplies no capture deposit, data license or complete clock/filter calibration. It does not unambiguously establish that the Kiwi capture itself was at 50.313 MHz; do not turn this ambiguity into either a verified hardware contradiction or an admitted HF fixture. |

At that audit checkpoint, Ding was the only admitted measured complex replay.
The later [Gannon import](fixtures/hf-gannon-2024/README.md) adds two exact hourly
records with stored UTC/sample-grid metadata, after resolving overlapping stream
metadata and duplicate basenames. Its [complete receiver trials](hf-magnetoionic.md#long-record-outcomes-and-conditional-failure-diagnosis)
now exercise long observations without wrapping. These are observed-voltage
stress tests, not calibrated broadband channel measurements. Recorded noise
becomes signal-proportional under multiplication; a native synthetic control
exposes that confound rather than treating noise-filled voltage as a proven
physical channel during a blackout. The same caution applies to unseparated
measurement noise in any complex-envelope estimate, including Ding.

Ding's duration, clock ambiguity and scalar bandwidth still apply.
Additional synthetic trajectories, another excerpt, or amplitude/phase ablations
do not increase the number of measured days; tiling or stretching it is not a
substitute for a longer record. Public sounding summaries can independently
constrain scenario observables without being mislabeled as modem replay data.

## Coupled magnetic and long-record extension

The [magnetic protocol](hf-magnetoionic.md) specifies two separate physical
families: `hf-magnetoionic-x`, `-y`, `-x-holdout`, `-x-spread` for the tilted
lower-cutoff path, and `hf-magnetoionic-parallel-x`, `-y`, `-x-holdout` for the
parallel upper-cutoff path. Normal-incidence vector fields, fixed antennas,
absolute mesh/frequency/profile checks, a separately enforced null-resolution
check and finite-memory time bounds precede complete native reception.
These cold layered models do not add three-dimensional scattering or caustics.

`hf-gannon-1700`, `hf-gannon-2000` and `hf-gannon-2000-late` replay the fixed
long recorded voltages and a nonwrapped late placement. Their joint raw-knot
normalization, Cartesian interpolation and private diagnostic conditions are
explicit. The separate campaign manifests retain complete messages, negative
controls, fresh placement/payload and a near-equal-energy magnetic duration
comparison; no recorded sample or physical channel truth reaches the receiver.

## Frozen bounded campaigns

Run a campaign with the contained executable and runner:

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-unusual.json
```

| Manifest | Predeclared question | Budget |
| --- | --- | --- |
| [hf-unusual](campaigns/hf-unusual.json) | Bright/weak mechanism pairs, mode recovery, noise and wrong-ordinal controls | 20 episodes; 180 s each; 2,400 s aggregate |
| [hf-measured-replay](campaigns/hf-measured-replay.json) | Complex versus amplitude/phase ablations, both record portions, clock ambiguity and same-aperture diagnostics | 15 episodes; 120 s each; 1,800 s aggregate |
| [hf-history-trend](campaigns/hf-history-trend.json) | Static/moving refraction at three integration durations and two signal levels, with matched history diagnostics | 18 episodes; 240 s each; 2,400 s aggregate |
| [hf-sensitivity-brackets](campaigns/hf-sensitivity-brackets.json) | Frozen intermediate levels after the bright/weak endpoints; fresh noise seeds at +8 dB-Hz | 30 episodes; 60 s each; 900 s aggregate |
| [hf-common-text](campaigns/hf-common-text.json) | HELLO under the canonical uncoded receiver, charging all airtime and energy | 5 episodes; 600 s each; 3,000 s aggregate |
| [hf-phase-followup](campaigns/hf-phase-followup.json) | Exact failed interior aperture, fixed phase-family comparison, fresh noise seeds and null controls | 16 episodes; 240 s each; 2,400 s aggregate |
| [hf-native-fec](campaigns/hf-native-fec.json) | Existing RS20/RS60 HELLO support, with separate decoded and raw-clean endpoints | 3 episodes; 600 s each; 2,100 s aggregate |
| [hf-fec-equal-energy](campaigns/hf-fec-equal-energy.json) | One conditional RS60 repeat after its +8 dB-Hz success, matching Off HELLO's total clean transmitted energy/noise density | 1 episode; 600 s each; 660 s aggregate |
| [hf-helper-controls](campaigns/hf-helper-controls.json) | Actual Helper plus DATA in the admitted full-band O/X cancellation model at the original bright/weak levels | 2 episodes; 180 s each; 420 s aggregate |
| [hf-helper-noise-support](campaigns/hf-helper-noise-support.json) | Bright AWGN and O/X Helper controls with explicit two-second leading-noise support | 2 episodes; 180 s each; 420 s aggregate |

These are hierarchical failure-finding observations. Paired roots, reused
records, different durations and adjacent public slots are not independent
success trials. Report each outcome and censoring reason; do not pool these
heterogeneous rows into a success probability. Follow-up seeds may test a
specific discovered contrast under a new frozen manifest, without changing
receiver thresholds or discarding the initial observations.

The later public-search, timing-control and complete-receiver manifests retain
their own budgets and stopping rules in [hf-public-search.md](hf-public-search.md).
Their receipts and any explicitly adaptive diagnostics are recorded separately
in [HF validation](hf-validation.md), not counted as independent extensions of
the original campaigns above.

## Separating available energy from receiver use

HF `oracle` mode provides two complementary private diagnostics:

- A known-waveform, known-channel white-noise binary/presence reference on the
  same public aperture. It omits acquisition, estimation and completion; it
  is not a practical unknown-channel performance limit.
- Clean-template projections and fixed-prior block-gain likelihoods for both
  candidate bits, using 0.25/1/4/16-second blocks and the full aperture.
  Reports include retained signal energy, wrong-template energy, residual,
  complex nuisance rank, noisy scores and matched noise-only scores.

Breaking an observation into more blocks can retain more signal energy while
increasing nuisance rank and false-admission cost. A larger projection alone
is not a better detector. These scores use configured timing and, by default,
unshifted clean templates. An explicit `history_reference_delay_seconds` selects
a constant evaluator reference delay within the public arrival bounds; it is
recorded and does not alter the receiver or insert the realized channel delay.
The unshifted static reference's timing mismatch must not be attributed to
phase incoherence. These are diagnostics of those explicitly stated models,
not another implementation of the operational modem. Actual canonical
decisions, accepted wrong content, erasures, false DATA, two-quiet completion,
memory and elapsed processing remain separate measurements.

The optional `hf_phase_bank` uses a frozen 97-member smooth phase family:
zero phase once, plus amplitudes 4/8/12/16 radians, periods 90/120/180 seconds
and eight equally spaced initial phases. Each model marginalizes one global
complex Gaussian gain. Models have equal normalized prior mass, and the two
bits have equal prior mass; a log-mean-exp pays for the additional alternatives.
The same family is evaluated on signal-plus-noise and matched noise-only input.
This bounded native diagnostic records work, workspace and elapsed cost. It
conditions on supplied template timing/carrier and is not an operational
acquisition implementation or proof of a general unknown-channel optimum.

The newer `hf-search` mode evaluates that same frozen phase family alongside
constant-CFO and affine controls, charging full public timing, clock, CFO and
model priors. Shared immutable phase tables and a native FFT plan avoid repeated
preparation without changing those priors. Eligible RRC timing atoms also use
the existing native two-bit source bank, with exact coordinate checks and the
canonical renderer retained as fallback; both memory bounds remain charged.
The opt-in
`--hf-public-phase-receiver` branch reserves weight 0.25 and shares the native
coarse acquisition, executor, codec and physical-completion path. Its admitted
scope is configured-calendar, single-tap, Helper-off CPU with IID proper-complex
white noise of known variance. Shadow likelihoods remain distinct from native
decisions; the off-grid last-bit failure prevents a broad graceful-degradation claim.
See the [public protocol](hf-public-search.md) and [quick commands](README.md#public-phase-history-continuation).

The time/energy campaign preserves actual native geometry. Increasing the
requested bit duration does not imply the same multiple of transmitted energy
or calendar length; use the receipt's actual waveform and energy. Total render
plus receiver wall time is not calibrated real-time receiver capacity.

## Executable external comparisons

The [upstream comparator](competitor/README.md) runs native FLDigi PSK31 and
Olivia plus WSJT-X FT8 and WSPR through the common channel. It records exact
binaries, source waveforms, complete decoded output, noise conventions and
transmitted energy. PSK31/Olivia text recovery, FT8 fixed-block decoding, WSPR's
structured beacon, and Robust's physical completion are different services.

Equal input C/N0 establishes neither equal energy nor equal delivered content.
Compare ordinary operating points separately from a predeclared energy-matched
experiment. Charge synchronization, coding, permitted latency and every
repetition. Existing FT8/WSPR phase-coherent processing must remain enabled
and message-specific hints must be disclosed. Lower transmit power and extra
path loss consume the same received link budget; they cannot be counted twice.

The desired orders-of-magnitude advantage remains a testable hypothesis.
Wrong messages at high energy, a gap from the private reference, or a loss of
gain with longer observation are useful adverse findings, not reasons to
weaken a test or claim that more computation necessarily solves the channel.
