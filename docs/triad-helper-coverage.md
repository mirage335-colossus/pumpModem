# Helper-assisted triple Robust coverage map

This is the implementation and evidence map for the isolated Helper-assisted
three-carrier experiment. Read it before starting another broad parameter
campaign. The 2.5-billion-point requested configuration space is a catalog, not
a reception test plan: a complete factorized necessary-constraint census and
a deliberately factorized set of sampled receiver cases provide different
evidence. Ordinary
Robust receiver integration and physical-radio performance are outside this
study.

The [November 5 extended qualification report](validation-data/triad-v3/2026-11-05-helper-extended/README.md)
records the new sampled high-rate and maximum-spreading outcomes, GPU timing
and quantitative multi-day boundaries. Do not infer a received configuration
from the domain census or a planning pass.

**Check source power before interpreting a low-rate C/N0 result.** The final
exporter defaults to `--power-reference whole-active`; the old two-second
startup-window reference overstated source energy by about 28.26 dB at Rate 3.6
and 6.35 dB at Rate 10 in this study. Historical low-rate successes are excluded
from sensitivity claims until repeated with corrected power. The
[sampled report](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
retains both the original observations and the correction evidence. Use
`--power-plan-only` for a cheap calibration check and `legacy-window` only for
explicit historical reproduction.

Start with the [Helper-assisted acquisition guide](triad-helper.md), the
[efficient performance-testing policy](triad-performance.md), the
[channel-model contract](channel-models.md), and the [rate-aware shaping
guide](triad-shaping.md). The earlier [v3 development workflow](triad-development-workflow.md)
explains when to reuse retained samples and when to generate new captures.

## Evidence layers

Do not count a configuration as received merely because it parsed, produced a
plan, or exported I/Q. Keep the evidence at its actual layer:

| Layer | What it establishes | What it does not establish |
| --- | --- | --- |
| Requested-axis catalog | The Cartesian combinations the user asked to consider and a reproducible order for representative cases. | Feasibility, waveform emission, reception, physical behavior, or exhaustive testing. |
| Native planner / `--plan-only` | Geometry, pulse and dwell fit, shaping and Helper placement, power checks, oscillator bounds, timing prior, and configured capture/workspace limits. | A transmitted waveform, receiver sensitivity, a false-admission rate, or physical-radio operation. |
| Sampled export | The keyed emitted I/Q and its metadata, including Helper plus DATA composition, modulation, filters, sampled channel, noise, and retained tails for that capture. | Receiver acceptance or any performance outside its sampled model and duration. |
| Receiver campaign | Correct, wrong, missed, and false admissions for the frozen receiver/search/calibration protocol and tested seeds. | Universal sensitivity or behavior outside the tested channel, timing, duration, and search family. |
| Cable / radio observation | Behavior of the tested device path, levels, clocking, and configuration. | Other transceivers, propagation channels, antennas, or regulatory operating conditions. |

The broad planning protocol records 2,500,485,120 requested
combinations across rate, IF, RF shift, timing, base/shift oscillators, spread,
selective modulation, C/N0, margin, Added Symbols, FHSS, and Symbol Burst. The
factorized representative plan contains 111 rows, plus separate missed-hop
patterns and tile-wait probes. Those are explicit coverage design artifacts;
they are not 2.5 billion receiver runs. The planner catalog and sampled
campaign are separate protocols. Consult the [sampled campaign record](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
before making any reception claim.

The first 59 completed cohorts actually sample rates 3.6/10/36/100/360/1200 Hz,
Added ×1/×10, DATA IF 1725/1775/1800/2000/4000/8270 Hz, and RF 0/0.5/1/30 MHz/5.8 GHz.
They include every AM mode/depth at selected conditions and both timing priors,
but no 0.001–1 Hz reception, Added ×100/×1000 reception, MHz-rate reception or
multi-day Helper observation. The 160.4-second maximum is simulated observation
duration, not a sustained-compute benchmark. The archived manifest-backed axis
inventory records missing combinations; successful native planning must never
fill those gaps implicitly. The subsequent extended campaign adds actual
Rate 1 Hz / Added ×1000 and Rate 10 Hz / Added ×100 observations and rates
3.6/12/18/24 kHz. It does not add 0.001–0.1 Hz, MHz-rate or multi-day
Helper-assisted reception; its maximum-spreading cases have measured misses.

### Exact factorized census, default 18 kHz path

`tools/triad_helper_domain.py` classifies the complete requested Cartesian
catalog (2,500,485,120 tuples) under a deliberately small set of necessary
constraints for the exporter’s **default 18 kHz audio sample rate** and the
wide DATA receiver frontend (400 Hz pass edge). It evaluates 408 combinations
of only Rate, DATA-center mode, Added Symbols and whole-bit Symbol Burst, then
weights each by the other nine axes (6,128,640 tuples per projection). The executable
report binds the classifier, axis source, native source geometry, Helper
placement, exporter and DATA receiver source hashes. Reproduce it with:

```sh
PYTHONDONTWRITEBYTECODE=1 /tmp/triad-weak-env/bin/python \
  tools/triad_helper_domain.py \
  --output local-evidence/triad-helper-extended/domain-final.json
```

The mutually exclusive classification below applies precedence in the order
shown. If a tuple violates more than one condition, it appears once in this
table; the nonexclusive counts include it under every necessary constraint it
violates. The residual means **not excluded by these checks**, and remains
unqualified for native planning, waveform emission and reception.

| Mutually exclusive classification | Tuples |
| --- | ---: |
| Whole-bit Symbol Burst ×3 has no matching Helper-exporter calendar | 1,250,242,560 |
| DATA center below the 1725 Hz Helper floor | 269,660,160 |
| DATA center at or above the default 9 kHz Nyquist | 98,058,240 |
| Nominal triplet upper edge at or above Nyquist | 471,905,280 |
| Nominal DATA half-bandwidth exceeds the wide frontend’s 400 Hz pass edge | 67,415,040 |
| Pattern sampling inequalities fail after earlier exclusions | 0 |
| Native pulse calendar origin exceeds the fixed 38 s lead | 122,572,800 |
| Not excluded by this census; still unqualified | 220,631,040 |
| **Total** | **2,500,485,120** |

The corresponding nonexclusive counts are: whole-bit burst unsupported,
1,250,242,560; center below the floor, 539,320,320; center at/above Nyquist,
196,116,480; nominal triplet edge at/above Nyquist, 1,237,985,280; DATA
half-bandwidth above 400 Hz, 1,470,873,600; source sampling inequalities
`Fs < 4*fine` or `Fs <= 1.1*occupied`, 1,029,611,520; and pulse calendar
origin beyond the lead, 367,718,400. These overlap and must not be summed.

The pulse test matches native integer coordinates:
`calendar_origin = ceil(8*Fs/fine) + 2` samples, which must be no later than
`38*Fs`. Therefore an ideal eight-chip halo of exactly 38 seconds fails by the
two-sample guard. The upper DATA edge equality is excluded. The 400 Hz check
uses only nominal source half-bandwidth (`occupied/2`); it does not include
clock, CFO or channel-line allowances. Nominal occupancy is not compact
spectral support and says nothing about residual skirts or sensitivity.

The census intentionally does not classify other axes as passing. FHSS dwell
aperture/protected-cell fit, margin-dependent propagation and oscillator
uncertainty, selective-modulation sidebands, Helper placement and leakage,
shaping, source tile workspace, capture/output size, and all receiver
performance remain unqualified pending the relevant native plan or sampled
test. Its energy helper separately reports ideal one-known-waveform presence
references; those do not cover unknown acquisition, bit discrimination,
outages, repeated searches or multi-day channel tracking.

The legacy frontend has a narrower 330 Hz pass edge, so it can reject additional
source widths that the 400 Hz census leaves unexcluded; these extra exclusions
are not counted here. The exporter now also has an opt-in `--sample-rate` control; the
`--frontend-profile rate-aware` receiver derives a DATA decimator from its
declared public pass edge. Those configurations, including higher-rate audio
fixtures, are **outside the 18 kHz census above** and require a new sample-rate-
aware classification and native plan. The default exporter remains 18 kHz.
Current CLI sample-rate inputs are multiples of 5 Hz in `[6000, 4294967295]`;
this range is only a parser bound, not assurance that memory, spectral
placement, receiver geometry or runtime supports every rate. Whole-bit Symbol
Burst ×3 remains unsupported; the exporter’s within-symbol wait tiles are a
different control.

## Supported and unsupported operating regions

The native Helper exporter defaults to 18 kHz complex audio samples and also
supports an explicit `--sample-rate` option. The public receiver converts to
zero-centered 900 Hz complex DATA samples by default. The legacy
profile uses a 330 Hz pass edge and the opt-in wide profile a 400 Hz pass edge;
this is a post-DDC processing bandwidth, not an absolute limit on DATA IF.
Helper placement follows the standardized DATA floor of 1725 Hz, with 1775 Hz
also available where selected. The high pilot starts at 2400 Hz and rises when its validated separation/leakage rules require it. Geometry must include
all three DATA carriers, pulse skirts, oscillator allowance, Helper lines, and
guards. A requested 8270 Hz IF alone does not demonstrate sub-9-kHz fit; the
full composite spectrum must fit below the 9 kHz Nyquist limit.

The DATA receiver checks the actual public nominal source half-bandwidth,
selected modulation/ray line offsets, clock scales and CFO envelope against
its chosen FIR pass edge. The old fixed 112.5 Hz source width and 90 Hz ray
separation restriction rejected narrower sources unnecessarily. Removing those
restrictions does not make finite pulse skirts compactly supported: sampled
interference and sensitivity still require validation. At 8270 Hz, narrower
low-rate configurations can fit while Rate 10/Added×10 does not; do not declare
the entire IF unsupported from one failed rate.

The rate range in the request is not all supported by one fixture. The exporter
has explicit rate, duration, epoch, output-size, pulse-halo, and source-tile
limits. In particular, the Helper lead is fixed at 38 seconds and payload
source halo must fit within it. Very low rates can require halos longer than
this lead. The tiled source bounds one tile to 1,000,000 fine cells; the
template/export path separately enforces a 2 GiB memory/output budget. Do not
extrapolate through any of those boundaries. Exact feasibility is the native
planner's answer for the current source revision and complete configuration.
The exporter accepts rates through 30 MHz at the CLI boundary, but that input
range is not a promise that the sample rate, audio spectrum, timing lattice,
workspace, or receiver can process every rate.

Current protocol limitations that need explicit handling:

- Fake FHSS is an envelope/calendar impairment. It is not RF retuning, hop
  synthesizer settling, or a physical missed-hop experiment. The seven frozen
  masks (none, first, last, end-plus-next-start, adjacent middle, central, and
  four dispersed dwells) are receiver/source-mask diagnostics only when actually
  run against samples.
- The user-requested whole-bit Symbol Burst 3 schedules the next symbol after a three-span wait on its UTC calendar. It must not
  be modeled as three within-symbol wait tiles. The native Helper export format
  currently supports the latter `--burst-wait-tiles` control but not a complete
  bit burst request.
- Added Symbols is the spreading multiplier in this format. Interleaving keeps
  whole symbol cells. A factor of zero is not a supported configuration; use
  one for the unexpanded baseline.
- `spread-preset none` bypasses added frequency-selective modulation unless an
  explicit nonzero `--selective-modulation-rate-hz` requests modulation-only
  stress. Sweep the rate independently when separating the channel components.
- A GPSDO oscillator preset constrains modeled oscillator behavior; it does not
  synchronize the host clock or remove public timing/arrival uncertainty.
- Per-carrier phase and amplitude measurements from the sampled channel are
  meaningful only for the full overlapping, continuous-filter waveform. Avoid
  carrier-label filtering when passbands overlap.

## Energy bounds and interpretation

DATA and Helper each need a received C/N0. With the inherited HF power ratio
(+10 dB) and microwave ratio (+25.7403 dB), DATA at −60 dB-Hz implies Helper
at −50 and −34.2597 dB-Hz. In 36 seconds their ideal total Helper energy/noise
ratios are only 0.000360 and 0.01350. Enabling Helper cannot create a reliable
reference at those energies. The study also considers an explicitly separate
strong-Helper policy; it does not relabel that policy as either original link.

For a known deterministic waveform in complex white Gaussian noise, the
favorable single-test Neyman–Pearson presence reference requires
`rho = (Phi^-1(1-alpha) + Phi^-1(Pd))^2 / 2`. It requires 6.5085 energy/noise
units for Pd=90%, alpha=1%, or 18.2732 for Pd=99%, alpha=10^-4. At the above
weak Helper levels these correspond to 7.53/21.15 days (HF) and
4.82/13.54 hours (microwave). Unknown frequency/channel, gating, pilot power
splits and repeated attempts make these optimistic lower references, not
achievable receiver durations or daily false-admission guarantees.

Even the binary-classification limit is slow at DATA −60 dB-Hz. If *all*
received energy belongs to one bit and phase/channel are known, orthogonal
alternatives require 19.01 days for 90% correct or 62.64 days for 99%; known-phase
antipodal alternatives require 9.51/31.32 days. Antipodal alternatives are
indistinguishable under an arbitrary unknown common phase. Actual keyed
waveform distance, `Pe = Q(sqrt(||s1-s0||^2/(2*sigma2)))`, is the relevant
conditional reference. Classification and admission are different tasks.
These formulas do not establish long-term oscillator/channel behavior.

There is a substantial waveform-allocation constraint in the 360 Hz / Added ×1
FHSS configuration. At fine rate60 Hz, the full protected-pulse condition leaves
`0.4 - 2*(0.055 + 0.005 + ramp + 8/60)` seconds for symbol centers: even zero
ramp allows less than one 16.667-ms grid interval. The selected 6.611-ms ramp
retains one center per interior dwell. Earlier exact source measurements put
the symmetric half-difference energy at 2.6444% of total DATA energy for the
broad campaign's source key. This is about15.78 dB below ideal known-phase
antipodal distance at equal energy; it is not
an achievable unknown-channel sensitivity gap or simply a cover-only fraction.

An existing configuration change offers a larger opportunity than small ramp
optimizations: Rate 1200/Added ×1 has fine rate200 Hz, 750 Hz nominal bandwidth,
39 protected centers per80 fine cells/dwell, and an8.8-second minimum tile.
For the strong microwave Helper, its current planner requires DATA at least
1778.125 Hz; the explicitly tested candidate is1800 Hz with high pilot 2928.125 Hz.
The wide DATA frontend nominally contains its750-Hz triplet plus selected6-Hz
rays/modulation. Planning and center counts alone do not establish its received
bit distance or error rate; use the sampled results below. Changing the
bit-independent cover or shortening pulses instead would require a separate
waveform identity and new interleaving, spectral and missed-hop qualification.

## Public campaign controls

`tools/triad_helper_campaign.py` forwards configuration to the native exporter
before it freezes the emitted public manifest and receiver protocol. The
exporter now exposes the following configuration controls (omitted values
preserve the exporter defaults):

```
--rf-hz --base-oscillator --shift-oscillator --spread-preset --margin
--selective-perturbation --selective-modulation-rate-hz
--timing-policy case-default|bounded|broad --data-power-w --helper-power-w
```

`--shaping auto` remains the campaign default. `--shaping legacy` is retained
for historical matched comparisons. Campaign receiver controls include
`--frontend-profile legacy|wide|rate-aware`, `--clock-step-ppm`,
`--doppler-scale-count 1|3|5|7`, `--aligned-fft-grid`, and `--lag-step`.
The public clock and Doppler banks are derived from the exported public
manifest; receiver construction does not inspect evaluator truth. The optional
raw-sample mask is a public erasure policy: its filter expansion halo must be
invalidated before the receiver scores downstream 900 Hz samples. The broad
campaign applies and records these masks before scoring, binds their identities
to the prepared observations, and retains separate calibration for each
procedure. They represent known missing ADC samples, not unknown losses or
physical RF retuning. Conservative FIR support exclusion can lose much more
useful evidence than the raw missing-sample fraction; the physical EME
10-ms-per-dwell failure is retained in the sampled record.

Use `--lag-step 1` for current campaigns. A prior `lag-step 4` timing lattice
produced a wrong-bit result on a real low-SNR captured realization despite
searching all thirteen public epochs. Post-outcome isolation with the same CFO
alternatives and full epoch bank showed that lag step one recovered the
correct bit at nearly the same comparison cost (about 7.8 seconds in that
diagnostic). This is a concrete correction for one failure, not an independent
performance or sensitivity qualification. Use lag step four only when
reproducing that historical comparison.

The campaign must record distinct outcomes: accepted correct bits, accepted
wrong bits, misses, and noise-only false admissions. A small calibration cohort
or a zero-false result in a few captures is not an operational false-alarm
guarantee. Calibrate over the maximum search and repeated attempts required by
the declared use case.

## Efficient coverage method

1. Run cheap native plan-only checks first for each distinct algorithm,
   feasibility, and resource regime. Include boundary rows that select a
   different IF, pulse support, sample rate, timing policy, RF oscillator,
   tile count, memory behavior, or fallback.
2. Reject infeasible cases with the planner's reason. Preserve those outcomes
   as unsupported-region evidence rather than silently dropping them.
3. Export paired sampled captures only for cases where channel/waveform effects
   matter; share the expensive preparation and retain I/Q when comparing
   receiver algorithms or search grids on identical channel realizations.
4. Freeze a small receiver campaign with independently generated validation
   seeds. Broaden duration and model complexity only if the cheap measurements
   or uncertainty show a plausible material benefit.
5. For missed-hop and wait-tile behavior, test the chosen explicit masks and
   tile-boundary cases. An envelope-only mask cannot substantiate resilience to
   an RF retune transient.
6. Reuse source-bound evidence only while the relevant producer, channel,
   receiver, search, arithmetic, and fixture identities remain unchanged.
   A change to shaping invalidates waveform and spectral evidence; a change to
   receiver scoring/search invalidates admission calibration; a channel-model
   change invalidates sampled channel results. Unaffected evidence may still
   be reused if the changed stage is isolated and matched-input validation
   supports that separation.

Do not generate every rate × IF × RF × timing × oscillator × spread × AM ×
C/N0 × margin × Added Symbols × FHSS × burst product. Keep a factorized plan
covering each distinct branch and pair high-risk interactions deliberately,
especially low C/N0 with spread/modulation, low-rate workspace limits, narrow
IF constraints, and the largest timing/search banks. Avoid repeating waveform
generation and receiver setup separately for each algorithm when the sample
and immutable preparation can be shared.

## Reproduction entry points

Native planning is cheap and does not write a capture:

```
build/dev-cli/triad_helper_export --case hf --plan-only --rate 36 \
  --added-symbols 10 --data-if 1725 --rf-hz 30000000 \
  --base-oscillator crystal --shift-oscillator ic-7100 \
  --spread-preset hf-worst --margin 3 \
  --selective-perturbation each-65 --timing-policy bounded
```

For a sampled receiver trial, use the campaign wrapper so the exporter,
manifest, receiver parameters, calibration seeds, and outcome report stay
bound together. For example, add `--templates PATH --output NEW_PATH
--exporter build/dev-cli/triad_helper_export --seed-base N` plus the intended
`--calibration-count`, `--signal-count`, `--null-count`, `--seconds`, and
`--cn0`. `--cn0` is DATA C/N0 in dB-Hz, not in-band sample SNR. Keep output
directories new and preserve their frozen manifests.

The [maintained compact evidence archive](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
contains the original broad protocols, frozen source overlays, input identities,
failed receipts and raw outcome records. Under its `evidence/coverage/v2/`, `accepted-protocol.json`
defines the requested axes and explicit unsupported behaviors;
`accepted-native-plan-results.json` records plan-only results;
`sampled-preflight.json` records 39 representative preflight rows;
`default-bridge/results.json` is a matched old/new exporter default-compatibility
check, not a reception study; and `inference/` contains focused post-outcome
diagnostics. Preserve their stated scope when reusing them.

## Campaign results and next useful work

The [sampled campaign report](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
records correct/wrong/missed/false admissions, uncertainty, source versions,
matched energy/model diagnostics and actual CUDA runs. The most consequential
findings are:

- Physical HF/EME cases and several strong-AM cases worked on the sampled
  holdouts. Short weaker cases still miss or admit wrong bits. Increased duration
  improves the observed EME results, but small null cohorts do not qualify an
  operational false-admission rate or a 99% duration planner.
- A known 10 ms ADC gap can become a 163 ms erasure after conservative FIR
  support exclusion. The optional raw-sample receiver recovered the two
  originally wrong physical-link cases and passed a fresh 16-signal cohort.
  It has explicitly narrower clock/CFO support and its own calibrated identity;
  use `tools/triad_helper_raw_study.py campaign` only inside that scope.
- `--assisted-only` or broad `"arms": ["assisted"]` avoids the unused full-bank
  comparison while preserving fallback. The free-base-crystal / IC-7100 example
  narrows strongly. Shared-crystal/500 kHz frequently falls back and remains
  expensive. Do not equate a large candidate-count reduction with the same
  end-to-end speedup.
- Extreme −60 dB-Hz DATA with the original Helper power ratio cannot acquire
  a reliable 36-second Helper reference even under favorable known-waveform
  energy assumptions. Multi-day Helper-assisted streaming remains unvalidated.

Prioritize changes that recover discarded sample evidence, reduce unnecessary
preparation, or address the demonstrated waveform-distance/acquisition gap.
Do not restart the whole research campaign to test a local fix. The inexpensive
Helper unit tests, native plan-only boundaries and a few matched saved fixtures
are the first checks. Recalibrate only the changed search/model and validate on
fresh seeds before making a new reception claim. A known-means oracle, a public
plan, a CUDA parity test and a passing function-entry witness each answer
different questions; none substitutes for an operational admission study.
