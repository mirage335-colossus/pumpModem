# Finite-bank acquisition and Gaussian likelihood references

This isolated three-adjacent-carrier v3 study addresses an identified receiver
failure: a nominal timing template can confidently select the wrong bit even
when a nearby public timing hypothesis supports the correct waveform. It adds
an actual-observation grouped acquisition path and a small exact covariance
reference. It does not integrate with the ordinary Robust receiver, change the
transmitted format, or establish an optimal receiver or minimum duration.

**The completed actual-noisy campaign fixes major timing-acquisition failures,
but admits all eight wrong-key controls at both gates.** Null admissions were
0/8. An experimental optional consistency postprocessor rejected those reused
controls and the fresh wrong-key controls below, but also removed one of 16
fresh legitimate receptions. The original failure remains part of the record;
neither result makes reception key-authenticated or operational false
admissions qualified. The ordinary receiver and production defaults are
unchanged.

Separately, **a fixed 1-dB classification gap is confirmed for the stated
0.125-second, known-synchronization Gaussian model**, using a declared
four-bound 95% confidence family. That calculation includes neither signal
admission, acquisition, wrong keys nor the finite Each65 channel law. The
**2–3 dB finite-acquisition goal remains unresolved**; neither the aligned
reference nor the finite reception campaign qualifies the practical receiver
at general reception/admission targets. A known-realized-channel bound is a
separate, more favorable benchmark.

The [preceding selective correction study](../2026-11-02-selective-correction/README.md)
records the motivating high-spread failures and timing-oracle diagnostics. The
[development workflow](../../../triad-development-workflow.md),
[channel contract](../../../channel-models.md), and
[precision guide](../../../gpu-precision-controls.md) remain applicable.

## Distinct questions and benchmarks

| Question | Observation/model | What a result establishes |
| --- | --- | --- |
| Known-channel bit classification | Both deterministic candidate mean waveforms and synchronization are supplied; only receiver noise is unknown. | Favorable equal-prior binary error bound. No signal-presence decision or false-admission guarantee. |
| Unknown Gaussian-channel bit classification | Alignment, noise variance and a specified Gaussian channel prior are known; the random channel is marginalized. | Exact finite-matrix Bayes likelihood for that prior. No acquisition, wrong-key control or operational admission test. |
| Practical fixed-bank reception | Actual noisy projections, public waveform/Gram/calendar metadata and residual noise calibration; synchronization groups are fixed before observing data. | Correct accepts, wrong accepts and misses for the tested bank/model. Noise-only and wrong-key controls remain separate populations. |

Correctly choosing bit 0 versus bit 1 does not establish that a signal should
be admitted. A noise-only admission bound also does not guarantee rejection of
a signal transmitted under another key, or prevent wrong-bit admissions under
channel-model mismatch. Counts and probability targets must retain all three
distinctions.

## Grouped actual-observation acquisition

The retired `triad_group_acquire.py` consumed
`triad-group-block-stats-v1`. Each fixed synchronization group contains both
bit alternatives and their complete local column union. The current selected
basis has two taps per bit; each tap waveform already contains the three
adjacent patterned carriers. A group therefore has four local columns, rather
than four independent physical observations.

The receiver reads only the manifest, group Grams, actual noisy group
correlations, received energy, intervals and valid-row counts. It does not
read the simulator's group means, noiseless signal energies, injected noise
variance, source bit, or selected timing truth to decide. Evaluation labels
are attached after scoring.

For each group, projection onto its complete fixed column union leaves an
orthogonal residual and an explicit number of complex noise degrees of
freedom. The stationary proper-complex white-noise upper-confidence bound
allocates its calibration failure budget across **all** declared groups.
Signal outside a group's modeled span can enter that residual; it must not be
silently interpreted as a perfectly matched channel or a free sensitivity
improvement. Nonlinear/non-Gaussian front ends are rejected by this path.

For bit `b`, the group scores `ell[g,b]` are combined as a normalized mixture,

\[
  L_b = \operatorname{logsumexp}_g \ell_{g,b}-\log H.
\]

All `H` groups remain in the mixture and calibration. The best group is
reported for diagnostics; it is not an uncharged selected receiver. Group
observations come from the same sampled input and are dependent. The export
does **not** save inter-group Grams, so independent conditional resampling of
group noise is unsupported. Actual-observation scoring avoids that invalid
shortcut.

For uniform weights, any included group's score obeys
`ell[g,b] - log(H) <= L_b <= max_g ell[g,b]`. This identifies one finite-bank
mixture cost; the extra noise-calibration allocation and actual off-grid
waveform mismatch also matter. It is not a universal fixed dB penalty.

The pilot retained `H=109` public arrival hypotheses. At rate 360, 2048
samples/s, `B=30 Hz`, 7-ms ray delay, 30-Hz ray separation, corrected Each65,
and 20 dB-Hz, the source bit was 0. The nominal group's two log scores were
`743.4221` and `816.9985`, favoring the wrong bit. The complete fixed-bank
mixture gave `4108.9674` versus `993.8741`, accepting bit 0 at both gates. The
best group arrival was `0.14887963 s`; the hidden actual arrival was
`0.14957102 s`, disclosed here only to assess acquisition. This is one
development realization, not a reception-probability estimate.

Pilot receipts are retained under
`local-evidence/triad-acquisition-improvement/pilot/`: `export-plan.json`,
`export-status.json`, `score-status.json`, `score.json` and `scores.npz`.
The nominal and full-bank scores above use the same grouped actual noisy
observation. They do not compare a fresh independent path or a changed signal
power. The configured family retains 16 receiver alternatives, eight duration
alternatives, two calibration alternatives, and 60-second acquisition
attempt spacing. The gates are approximately `22.7211` nats for the strict
daily budget and `17.4227` nats for the relaxed 12-hour budget, with the strict
noise calibration retained for the relaxed comparison. These analytical
fixed-family allocations are not empirical rare-event qualification.

