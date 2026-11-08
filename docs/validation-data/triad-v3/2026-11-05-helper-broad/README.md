# Broad Helper-assisted three-carrier study

This study improves the isolated receiver and maps failures across selected
Helper-enabled configurations. It does **not** qualify the 2.5-billion-point
Cartesian product, ordinary Robust, live radios, a daily false-admission target,
or multi-day Helper-assisted reception. The [coverage map](../../../triad-helper-coverage.md)
separates planning, sampled reception and physical evidence. The
[maintained receiver guide](../../../triad-helper.md) describes the implemented
entry points and restrictions.

**Power-reference correction:** the first 59 completed cohorts below use the
historical 39–41-second DATA reference window. At low rates that window can
sample the startup ramp, overstating source energy by28.26 dB at Rate 3.6 and
6.35 dB at Rate 10. Those low-rate successes do not establish their labelled
−6 dB-Hz sensitivity. The final exporter default fixes this; historical results
and their original source versions remain intact. See the power section below
and the separate corrected-power qualification, rather than silently treating
all historical runs as results for the new default.

## Changes supported by experiments

1. The default timing lattice now tests every 900 Hz DATA sample. A retained
   weak realization that chose the wrong bit on the former four-sample grid
   selects the correct bit on this grid. The larger paired campaign also
   retains failures: finer timing is not a universal sensitivity improvement.
2. Frontend admission uses the actual public source width, selected Doppler/AM
   line extent, CFO and clock bounds. The old fixed-width / 90 Hz Doppler cap
   rejected the narrower 165 Hz Aurora case; the corrected case received 8/8
   held-out bits correctly. This is a nominal-support guard, not a claim of
   compactly supported pulse spectra or all-channel performance.
3. Known missing ADC rows are applied before continuous filtering and bound to
   preparation, noise calculations and calibration. A new optional raw-sample
   FFT receiver avoids the large FIR erasure halo for the bounded GPSDO cases
   it supports. It corrected two original wrong bits and received 16/16 fresh
   physical-link trials correctly. Its weaker 1.5 dB-Hz trial still admitted
   one wrong bit; it is not a general replacement for the filtered receiver.
4. An explicit assisted-only execution policy resolves Helper's selected bank
   before preparing DATA hypotheses. It avoids computing an unused full-bank
   comparison. Ambiguous Helper estimates still execute the full fallback.
   This policy has its own frozen gates; an omitted baseline is never reported
   as measured or successful.
5. Native export and campaign controls now expose RF shift, base/shift
   oscillator, spread/margin, independent AM rate, timing policy and power.
   Before the final power-reference correction, default waveform samples,
   validity and evaluator truth were byte-identical to the prior exporter in
   the matched HF/EME bridge. The final explicit legacy mode preserves composite
   I/Q and validity exactly; the corrected default intentionally changes DATA
   gain while preserving Helper-only I/Q.
6. Cache memory planning now caps the configured capacity by the number of
   reachable epoch/clock/RX-scale keys. The original low-rate case reserved
   400 entries although only 13 could exist:183.589 GiB versus 9.291 GiB of
   projected arrays for the actual CUDA batch-32 configuration. Shared subset views retain their parent's reachable
   capacity. This corrects a false preflight rejection; array bounds are not
   measured resident-memory guarantees.
7. The exporter now calibrates one public bit 0 source over its whole active
   calendar, instead of measuring a possibly near-zero startup interval.
   DATA alone receives one fixed scalar correction before propagation. Helper
   generation, its fixed preview and its samples remain unchanged. The legacy
   option and a cheap power-only plan make the distinction reproducible.

Encryption/keyed alternatives, default automatic shaping, interleaving,
38-second Helper lead, 36-second observation, waveform power allocation and
continuous sampled channel/tails remain in use. The ordinary receiver was not
integrated or subjected to its unrelated regression campaign.

## Scope and protocol

The factorized native census has **111 rows** spanning every requested enum
choice; 109 return plans and two reject half-rate DATA IF below the Helper
1725 Hz floor. Planning is not reception. Among the returned plans, 98 report
valid geometry, and 86 of those report valid Helper placement. The 18 kHz
exporter and 900 Hz post-DDC frontend impose additional sample, bandwidth,
halo, memory and source-tile constraints. See the exact plan records rather
than interpreting any single failed row as an entire unsupported axis.

