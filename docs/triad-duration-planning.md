# Provisional weak-receiver duration planning

[`tools/triad_weak_plan.py`](../tools/triad_weak_plan.py) is the recommended
automatic entry point for choosing a weak-receiver DATA duration and, optionally,
exporting one native bit. It asks the native exporter for shaping geometry,
passes the selected duration back to that same source, checks the resulting
tile calendar, and records a JSON plan beside the export. It does not run the
receiver. Its duration is a model forecast, not a sensitivity or reliability
qualification.

```sh
python tools/triad_weak_plan.py --target-cn0 8 \
  --output local-evidence/weak-one --report local-evidence/weak-one-plan.json \
  -- --case hf --sample-rate 6000
```

Supply the nominal active DATA C/N₀ with `--target-cn0` for automatic duration
selection. The native exporter's `--cn0` controls injected simulation noise;
it is not the planner's target. When planning from an explicit target, set
native `--cn0` separately if a matched simulated capture is wanted. If
`--target-cn0` is omitted, provide `--seconds` for a manual duration; its
forecast target then comes from native `physics.data_cn0_db_hz`, which can
reflect a native `--cn0` override. The default request is 99% correct reception,
`alpha_per_day=1e-4`, one bit, one duration alternative, no outage, and full
retained energy. These are planner inputs, not claims about achieved reception
or a qualified false-admission rate.

The wrapper freezes a plan-only probe, evaluates legal native tiles, then
requests a second plan-only probe at the chosen duration and checks tile count,
active energy duration, and elapsed bit calendar against native shaping. Export
is optional: omit `--output` to write only the report. The forecast horizon
defaults to 10 Julian years and can be set with `--max-active-days` or
`--max-active-seconds`. When a duration is selected, the native exporter now
checks its integer geometry with `--plan-only`, regardless of duration. That
check uses bounded work independent of observation length: it reports tile,
dwell, coefficient, source-sample and reusable-tile-workspace counts without
generating waveform samples or templates.

Forecast horizon, checked geometry and sampled execution are separate
availability results. Native geometry can reject an unrepresentable count; the
plan still reports the model forecast and records that geometry verification
failed. `availability.duration_forecast_available` means a candidate duration
has a forecast; `availability.modeled_detection_allocation_met` separately
states whether it meets the surrogate detection criterion. A manually requested
duration can have a valid forecast while failing that criterion. A verified long plan has
`availability.native_geometry_verified=true`, while
`availability.sampled_export_preflight_available=false`. Sampled exports and
power-only calibration remain limited to 3,600 requested active seconds, plus
their independent sample, byte and calibration limits. If sampled export was
requested but is unsupported, the wrapper exits nonzero and retains the plan.
Geometry verification does not establish that the current receiver can search
or decode a multi-day waveform. The reported source workspace is the native
reusable-tile bound; it is not total process RSS or an estimate of the complete
sampled artifact size.

Use `--seconds` for a manual active-duration override. It is rounded up to the
native tile lattice and still receives a forecast; the report says
`manual_duration_override` and records whether it meets the modeled detection
allocation. This option does not assert that the selected tile count is enough.
Use `--days` for the same manual forecast in days. For example, request a
38-day candidate forecast without generating a
waveform:

```sh
python tools/triad_weak_plan.py --target-cn0 -60 --days 38 \
  --report local-evidence/weak-38d.json -- \
  --case hf --sample-rate 6000 --rate 100 --added-symbols 1 \
  --base-oscillator gpsdo-ocxo --shift-oscillator gpsdo-ocxo \
  --rf-hz 500000 --timing-policy bounded \
  --spread-preset none --selective-perturbation off
```

For automatic selection up to ten years, replace `--days 38` with
`--max-active-days 3652.5` and omit `--days`. `--target-cn0` remains the nominal
active DATA C/N₀ used for selection. The configuration above is illustrative;
the forecast assumes its declared channel and oscillator law persists over the
entire duration.

