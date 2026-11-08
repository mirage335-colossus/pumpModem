# Rate-aware DATA shaping and standard Helper placement

This is isolated three-carrier v3 qualification, not ordinary-receiver, live-radio,
or release certification. Use the maintained [shaping guide](../../../triad-shaping.md)
for the algorithm and cheap test hierarchy. The final source/build identities,
raw measurements, frozen campaign protocols, errors and uncertainty are retained
in the compact archive indexed below. Existing
[Helper GPU evidence](../2026-11-04-helper-gpu/README.md) remains historical evidence;
its admission results are not replaced by these larger fresh samples.

## Result and operating scope

The planner chooses a DATA pulse, protected-cell lattice and ramp from a bounded
family, preserving a valid configured allocation and calendar. Its finite gated
pulse spectra feed the unchanged production Helper placement rule. Default
selection uses a unit-energy RRC with a C2 taper over the outer two of its eight
chip periods. No source/channel samples are removed to enforce the calculated
band. Helper modulation, fixed 550/950 Hz tones, ramps, 38-second lead and power
allocation remain unchanged; its high-pilot frequency follows the existing rule.
Encryption, clock-derived keyed patterns, whole-cell Added Symbols/interleaving,
400-ms dwells and actual channel/filter tails are preserved.

For the requested EME/Each 65% case, the new auto plan uses DATA 1725 Hz,
high pilot 2400 Hz, and a 6.611111 ms DATA ramp. The earlier explicit 6 ms design
used high pilot 2654.1667 Hz. Both retain 64 protected cells per 26.4-second tile.
At DATA 1775 Hz the default 0.001 tail budget selects 2450 Hz; the explicitly looser
0.005 budget selects 2400 Hz with the same DATA samples. The tighter default
remains unchanged. These are expected-energy planning criteria, not hard
spectral masks or worst-key/channel guarantees.

The final 26-case census takes 9.08 seconds on the local host. Representative
higher-frequency planning results under the same strong Helper power ratio are:

| GUI Rate / Added / FHSS | DATA IF | Placement width | High pilot | Evidence |
|---|---:|---:|---:|---|
|360 /1 /on|1725 Hz|264.375 Hz|2400 Hz|Source, channel, two-way metrology and actual GPU receiver|
|360 /1 /on, tail 0.005|1775 Hz|225 Hz|2400 Hz|Paired sampled metrology and actual GPU receiver|
|1000 /1 /on|1775 Hz|695.3125 Hz|2872.65625 Hz|Adaptive quadrature and independent rendered spectra; placement planning|
|1200 /1 /off|1775 Hz|750 Hz|3700 Hz|Placement planning|
|1200 /1 /on|1800 Hz|756.25 Hz|2978.125 Hz|Placement planning|
|3600 /1 /off|2600 Hz|2250 Hz|4325 Hz|Placement plus actual sampled custom-filter metrology|
|3600 /1 /on|2600 Hz|2259.375 Hz|4329.6875 Hz|Placement planning|

The low-tone separation minima are 1747.65625 Hz at Rate 1000/FHSS,
1778.125 Hz at Rate 1200/FHSS, and 2529.6875 Hz at Rate 3600/FHSS. They explain
why those default-budget plans cannot all stay at 1725/1775. The high-pilot
values also include the existing power-dependent leakage guard. These are
planning results, not demonstrated physical minimum frequencies. A small
increase above the geometric DATA minimum can reduce the required high-pilot
frequency because it reduces leakage from the fixed low tones.

At Rate 12000, even DATA 5150 Hz cannot fit the production Helper guard below
the 18 kHz fixture’s Nyquist limit. Rate 24000 cannot fit the nominal DATA band at
its 8900 Hz geometric lower bound. Those are fixture/placement limitations.
The maintained 900 Hz DATA receiver has a separate 330/400 Hz frontend limit;
a new public-band guard rejects unsupported work before scoring rather than
reporting filtered-away signals as sensitivity failures. The Rate 3600 custom
filter measurement does not qualify a corresponding wideband DATA receiver.

Very low-rate FHSS may lack any complete protected pulse. Rate 0.36/Added ×1000
at fine rate 60 with FHSS needs 1,536,048 raw calendar cells, exceeding the unchanged
one-million-cell tile workspace cap. This is distinct from a physical pulse-fit
failure. Added ×100/FHSS and Added ×1000/continuous are actually rendered in the
extended source check; no expensive 18 kHz channel capture is implied by their
planner feasibility.

Continuous sub-Hz planning also does not qualify the sampled exporter: its
fixed 38-second lead rejects pulse halos `8/fine_rate > 38` (fine rate below
approximately 0.211 Hz), separately from its 2 GiB export cap. These are
fixture/calendar limits, not demonstrated physical low-rate boundaries.

