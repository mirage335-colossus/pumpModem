# Burst scheduling controls and research

The GUI minimum-wait choices Off, 1, 2, 3, 5 and 10 symbols map to
`HelperBurstConfig::wait_bits`; Random extra 0 through 3 maps to
`random_extra_bits`. These are complete logical DATA-bit waits in the native
`HelperBurstPlan`, not individual chips or FFT tiles. The native
`Format::burst_wait_tiles` remains a separate within-bit calendar.

Both peers share the same public anchor, initial ordinal, key, DATA cadence and wait settings.
In Scheduled mode each support starts on the UTC 30-second grid. Helper-enabled slots preserve a
fresh 38-second lead, complete DATA pulse support and protected Helper tail.
Off removes the requested additional wait. The current calendar still includes
the waveform support, guards and grid alignment.

Random extra selects 0 through the configured limit from a keyed permutation
with equal counts in each 252-slot cycle. Anchor, initial ordinal and cycle
separate the permutation domain; a fixed cycle total allows bounded direct
lookup at distant ordinals. Without encryption the public Amateur key supplies
a reproducible calendar, not secrecy. Message completion does not restart the
calendar. Waits do not become missing DATA slots or quiet-completion evidence.

The GUI, exporter and receiver use the same public slot geometry. Source and
channel state remain continuous across selected waits, including pulse and
delayed channel tails. The implementation and bounded checks are distinct from
long-duration or physical-device qualification.

## Baseline timing with Helper Off

