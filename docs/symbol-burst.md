# Burst scheduling controls and research

The GUI minimum-wait choices Off, 1, 2, 3, 5 and 10 symbols map to
`HelperBurstConfig::wait_bits`; Random extra 0 through 3 maps to
`random_extra_bits`. These are complete logical DATA-bit waits in the native
`HelperBurstPlan`, not individual chips or FFT tiles. The native
`Format::burst_wait_tiles` remains a separate within-bit calendar.

Both peers share the same public anchor, initial ordinal, key and wait settings.
Each support starts on the UTC 30-second grid. Helper-enabled slots preserve a
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

The native receiver probe can select a sample-grid calendar with consecutive
DATA gates, retaining the original waveform tapers, padded reference supports
and final source tail. This experimental geometry requires Helper and both
wait controls Off. The GUI and live paths continue to use the UTC calendar.
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
empty aperture stays unresolved. This path currently excludes affine
continuation, Helper and physical RF retuning.

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
boundary evidence. Flexible delay–Doppler basis models have been studied
under specific waveform and sparsity assumptions. Those assumptions
require separate qualification in Robust. [Projection assumptions](https://doi.org/10.1109/LSP.2023.3270080)
· [Composite-null likelihood bounds](https://pmc.ncbi.nlm.nih.gov/articles/PMC7382245/)
· [Safe testing](https://academic.oup.com/jrsssb/article/86/5/1091/7623686)
· [Doubly sparse channel models](https://arxiv.org/abs/2504.13762)
· [Fractional-Doppler basis expansion](https://arxiv.org/abs/2504.18901)

The matched receipts in ignored `local-evidence/continuous-robust.dTQUhbM0`
record the present packed-calendar failure and control results. Retain
the adjoining-DATA absences, the scheduled and packed message controls, the
protected-path comparisons and the decisive boundary-model counterexamples
for comparison against the next complete receiver. Record that comparison
in validation and remove the superseded receipts.

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