## Independent waveform and channel checks

The source checks compare actual bit 0/bit 1 waveforms, including fixed first/last,
adjacent-middle and dispersed missed-dwell masks. HF, FHSS and a realized
Symbol Burst retain their useful discrimination energy within 0.001 dB in these
unit-amplitude cases. Full emitted energy changes stay within 0.011 dB.
Complete Added ×100/1000 continuous patterns and a 2560.8-second Added ×100/FHSS
pattern pass the same checks. The extended final check takes 16.81 seconds,
including the default checks and noncommensurate spectral validation.

Fresh-key FFTs of complete source waveforms independently check the planner:

| Source regime | Expected tail | Largest rendered tail | Sampling-refinement difference |
|---|---:|---:|---:|
|Fine 60 FHSS|0.09537%|0.10106%|4.20e-7 fraction|
|Fine 60, two active tiles with 3 wait tiles|0.09533%|0.10249%|4.01e-7 fraction|
|Rate 1000 noncommensurate FHSS|0.09659%|0.09979%|1.31e-6 fraction|

The first two rows use four fresh keys, both bits and 960/1920 Hz sample rates.
The last uses two fresh keys, both bits and 4000/8000 Hz. They are finite
engineering checks, not key-population confidence bounds. The noncommensurate
case initially fell back to 4225 Hz with a 64-phase cap. A general cached
64→128→256 phase-refinement ladder qualifies 695.3125 Hz without changing its
600 protected cells/tile. Sampling-only failure still falls back; extra phase
work cannot resolve it. Work caps remain explicit, and exact 24-phase fine 60
cases retain one refinement attempt.

A separate full 18 kHz comparison covers 26 public templates (13 epochs × 2 bits).
Against the prior 6 ms design, total source energy changes by +0.00095 to
+0.00348 dB and bit-distance energy by +0.01167 to +0.01171 dB under the unchanged
public power-reference rule. Using an old template for the new source would
cost only 0.00065–0.00077 dB in the source-only known-waveform correlation measure;
actual receivers use matching new templates. Mean spill beyond±450/±600 Hz
from the DATA center falls by 11.04/18.95 dB. This is emitted-spectrum evidence,
not a channel-output bound.

The tapered source also passes the existing continuous-channel missed-dwell
checks with Each 65%, 6 Hz Doppler and 250 ms delay: valid rows remain exactly equal
to an uninterrupted capture across different chunk sizes, and an actual
post-source delayed signal tail remains present. No second receive-side FHSS
gate is introduced.

## Two-way sampled interference and Helper quality

Three paired EME seeds (95001, 95011, 95012; the latter two frozen independent
validation) compare legacy 6 ms/1725, auto 1725 and auto 1775/tail 0.005. Each uses
composite, DATA-only, Helper-only and both-off captures with identical physical
roots. The subtraction identity is checked, not merely assumed. Complete
continuous filter tails are retained. A late 36-second Helper aperture overlaps
actual DATA; the mandatory lead-only aperture could not establish this direction
of interference.

Across these cases, auto DATA emitted energy changes by about+0.00145 to
+0.00165 dB and received energy by +0.00138 to +0.00252 dB. The largest measured
Helper-to-DATA noise-equivalent power penalty is 0.000192 dB; the conservative
coherent norm envelope is 0.0576 dB. Four-control relative linearity residuals
are below 4e-16. Source power is not renormalized per trial or received fade.

Representative additional four-control measurements cover HF 36×10/no-FHSS
(Helper leakage 74.01 dB below filtered noise), a realized 132-second EME burst
calendar (45.61 dB below noise), and Rate 3600/IF 2600 with an explicitly custom
1375/1500 Hz DATA filter (65.74 dB below noise). The burst contains two 26.4-second
active tiles separated by 79.2 seconds; its measured Helper window contains 49.6%
of the total received DATA energy. All 90 phase apertures resolve in these
measured cases. The high-rate custom filter is metrology, not receiver qualification.

**The provisional 5% net Helper-precision target is not uniformly met.**
Moving frequencies changes the fading and noise exposure. Across the three
paired seeds, auto 1725 high-pilot fresh-phase RMS changes by 0.601–1.177×.
Auto 1775 low-pilot RMS increases 1.151–1.856×, to 3.69–5.54 degrees. The larger
net changes are not DATA interference: within-design DATA contamination changes
fresh-phase RMS by at most 5.1 parts per million. Received Helper energy changes
are retained separately in the reports.