Sampled protocols deliberately pair difficult interactions instead of repeating
equivalent code paths across the full product. They include bounded and broad
arrival priors; HF, EME, Sub9, Aurora and flat channels; strong and weak C/N0;
independent keys; fixed and free-running clocks; AM Off/Slow/Fast/Each;
FHSS envelopes; within-bit wait tiles; and known sample-loss patterns. The
bounded prior is −55.5 to +125.5 ms, while the broad prior is ±7.035 s.
Both retain thirteen keyed epoch alternatives. This differs from eliminating
epoch uncertainty merely because the arrival prior is narrow.

The first campaign set contains 59 completed cohorts and 2,081 scored captures
(calibration included). All69 planned entries are accounted for as 59 completed,
five failed, one externally interrupted and four not run in their original
protocol. Reissued repair/assisted-only entries are separate results, not
retroactive passes. The completed set covers six rates (3.6,10,36,100,360,1200 Hz),
Added factors1/10, six DATA IFs (1725,1775,1800,2000,4000,8270 Hz), RF shifts
0/0.5/1/30 MHz and 5.8 GHz, both timing priors, and every requested AM mode/depth
at selected conditions. Maximum actual bit duration is 160.4 seconds. It does
not sample0.001–1 Hz, Added ×100/×1000, all oscillator/RF cross-products, high-rate
audio or days of observation. `analysis/sampled-axis-inventory.json` in the
archive records explicit manifest values and missing requested choices.

Original failed entries are Aurora165 under the superseded fixed-width guard;
Rate 1200/HF-moderate at IF 1725; Rate 10/Added ×10 at IF 8270; the 90 dB Helper/DATA
power-ratio placement; and Rate 3.6 with the inflated cache reservation. Separate
later cases correct the guard, raise IF, use narrower8270 spectra, or fix memory
planning. The main paired campaign was stopped at a declared cost boundary;
its last five clock/low-rate cases were reissued as assisted-only, retaining
the original interrupted/not-run statuses.

Each completed cohort uses 19 Helper-present, DATA-absent calibration captures,
then a frozen rank gate at 0.05 and independent signal/null holdouts. Signal
bits are balanced fixed strata. Nulls retain the Helper and selected channel;
they are not an empty white-noise shortcut. Versions, native binary, input
hashes, keys/seeds, search banks, masks, gates and outcomes are retained.
Intentional repeated seeds compare duration or algorithms and are not counted
as independent validation.

Physical 4 W / 180 dB with the inherited −164 dBm/Hz noise density yields
20.02 dB-Hz DATA C/N0. Lower labelled cases use an explicit `--cn0` override
to raise injected noise relative to that physical reference. They are not
claims that the unchanged physical link itself has the lower C/N0.

The gate's minimum rank p-value is 1/20. Its exchangeability interpretation is
**marginal per capture**, not a promise of a conditional 5% false rate for every
realized gate. It does not calibrate repeated attempts per day or the chance
of admitting the wrong bit. No gate was lowered after observing a miss.

## Representative reception outcomes

Counts below are correct / wrong / missed admissions, followed by admitted
DATA-absent captures. Unless identified as raw, these use the filtered
two-ray likelihood and Helper assistance with lag step one. C/N0 is total
DATA reference power per noise density in dB-Hz, not in-band SNR. These are
legacy-window results; actual finite-source energy is reported separately.
Durations exclude the
38-second lead and capture/filter tails. The complete per-arm, per-bit results
and confidence intervals are in `evidence/analysis/cohort-audit.json` inside
the compact archive.

