# Receiver probability coverage and EME qualification

The probability estimator is separate from receiver admission. Changes described
here affect planning statistics and their availability labels; they do not change
transmitted bits, searches, thresholds, pending progress or physical completion.
An available model is conditional on its noise, waveform and timing assumptions.
It is not a field measurement or a certified lifetime false-alarm probability.

## Coverage inventory

| Selected receiver/statistic | Current probability treatment | Evidence and maintenance boundary |
| --- | --- | --- |
| Existing short coherent pattern / Tone | Analytical engineering approximation | `test_simulation_estimate`: framing, raw-sample geometry, search coverage; one-chip patterns are excluded because both rows span the same unknown-gain subspace. Tone is separate. |
| Existing complex coherent + four quarters | Joint finite-candidate Monte Carlo; dense rows use an orthogonal ensemble approximation | `test_receiver_probability`, `receiver_statistic_controls`; existing sampled comparisons are bounded engineering checks, not precision calibration of every acquisition bank. |
| Existing local differential, aligned circular windows | Joint coherent/quarter/local model with finite source/template statistics | `test_differential_probability`, `test_differential_receiver_probability`, `differential_model_limits`; window/integration caps remain explicit. Hardware power is normalized independently from nominal simulation noise. |
| Compact local differential with partial windows / real covariance | Joint real atoms; physical tail retained | `real_atom_reduction`, `partial_compact_probability`; first-bit reference survives when multi-bit timing ownership is unsupported. |
| Interleave / smooth FHSS actual-source statistics | Conditional reference; complete adaptive acquisition is unqualified | `versioned_outer_work_and_probability_scope`, `fhss_probability_cache_identity`; source seed, envelope, epoch and source-bit identity must not be borrowed. |
| Legacy outer DSSS | Conditional matched-template AWGN reference | Outer-code presence rejection and complete acquisition sensitivity remain unqualified. |
| Nonsmooth FHSS power gaps | Conditional reference | Matched-gap rows use hop-dependent energy; ungated rows omit power-gap and transition loss. Neither qualifies full-bank sensitivity. |
| UTC timing / bounded arrival models | Timing/work scope and conditional references as reported by the selected model | `utc_bank_estimate`, `clock_dsss_reference`; device timing and peer steering are not measured by a probability calculation. |
| Frequency-axis complex FFT | Public EME actual-source first-bit AWGN model qualified at the seven conditions below; other geometries remain unavailable or conditional references | `test_frequency_axis_probability`; FFT equivalence and strong reception alone do not qualify sensitivity. |
| Amplitude-only | No full-bank RX probability | `receiver_strategy_model`; coherent probabilities do not describe this statistic. |
| Scheduled packed / split mapped complex | No full-bank RX probability | `scheduled_packed_complex_model`, `scheduled_split_complex_model`; contiguous-symbol probabilities do not describe calendar acquisition. |
| Fast / Legacy paths | No regular Robust-receiver probability borrowed | Their receiver tests remain separate. |

A receiver change must review this inventory against the **selected backend**,
not just a requested Automatic policy. Review source power, template overlap,
projection noise covariance, coherent/quarter/local detector choice, original
trial charges, peak arbitration, timing ownership, continuation and physical
absence. A kernel agreement test does not establish an acquisition probability.
Keep unsupported configurations explicit and add a negative availability case
when a backend or waveform is outside the demonstrated scope.

## Maintenance corrections

The audit found real estimator maintenance gaps rather than evidence that every
receiver model was obsolete. In hardware planning, the entered radio power is
actual transmitted mean power. Three unshaped finite-source paths incorrectly
applied source amplitude again: coherent quarters, circular local-differential
projection and partial-window real atoms. Hardware normalization now removes
that double counting while preserving projection loss, finite template mismatch
and tail energy. Sampled simulation retains nominal-power-referenced noise.
Independent production-source fixtures exercise each path and verify their
physical-energy relationship directly.

One-chip pattern alternatives span the same unknown-complex-gain subspace.
Those configurations now keep work/timing diagnostics but withhold unsupported
reception probabilities; Tone and the distinguishable two-chip control retain
their existing treatment. No receiver admission or waveform code changed.