Absolute frequency errors against each design’s noise-free finite-window fit
remain below 34.5 microhertz. These descriptive errors are not estimator-variance
confidence bounds and do not remove possible model bias. For scale, that
constant error over 52.8 seconds gives about 0.00005 dB rectangular coherent loss.
Assuming zero-mean Gaussian phase errors, the largest observed low-pilot RMS
increase implies about 0.029 dB additional coherence loss. These are conditional
calculations; pilot-to-DATA channel phase transfer remains unqualified.

## Known-channel discrimination benchmark

An additional 18 full-tail DATA-only captures compare both bits at each of three
previously frozen signal seeds and all three designs, using the actual 26.4-second
DATA frame. Selection of seeds 97019–97021 for this oracle diagnostic occurred
after viewing receiver outcomes; it is not independent oracle validation. Identical physical/noise roots are checked before subtraction;
the common noise cancels. With known complete complex channel trajectory and synchronization,
equal bit priors and white complex noise variance σ², the optimal binary error
probability is Q(sqrt(||s1−s0||²/(2σ²))). This is an oracle bound, not the
achievable unknown-channel/search receiver performance.

At fixed 40 W DATA power, the auto designs improve received bit-distance energy
by +0.01166 to +0.01172 dB. After separating emitted-energy changes, the distance
per emitted energy improves +0.01018 to +0.01024 dB. Thus this actual-channel
known-waveform sensitivity measure shows no loss. At the physical C/N₀,
legacy d²/σ² spans 25.96–32.49 and auto 26.03–32.57, giving known-trajectory
BER 2.79e-5–1.57e-4 versus 2.72e-5–1.54e-4. Analytically increasing the noise
variance to 1.5 dB-Hz gives 6.54–8.84% versus 6.51–8.81%. This second row is a
calculation on the same measured channel means, not another receiver experiment.
Even the favorable oracle does not deliver 99% bit correctness at 1.5 dB-Hz for
these frames. These three selected channel realizations do not bound every
channel seed or the remaining acquisition/modeling loss.

A post-outcome diagnostic revisits the physical wrong admission at seed 98041.
Two additional both-off controls recover the exact noise realization. The
known-trajectory oracle correctly selects bit 0 for both legacy and auto
waveforms: log L(bit 0) − log L(bit 1) is +17.0737 and +17.1343, respectively.
Both practical receiver arms admitted bit 1. This establishes an inference,
modeling or acquisition gap on that realization, rather than a loss caused by
reshaping. It does not identify the precise cause or claim an achievable oracle
receiver. The controls preserve all sample rows and channel tails; clean-energy
reconstruction agrees with the native reports within 6e-9.

## Actual receiver outcomes and uncertainty

Eight frozen CUDA campaigns process 536 full sampled captures. Each has 19
calibration captures, 32 alternating-bit signal captures and 16 independent null
captures. The original six conditions use paired seeds across legacy 1725,
auto 1725 and auto 1775, at physical 10.0206 dB-Hz and explicit 1.5 dB-Hz. A fresh
auto 1725/1.5 campaign uses disjoint seeds 99000 onward. A later diagnostic turns
Helper off on the same 97000-series channels/noise, with its own frozen null
calibration; it was selected after viewing the first comparisons.

No threshold or waveform was tuned to the held-out outcomes. These are marginal
per-capture rank gates with alpha 0.05, not an operational daily false-admission
qualification or bit authentication. Correct/wrong/missed counts below sum to 32;
false admissions are out of 16 nulls.

| Condition | Arm | Correct | Wrong | Missed | False |
|---|---|---:|---:|---:|---:|
|Legacy 1725, 1.5 dB-Hz|Full bank|20|3|9|0|
|Either auto design, paired 1.5|Full bank|21|3|8|0|
|All three designs, paired 1.5|Helper-assisted|24|5|3|0|
|All three designs, physical|Full bank|31|1|0|0|
|All three designs, physical|Helper-assisted|31|1|0|1|
|Auto 1725, fresh independent 1.5|Full bank|21|3|8|2|
|Auto 1725, fresh independent 1.5|Helper-assisted|23|6|3|1|
|Auto 1725, Helper off, 1.5|Both arms (fallback)|21|3|8|0|

All paired assisted decisions and raw signal-bit choices agree between old and
new shaping at both power levels. One full-bank miss becomes a correct admission
at 1.5 dB-Hz. Removing Helper preserves every full-bank held-out outcome and raw
signal-bit choice; the largest presence-score change is 0.00219. Thus the observed
receiver failures remain present in the legacy control and are not resolved by
shaping.

The uncertainty is substantial. For 24/32 correct assisted receptions, the
conservative balanced-bit 95% interval is[0.466, 0.913]; for 31/32 it is
[0.711, 0.9996]. Zero false admissions in 16 gives a two-sided 95% interval
[0, 0.206]; one gives[0.00158, 0.302]. Even zero paired signal-outcome disagreements
in 32 has a conservative balanced-bit 95% upper bound of 20.6%. Bounds condition
on the frozen gates and representative independent draws within bit strata;
they are not simultaneous across conditions or arms. These data support the
finite paired comparison, not a confidence-qualified 0.2 dB population
noninferiority claim, 99% reception, or a daily false-admission target.