| Sampled condition | DATA duration | C/N0 | Correct / wrong / miss | False / null N |
| --- | ---: | ---: | ---: | ---: |
| HF worst ×3, Each65, physical 4 W / 180 dB | 23.2 s | 20.02 | 8 / 0 / 0 | 0 / 8 |
| Same, reduced C/N0 | 23.2 s | 8 | 8 / 0 / 0 | 1 / 8 |
| Same, weaker | 23.2 s | −6 | 0 / 0 / 8 | 0 / 8 |
| Same seed schedule, longer | 46.4 s | −6 | 3 / 0 / 5 | 1 / 8 |
| Same seed schedule, longer | 92.8 s | −6 | 7 / 0 / 1 | 1 / 8 |
| HF margin1, broad timing | 23.2 s | 6 | 8 / 0 / 0 | 1 / 8 |
| Aurora 55 Hz ×3, Rate 100 / Added ×1, corrected width guard | 20.8 s | 8 | 8 / 0 / 0 | 1 / 8 |
| EME Rate 360, FHSS, Each65, physical 40 W / 200 dB | 26.4 s | 10.02 | 16 / 0 / 0 | 0 / 16 |
| Same, reduced C/N0 | 26.4 s | 1.5 | 10 / 3 / 3 | 0 / 16 |
| Same seed schedule, longer | 105.6 s | 1.5 | 16 / 0 / 0 | 3 / 16 |
| Same seed schedule, longer | 158.4 s | 1.5 | 16 / 0 / 0 | 0 / 16 |
| EME Rate 1200, DATA IF 1800 / high pilot 2928.125 | 8.8 s | 1.5 | 4 / 2 / 10 | 2 / 16 |
| Same rate, different seed schedule | 17.6 s | 1.5 | 12 / 0 / 4 | 1 / 16 |
| Same seeds as 17.6 s | 26.4 s | 1.5 | 14 / 0 / 2 | 2 / 16 |
| EME Rate 1200, weaker | 17.6 s | −6 | 0 / 1 / 15 | 3 / 16 |
| EME Rate 1200, longer, fresh seeds | 105.6 s | −6 | 4 / 0 / 4 | 0 / 8 |
| EME physical, 10 ms raw gap each dwell, filtered | 26.4 s | 10.02 | 6 / 2 / 0 | 0 / 8 |
| Same exact masked observations, raw receiver | 26.4 s | 10.02 | 8 / 0 / 0 | 0 / 8 |
| Raw receiver, fresh key/seeds and same gap recipe | 26.4 s | 10.02 | 16 / 0 / 0 | 0 / 16 |
| Paired no-gap raw control for those fresh seeds | 26.4 s | 10.02 | 16 / 0 / 0 | 0 / 16 |
| Raw receiver, fresh weak gap cohort | 26.4 s | 1.5 | 15 / 1 / 0 | 2 / 16 |
| Raw receiver, Rate 1200 physical, 10 ms gaps | 8.8 s | 10.02 | 8 / 0 / 0 | 0 / 8 |
| HF free base crystal / IC-7100, bounded timing | 23.2 s | 8 | 4 / 0 / 0 | 0 / 4 |
| Shared crystal, 500 kHz shift, HF best | 23.2 s | 8 | 4 / 0 / 0 | 1 / 4 |

HF Each1/Each25 and the separate independent-key HF8 cohort also received all
eight signal trials correctly; the latter had one null admission. Small
four-signal cohorts cover Slow/Fast 1/5/10/25, Each5/10 and Off; all signals were
correctly admitted, with occasional null admissions. These do not prove equal
sensitivity across AM settings. Separate EME physical and 1.5 dB-Hz keys both
received 8/8 correctly; key-dependent pattern distance matters.

At physical EME power, the first/last interior dwell, adjacent middle dwells,
and three dispersed missing-dwell recipes each received 8/8 signal trials;
the middle-pair recipe admitted one null. A four-trial within-bit three-wait-tile
case also passed. These are known ADC erasures on a nominal calendar, not a
proof for every relative arrival or a true RF hop missed by the receiver.

Uncertainty is substantial. Zero null admissions in 4, 8 or 16 draws has a
one-sided 95% upper bound of 52.7%, 31.2% or 17.1%, respectively. The
conservative equal-bit-population 95% lower bounds for 8/8 and 16/16 correct
admissions are only 33.4% and 57.8%. Bounds are per named metric, not
simultaneous across the whole study. The observed 16/16 at 105.6/158.4 seconds
is useful evidence for a duration benefit, not a 99%-reception planner setting.
The 3/16 null result at 105.6 seconds must not be hidden by the zero result at
158.4 seconds or by pooling unrelated cohorts.

## Why the failures occur

### Low-rate source-power reference

The old reference measures seconds1–3 after DATA starts. The source's global
startup edge is `max(ramp,8/fine_rate)`:13.333 seconds for Rate 3.6/Added ×1 and
4.8 seconds for Rate 10/Added ×1. Dividing by the near-zero startup mean gives
a large unintended gain. Measured complete-source energy divided by elapsed
duration is28.262 and6.348 dB above the nominal reference in the affected
non-FHSS fixtures. This is pre-channel power bias, not beneficial modulation,
propagation gain or receiver sensitivity.