Existing-only EME also had a stale initial-batch trial charge, although its full
payload peak arrives in the second batch. The exact EME family now uses that
second-batch charge in its original probability engine. At 18 dB with 4096 draws,
the 128/256-second predictions change from 0.844482421875/0.8076171875 to
0.5816650390625/0.48779296875. These remain conditional first-bit references:
shaped-source and joint timing/peak selection are approximate. Explicitly
selecting Existing-only does not inherit the frequency-axis qualification.
Other long-FFT geometries retain their earlier engineering approximation.

## Exact EME case

The built-in unkeyed example resolves to 6,000 samples/s, a 1,500 Hz audio
carrier, 1 Hz configured rate, 12,000 samples/chip and a 128-second symbol
(64 chips). The independent GPSDO-OCXO baseband and 5.8 GHz shift policy at
margin three admits 1,783 frequency/clock pairs. A 256-second companion has
128 chips and 3,565 pairs. Longer capped banks are outside this work.

Both waveforms have 96,000 samples (16 seconds) of leading pulse support.
The independent source support endpoint includes trailing pulse padding; it
must not be mistaken for the nominal payload-grid endpoint when checking
physical absence.

The earlier probability engine charged the initial one-second acquisition
batch. The true payload peak arrives in the second logical batch. With
750-sample projected bins, the correct charges are:

| Symbol | Initial starts | Next hop | Frequency/clock pairs | Second-batch admission threshold |
| --- | ---: | ---: | ---: | ---: |
| 128 seconds | 8 | 512 | 1,783 | 58.6848157444 |
| 256 seconds | 8 | 1,024 | 3,565 | 62.1342645447 |

These use the unchanged receiver formula
`-log(1e-10) + 2*log(charged_trials+1) + log(2*pairs)`.
Changing the estimator's charge does not change the actual receiver threshold.

The model streams the shaped, radially limited analytic transmitter waveform
into source prefixes, including the conjugate carrier image. Each Monte Carlo
draw supplies one shared complex Gaussian observation field. Both candidate bits,
all local timing starts and nearby original frequency/clock hypotheses use that
same field and their own shifted-window energy. The strongest cell wins in the
production job/start order, followed by the unchanged evidence threshold and
same-cell alternative-bit margin. Coherent and four-quarter scores call the
production scoring functions, including strongest-quarter removal.

The full original bank is charged at admission; a bounded local neighborhood
models signal competition. Nested timing/carrier neighborhoods and finer source
integration must converge before the model is used. This does not simulate every
remote noise-only cell or establish the nominal rare false-alarm rate.

The actual preset includes 0.58 Hz frequency offset and 0.0001 ppm clock error.
The source prefixes retain their combined carrier rotation and boxcar loss;
the local hypotheses come from the original full bank around that frequency.
The clock's envelope displacement is below 0.00018 input sample over this source
extent. Independent production-channel projection checks bound that approximation.
Source integration uses 75 subdivisions per 750-sample bin, checked against 125.

Existing Interleave statistics keep their 524,288-source-sample cap. Only this
exact public EME family may integrate up to 1,536,000 useful source samples.
Private/legacy keyed rows, changed receive oscillator policies or bank
multipliers, source phase, other offsets, multiple profiles/keys, active UTC,
DSSS and hopping are outside this initial scope. Centered zero-offset calculations
remain conditional references, separate from the actual-preset qualification.

Only first-bit reception is qualified; a successful held-out
capture must also complete the exact bit after the independent physical-end
bound. A multi-bit product of independent symbol probabilities does not model retained timing ownership,
competing tracks or cumulative carrier history. Whole-draft confidence remains
unavailable for those drafts, even when the first-bit value is available.

## Reproduction and qualification method