### Search coverage is arrival only

The 109 groups search **arrival only**. Every group fixes common frequency
offset, sample-clock error, keyed-epoch offset and frequency drift to zero.
The sampled transmitter and receiver GPSDO-OCXO oscillators remain independent;
their errors are not set to zero or revealed to the receiver. The manifest's
static oscillator CFO bound is approximately `±9.77000000049e-7 Hz`, and its
relative audio-clock bound is `±0.000100000000005 ppm`. These narrow selected
priors do not turn zero receiver alternatives into a frequency or clock
search.

The campaign arrival endpoints `±0.090500000001 s` cover the configured
**initial** timing prior `±0.09050000000000001 s`, using approximately
`1e-12 s` (0.001 ns) of endpoint padding. This corrects the pilot's one-ULP
decimal endpoint shortfall; it does not retroactively change that pilot or
cover all joint clock/calendar effects. Native metadata correctly reports
`covers_initial_timing_prior=true`, with
`covers_static_oscillator_frequency_prior=false`,
`covers_audio_clock_prior=false` and `covers_suggested_epochs=false`.

The suggested keyed-epoch offsets `[-1,0,+1]` are **not** searched: only zero
is exported. Reception is therefore conditioned on the known key and matching
keyed epoch, with the selected narrow GPSDO uncertainty left in the actual
samples. It is not full blind acquisition, coverage of every selected
uncertainty, or a GPSDO-free/general oscillator result. The broader 2–3 dB
finite-acquisition goal remains unestablished.

## Completed actual-noisy campaign

The accepted audit verified **172 exports and 200 scoring jobs**, with no
missing/invalid jobs. It used the shared sampled transmitter/channel, corrected
selective operator, independent physical oscillator/channel/noise roots for
the independent sets, and the fixed arrival-only bank above. Primary scores
used block16, a 0.25-second Gaussian window, FP32 bulk and FP64 preparation/
finishing. `high10` has Gaussian width/ray separation `B=D=10 Hz` and 6-ms
delay; `high30` has `B=D=30 Hz` and 7-ms delay. These are 120-second requested
transmissions, rounded to the source's complete private tiles, not the
0.125-second Gaussian reference experiment.

The selected RF center is **8,270 Hz**, GUI rate 360 Hz, Added Symbols ×1,
with independent GPSDO-OCXO audio/RF references and no whole-bit Symbol Burst.
These explicitly overridden high-spread laws are simulation stress cases,
not field-calibrated predictions for that RF band. This campaign does not
establish 30-MHz or 5.8-GHz acquisition performance.

The independent signal sets balance 32 transmitted bit-0 and 32 bit-1 paths
each. Entries below are `correct / wrong / missed`; both views score the
**same observations with the same already-conservative calibration and gates**.
The nominal view selects the fixed public group nearest source-coordinate
zero. It is neither a separately calibrated receiver nor an oracle.

| Independent Each65 signal | Full arrival bank, strict | Nominal group, strict | Full bank, relaxed | Nominal group, relaxed |
| --- | ---: | ---: | ---: | ---: |
| high10, 8 dB-Hz | 63 / 0 / 1 | 7 / 0 / 57 | 64 / 0 / 0 | 8 / 0 / 56 |
| high30, 12 dB-Hz | 64 / 0 / 0 | 6 / 3 / 55 | 64 / 0 / 0 | 6 / 3 / 55 |

This is measured acquisition improvement on the declared timing bank. It
does not prove that frequency, clock, epoch or every channel uncertainty was
searched, or that 90%/99% operational reliability follows from these counts.

### Wrong-key failure and uncertainty

At high30/Each65, 20 dB-Hz, the independent controls gave:

| Actual-noisy control | Full bank, strict | Full bank, relaxed | Nominal group with same calibration |
| --- | ---: | ---: | ---: |
| Null | 0 admitted / 8 | 0 admitted / 8 | 0/8 at either gate |
| Wrong-key signal | **8 admitted / 8** | **8 admitted / 8** | **8/8 at either gate** |

Thus wrong-key failure also occurs in the nominal view; it cannot be
attributed solely to adding arrival hypotheses. The eight signal-present
wrong-key controls are a different distribution from noise-only captures.
An analytical Gaussian noise-only gate does not promise their rejection.
The cause remains under investigation; these observations do not establish
an intrinsic impossibility or general key-rejection guarantee. The optional
consistency followup below is a separate research postprocessor, not a
retroactive correction of these base-receiver outcomes.

The audit supplies **pointwise two-sided Clopper–Pearson 95% intervals**.
Each per-bit interval assumes independent Bernoulli outcomes with a common
probability within that named bit stratum. Its separately labeled pooled
interval additionally assumes a common success probability across bit-0 and
bit-1 populations. Balanced allocation alone does not justify that extra
assumption. Representative full-bank intervals are:

| Population/event | Bit-0 count and interval | Bit-1 count and interval | Optional pooled common-probability interval |
| --- | --- | --- | --- |
| high10 strict correct | 31/32, [0.83783, 0.99921] | 32/32, [0.89112, 1] | 63/64, [0.91599, 0.99960] |
| high10 relaxed correct | 32/32, [0.89112, 1] | 32/32, [0.89112, 1] | 64/64, [0.94399, 1] |
| high30 correct, either gate | 32/32, [0.89112, 1] | 32/32, [0.89112, 1] | 64/64, [0.94399, 1] |
| Null admission, either gate | 0/4, [0, 0.60236] | 0/4, [0, 0.60236] | 0/8, [0, 0.36942] |
| Wrong-key admission, either gate | 4/4, [0.39764, 1] | 4/4, [0.39764, 1] | 8/8, [0.63058, 1] |