## Runtime, provenance and economical reproduction

The GPU was an RTX PRO 6000 Blackwell Workstation, using strict complex FP32
FFT/bulk arithmetic with TF32 disabled and explicitly retained FP64 small
preparation/reductions. The public bank includes 13 epochs, 42 lags and 125 CFO
points; Helper commonly retains 4–5 CFO points. Large templates were generated
remotely after a 3.63 MB upload. No GPU algorithm or arithmetic policy changed in
this shaping work.

Campaign walls range 188–221 seconds including native generation, loading,
shared setup and scoring. Exports account for 157–184 seconds and paired receiver
scoring for 30–37 seconds per 67-capture campaign. The Helper-off diagnostic
intentionally overlaps the final scheduled campaigns; these are campaign cost
measurements, not isolated latency benchmarks. The earlier GPU speedup study
remains separately linked above. All results were downloaded and verified before
the temporary Pod and its ephemeral storage were deleted.

Final focused checks:16 isolated triad tests pass in 31.16 seconds; 101 Helper
Python tests pass in 8.95 seconds; five complete exporter smoke captures plus
planner/rejection checks pass in 22.86 seconds. Ordinary Robust integration and
its unrelated campaign were not run.

The 536-capture paired GPU campaign and its focused checks are historical
qualification evidence. They distinguish source geometry, channel-oracle and
actual receiver behavior, and preserve matched control outcomes. Do not replay
the campaign as routine CI. For future computationally intensive Robust checks,
use the maintained [abbreviated triad harness](../../../triad-performance.md)
and [material lifecycle policy](../../../development.md#material-lifecycle).

Two implementation corrections matter to provenance. An initial exporter seed
of 6 ms for all auto FHSS rates silently reduced valid high-rate allocation;
removing it restores 1480→1560 cells at Rate 1200 and 5800→6040 at Rate 3600 for
the 17.6-second comparison. The fine 60 waveforms are unchanged. The final binary
has an actual observation/validity/truth byte bridge to the GPU binary, with
identical selected format and Helper plan; only timing and spectral work-count
metadata differ. Separately, adaptive phase refinement resolves the Rate 1000
planning failure without relaxing the convergence or spectral-tail checks.

Earlier single-seed/small-campaign successes do not establish the stronger
reliability claims excluded above. The old `nominal+2/ramp` placement allowance
is superseded only for validated planner candidates, not as a universal channel
or capture-bandwidth bound. Fixed low-tone guards, finite fixture bandwidth,
workspace limits, actual RF retuning, unknown pilot-to-DATA phase transfer and
receiver admission reliability remain explicit limitations.


## Compact evidence

The [3.32 MB evidence archive](evidence.tar.xz) contains 2,197 verified members:
raw trial records, frozen protocols and gates, evaluator identities, metrology
controls and measurements, source snapshots, build flags, failure notes and the
final-binary byte bridge. Large I/Q arrays and executable binaries are excluded;
their hashes and generation commands are retained. The archive SHA-256 is
`8de2f4eaf250a5cb4f4671d64edb10d08aac6fd764b53a492331800297c3e834`.

The [source bindings](source-bindings.json), [archive verification](archive-verification.json),
[planner census](planner-census.json), [raised-IF plans](raised-if-plans.json)
and [paired outcomes](paired-outcomes.json) are also directly inspectable.
Rechecking all eight campaigns reproduced the frozen decisions and counts;
33 interval endpoints differed by only 2.78e-17 with the local numerical library.

Verify checksums without numerical dependencies, then optionally recheck the
stored admission decisions with NumPy/SciPy available. These commands do not
rerun DSP or acquire cloud resources:

```sh
python3 docs/validation-data/triad-v3/2026-11-04-shaping/verify-evidence.py
mkdir -p /tmp/triad-shaping-evidence
tar -xJf docs/validation-data/triad-v3/2026-11-04-shaping/evidence.tar.xz -C /tmp/triad-shaping-evidence
python3 /tmp/triad-shaping-evidence/gpu/tools/triad_helper_evidence.py /tmp/triad-shaping-evidence/gpu/results/*/ --output /tmp/triad-shaping-evidence/rechecked.json
```

The result verifier deliberately refuses to overwrite an existing output.
Use a fresh output filename when rechecking. The source snapshot is an
uncommitted study state bound to its recorded base commit and patch; it is not
release certification. GPU cleanup was verified after downloading the evidence.