The older native manual-duration workflow remains useful for historical studies;
use the wrapper when an automatic, reportable plan is wanted.

## What the forecast counts

The planner retains the full public candidate grid and its original prior mass,
including all 13 epochs, both bit hypotheses, and the fixed three-model
coherence prior. Helper search-space narrowing does not reduce the charged
candidate count. The Gaussian-null
operational threshold comes from the embedded `triad_operational_admission.py`
policy: declared alpha per day, minimum attempt spacing, and duration
alternatives determine the attempt bound. The planner budgets half of the
requested per-message miss probability to true-candidate detection and reserves
the other half for acquisition, Helper availability, and wrong-bit errors:

`per-bit detection miss budget = (1 - requested correct probability) / (2 × message bits)`.

That remaining budget is reserved, not demonstrated. The detection calculation
uses the true atom with its original full-grid weight and the operational
threshold plus full-grid/model-prior penalty. Duration selection uses a
geometric bracket, integer refinement, and a previous-legal-neighbor check. It
evaluates a bounded set of tiles; this search procedure is a practical selector,
not a universal monotonicity theorem.

Let `rho` be the nominal active DATA C/N₀ in linear Hz, `Tactive` the total
transmitting time, `q` the declared outage fraction, and `eta` the declared
retained-energy fraction. The modeled received energy-to-noise ratio is

\[
E = \rho\,T_{\rm active}(1-q)\eta.
\]

For random Gaussian channel evolution, the planner summarizes the finite-span
ray covariance by an effective mode count. With `Rays` independently resolved
rays, span `Tspan`, Doppler width `B=2 sigma`, and relative phase diffusion
`sigma_phi` radians/√s, its correlation and mode summary are

\[
r(u)=\exp\!\left(-\frac{\pi^2B^2u^2}{2}-\frac{\sigma_\phi^2|u|}{2}\right),\qquad
M=\frac{\mathrm{Rays}\,T_{\rm span}^2}
 {2\int_0^{T_{\rm span}}(T_{\rm span}-u)|r(u)|^2\,du}.
\]

The H₁ true-atom log-likelihood ratio is approximated by

\[
L=\lambda\,\Gamma(M,1)-M\log(1+\lambda),\qquad \lambda=E/M,
\]

where `Gamma(M,1)` has shape `M` and unit scale. The threshold is the
operational log-e threshold plus `log(2 × full-grid-count × model-count)`;
the planner uses a lower-tail Chernoff bound for this equal-mode surrogate.
Fractional `M` is a moment summary, not the actual covariance eigenvalue
spectrum, and the bound applies to the surrogate only.

When the modeled modes grow with span (`M` proportional to `B Tspan`), the
low-per-mode-SNR energy requirement gives the rough scaling
`Tactive ~ B/rho²`; with a fixed coherent mode count it is roughly
`Tactive ~ 1/rho`. These are asymptotic derivations under this model and fixed
threshold, not literature performance claims or receiver measurements.

For the legacy fixed-amplitude channel, the planner instead uses a fixed-
actual-energy unknown complex-gain subspace model. Its effective rank can be
fractional as a surrogate; it is not an observed finite-dimensional basis.
Static Rayleigh amplitude fading and a fixed-energy branch are different
assumptions: static fading can have deep amplitude outages, while phase
diffusion alone does not create Rayleigh amplitude outages in the fixed-energy
branch.

The random Doppler parameter `B` is `2 sigma` for each ray's Gaussian spectrum;
ray-center separation is a separate parameter. Relative phase follows a
Wiener-diffusion model. The forecast computes finite observation-span covariance
modes, using elapsed DATA calendar for burst timing and active seconds for
nominal signal energy. Native plan and export checks verify the respective
elapsed-bit and active-energy durations; burst gaps add elapsed time without
adding active DATA energy. It reports tile/elapsed/active durations, sample-clock
and arrival-grid context, source/exporter hashes, and assumptions in the plan
JSON. Native shaping continues to own the exact sample count, UTC/source epoch,
tile and burst calendar, pulse, ramp, FHSS, and waveform construction. Planning
does not change tile shaping or source epochs.