The final default, `--power-reference whole-active`, integrates the fixed public
bit 0 source across every active tile. It excludes burst waits and FHSS quiet
time from the power denominator, retains nonzero ramps, and freezes the same
gain for both bits, all epochs and all channel/control realizations. It never
normalizes an outage or a received fade. The existing production Helper preview
and common gain remain unchanged; DATA alone receives the required correction.
`--power-reference legacy-window` reproduces old emitted I/Q, and
`--power-plan-only` returns calibration without generating channel captures or
thirteen template alternatives.

Stratified quadrature starts at 32,768 nodes and checks refinement through at
most 262,144; unresolved differences above 0.1% fail explicitly. That is an
engineering convergence check, not a proven error bound. Seven independent
every-sample source-energy checks at3.6/10/36/360 Hz, FHSS, burst, a second key
and bit 1 passed in 48.576 seconds. All bit 0 active means were within 0.067% of
one; bit 1 retained the same gain and differed by 0.0603%. Calendar means preserve
the intended duty: about 0.725 for EME FHSS and 0.290 for the tested wait calendar.
The correction is −28.2618/−6.3480 dB for the affected low-rate key, compared
with +0.0539 dB for the 11.6-second HF control and +0.0389 dB for EME. These finite
waveform values are not universal rate-only constants.

Eight matched old/new exports verified exact legacy composite I/Q and validity,
and exact Helper+noise I/Q under the corrected DATA default, for HF and EME.
That bridge took 25.805 seconds. Old gates remain tied to their old source and
procedure identities. The sampled low-rate IF 8270 cases each received 8/8 with
one null admission, and the cache-fixed IF 1725 case received 4/4 with zero nulls;
because those precede this power fix, they establish execution/placement only,
not the advertised low-C/N0 sensitivity.

A separate fresh-seed corrected-power cohort tests Rate 3.6 / Added ×1,
DATA IF 1725, no RF shift, OCXO base clock, Sub9-moderate (0.005 Hz / 0.05 ms),
margin 1, AM Off, no FHSS/wait tiles and the bounded arrival prior. At nominal
active DATA C/N0=−6 dB-Hz and 160.4 seconds, it received **8 correct,
0 wrong, 0 missed**, with **0/8 null admissions** after 19 calibration captures.
Measured source active means are 0.999982–1.000603 with one fixed gain for both
bits. Helper remains enabled at 40 W versus 4 W DATA (Helper C/N0=4 dB-Hz), but
all 35 captures used the full fallback: 34 flat profiles and one insufficient
presence diagnostic. This establishes no Helper search-narrowing benefit.
It retains the three-CFO public bank, thirteen epochs and all timing lags.
The same wide uncertainty applies: the balanced correct-admission 95% lower
bound is 33.4%, and the one-sided null upper bound 31.2%. No 99% reception or
daily false-admission qualification follows from these eight holdouts.

That complete local CPU command took 697.490 seconds on a Ryzen 5 PRO 5650U
with two numerical-library threads, including startup, generation, preparation
and finalization. Native export accounts for 409.752 seconds; DATA scoring
201.580 seconds; shared capture preparation 71.476 seconds. Maximum child-process
RSS was 2,695,028 KiB: high-water accounting, not a sum over concurrent processes.
Source/binary identities, package versions, raw records and an independent audit
are separate from the 59 legacy-window cohorts. The study therefore has
**60 completed cohorts / 2,116 scored captures** across the historical and
corrected protocols; they must not be pooled into a common success rate.

### Missing samples and filtering

Matched five-way native controls retain composite, both source alternatives,
Helper-only and noise-only realizations. For the two original wrong 10 ms-gap
cases, raw gaps lose only 0.0243 and 2.955 dB of binary difference energy.
The legacy FIR erasure halo loses 5.795 and 20.560 dB; the longer wide-profile
FIR loses 29.476 and 37.569 dB. A 10 ms gap expands to approximately 72 or
163 ms of unusable downstream samples. Treating these as propagation or
fundamental low-SNR failures would therefore be incorrect.