Build only the affected targets through the existing incremental `build/dev`
profile. The local study uses Linux, GNU C++ 14.2.0, Release, default search
arithmetic and one receiver worker per capture; the orchestrator can run eight
independent captures concurrently. Explicit arithmetic alternatives do not
inherit this sensitivity qualification. Cross-platform and SDK qualification
remain deferred.
`test_frequency_axis_probability --strong-controls` compares both FFT backends
on strong one-bit production-channel captures, pending events and independently
derived physical-end bounds. The default also includes a strict three-bit
control that currently fails, as recorded below. Statistical batches are explicit:

```sh
build/dev/test_frequency_axis_probability --batch 128 18 10000 256 0 1 1
# seconds, actual Es/N0, first seed, count, compare backends, wire bits, hardware power
build/dev/test_frequency_axis_probability --noise 128 2000009
```

The production sampled channel supplies independent startup, carrier phase,
preset frequency/clock offsets, phase diffusion and AWGN streams. Receiver
search receives the configured full bank and normal start uncertainty, never the seeded true arrival. Source bits
come from a separate pseudorandom draw. Inserted delays span the positive
six-second configured window; production startup adds approximately 0.05–0.30
seconds. Accuracy is an ensemble claim, not a pointwise guarantee at every delay.

Hardware-mode captures independently measure useful-symbol analytic energy
`E=sum(abs(z)^2)`, real energy and carrier image `C=sum(z^2)`. The phase-averaged
mean-power ratio is `q=E/(2*N*nominal_signal_power)`. The channel noise is set to
`actual_EsN0 - 10*log10(N/2) - 10*log10(q)`. Channel interpolation and diffusion
remain impairments; they are not normalized away. Source fixed-phase/ensemble
power and `abs(C)/E` are logged, rather than assuming they coincide. This is an
AWGN surrogate for a measured-power link budget, not physical radio qualification.

Discovery seeds and held-out seeds must be disjoint. The driver freezes the
executable SHA256, selected project-source hashes, seven cells, seed inventory
and stopping rule before any held-out capture. The executable identity covers
the built execution; the selected-source inventory is not a complete dependency
manifest. Held-out seeds start at 10000 with a 100000 stride per cell.

The seven fixed cells are 128 seconds at the built-in actual Es/N0
(1.0926996097583093 dB), 128 seconds at 17/18/19 dB, and 256 seconds at 17/18/19 dB.
Each cell has predeclared cumulative stages 64, 128, 256, 512, 1024 and stops only
when its complete agreement bound passes. The conservative confidence allocation
is alpha 0.05/42 per bound: 35 exact observed Clopper–Pearson intervals plus seven
reserved model allowances. The displayed model probabilities use 4096 fixed draws.
A deterministic conservative Monte Carlo allowance is
`sqrt(log(2/alpha)/(2*4096))`; the held-out observed intervals establish agreement
against the **fixed displayed predictions**. Model-development draws are not
claimed as fresh independent statistical validation of an ideal population model.

For each cell require the complete difference interval
`[observed_low-model_high, observed_high-model_low]` to fit inside
`[-0.15,+0.15]`. Mere interval overlap is insufficient. The predetermined
stages and confidence allocation protect the simultaneous 95% agreement claim
when stopping early. A process failure, changed identity, missing capture,
premature completion or unfinished stage is never a statistical pass. Logs and
terminal failures are retained; interrupts join only the driver's own children.

Capture scheduling may split a stage into bounded parallel chunks. Statistical
looks always wait for the exact original seed prefix at the declared endpoint;
completion speed never selects the samples. Completed stages can be reused only
with the same executable/model identity, complete seed inventory, recorded log
hashes and reproduced assessments. Failed or unvalidated partial logs are not
accepted as completed evidence. A scheduling amendment preserves the original
run and its incomplete status separately, waits for all its writers to exit and
reuses an immutable snapshot of already validated stages. Receiver failures
remain fatal; they are never converted into statistical misses or new seeds.

This is a coarse **absolute** accuracy target at the tested conditions and
arrival/bit/noise ensemble. It establishes neither uniform accuracy between
anchors nor physical-radio performance, rare-event reliability, general phase
noise models or multi-bit continuation. At the nominal low-energy point, zero
observed successes does not establish a zero reception probability.