## Long-span synchronization and error accounting

Longer integration can demand much finer frequency and clock searches. The plan
reports the current final grid and separate analytic design references. For a
hypothetical rectangular coherent integration across elapsed pattern span `T`,
it reports frequency spacing `1/(4T)`, corresponding to a worst half-bin loss
of about 0.2244 dB. It also reports the error of the existing frequency grid and
the number of points such a coherent reference grid would need across the
declared oscillator bound. A renewing short-block receiver may tolerate a
coarser frequency grid by combining shorter coherent blocks, with a separate
sensitivity cost.

The clock diagnostic compares accumulated drift from the current grid with an
engineering reference of 0.125 fine chips; it reports the associated step and
point count. This tolerance is an explicit design assumption, not a validated
waveform-loss bound. Frequency/clock point counts are diagnostics only: they do
not expand the charged search grid, adjust the modeled duration, or prove that
the current receiver can execute that search. The report therefore marks long-
span receiver qualification false.

`alpha_per_day` continues to describe a conditional Gaussian-null bound for
each 24-hour acquisition schedule. For a multi-day message, the report also
gives a conservative union bound over the covered 24-hour periods,
`min(1, covered_days × alpha_per_day)`, without assuming the periods are
independent. Its explicit assumed accounting horizon allows a Helper lead,
up to 30 seconds of UTC alignment, pulse halos and an endpoint allowance for
every bit. This is separate from the approximate contiguous-message time;
longer listening schedules require their own exposure bound. Neither quantity
qualifies a physical audio/ADC null.

## Limits and validation

Selected channel effects may remain outside the likelihood forecast, including
selective amplitude modulation, moving-delay mismatch, overlapping ray spectra,
FHSS hop loss, and burst-gap stationarity. Declared outage and retained-energy
fractions are assumptions, not measured certificates. The known-waveform
energy reference is only a reference. A report with a 99% request does not
establish 99% correct reception, acquisition inclusion, Helper success, a
physical null, or physical audio/ADC behavior. The embedded operational
admission policy's Gaussian-null conditions and exclusions remain applicable;
its runtime manifest policy is metadata for the consumer, not proof that the
consumer or physical setup satisfies them.

The wrapper places `weak_admission_policy` in the exported manifest. The
consumer path in `tools/triad_stream_run.py` and
`tools/triad_hf_noise_measure.py` passes it into `WeakReceiver`; the receiver
also defaults to the manifest policy when no explicit override is supplied.
Manual historical exports remain available and retain their legacy
manual-duration default; they do not gain the wrapper's automatic duration
selection or policy-enriched manifest.
Keep clean likelihood and Gaussian-null theory separate from physical
Helper/audio qualification. The [finite Gaussian likelihood reference](validation-data/triad-v3/2026-11-02-acquisition/README.md#exact-finite-gaussian-reference)
gives the likelihood conventions and detection benchmarks; the [stochastic
channel guide](triad-stochastic.md) links ITU-R F.1487. These are theoretical
context, not validation of this receiver or channel.

Routine model and wrapper checks are the focused Python tests
`tests/test_triad_duration_model.py`, `tests/test_triad_weak_plan.py`, and
`tests/test_triad_operational_admission.py`; they take about a second in the
maintained `/tmp/triad-weak-env` environment. They check arithmetic, tile
selection, manifest policy propagation, and operational-policy accounting.
They are not sampled sensitivity campaigns. The dated measurement and
qualification record belongs at
[`validation-data/triad-v3/2026-11-06-duration-planner/README.md`](validation-data/triad-v3/2026-11-06-duration-planner/README.md).
The [long-forecast record](validation-data/triad-v3/2026-11-06-long-forecast/README.md)
documents multi-day through ten-year integer-geometry checks and search-resolution
diagnostics. It does not demonstrate multi-day signal generation or reception.