These are not simultaneous bounds across outcomes, settings, gates or chosen
operating points. No 90%/99% reception qualification is claimed here, and
0/8 null admissions cannot empirically establish `1e-4/day` or `1%/12h`.
The actual-noisy campaign intervals are also distinct from the independently
declared four-bound confidence family in the Gaussian classification
confirmation below.

### Paired weak cases and block-cost tradeoffs

The earlier four-path seed sets were reused for bounded model/geometry
comparisons. Full-bank strict/relaxed correct counts were 2/4 and 3/4 for
high10/Each65 at 6 dB-Hz, and 4/4 at 12 dB-Hz. High30/Each25 at 8 dB-Hz
missed all four; Each65 accepted 1/4. At 20 dB-Hz both depths accepted 4/4
with no wrong bits. These small paired variants and reblock views are not
additional independent receptions.

For high30/Each65 at 8 dB-Hz, the median measured receiver costs and full-bank
outcomes were:

| Effective block | Physical Gaussian window | Median receiver seconds | Strict correct / wrong / missed | Relaxed correct / wrong / missed |
| ---: | ---: | ---: | ---: | ---: |
| 8 samples | 0.25 s | 425.64 | 1 / 0 / 3 | 1 / 0 / 3 |
| 16 samples | 0.25 s | 84.65 | 1 / 0 / 3 | 1 / 0 / 3 |
| 32 samples, reblocked from 16 | 0.25 s | 24.24 | 0 / 0 / 4 | 1 / 0 / 3 |

The finer export cost about five times as much without an observed admission
gain in these four paths. Coarsening was much cheaper but lost one weak
strict admission. Finer block8 reblocked to 16 took a median 102.81 s and
also accepted 1/4 at each gate. This is a measured cost/sensitivity tradeoff,
not permission to prune weak candidates or change defaults silently.

The accepted recount, including retained JSON/NPZ score arrays and manifest
bindings, is `local-evidence/triad-acquisition-improvement/analysis/`:
`main-campaign.json`, `main-campaign.md`, `main-campaign-details.json` and
`main-campaign.status.json`. Main analysis SHA-256 is
`4f74b6d8ce6d78730ed92b517303ec1d8ab5a2f2606a6e4bf1deb6212d70792a`;
accepted analyzer SHA-256 is
`ed48a3326c1968215481336b2e2b0d4102e9ee04473cbe3c6893e33fc8967e55`.
The audit completed in 2.485 seconds; this is evidence-processing time, not
receiver throughput. Raw-byte verification scope is stated below.

## Experimental pattern consistency and duration followup

The accepted followup audit rechecked 204 full-score JSON/NPZ records and
manifests, and 200 per-window score NPZs. These comprise 168 reused
development captures, four paired duration extensions and 32 fresh physical
cases; they are not 204 independent validation trials. The optional
`pattern_consistency.py` recomputed
window likelihoods at the **unchanged winning bit's post-data best group**.
It neither changes the full-bank bit winner nor reruns acquisition with
simulation truth.

For window scores `ell[w,0]` and `ell[w,1]`, its contrast is

\[
 C_b=\sum_w\left[\ell_{w,b}
       -\log\frac{\exp(\ell_{w,0})+\exp(\ell_{w,1})}{2}\right].
\]

This compares one fixed bit across windows with a **window-bit-switch
proxy**. The preselected primary rule is **`C_winner >= ln(100)` AND the
existing noise admission**, with `ln(100)=4.60517019`. A zero-threshold rule
is retained as a diagnostic only. Because a post-data best group and
UCB-scaled approximate likelihoods enter the contrast, `exp(C)` is not a
calibrated wrong-key Bayes factor. A wrong key is not a source that chooses
independent uniform bits in each window. The statistic is neither
authentication nor a proved wrong-key rejection probability.

The AND rule can only remove an existing admission. It therefore preserves
any valid existing noise-only admission bound by set inclusion, without
creating a correct-reception or wrong-key guarantee. Its additional observed
cost was about 3.17–6.60 seconds per saved-capture replay, including wrapper
and input verification, with median 3.52 seconds across these 200 jobs. This
is separate from the full grouped receiver's much larger preparation cost.

### Reused development observations

Among the 168 original block16/reblock1 captures, the primary rule rejected
the original **8/8 admitted wrong-key signals**, while the null controls
remained 0/8 admitted. It removed **no previously accepted legitimate
signal** among the 128 independent-development and 24 paired signal cases.
Their existing misses remained misses; the AND rule cannot rescue them.
These observations helped select the proxy, so they are not new independent
wrong-key qualification or evidence that the rule never loses sensitivity.

### Fresh fixed-rule validation: the primary guard loses one signal

The fresh plan froze the primary `ln(100)` rule before its observations,
used disjoint six-root vectors, and retained both existing noise gates.
There were 16 high30/Each65 signal cases at 8 dB-Hz with 240 seconds requested
duration, plus eight 120-second wrong-key cases at each of 12 and 20 dB-Hz.
The results are deliberately kept separate from reused development:

| Fresh population | Base strict | Base relaxed | **Primary ln(100), strict / relaxed** | Diagnostic zero, strict / relaxed |
| --- | --- | --- | --- | --- |
| Signal, 8 dB-Hz, 240 s; correct / wrong / missed | 16 / 0 / 0 | 16 / 0 / 0 | **15 / 0 / 1 at both gates** | 16 / 0 / 0 at both gates |
| Wrong-key, 12 dB-Hz, 120 s; admitted / 8 | 3/8 | 6/8 | **0/8 at both gates** | 0/8 at both gates |
| Wrong-key, 20 dB-Hz, 120 s; admitted / 8 | 8/8 | 8/8 | **0/8 at both gates** | 0/8 at both gates |