The optional 18 kHz raw receiver applies the actual row mask in correlations
and Gram matrices, preserving the original white-noise variance. It retains
the full public timing/epoch/CFO search, continuous channel effects and Helper
interference. Its initial scope is singleton nominal audio/RX clocks under a
checked negligible GPSDO-drift approximation, CFO within ±0.6 Hz, and explicit
memory limits. It does not silently approximate free-running clocks as stable.
Fresh Helper-present calibration is required. The two-ray fit still omits the
full selective-AM basis; raw processing alone cannot cure model mismatch.

### Waveform distance, energy and unknown acquisition

At Rate 360 / Added ×1 / FHSS, protection of full pulse tails leaves one protected
fine-symbol center per dwell. Measured source energy is 18.970 normalized
seconds and symmetric half-difference energy is only **2.6444%** of total DATA
energy for the sampled key. Rate 1200 / Added ×1 permits 39 centers per dwell;
its 8.8-second source has energy 6.3739 and a **34.625%** difference fraction.
The fraction improves about 13.1×, but the shorter source also has less energy.
It is not a demonstrated 13.1× duration or sensitivity improvement.

Post-outcome matched-channel oracles separate energy from receiver mismatch.
Three weak EME Rate 360 cases have known-means binary error probabilities
7.74–9.73%; even the oracle makes one error. The selective channel increases
their received energy by about 0.5–1.24 dB while the two-ray subspace retains
only 64–69% of the clean waveform. Conversely, a failed HF−6 case has oracle
error 0.191%, and a failed short Rate 1200 EME case 0.0874%; practical blind
search can still select noise or reject presence. Their known-sync two-ray
retention is about 72%. An energy gain from modulation is not evidence of a
better channel fit, and a known-signal binary oracle is not a blind admission
benchmark.