```sh
python3 -B tools/qualify-eme-probability.py --self-test
python3 -B tools/qualify-eme-probability.py --check-plan
python3 -B tools/qualify-eme-probability.py --workers 4 --output /claimed/empty/output
```

Methods: [NIST exact binomial intervals](https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm),
[NIST Bonferroni procedure](https://itl.nist.gov/div898/handbook/prc/section4/prc473.htm),
[Hoeffding's bounded-draw inequality](https://www.cs.rpi.edu/academics/courses/spring06/random/hoefding.pdf).

## Current evidence

All seven predeclared cells passed, using 2,112 held-out captures. The complete
difference intervals below satisfy the simultaneous 95% agreement target of
15 percentage points. Probabilities are fractions, not percentages.

| Symbol seconds | Actual Es/N0 dB | Fixed model probability | Correct complete captures | Observed minus model bound |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 1.0926996097583093 | 0 | 0/64 | [-0.030110, 0.109561] |
| 128 | 17 | 0.15234375 | 42/256 | [-0.084980, 0.128067] |
| 128 | 18 | 0.567626953125 | 278/512 | [-0.127311, 0.076748] |
| 128 | 19 | 0.926025390625 | 63/64 | [-0.099255, 0.104075] |
| 256 | 17 | 0.103271484375 | 4/64 | [-0.127442, 0.147981] |
| 256 | 18 | 0.455322265625 | 500/1024 | [-0.048038, 0.114126] |
| 256 | 19 | 0.885009765625 | 116/128 | [-0.119867, 0.114520] |

Displayed bounds are rounded outward. The 256-second 18 dB cell required its
predeclared 1024 stage; its 512-stage upper bound was 0.1546596921 and did not
pass. No tolerance or seed rule changed. An independent 80-digit Decimal
binomial recurrence checked the interval roots separately from the driver's
log-sum implementation.

Only these exact hardware-planning signal levels, preset impairments, default
TX/RX arithmetic and 4096 model draws promote a **single raw bit** from reference
to confidence. Between anchors, other modes and other impairments remain
references. Multiple raw bits and coded drafts retain a conditional first-bit
reference but have no whole-draft probability. GUI cards and mixed
qualified/reference curves retain these limits.

Focused maintenance, production score algebra, source projection, six nested
neighborhood convergence cells, unsupported-scope guards, short-wire/transfer
compatibility, selected GUI/reference/cancellation checks and 128/256-second
noise-only controls pass. Strong one-bit controls verify both backends' actual
second-batch trial charges, identical accepted events and identical publication
polls. The final seven displayed predictions exactly match the frozen study.
The affected application targets build incrementally; this is local evidence,
not broad platform or release qualification.

The independent sampled-mode 30 dB seed 3 diagnostics distinguish two findings:

- One bit is correctly received and completes at sample 1,540,096. The accepted
  event ends at 769,500; its complete failed-symbol absence bound is 1,537,499,
  which passes. The earlier 1,637,290.2949 comparison used the transmitter's
  nominal grid. Public FFT acquisition without a qualified window may accept
  an earlier grid, so that stricter timing-oracle mismatch alone does not
  establish a physical-end contract defect.
- The three-bit control still **fails content recovery**, receiving only two
  bits. Completion at 2,310,144 passes its accepted-grid bound of 2,305,499.
  Both backends agree on event coordinates and publication polls. The missing
  third bit remains unresolved; continuation remains unqualified.

`--diagnose-end 128 30 3 1` and `--diagnose-end 128 30 3 3` report those separate
coordinates. A diagnostic exit zero is not a content-recovery pass. The default
harness retains its strict source-grid and exact three-bit assertions and
remains failed. Neither diagnostic enters the seven-cell hardware study, whose
captures all passed the original stricter timing check. Receiver source is
unchanged by this estimator work.

Broad regression, unrelated calibration, sanitizer, native GUI, SDK and packaging
checks remain deferred by the user's batch-validation instruction. They are not
passes. Earlier unrelated ALSA clipping and native workflow failures in
[the validation record](validation.md) are not resolved by this batch.