The newly lost signal, index 250012/bit 0, had a base winning score of
33.38790 nats but `C=3.49946`: above zero and below `ln(100)`. **The primary
result is 15/16, not 16/16.** Switching to zero after seeing this case would
change the selected rule; its favorable diagnostic result is not promoted
to the primary conclusion. The guard demonstrates a rejection/sensitivity
tradeoff needing broader independent study.

The same pointwise two-sided CP95 assumptions stated for the main campaign
apply. Primary signal success was 7/8 in bit 0, interval
`[0.47349,0.99684]`, and 8/8 in bit 1, `[0.63058,1]`. The optional pooled
common-probability interval for 15/16 is `[0.69768,0.99842]`. Each fresh
wrong-key cell's 0/8 admission interval is `[0,0.36942]` under the extra
pooled assumption; its per-bit 0/4 intervals are `[0,0.60236]`. These wide,
pointwise intervals establish neither 90%/99% general reliability, rare
wrong-key rejection nor operational daily admission performance.

### Paired longer observations

Four high30/Each65, 8-dB-Hz source durations increased from 120 to 240 seconds
requested, or **123.2 to 246.4 seconds actual source elapsed time**. The base
receiver improved from **1/4 correct at both gates to 4/4**, with no wrong
bits. Its physical likelihood window and all 109 public arrival groups stayed
fixed. Measured postchannel signal-energy ratios were
`2.017006, 1.995348, 2.001460, 1.990277`, a range of **1.99028–2.01701**.
Energy was measured from the sampled channel, not assumed to double exactly.

These are genuinely longer sampled transmissions with paired seed roots,
not repeated short arrays. Equal roots alone do not prove identical indexed
noise or waveform prefixes, equal mean power, a minimum sufficient duration,
or a population reception probability. The separate 480-second stage was
conditional, not required to repeat a successful four-case diagnostic. The
fresh 16-case signal result above is a distinct population and must not be
pooled with these four development paths.

The accepted result is
`local-evidence/triad-acquisition-improvement/analysis/followups.json`, SHA-256
`849763dd13f77fac913c62741f441f767ac0433f74d5a4f8c9f029d978045a70`.
It binds analyzer SHA-256
`f9aa0ce7fbd64b8cad95d9d918a6223f9a152830eb0d44018d45924f8f13c77f`
and consistency-driver SHA-256
`8b6d245dfa36acf24c57fcf2afee61777a3c1faab226a9fc3e602701c92cd9b5`.
Earlier incomplete followup reports and the duplicate-inventory path
normalization repair remain diagnostic history, not completed campaign results.

## Exact finite Gaussian reference

The retired `triad_covariance_reference.py` and its focused tests provided a
deliberately bounded reference of at most 512 samples. The saved
experiment uses 256 samples, not a dense multi-day receiver.

For deterministic public tap waveform columns `a[b,k]`, independent
proper-complex tap processes with powers `p[k]`, and physical times `t[i]`,
the stipulated coefficient covariance is

\[
 C_{ij}=\exp[-\tfrac12(\pi B(t_i-t_j))^2],\qquad
 K_b=\sum_k p_k\,\operatorname{diag}(a_{b,k})C
                         \operatorname{diag}(a_{b,k})^H,
 \qquad R_b=\sigma^2 I+K_b.
\]

`B` is twice the RMS Gaussian Doppler width of each tap process; it is not
the separation between ray-center frequencies. Local time differences avoid
losing short lags at long absolute epochs.

For `y ~ CN(0,R_b)`, the proper-complex density gives, after dropping the
common `-N log(pi)` term,

\[
 \ell_b(y)=-\log\det R_b-y^H R_b^{-1}y.
\]

There is no extra real-Gaussian factor of one half. The implementation uses
Cholesky factors and triangular solves, not explicit inverses. It validates
Hermitian/positive-semidefinite signal covariance and adds no hidden diagonal
jitter.

For two **known deterministic means** in noise `CN(0,sigma^2 I)`, the
equal-prior optimum bit error is

\[
 P_e=Q\!\left(\frac{\|\mu_0-\mu_1\|}{\sqrt{2\sigma^2}}\right).
\]

For antipodal means `+x` and `-x`, this reduces to `Q(sqrt(2 E/N0))` under
the stated complex-noise convention. The accepted scalar check uses distance
2 and noise variance 0.5, giving `Q(2)=0.02275013`. Earlier reference receipts
with the incorrect known-channel normalization are superseded; use only the
two `scipy-final-*` receipts identified below.

Known-waveform **presence versus noise** is another distinct favorable
benchmark. After whitening, let `H0: y ~ CN(0,I)` and
`H1: y ~ CN(mu,I)`, with waveform, channel, phase and alignment all supplied.
For `rho=||mu||^2`, the log likelihood ratio is
`2 Re(mu^H y)-rho`. The exact Neyman–Pearson detection probability at a
single predetermined false-admission probability `alpha` is

\[
 P_D=Q\!\left(Q^{-1}(\alpha)-\sqrt{2\rho}\right).
\]

This is neither the binary bit-error formula above nor the unknown random
channel model below. A log-likelihood threshold `log(1/alpha)` instead gives
a conservative likelihood-ratio/Markov gate, not the exact threshold with
false-admission probability equal to `alpha`. Searching hypotheses, repeating
attempts and selecting durations require their declared overall admission
budget; a single known-waveform result cannot be called a daily receiver
limit.

### Cancellation-aware comparisons and low-power scaling

An equivalent way to evaluate a small likelihood difference avoids
subtracting two large common log likelihoods. Factor `R0=L0 L0^H`, form

\[
 E=L_0^{-1}(K_1-K_0)L_0^{-H}=U\operatorname{diag}(e_i)U^H,
 \qquad u=U^H L_0^{-1}y.
\]