The retained `theory/rank2_presence_reference.py` computes a complementary
ideal unknown-coefficient reference. For a known rank-two complex subspace in
white noise, the null projection statistic is Gamma(2,1); an alternative has
the corresponding noncentral chi-square law. At alpha=.05, 90/99% detection
requires energy/noise 7.70/12.62 for one known subspace. A conservative union
over 21,320 candidate tests requires 22.14/29.78. This is an achievable
reference detector under its assumptions, **not a fundamental lower bound on
all blind searches**. Correlated candidates, nuisance priors, residual Helper,
colored filtering and model mismatch change the comparison. Foundations:
[matched-subspace detection](https://www.mssc.mu.edu/~daniel/mscs6960/spring2010/ScharfFriedlanderTSP94.pdf)
and [single-tone parameter bounds](https://doi.org/10.1109/TIT.1974.1055282).

### Extreme C/N0 and implementation boundaries

The short −60 dB-Hz fixed-power trials do not receive DATA reliably. HF has
0 correct / 0 wrong / 4 misses; EME has 0 / 2 / 2 and one null admission.
Helper falls back. With the original power ratios its own 36-second energy is
only 0.000360 (HF) or 0.01350 (EME) times noise energy. A favorable known-waveform
single-test presence calculation needs 6.5085 for 90% detection/1% false alarm,
or 18.2732 for 99%/10^-4. Fixed lead-in cannot supply that reference energy.

An explicitly different strong-Helper policy, DATA4 µW / Helper16 W with
DATA−60 and Helper+6.02 dB-Hz, fits at DATA IF 4000 / high pilot 5812.5 but still
gives only 1 correct / 1 wrong / 6 misses in eight short trials. The attempted
90 dB power-ratio policy fails placement within this fixture. Increasing Helper
power does not create DATA bit energy. Ideal known-channel orthogonal bits at
−60 need 19.01/62.64 days for 90/99% correct classification before duty,
unknown acquisition or channel loss. These calculations are not multi-day
waveform/oscillator validation; none was performed in this Helper campaign.

Other exclusions are concrete: very low rates can violate the pulse-halo/
38-second-lead constraint; high rates exceed the 18 kHz fixture or 900 Hz DATA
frontend; high spreading can exceed source-tile/output limits; half-rate IF
below1725 violates Helper placement. IF 8270 is conditional on complete occupied
spectrum, not categorically unsupported. Symbol Burst's whole-bit UTC calendar
is not represented by within-bit wait tiles. Fake FHSS contains no RF retune
transient. Static two-ray/periodic-AM simulations do not establish arbitrary
diffuse-channel resilience or long-term oscillator coherence.

## Computation and reproducibility

The GPU was one RTX PRO6000 Blackwell Workstation Edition, 96 GiB, driver
595.91.07, PyTorch2.8.0/CUDA12.8. The host was EPYC9355P; numerical libraries
used two threads. Campaigns overlapped. Bulk FFT/correlation uses FP32 with
TF32 disabled; sensitive preparation and reductions retain FP64. CPU FP32,
CPU FP64 and actual CUDA parity checks are retained separately from reception.
No new general CPU/GPU speedup is inferred from this campaign; the
[earlier paired CPU/GPU benchmark](../2026-11-04-helper-gpu/README.md)
remains the appropriate measured hardware comparison.

The new bounded free-base-crystal / IC-7100 case uses eight endpoint-inclusive
audio-clock values, three RX-rate alternatives and thirteen epochs. Helper
reduces 17,035 CFOs to about five and usually retains three audio-clock values.
All 27 captures narrowed. By contrast, shared-crystal/500 kHz narrowed only
7/27 and used the full fallback for 20; that cohort took 1,448.7 seconds,
including 1,332.7 seconds of DATA scoring. Helper availability alone is not a
guarantee of computational savings.

The same-fixture cost pilot compares full paired processing with assisted-only
execution. Cold in-process times are 420.855 versus 89.953 seconds; resident
replays are 197.376 versus 3.968 seconds. These are approximately 4.7× and 49.7×
less elapsed time for the named workflows, not pure GPU acceleration or a
confidence-qualified throughput claim. They exclude native generation and
process import/startup. Concurrent work and caching affect these observations;
the independent score/bank equivalence audit is retained. Full fallback remains
necessary when Helper's ambiguity guards fail.

The raw campaign's 223 captures took 743.498 seconds, including generation and
loading. The two additional Rate 1200 duration cohorts took 538.360 seconds.
No simulation rerun belongs in routine CI merely to reproduce those research
tables. Reuse source-bound records; use cheap focused tests for unchanged
branches and freeze a new small campaign only for the affected model/search.

The final cache-bound revision passed141 Helper Python tests in 10.584 seconds,
with one explicit local CUDA skip. The separate actual-CUDA raw suite passed
all six tests in 0.561 seconds. Native export smoke passed eight captures and 14 rejection cases in
28.18 seconds. The 111 native plans took 9.062 seconds. The matched default
export bridge took 14.52 seconds. These functional checks do not establish the
campaign's statistical performance by themselves.

After the power correction, the updated native smoke passed eight captures and
16 rejection cases in 31.381 seconds. The separate seven-capture power check and
eight-capture byte bridge are occasional qualification, not additions to every
routine regression invocation. One local launch initially failed before starting
an experiment because `/usr/bin/time` was unavailable; the retained setup-error
log and standard-library bounded runner distinguish that setup failure from a
receiver result.

The Git base is `eab77e0754d401c4b1e123e3b2e68d8dda4eb133`. The legacy campaign
native exporter SHA256 is `3d94458f4b85c73bc9b3364477c550da6cde96dfdd7841f88148588aebc3e4fa`;
the corrected-power v5 binary is
`41662b4b302d77821353f03245c8c328362cf5e4a88b774adb204273332dff77`.
The compact archive contains exact v1/v2/v3/v4/v5 source overlays, protocol/input/gate
identities, raw JSONL results, failed receipts, analysis scripts and summaries.
Large regenerable I/Q arrays and the duplicate full source tree are omitted;
their hashes and production commands remain in the input inventory. Source
versions must not be mixed when reproducing a historical score.

The [compact archive](compact-evidence.tar.gz) is72,063,592 bytes and contains
9,286 entries, including72 unmodified JSONL streams. Its
[manifest](artifact-manifest.json), [input inventory](raw-input-inventory.json)
and [checksums](SHA256SUMS) describe included records and omitted regenerable
arrays. Archive SHA256:
`b71959e934b71c80d05ae11940c8da0b1ef4016e4679e60a26ae3f491dcad45a`.

This is a completed historical cohort, not an executable campaign recipe. Its
manifest, checksums, retained failures and independent seed identities record
the evidence boundary; they do not authorize restoring retired campaign code
or extending the cohort. The current method for any necessary compute-heavy
Robust check is the [hierarchical abbreviated harness](../../../triad-performance.md),
and temporary evidence follows the [material lifecycle](../../../development.md#material-lifecycle).