The UTC grid and the 60-second minimum start spacing originated in the
[optional Symbol_Burst design](https://github.com/mirage335-colossus/pumpModem/blob/45846aad/docs/symbol-burst-implementation-plan.md#L16-L31)
as a way to give a backlogged receiver time to catch up. The
[native stream integration](https://github.com/mirage335-colossus/pumpModem/commit/1c58507a5178c7700cd15d0fcdd05a0091c6dba9)
adopted the same recurrence with Helper and both waits Off. The shared
recurrence accounts for the complete waveform allocation and grid alignment.
Receiver clock, arrival and delay-spread bounds affect receive windows and
can inform public duration planning. See the
[simulator boundary](channel-models.md#robust-simulator-boundary).

In the Helper-off 6 kHz case at Rate 3.6 Hz, 30 MHz shift, 1,725 Hz IF,
GPSDO XO, 3× margin, HF Worst, Each 65% and Target 32 dB-Hz, the DATA gate
lasts 160.4 s. Native planning gave the same timing at minimum carrier
spacings of 0 and 265 Hz. The 210 s recurrence leaves 49.6 s nominally quiet
between DATA gates. Of this, 26.668 s is reserved source support outside the
gate, where the [envelope is zero](../src/triad_shaping.cpp), and 22.932 s is
rounding to the [UTC grid](../include/datapump/triad_helper_burst.hpp). In
this profile the 60-second minimum is inactive. Disabling spread and selective
perturbation shortened the observation window by 21 ms at both carrier
spacings while preserving transmit timing. The configured 0.0003 ppm clock
bound amounts to about 63 ns of affine drift across 210 s.

An isolated native geometry check on the second slot compared the 210 s
recurrence with two experimental cadences, retaining the existing transmit
envelope, support and broad receiver priors. All three used a 209.554 s
interior observation. The ranges below compare that observation with the
declared possible arrival bounds of neighboring active DATA, including
the modeled tail. Negative clearance means those bounds overlap.

| Cadence | DATA silence | Previous / next DATA clearance | Quiet aperture | Daily opportunities |
| --- | ---: | ---: | ---: | ---: |
| 210 s current | 49.6 s | +14.031 / +21.025 s | 202.059 s | 412 |
| 188 s whole-second rounding | 27.6 s | −7.969 / −0.975 s | 173.941 s | 460 |
| 196 s experimental | 35.6 s | +0.031 / +7.025 s | 189.941 s | 441 |

The scorer inherited these quiet apertures and calendar counts. With an
additional 7 s of declared live timestamp uncertainty, possible neighboring
DATA reached the observation even at 210 s. This was a geometry check at a
fixed calendar anchor. Reception, threshold sensitivity and complete service
at the experimental cadences remain unqualified.

### Native packed DATA checks

**Robust setup → DATA cadence → Continuous DATA** selects a sample-grid
calendar for simulation and live audio. Both peers select the same cadence.
Continuous DATA uses consecutive DATA gates with the original waveform
tapers, padded reference supports and final source tail. It requires Helper,
both wait controls and FHSS Off.
Helper has its own protected acquisition lead, which needs a separate design
and qualification to support continuous DATA.

In the 6 kHz profile above at 265 Hz minimum carrier spacing, the packed
sample-grid stride is 962,401 samples (160.400167 s). The full interior
observation stays at 209.554 s. The common quiet aperture drops from 202.059 s
to 145.409 s, so the packed geometry includes neighboring energy in parts of
the full window even with the GPSDO XO clock bound.

The CPU `triad_receiver_probe message` comparison used 1,500 samples/s,
500 Hz DATA IF, Rate 120 and Target 80 dB-Hz with fixture seed 74021.
The scheduled control decoded `e` and scored `0, 0, 1, quiet, quiet`. Packing
10.4 s DATA gates produced `0, 0, 1, 1, quiet` and no completed message, with
the same decisions whether the retained affine hint was enabled or disabled.
With HF Worst, Each 65% and 8 Hz Doppler, one packed message at the same
target gave the correct five decisions. This is a geometry-specific result.

The `admission` comparison used the broad timing policy, Rate 36,
`--seconds 4`, Target 32 dB-Hz and observed 50 dB-Hz, HF Worst, Each 65%,
and `--slot-index 1 --stratum noise --adjacent-data 01`. At the same sample
rate and IF, both adjacent slots carried their own addressed DATA and the
center slot transmitted nothing. The current admission policy scored the
center quiet with the UTC calendar. It admitted false DATA with packed gates
despite a nonempty protected quiet aperture. With the neighboring emitters
removed, the packed receiver scored the empty center quiet. The case
demonstrates the need for adjacent-signal handling in the DATA statistic as
well as correct quiet aperture bounds. A joint structured-neighbor model must
account for both bit values and timing uncertainty without treating previously
decoded bits as known.
[Matched subspace detection](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf)
· [HF reconciliation](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md)

The native CPU probe can also run `--continuous-data on --protected-packed on`
through the real stream controller and codec. The full observation still
supplies coarse proposals and input validity. Every admitting family uses a
projection built within the protected aperture. Families that cannot retain
their public grid keep their prior weight and contribute zero evidence. An
empty aperture stays unresolved. This explicit probe override excludes affine
continuation. The production packed receiver retains a fresh predecessor
nomination. Packed operation requires Helper and physical RF retuning Off.

On the same captured I/Q as the original tight clean packed failure, protected
scoring received `e` and verified both quiet slots. It also completed the
tight HF Worst message at Target 80 dB-Hz and 8 Hz Doppler. A lower-level
control at Rate 120, Target 32 dB-Hz, observed 15 dB-Hz, HF Worst, Each 65%
and 8 Hz Doppler completed `e` with the scheduled, packed and protected
packed paths. The two packed receivers scored the same I/Q. This preserves
the previously successful compact HF placement on the shorter timeline. In
the broad HF pair the protected path scored the absent center quiet at
observed 50 dB-Hz and
still missed the wanted bit at 15 dB-Hz. The protected path retained the
original drift lattice and evaluated 19 and 16 timing atoms respectively.
Its original frequency and slope coordinates are shared with the full path,
including the reference time remapped into the protected interval. These
are complete-path controls for the protected component. In the broad HF
geometry, the seven protected Fourier blocks and 147 modes held 11–12% of
the nominal bit template energy, versus over 99.99% in the tight geometry.
A boundary-dominant bit can mimic quiet when only this restricted support
is scored.

#### Protected discovery and polarity

`--protected-discovery on` allows one additional native coarse search after
valid unresolved protected scoring. That search masks samples outside the
aperture before projection, keeping the arrival, frequency, clock and epoch
grid fixed. Only newly nominated affine cells are scored on the actual
protected observation, at their original full-grid weights. The fallback
keeps accepted DATA and wrong-key rejections. It reuses the ordinary coarse
and fine workspace and the same executor.

In a deterministic near-far fixture, synthetic nuisance outside the aperture
displaced the ordinary shortlist while the protected IQ stayed fixed. The
ordinary score was −8.48 nats and ambiguous. The fallback scored three new
cells, restored the correct bit at +186.20 nats and matched the clean score
to the reported precision. In the broad HF pair at 1,500 samples/s, discovery
found four new cells per slot and added about 1.2 s of coarse work per slot.
It preserved the 50 dB-Hz absent-center QUIET and the 15 dB-Hz wanted erasure.
The compact 15 dB-Hz complete message and the
[physical I3 controls](validation.md#packed-data-under-physical-hf-models--22-november-2026)
retained their decisions. The broad replay shows actual extra cost and no
demonstrated sensitivity gain at that placement.

A separate phase-insensitive carrier-band power comparison falsely favored
bit 0 for a correctly received bit 1 at 6 kHz, Rate 3.6 Hz, HF Worst and
Each 65%. Both bit log scores were negative on that strong capture. It
passed a separated-carrier control with independent fast phase rotations,
which did not cover the coupled delay and cross-carrier effects. The failed
power-shape implementation was removed. A spherical directional null does
not represent arbitrary wrong-key waveforms.
[Angular density and its assumptions](https://www.eurasip.org/Proceedings/Eusipco/Eusipco2016/papers/1570256519.pdf)
· [Earlier normalized covariance tests](../_appraisal/highFidelity-triple_Robust_Modem/hf-normalized-null.md).

The carrier-beat comparison retains the relative phase discarded by
separate power bands. For every complete disjoint protected block it
regresses squared I/Q magnitude against DC and the two nonzero carrier
spacing harmonics. A common phase cancels from a single arriving waveform's
self-products. Distinct delayed paths retain cross-terms. The two fixed
templates compare the direct self-products and an incoherent sum of
delayed self-products, without simulator phase, arrival, or path state.
Each harmonic fits one complex gain over the aperture, and the two
chronological halves also predict one another using gains fitted on the
other half. These predictive comparisons are empirical on colored input.
[Spectral correlation in multicarrier signals](https://doi.org/10.1155/2010/794246)
· [Pilot assumptions of a doubly selective receiver](https://arxiv.org/abs/2503.17565).

Two previously inverted packed slots with the reported severe 3.6 Hz
geometry had the same I/Q before and after diagnostic scoring. The
nominal direct fits explained 0.513 versus 0.040 of the measured beat
energy for the first wanted `0`, and 0.507 versus 0.049 for the final
wanted `1`. The corresponding dispersed fits explained 0.544 versus
0.045 and 0.524 versus 0.053. A previously correct `1` control also
favored its transmitted bit. Clean phase-rotation and gain controls
with separated carriers favored both true bits and preserved predictive
fractions within 1e−13 when arbitrary common sample phase and per-block
gain were applied.

The experimental `--carrier-beat-polarity on` branch runs after provisional
native DATA classification, including cases the selected wrong-key policy
then rejects.
Both templates must explain a majority of full-aperture beat energy and
favor the same alternative in both held-out comparisons. The public nominal
coordinate and the two native affine nominations can be tested. An opposite
qualified nomination prevents reversal. If none qualifies, a bounded
search shares measured beat blocks and a shifted template bank across all
13 public epoch offsets and the arrival interval at block spacing on the
nominal clock. It charges its scratch to the existing owner and declines
geometries beyond 25 million block-coordinate pairs. The full native clock
and frequency search remains in force. The winner can nominate one omitted
affine cell, snapped to the original public grid and deduplicated against
both ordinary and protected-discovery cells. Both fresh native likelihoods
at that cell enter at their original grid and family weights. Native
admission retains the line and equal-bit mixture weights and applies the
selected wrong-key policy to the resulting evidence. The beat fit
contributes no likelihood. Scored quiet apertures, validity checks and
coherent-line protection still apply. A short nonnominal regression
exercised the retained line case and verified that it kept both correct
native bits and their original likelihoods. The beat rule is not a
calibrated test of key identity.

Scheduled and Scheduled + short transitions can also nominate an omitted
affine cell on a changing selective channel when the short-block scorer is
unavailable. The common public DATA interior excludes both outer ramps and
possible bridge energy over the allowed arrival, clock and delay bounds,
including the receive filter halo. The scorer uses that interior only for
the beat statistic. A proposed polarity change is checked against competing
timing matches and must agree with the new full-window likelihood. A matching
proposal may also strengthen a bit that failed Guarded, when the new native
likelihood confirms the original bit at the same Guarded threshold. The
ordinary likelihood, physical-quiet aperture and owner lease still cover
their original geometry. An empty or unsupported interior leaves the
ordinary decision in effect. [Paired Scheduled measurements](validation.md#carrier-beat-nomination-on-the-ordinary-scheduled-scorer)

In a fresh 6 kHz, Rate 3.6 Hz HF Worst and Each 65% capture, seed 74022,
the nominal and native nominations explained at most 0.017 of the beat
energy, and the native bit was inverted. The 741-coordinate search found a
nomination at epoch offset 0 and arrival −4.965 s with direct and dispersed
fits of 0.894 and 0.910 for the transmitted bit. The fresh weighted native
anchor supplied about 203.5 million nats, above this slot's 15.5-nat
threshold. The initial experiment corrected the bit on identical I/Q.
It kept the original Guarded margin. This distinction led to adding the
unique two-bit affine cell and reevaluating admission with the native policy.
The holdout covers one fresh active window under the severe synthetic setting.

An exploratory proposal removed the majority-fit, provisional-DATA and
singleton-admission prerequisites. It chose one cell by the weakest positive
held-out gain across both models and halves, then scored it in the ordinary
affine mixture. At 20 dB-Hz on the long holdout, the strongest proposal favored
the true bit with about 0.05 of the beat energy and gave −35.55 nats of weighted
native support. An evaluator-only check at the same channel's higher-power
alignment gave −34.35 nats on identical low-power I/Q. Both stayed erasures.
The broader 15 dB-Hz wanted capture also remained an erasure after searching
1,105 cells. Its short beat block passed a measured 5×5 Gram condition check
of about 4.19. The matched absent center stayed quiet despite a sizable beat
fit, and the matched strong foreign capture kept its original admission.
These checks distinguish insufficient affine support from a hard majority
cutoff or a one-step timing error on those samples. The relaxed affine
scorer and fixed evaluator alignment were removed. The low-information
ranking remains available to the separate raw-simulator likelihood.

#### Weak beat evidence on raw simulator input

`--white-beat-evidence on` is a native probe option for protected
packed captures with independent proper-complex white thermal noise. The
probe verifies the absence of frontend processing and AGC, and checks the
declared thermal variance against the scorer's variance. The receiver uses
the existing fixed-prior supplement and reserves half the likelihood
weight for it even when the ordinary models already reach provisional DATA.
The remaining half keeps the native model families and their priors.

The supplement selects one ranked beat coordinate only after provisional
native non-DATA. It snaps the coordinate to the existing finite grid. In
each time direction, harmonic gains fitted on one half define bounded
signed weights `a` on the other half. If a held sample's normalized
power is `X`, each log likelihood term is `a X + log(1 − a)`.
The likelihood averages the two directions, two physical models and three
fixed scales. It retains original grid and bit weights. The native line
check, physical quiet test and selected wrong-key policy still decide the
result. Reusing mutually trained halves as independent evidence would
invalidate this calculation. Colored real audio requires a separate
conditional noise model. [E-value mixture principle](https://sas.uwaterloo.ca/~wang/papers/2020Vovk-Wang-AOS.pdf).

At 20 dB-Hz, the fixed-coordinate evaluator at the blind proposal near
−4.715 s gave 26.75 and −16.86 nats after the affine-grid and bit charge.
The standalone white statistic also gave about 47.83 million nats for
bit 1 on the strong independent-key observation. For that capture the
ordinary native components are much larger, and provisional DATA skips
the supplement. The actual fixed mixture preserves the original Guarded
rejection. The held-out true-bit preference therefore supports a
supplemental role on these controls and supplies no standalone
wrong-key test. The evaluator-only pinned coordinate was removed.

##### Stationary colored Gaussian PCM qualification

The signed beat statistic has a separate native likelihood for stationary
Gaussian PCM. It uses the power at each sample after the fixed analytic
projection. The complete receive FIR support and fixed gain make expected
power constant under the zero-mean stationary null. The statistic centers
each keyed signed tilt within its block, including blocks containing a
fractional number of beat cycles.

In real-augmented coordinates let `Σ` be the joint IQ covariance and let
`ν` be the declared complex variance. The bound assumes `Σ ⪯ ν I/2`.
For deterministic centered weights `a`, the Gaussian quadratic form uses
`B = 2 Σ^(1/2) diag(a,a) Σ^(1/2) / ν`. Constant expected power gives
`tr(B)=0`. With `|a| ≤ ρ < 0.5`, the log moment-generating function is
at most `g(ρ) Σ a²`, where `g(ρ)=(-log(1-ρ)-ρ)/ρ²`. This follows from
`tr(B²) ≤ 2 Σ a²` and the Gaussian determinant identity. It accounts for
correlations across the entire observation and permits improper IQ.

The scorer selects four real gain components from a fixed 19-point grid for
each path model. The selected statistic pays `4 log(19)` nats, even for a
zero estimate and uses the radius bound specific to its chosen gain. The
score averages the two fixed path alternatives. A
selected arrival, epoch and clock cell keeps its full public-prior debit.
Blockwise harmonic moments and a four-dimensional Gram matrix suffice for
this whole-window calculation. It needs no whitening matrix or independence
assumption between fitted and evaluated sample halves.

The protected admission probe offers `--colored-beat-evidence on` for a
matched comparison. Add `--project-colored-pcm on` to pass the same entire
capture through real PCM, a fixed contractive MA(1) filter and the native
analytic projection. Both signal and noise pass through the filter. The
receipt hashes the projected IQ and charges the public FIR halo. The native
component check also uses real PCM with a synthetic colored-noise model. Live
requires a bound at the normalized input, which a short noise-floor sample
alone cannot establish. The signed model is disabled in ordinary operation
pending evidence for spectral envelopes and departures from stationarity,
including resampling, interference, clipping and impulsive noise. The
quiet-null probability bound does not control wrong-key or in-band signal
admission. [Weighted Gaussian quadratic forms](https://doi.org/10.1214/aos/1015957395)
· [Colored spectral-correlation statistics](https://doi.org/10.1186/s13634-025-01241-8)
· [Physical noise and real PCM assumptions](../_appraisal/highFidelity-triple_Robust_Modem/research-reconciliation.md#real-pcm-covariance-and-historical-scheduled-repeated-bit-evidence)

#### Severe simulated HF observations

At the reported 6 kHz, Rate 3.6 Hz and 265 Hz spacing, a protected packed
admission with seed 74021 received bit `1` between adjacent `0` and `1`
at the configured 77 dB-Hz. The native CPU scored the 1,257,322-sample
observation in 23.94 s. This single strong placement measures the wanted
slot at the reported geometry. A complete native `e` trial at the same
settings then scored `1, 0, 0, quiet, quiet` on an expected `0, 0, 1`.
It missed the message despite both final quiet decisions. The receiver
service took 107.24 s for the 5,064,925-sample packed capture. The probe's
separate EOF control took 24.16 s. This CPU probe used 24 receiver workers
on 12 visible logical CPUs. The short changing-gain plans declined the two-cell
geometry and the long drift extension exceeded its preserved owner lease,
matching the previously observed planning limitation at Rate 3.6 Hz.
[Prior strong-signal receiver checks](validation.md#strong-signal-raw-bit-simulation-at-36-hz--19-november-2026)

The ordinary 210 s schedule at the same declared settings and seed scored
`1, 0, 1, quiet, quiet`, also missing `e`. It scored 6,255,321 samples in
119.72 s of receiver service and took 25.99 s for the separate EOF control.
The changing-gain and drift limits were the same. Changing the schedule
also changes the keyed transmit addresses and received samples, so the
pair characterizes complete paths at one seed. It does not isolate a
timing-only sensitivity or throughput effect.

An isolated wanted-center control used the same broad HF geometry with
`--adjacent-data qq` and transmitted bit `1`. At observed 15 dB-Hz both
scorers returned an erasure on identical input. At 18 dB-Hz the full-window
scorer admitted bit `0`, while the protected scorer received bit `1` on the
same input. Shorter support can improve model fit in a particular fading
realization even when it discards most nominal template energy. The paired
packed `message` control at 18 dB-Hz and the same broad HF settings failed
to recover `e` with either scorer on identical samples. The full-window
scorer gave `1, 0, ambiguous, quiet, quiet`. Protected scoring gave
`1, ambiguous, ambiguous, quiet, quiet`. These placements do not determine
a sensitivity trend.

#### Boundary model findings

The native receiver limits packed DATA to the intersection of the full capture,
the reachable wanted support, and the support free of both neighbors under
the configured arrival, clock, channel-tail and receive-filter bounds. The
wanted extent uses the full search bounds. It can recover leading or trailing
samples near the ends of the configured calendar. Interior samples that a
neighbor could occupy remain excluded. The physical quiet aperture keeps its
original bounds and whole-block projections. The receiver searches the full
public input for proposals and can revisit the protected samples or rescore an
omitted coordinate from the preceding accepted DATA slot with fresh evidence.

Fixed-coordinate matched tests used the same native center I/Q throughout
each comparison. They used the known public schedules and ordinary receiver
nominations, with no simulated channel knowledge. Their statistics omitted
the operational search prior and admission gate.

- Projecting both adjacent bits out of the observations and templates
  changed the tight false quiet-slot evidence from +759 to −646 nats.
  An error of 2.5 ms in the preceding slot's estimated arrival instead
  increased it to +1,163 nats. At broad HF Worst and Each 65%, the absent
  center still gave +95,176 nats with the receiver's estimates. Confirming
  the exact direct-path arrival in an evaluator-only control left
  +84,019 nats. The mismatch therefore extends beyond arrival estimation.
- A joint fit with three states per neighbor shared each hypothesized
  bit across native blocks. It integrated Gaussian ray gains in the DATA
  model and profiled neighboring gains in the absent model. Allowing
  independent gains in three frequency bands reduced tracked absent
  evidence from +22,381 to −19,007 nats at 1,500 samples/s. At 2,000 samples/s
  with 265 Hz carrier spacing this fit still gave +4,329 nats. Mixing
  native relative-Doppler and delay bases reduced the tracked result to
  −2,386 nats, but still left positive absent evidence at the nominal
  coordinate and negative evidence for the broad wanted bit. At 1,500
  samples/s, the tracked null fit improved for each selected channel
  effect alone, with positive false evidence when they were combined.
- Sharing a linear gain per ray across groups of at least two native fine
  cells reduced the tracked absent statistic to −3,750 nats in the 265 Hz
  fixture. A Gaussian transition that correctly transported affine gain
  and full covariance between group time coordinates moved it to −3,758.
  The broad wanted score stayed negative, shifting from −223 to −225 nats.
  Nominal neighbor positions still gave approximately +4,506 nats with
  the center absent. The retained likelihood agreed with an independent
  covariance calculation to within 7.1e−14 nats on 12 artificial
  one- and two-ray trajectories. The 6 kHz, Rate 3.6 Hz quote for one
  coordinate triple was 838 blocks, 150,040 modes, 59 groups and
  163,332,160 scratch bytes. It measured geometry only.
- A clean control at observed 28 dB-Hz substituted the matched
  absent-center samples for the 3,431 samples inside the protected
  interval. The remaining samples came from the valid active capture.
  The protected statistic stayed negative. Joint models favored the
  correct bit, with about +6,043 nats for the retained gain model.
  This isolates decision information in the boundary samples.

The fixed-coordinate experimental code was retired after these checks.
A general boundary statistic needs an absent-center model covering the
allowed neighboring waveforms and channel uncertainty. The desired and
absent models must use the same observed support, including any admitted
boundary evidence. Taking the minimum of statistics validated in separate
arrival strata preserves the composite null bound. When the preceding and
following emissions can arrive independently, the preceding-latest and
following-earliest stratum still has the original narrow safe aperture.
Sharing one latent arrival between neighbors needs separate evidence that
they are from one source. Flexible delay–Doppler fits and recent joint
factor-graph detectors also depend on their channel or waveform assumptions.
[Projection assumptions](https://doi.org/10.1109/LSP.2023.3270080)
· [Composite-null likelihood bounds](https://pmc.ncbi.nlm.nih.gov/articles/PMC7382245/)
· [Safe testing](https://academic.oup.com/jrsssb/article/86/5/1091/7623686)
· [Blind detection under modeled inter-symbol interference](https://doi.org/10.1109/TCOMM.2025.3541033)
· [Doubly sparse channel models](https://arxiv.org/abs/2504.13762)
· [Fractional-Doppler basis expansion](https://arxiv.org/abs/2504.18901)

The matched receipts in ignored `local-evidence/continuous-robust.dTQUhbM0`
record the present packed-calendar failure and control results. Retain
the adjoining-DATA absences, the scheduled and packed message controls, the
protected-path comparisons and the decisive boundary-model counterexamples
for comparison against the next complete receiver. Record that comparison
in validation and remove the superseded receipts.

## Scheduled gaps and temporary-key noise

**Robust setup → DATA cadence → Scheduled + gap fill** enables native
same-band filler between DATA bits. Both peers select the filled cadence.
It uses the ordinary Scheduled addresses and wait settings. Helper and FHSS
are Off for this profile. The transmitter generates an independent temporary
key for the message's fill, and the receiver needs only the public calendar
and common guard settings. Filler uses the same three-carrier waveform,
native pulse shaping and calibrated DATA gain. A shortened edge follows the
native taper, bounded by unity. The renderer emits filler only between DATA
slots in the submitted draft and keeps the final source and two physical
quiet slots.

The source guard is placed between the complete reachable wanted DATA
envelopes. It allows the public clock and arrival search, modeled channel
tails, the analytic projection FIR and both endpoint sample-rate conversion
filters. Parts of that fill may
still enter a full receive window. In that case the receiver removes those
possible parts before the coarse observation filter and evaluates the native
likelihoods on the remaining uninterrupted aperture. The mask is public and
uses a single full proposal search, with its original clock, timing,
epoch and frequency axes. The short frequency tracker preserves its original
lattice when the cropped terminal block has a different length. Physical
quiet is scored on its own valid aperture. Slots where possible fill stays
outside the full observation use the ordinary Scheduled receiver. Later
accepted timing is still usable by the next public slot.

The source bounds are frozen from the shared profile. A receive device's
additional timing uncertainty enlarges its local exclusion around the same
source fill. A sufficiently wide capture also checks the bounds of more
distant fills even when its two neighboring gaps are empty. Excess local
uncertainty can narrow the wanted aperture, and
a public gap too short for the declared promises carries no fill. Physical
effects outside the delay and filter bounds can still leak into wanted
samples. Live playback uses existing PCM headroom checks. ADC gain, AGC,
analog ringing and radiated spectral compliance depend on the hardware.
[Appraisal front-end evidence](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/55-equipment-and-front-end-effects.md)
· [ITU-R treatment of pulse shaping and transmitter transients](https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.328-12-202509-I%21%21PDF-E.pdf)
· [ETSI HF gain attack and recovery test, section 10.16](https://www.etsi.org/deliver/etsi_EN/300300_300399/30037301/01.03.06_30/en_30037301v010306v.pdf)

The scheduled DATA patterns keep their public UTC addresses and can carry a
previously accepted timing and frequency proposal across a gap. The receiver
still scores that proposal afresh when DATA resumes. The channel model evolves
through the elapsed time. An independent temporary-key filler gives the
receiver no known waveform to correlate over that time. A known pilot can
provide such a measurement under an appropriate channel model. [Watterson channel model](https://its.ntia.gov/publications/details?pub=3106)
· [Known pilot channel estimation in doubly selective channels](https://arxiv.org/abs/2503.17565)

For a fixed linear channel, gap fill contributes the separately propagated
filler waveform. When that contribution lies outside the scored observations,
the receiver sees the same DATA, noise and timing. If independent filler
instead overlaps a wanted observation, the added covariance can only reduce
or preserve the known-template discrimination at fixed wanted energy. An
audio gain-control or transmitter gating benefit would require a separate
device test. Filler that overlaps an echo can mask it. The same additional
power can also mask wanted DATA or interfere with physical quiet scoring.

In the broad 6 kHz, 3.6 Hz, 265 Hz spacing, HF Worst and Each 65% profile,
two consecutive 209.553667 s receive windows leave 0.446333 s between
them on the 210 s scheduled grid. The declared arrival span is 14.07 s
before multipath tails and receiver filtering. A same-band fill in the nominal
49.6 s between DATA gates can therefore enter the full observations. The
filled profile instead guards the reachable wanted support and can place
about 19.5 s of filler in the middle of each gap in this example. The earlier independently
keyed [Watterson I3 replay](validation.md#packed-data-under-physical-hf-models--22-november-2026)
produced a false DATA decision before the codec rejected the episode.
The two post-DATA quiet slots remain physical observations and are outside
any inter-DATA fill test.
The [matched native comparison](validation.md#scheduled-gap-fill-comparison--22-november-2026)
found exact receive-window equality for a guarded compact fill and observed
receive-window overlap when fill began near the end of a DATA gate.
The remaining quiet portions in the broad example are long enough for the
simulation's 0.75 s gain-control state to settle. Keeping actual radio gain
or a hardware transmit detector active through those portions remains a
separate qualification. [Native paired results](validation.md#native-guarded-scheduled-gap-fill--22-november-2026)

### Short audio transitions

**Robust Setup → DATA cadence → Scheduled + short transitions** is the
visible startup selection. It continues to operate with Helper and FHSS
while preserving the Scheduled addresses and optional waits. It overlaps
independently keyed, same-band filler with the bit-independent DATA taper,
then runs the native filler through the nominal gap. Both peers select the
same cadence. The last DATA gate ends
normally and the receiver still requires two physically scored quiet slots.
The receiver uses the full ordinary Scheduled window in this cadence. In
broad timing conditions the filler can produce additional Current decisions.
The GUI admission filters can help interpret the retained evidence. The
guarded **Scheduled + gap fill** cadence remains available for isolation.

Helper retains its original acquisition lead, timing, pilot and metadata
envelopes, gain and endpoint. The filler is added to the DATA leg at its
calibrated gain. There is no new composite normalization. An active Helper
component is unchanged when the DATA filler crosses the boundary into the
next scheduled lead. Reception still uses the existing Helper acquisition
and ordinary full DATA search and scores two physical quiet slots at the end.
Pilot acquisition depends on the DATA isolation achieved by the receiver and
channel together. The matched checks cover an isolated pilot pass and the
original fallback for gated pilots. A separate HF pilot study also finds
that errors in locating pilots can limit reception even where the payload is
strong, in a different waveform with information-bearing pilot locations.
[Native paired checks](validation.md#named-short-transition-default-with-helper-and-fhss--22-november-2026)
· [HF pilot study](https://arxiv.org/abs/2402.16618)

Measure the longest interval of low **in-band power** through a DATA-to-fill
or fill-to-DATA transition. The working target is below 100 ms, preferably
below 10 ms. Count low short-window RMS relative to the adjoining settled
DATA at multiple levels. Exact zeros alone can miss a deep envelope dip.
Retain the prefilter and postfilter levels, peak headroom and transient
out-of-band energy. Compare the result with the radio's detector threshold,
AGC time constants and, if it hops, measured time to usable in-band output.
[HF equipment appraisal](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/55-equipment-and-front-end-effects.md)
· [ITU-R pulse shaping and transient emissions](https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.328-12-202509-I%21%21PDF-E.pdf)

The outer DATA taper is at least eight fine chips, or 13.33 s in the
3.6 Hz example. A short independent ramp at its edge cannot compensate for
that falling power. Overlapping gradual transitions can keep the narrow
waveform and may use the existing bit-independent cover at each edge. Their
headroom depends on the coherent sum. An equal-power weighting can add
up to 3 dB to the peak for two equal-amplitude streams. A convex weighting
bounds that sum by the larger instantaneous amplitude and still permits
cancellation. Measure the mixed waveform after the intended bandpass.

The common clean receive aperture can be empty when the whole nominal gap
is filled. Conditioning on one public timing hypothesis moves the desired
DATA and filler boundaries together when they come from one transmitter.
This permits a longer aperture after subtracting differential path and
filter support. Other timing hypotheses can put full-strength filler in that
same region. Candidate-relative exclusion is therefore a proposal tool until
its admission likelihood accounts for admitted filler under competing timing,
clock and channel hypotheses. A matched test needs those alternatives and
interior absence controls in addition to wanted reception. The two physical
quiet slots after the message use their own absence test.

The simulated FHSS profiles use 55 ms of silence on each edge of a
400 ms dwell. Successive dwells therefore have approximately 110 ms with
zero DATA envelope before either ramp. Short transitions shapes the DATA
crossfade first, then applies the original dwell gate to the added filler at
the Scheduled DATA phase. The filled and ordinary profiles use the same 400
ms dwell, ramps, mute intervals, and hop selection. The illustration leaves
external radio tuning fixed.

## Retained scheduling lessons

Separating transmissions can give a receiver time to process queued work, but
a wait does not lower the peak workspace of one acquisition. Preparation cost,
first-result latency, average tracking cost and backlog must be counted
separately. Do not assume random extra wait rescues the shortest allowed gap.

The retired Sched_Hop fragmented individual long DATA patterns into active
and null groups. Coherent search followed physical time across the gaps, so
longer apertures required finer frequency and clock hypotheses. The demanding
14 s arrival and narrow 105 ms timing trials failed to complete within their
respective limits. A narrower mocked case completed, and its continuous
control was already feasible. This evidence gives no general processing rescue
or measured sensitivity penalty for the current inter-bit gaps.
[Historical search and measurements](https://github.com/mirage335-colossus/pumpModem/blob/a858f6f31d19431d3d15b077752f67a30cd1866f/docs/scheduled-hops.md#L1737-L1756)

At fixed active power and bit duration, shortening idle time delivers the same
energy per DATA bit more quickly. For the 160.4/210 s example, the nominal DATA
duty is 76.4%. Packing unchanged patterns end to end would increase DATA
opportunities and, at fixed per-slot work, scoring demand by about 31%. The
earlier search receiver carried timing and accumulated carrier-frequency
evidence over symbol decisions. The current receiver transports fine state
within each slot and can reuse a preceding affine timing and frequency hint
after the ordinary search. Adjacent patterns could supply closer observations
for cross-slot estimates. Their gain depends on the channel memory and the
ability to recover with the full prior after outages.
[Earlier receiver](https://github.com/mirage335-colossus/pumpModem/blob/52ace0a/docs/modem.md#L384-L425)
· [Current stream](triad-continuous.md)
· [Coherence at low SNR](https://www.mit.edu/~medard/allerton1.pdf)

Changing the recurrence changes keyed UTC addresses. Both endpoints need the
same calendar. Tighter support also changes adjacent-signal exclusion and the
[number of daily opportunities](triad-stochastic.md#wrong-key-admission) used
by the presence gate. Qualify a tighter calendar with actual adjoining DATA,
wider timing and multipath priors, scored quiet slots, weak-signal reception
and complete service under the new cadence.

Quiet source intervals are not automatically safe to discard from a capture.
Pulse tails, channel delay, timing uncertainty, rate drift and outstanding
observations can overlap them. A receiver may exclude samples only when the
retained observation requirements establish that they are unnecessary. Missing
timestamps or unexplained discontinuities cannot be replaced by invented silence.

Scheduled waiting is not failed-symbol evidence or a message terminator.
The current protocol requires two fully scored quiet public DATA slots for
physical completion. A long coordinate or simulated gap is not a long-running device test.
Study scheduling with [minimal abbreviated checks](triad-performance.md) and
preserve the [audio timing contract](clock-sync.md).