Then

\[
 \ell_1-\ell_0=
 \sum_i\left[\frac{e_i}{1+e_i}|u_i|^2-\log1p(e_i)\right],\quad e_i>-1.
\]

`E` can be indefinite. Constructing the difference from signal covariances
avoids subtracting the identical noise diagonal; `log1p` and the ratio above
preserve small relative changes. This is a stated algebraic evaluation option,
not a claim that the current scoring helper implements this combined formula.
The helper already uses relative-covariance eigenvalues and small-argument
series for cancellation-sensitive KL and Bhattacharyya diagnostics.

For the zero-mean Gaussian model `R_b=I+c K_b` with fixed finite `K_b`, direct
expansion gives

\[
 D(P_1\|P_0)=\tfrac12c^2\|K_1-K_0\|_F^2+O(c^3).
\]

This quadratic low-power information is different from known-mean coherent
energy accumulation. Independent repeated windows add KL, but their physical
independence and fixed covariance must be justified before deriving a duration
law. If two bit patterns induce identical covariance after the allowed channel
nuisance transformations, this receiver has no bit information. For example,
a global sign change absorbed by an unknown zero-mean scalar Gaussian gain
cannot be recovered merely by longer integration.

## Accepted numerical screen

The historical reproducer reused a public sampled
two-tap waveform basis over sample indices `[1024,1280)` at 2048 samples/s:
timestamps `0.5` through `0.62451171875 s`, or **0.125 s of sample-slot
duration**. Each candidate/tap column is normalized to unit mean power, with
tap prior powers `(0.5,0.5)` and `B=30 Hz`. Although the basis export's channel
metadata mentions Each65, the reference prior is **Off-selective Gaussian**.
It does not propagate the physical finite Each65 modulation law or establish
its exact likelihood.

The exact 256-row covariance is compared with block-center coefficient models
using 8, 16 and 32 samples per block. These mean 3.90625, 7.8125 and 15.625 ms
per block. **The physical reference window stays 0.125 s in every model**;
changing block size does not buy extra transmitted energy. Window boundaries
declare independence; they are not evidence of independent adjacent physical
channel blocks.

The full screen used 2,048 draws per truth bit at six C/N0 values. The
independent validation used 4,096 draws per truth bit on a finer ten-point
grid. All models and arithmetic alternatives reused each exact-observation
draw within a screen. Selected FP64 validation error fractions are:

| C/N0 dB-Hz | Exact covariance | Block 8 | Block 16 | Block 32 | Mean known-realized-channel Q bound |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 20 | 0.11865 | 0.11914 | 0.12170 | 0.14270 | 0.003658 |
| 21 | 0.08618 | 0.08911 | 0.09326 | 0.11426 | 0.001625 |
| 25 | 0.01282 | 0.01331 | 0.01575 | 0.02405 | 0.00001868 |
| 26 | 0.006958 | 0.007568 | 0.009521 | 0.01489 | 0.000004450 |
| 28 | 0.001587 | 0.001587 | 0.001831 | 0.005127 | 0.0000001723 |

The gap between the last column and unknown-channel classification measures
the benefit of revealing the channel under this favorable prior; it is not
all avoidable decoder loss. Sampled block-8 classification tracks the exact
reference closely here. The first tested 99%-correct point fraction was
26 dB-Hz for exact/block8/block16 and 27 dB-Hz for block32. These finite-grid
fractions alone do not establish a 1-dB target, a physical Each65 penalty, an
admission probability, or a full-duration sensitivity boundary. The separately
frozen confirmation below resolves one aligned classification target.
Pointwise Wilson intervals in the receipts are numerical diagnostics, with
no simultaneous guarantee across settings or fixed-bit populations.

`fp32-bulk` lowers **only triangular whitening**. Covariance construction,
Cholesky, log determinants and quadratic/evidence reductions remain FP64.
Across these two saved screens it caused zero sampled bit-winner changes;
the maximum absolute log-likelihood difference was below `1.62e-5` nats.
This is partial precision coverage, not an all-FP32 receiver qualification,
GPU acceleration demonstration, or proof that every retained FP64 stage is
necessary. The final full and validation screens took approximately 24.17
and 44.19 seconds on their recorded environment; timing is workload-specific.

Authoritative receipts, retained in ignored local evidence:

| Receipt under `local-evidence/triad-acquisition-improvement/reference/model-screen/` | SHA-256 |
| --- | --- |
| `scipy-final-full.json` | `5e489e34ad435c76b647c096a4742a5cfcf2140c015f7fad59a3c58fc21e924f` |
| `scipy-final-validation.json` | `400dd24ea9f6c15d47f37bdbb1884dc3b6c3097b24ae59b9eae8bb7cd1badf45` |

Both bind reference helper SHA-256
`e6ca43a136d9781b6c0d9883e72d2069302bcf5ef285e7f8c70137b9fe9e31cb`
and reproducer SHA-256
`f6595131cc2320d94644894e4c5af00179704969c33de4c87c5fd837e864d9b6`.
Source, basis, manifest, seeds, power normalization, environment and arithmetic
contracts are recorded inside each receipt.

## Confirmed fixed 1-dB classification comparison

The accepted historical confirmation driver fixed the same 256 samples,
0.125-second window, public basis, unit-power tap
normalization, equal bit priors, `B=30 Hz` Gaussian channel and **Off-selective**
law. Synchronization, key, prior and noise variance are known. It compares the
exact-covariance classifier at **25.25 dB-Hz** with the block-16 classifier at
**26.25 dB-Hz**. This is a selected fixed 99%-correct classification comparison,
not a threshold search over new outcomes.

Each of the four predetermined model/truth cells used **1,048,576 trials**:

| Classifier | C/N0 dB-Hz | Truth bit | Errors / trials | One-sided CP direction | CP error-probability bound |
| --- | ---: | ---: | ---: | --- | ---: |
| Exact covariance | 25.25 | 0 | 11,759 / 1,048,576 | Lower | 0.01098501 |
| Exact covariance | 25.25 | 1 | 11,075 / 1,048,576 | Lower | 0.01033942 |
| Block 16 | 26.25 | 0 | 7,589 / 1,048,576 | Upper | 0.00742519 |
| Block 16 | 26.25 | 1 | 7,456 / 1,048,576 | Upper | 0.00729673 |

Each one-sided Clopper–Pearson bound uses `alpha=0.0125`. Bonferroni gives
at least **95% simultaneous coverage of these four bounds**, without treating
the two bit strata as one common Bernoulli population. Averaging the two
stratum bounds for equal bit priors gives

- exact-classifier mean error **at least 0.0106622150** at 25.25 dB-Hz;
- block-16 mean error **at most 0.0073609614** at 26.25 dB-Hz.

At that confidence level, the exact classifier has not reached 99% correct at the lower point,
while the block-16 classifier exceeds 99% correct with 1 dB more C/N0. Under
this fixed model, the optimal exact-classifier error cannot increase when
independent white noise is reduced: a noisier observation can be obtained
from a less noisy one by adding independent noise. This brackets the
99%-classification cost of the tested block-16 point within 1 dB of the exact
reference's required C/N0. It does not locate the minimum crossing precisely,
establish the same gap at other targets, compare against the known-realized
channel bound, or demonstrate a finite-acquisition/admission gap.

### Exact marginal quadratic-form simulation

The primary confirmation does not need to generate and whiten millions of
dense raw sample vectors. For a fixed truth `t`, let the **actual exact**
observation covariance be `S_t=L_t L_t^H`. If `R0,R1` are the two covariances
used by the classifier, its signed quadratic likelihood ratio satisfies

\[
 \Delta=c+y^H(R_0^{-1}-R_1^{-1})y,
 \quad c=\log\det R_0-\log\det R_1.
\]

Diagonalize the Hermitian matrix
`L_t^H (R0^-1-R1^-1) L_t`, retaining **all 256 signed eigenvalues** `lambda_j`.
Unitary invariance of proper-complex standard Gaussian noise gives the exact
marginal decision-statistic law

\[
 \Delta\overset{d}=c+\sum_{j=1}^{256}\lambda_j X_j,
 \qquad X_j\ \text{independent Exp}(1).
\]

This holds for the mismatched block-16 classifier as well: its scoring
covariances differ from the actual exact truth covariance used in the
diagonalization. The method costs cubic preparation per fixed model/truth
and `O(N * trials)` sampling/reduction afterward, compared with dense raw
generation/whitening's `O(N^2 * trials)`. It samples the exact statistic under
the stated finite Gaussian model; it is not an approximate channel waveform
simulation. A small paired raw/rotated preflight agreed within `3.81e-13`
nats with zero ranking changes. Raw observation Monte Carlo is therefore
unnecessary for this primary marginal classification confirmation, while
remaining useful for implementation and joint-observation diagnostics.

The frozen primary family uses **quadratic FP64 only**. Raw and quadratic
samplers have distinct random namespaces and are not individual-trial pairs;
raw CPU/GPU arithmetic runs are diagnostics, not a second family from which
to select the more favorable confidence result. This shortcut does not retain
joint dependence across an acquisition bank, simulate physical Each65
modulation, or validate signal-admission/null/wrong-key behavior.

The accepted run completed in **7.9847 seconds on one CPU thread**, including
preparation, preflights, exponential RNG and counting. The driver name does
not imply that this receipt used a GPU. This is a measured efficient
confirmatory calculation, not a measured GPU speedup or multi-day receiver
throughput result.

Receipt:
`local-evidence/triad-acquisition-improvement/reference/confirmation/quadratic-fp64-primary.json`,
SHA-256 `a7f35745f65669628d9572cf474da6f48ca085f59c102ce9c40b88f79aea7279`.
It binds driver SHA-256
`ceb56e4430e22c76d672e9eda87c72c359df96868263da98d523e8590dc2e32a`
and the same helper/basis/manifest hashes as the initial screens. The earlier
screens remain useful diagnostics; this receipt is the fixed-count primary
confirmation for the 1-dB statement.

## Marginal Chernoff diagnostic did not rescue the weak miss

The accepted historical Chernoff diagnostic kept
the weak high30/Each65, 8-dB-Hz capture's projection, strict noise UCB,
Gaussian factors and FP32 score unchanged. It examined post-hoc group 59,
bit 0. Under the stated proper-complex Gaussian null, its positive quadratic
has a computable moment-generating function. Optimizing the Chernoff parameter
improved the log tail bound from `-18.44770` to `-18.84347`, only **0.396 nat**.
After charging all 109 groups, both bits and the declared operational family,
the required log bounds were `-27.41241` strict and `-22.11409` relaxed.
**Neither gate passed.**

This was a marginal-tail calculation for one selected group, not exhaustive
acquisition, an anytime rule or an e-value to mix without further justification.
It does not assume that the physical signal follows the Gaussian alternative,
but it retains stationary white-noise and residual-calibration assumptions.
The receipt is
`local-evidence/triad-acquisition-improvement/gaussian-confirmation/chernoff/group59-final.json`,
SHA-256 `4a7cba88ed16eefb8e92dfdbcc7f2547fe9d1be22464b1eadb737ecba882a047`.
The small improvement and preserved miss reject this particular cheap rescue;
they do not prove that every alternative detector is ineffective.

## Measured precision and CPU/GPU costs

Two retained actual-noisy high30/Each65, 8-dB-Hz captures were replayed through
the same grouped receiver on CPU and an **RTX PRO 6000 Blackwell Server Edition**
GPU, in FP64 and the explicit FP32 bulk path. All group orderings, best
groups, bit winners and strict/relaxed admission decisions matched CPU FP64
on these two captures. The maximum absolute group-score difference was
`7.67e-6` nats. This is a small paired arithmetic comparison, not precision
qualification across paths or thresholds.

Covariance/rank preparation, residual calibration and evidence finishing
remain CPU FP64. This grouped-receiver arithmetic contract is distinct from
the reference helper's FP32 triangular-whitening experiment. Descriptive
means over the two captures were:

| Grouped actual receiver | Receiver seconds | Bounded end-to-end seconds |
| --- | ---: | ---: |
| CPU FP64 | 83.26 | 85.39 |
| CPU FP32 bulk | 83.65 | 85.89 |
| CUDA FP64 bulk | 86.92 | 89.49 |
| CUDA FP32 bulk | 88.31 | 90.99 |

About **90% of CPU receiver time was FP64 factor preparation**. CUDA was
about 5–7% slower end to end in these individual paired runs; moving the
small bulk score stage did not accelerate the whole grouped receiver. These
are single serial timings without timing confidence intervals. FP32/CUDA
support and a favorable bulk dtype do not imply useful full-pipeline speedup.

A **different dense raw-Gaussian generation/whitening workload** did show
actual CUDA acceleration: complete bounded CPU/CUDA ratios were **2.82× for
FP32 and 3.10× for FP64**, including RNG, preparation, transfers and finishing.
It used 131,072 raw trials per model/truth point on the same 256-sample
reference. Aggregate error counts matched across those arithmetic replays;
aggregate agreement is not proof of every sample's decision parity. This
benchmark is not the grouped receiver and is not the primary quadratic
classification confirmation. Do not transfer its acceleration to acquisition.

Accepted result/verification files under
`local-evidence/triad-acquisition-improvement/` are:

| File | SHA-256 |
| --- | --- |
| `precision-benchmark-summary.json` | `a98b8779fec2dd1f5918827af31d9d8ede8fa1fb9c725a9d57f1b323754e2196` |
| `precision-benchmark-verification.json` | `e586d5bd11c644736fbb564f5f3d0b31994af992cdbdb9dbd28c67384f71dbe4` |
| `raw-gaussian-benchmark-summary.json` | `22662373a069136ef2f79a4c56e5358a18a3feac73e007454918cb5fa993bc45` |

### Post-campaign exact prior cache

This accepted optimization was made **after** the recorded campaigns.
Their Gaussian-window helper remains
`dd5d8083999a97f66f74673264f80117b06557c45b3f4646a88e9668a29d03ee`.
The new runtime helper is
`e5953a375d54ff8c4aa050801b90cb63b06b233549a382b41721738894b2cf6a`.
Historical campaign provenance, benchmark results and the archived old source
package are unchanged; exact historical execution uses that retained source,
not a relabeled current helper.

The change is a **constructor-local one-entry cache**, keyed by the exact
FP64 bytes of relative block-center times. Width, tap powers and tolerance
are fixed within that constructor. Only an immediately preceding identical
calendar reuses its prior square root; an irregular or partial calendar
recomputes it. The cache adds bounded key memory and cannot accumulate a
dictionary of every window. There is no prior approximation, mode truncation,
rank-policy change, lower-precision substitution or observation reuse.

On the retained weak high30/Each65, 8-dB-Hz capture, arrival groups `[56,60)`
were prepared over the complete **253,490-sample capture**,
`123.7744140625 s` at 2048 samples/s, including guards/tails. The source
elapsed time is 123.2 s; it is not the complete capture span. The physical
likelihood window remained 0.25 s. Local before/after results were:

| Measurement | Old helper | Exact cache |
| --- | ---: | ---: |
| Prior eigensolves | 496 | 2 |
| Other eigensolves | 3,969 | 3,969 |
| Median preparation, five alternating pairs | 3.3176 s | 3.1270 s |
| Factor arrays | Reference | Bitwise identical |
| FP32 and FP64 scores | Reference | Bitwise identical |

The measured preparation ratio is **1.06094×**. It is neither full-receiver
acceleration nor a sensitivity gain. Most information/Gram eigensolves
remain, so reducing the prior solve count does not imply a proportional
end-to-end gain.

The first benchmark's arbitrary `1e-8` absolute comparison with an archived
remote score failed: **both** local old and new helpers differed from that
archive by the same `1.23700854e-8` nats. The failed assertion, initial driver
and log were preserved. The repaired comparison tests exact same-environment
old/new factors and scores, and separately checks archived ordering/admission
invariants. No same-environment parity tolerance was relaxed to accept the
cache.

The accepted receipt is
`local-evidence/triad-acquisition-improvement/prior-cache/comparison.json`,
SHA-256 `2c6dd686cea31d3a31e3a21a90d01bf23bb6a49743303668aa4bd0628f7e9577`;
`scope-clarification.json` records the complete capture-span distinction.
After the cache change, **33 affected checks passed**: nine Gaussian-window
tests, ten grouped-acquisition tests and 14 conditional-sweep tests. These
post-change checks are separate from the historical 39 checks below; the
old scientific campaigns were not repeated merely to test exact cache reuse.

## Source provenance and retained evidence

The collector independently hashed **1,204 numerical array files across all
172 workspaces on the execution host**, alongside complete manifests,
commands, six seed roots and all 200 JSON/NPZ score outputs. The accepted main
audit then checked the retained reports, NPZ scores, manifests, inventories
and execution receipts. Its completion does not mean every large array was
downloaded and rehashed locally.

Retained packages are recorded under
`local-evidence/triad-acquisition-improvement/retained/`:

| Package | Bytes / members | SHA-256 |
| --- | ---: | --- |
| `compact-evidence.tar.gz` | 8,300,092 / 1,768 | `2a77c10d63136692a578e13fd3d2d4acdab030d854db63c9da1ca5ff24390a2c` |
| `selected-raw-workspaces.tar.gz` | 1,833,576,117 / 32 | `abdc004e7b52251a39e3cebd29b5e1d5ae97243557c19354687fed3b026267dc` |

The compact package's members and all **32 members of the four preselected
full raw workspaces** were hash-verified locally. The four retained raw
workspaces are paired high30/Each65 at 8 dB-Hz/index 120002 and 20
dB-Hz/index 120003, independent high30/12 dB-Hz/index 210000, and independent
high10/8 dB-Hz/index 200001. The **other 168 raw workspaces are not retained
in the original main package**. Their source/seed regeneration recipes and original remote hash
receipts remain; that is not a claim of local raw-byte verification or
cross-platform byte-identical regeneration. `collection-status.json`, member
manifests, `compact-verification.json` and `raw-verification.json` preserve
the precise retention/verification scope. Here a full raw workspace means all
exported grouped-statistic arrays; it does not mean retained baseband I/Q.

The followup compact package separately contains **1,118 members**, 4,446,178
bytes, SHA-256
`33174ed284bb69374b879e21a91df9ed5f13bce6ee8fcc18a057dce8e8d6288c`;
all compact members were verified locally. Its collection record is
`local-evidence/triad-acquisition-improvement/followup-retained/collection-status.json`.
Nine additional full grouped workspaces were selected for a 72-member raw
archive: four paired 240-second captures, four fresh captures selected
before their outcomes, and one original wrong-key capture selected **after
its failure** for diagnosis. They are not an unbiased new raw sample.
That archive is 6,877,861,751 bytes, SHA-256
`d5f1333ff45cb7928e89ce24394940d081b97886f2bb54a2eba92bf5968d915e`.
The complete archive hash and **all 72 members were verified locally**;
`followup-retained/raw-verification.json` records that check. The followup
statistical audit separately rehashed score/window NPZs and JSON, using
collector input-array receipts for the cases not retained in full. The nine
selected raw workspaces do not imply local raw-byte verification of every
capture in the campaign.

A separate full workspace for the newly lost fresh signal, index 250012,
was selected **after** that validation failure for debugging without another
export. Its eight-member diagnostic archive is 916,976,398 bytes, SHA-256
`01f72045e3b05c28d543905d07ee65a90a96d2d749c118397f4172527349e3f1`.
It has now been downloaded and all members verified locally; the receipt is
`local-evidence/triad-acquisition-improvement/followup-retained/guard-loss/verification.json`.
This additional selection does not change the original nine-workspace
selection and is not an independent new signal trial.

After local archive and member verification, the temporary RunPod and its
ephemeral storage were deleted. The provider reported no ongoing spend;
no persistent cloud volume was retained.

The built source archive was 10,431,327 bytes with 1,770 members, SHA-256
`5b52fae92538df17edb796e947241c208f25d55d403b4a41ff7cd02269b48a1d`.
Its original provenance mislabeled the base revision as
`acba36fce865876adc8772bba7cbb5c0c8ed4208`. The preserved v2 erratum corrects
the **base HEAD** to `ea05d3a8346be7b06a2a53c0e4050e44a963b34b` and explicitly
states that the archive includes **uncommitted acquisition changes**. The
archive SHA and full member inventory identify the built source; base HEAD
alone does not. The original bad label and earlier erratum remain preserved.
The accepted v2 erratum is
`local-evidence/triad-acquisition-improvement/provenance/source-provenance-erratum-v2.json`,
SHA-256 `f69c40b0ed8ff60764d7b33efbf5bff4ab8bfa7b1d5b430c588550a4d2cc8aad`.
This is a metadata correction, not a replacement build or reset of the
experimental history.

## Computational approach and remaining campaign evidence

Use a cheap `configure` preflight before exporting a new rate/bank. Reuse
matching grouped actual observations for model/window/precision comparisons.
Group tiling bounds resident design and factor memory; cached public factors
need exact waveform, epoch, timing, tap, calibration and arithmetic identities.
Preparation, calibration and complete receiver time count toward the budget,
not just bulk scoring. The pilot's FP64 factor preparation was about 30.9 s
of 43.0 s total receiver time, so moving a small scoring kernel alone would
not remove its main cost.

The dense reference has cubic factorization cost and quadratic storage in its
sample extent. Block/window approximations trade retained channel detail for
smaller factors; measure both lost information and complete cost. State-space
or low-rank GP approximations are plausible next alternatives, rather than a
prescribed architecture. The author-hosted paper
[Kalman filtering and smoothing for temporal Gaussian-process regression](https://users.aalto.fi/~ssarkka/pub/gp-ts-kfrts.pdf)
describes exact Matérn and approximate squared-exponential state-space models.
It motivates those experiments; it does not establish accuracy for this waveform,
finite modulation law or acquisition bank.

The recorded focused software validation passed **39 checks**: the 14-test
isolated triad group, 15 covariance-reference tests and 10 grouped-acquisition
tests. Main scientific execution completed 172 exports/200 full scoring jobs;
followup execution added 36 exports/36 full scores and 200 consistency
postprocessors. Those counts describe different evidence tasks, not one
pooled independent reception population.

For subsequent code changes, start with inexpensive deterministic checks and
follow the [current abbreviated testing policy](../../../triad-performance.md).
The [historical findings record](reproduction/README.md) summarizes relevant
results and limitations; it is not a larger campaign recipe. The historical
documentation checks verified receipt/count/hash literals, local link targets and
whitespace; they are separate from the recorded execution above.
