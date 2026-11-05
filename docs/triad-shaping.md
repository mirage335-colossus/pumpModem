# Rate-aware three-carrier shaping and Helper placement

This guide describes the default shaping planner for the isolated three-carrier
Helper exporter and campaign runner. Both default to `--shaping auto`; use
`--shaping legacy` for historical reproduction with matching legacy templates.
It does not change the ordinary Robust receiver. Start with [the v3 format](triad-v3.md), [Helper integration](triad-helper.md)
and [the channel-model contract](channel-models.md).

## What the planner chooses

`plan_shaping()` jointly examines the DATA pulse, finite source envelope,
protected-symbol lattice and FHSS dwell ramps; the resulting bandwidth feeds
the unchanged Helper placement algorithm. It searches
bounded candidate ramps and pulse forms, then accepts a placement bandwidth
only when the finite-kernel spectrum converges within its declared numerical
allowance. The default tail-energy request is 0.001. The selectable
`rrc-025-span8-c2-tail2` pulse keeps the existing eight-chip support while
tapering the outer two chip periods to zero with a smooth C2 edge. The legacy
unit-energy RRC remains available through `--shaping legacy`. The low-level
`Format` descriptor still starts from that legacy pulse; exporter defaults
invoke the joint planner rather than changing unrelated resolved formats.

The planner may lengthen a dwell ramp only up to the space available around a
protected symbol, while retaining the configured quiet interval and margin. It
can choose a center-aligned protected symbol where the current lattice permits
one. It preserves tile and bit duration, fine and inner coefficient counts,
source sample extent, interleaving, Added Symbols mapping, FHSS dwell calendar,
Helper lead and power allocation. The estimator does not depend on the secret
pattern, seed or message bit. The new pulse identity is part of public format
identity so waveform templates cannot be silently reused across shapes.

For failed geometry, the planner reports the underlying cause. For example,
the 0.36 Hz GUI-rate, ×1000 Added Symbols, FHSS case needs 64,000 protected
cells over 64,002 dwells, or 1,536,048 calendar cells before padding. This
exceeds the existing one-million-cell tile workspace cap. The cap was not
raised: this is a software workspace limit, not a demonstrated physical
transmission limit. Other very-low-rate FHSS cases can fail because no complete
eight-chip pulse fits inside the configured dwell aperture. These are separate
planner failures.

A very-low continuous-rate planner success is also separate from exporter
support. This fixture keeps a fixed 38-second Helper lead and rejects a DATA
pulse halo longer than that lead (`8/fine_rate > 38`, approximately fine rate
below 0.211 Hz). Its separate 2 GiB capture cap also remains in force. These
are software calendar/export limits; they do not demonstrate a physical
low-rate limitation or authorize truncating the pulse.

## Helper frequencies and the spectral certificate

The production Helper placement plan remains authoritative. The study enforces the standard
1725 Hz minimum DATA center and the existing fixed low-tone separation guards. Helper’s fixed tones, ramp and modulation shapes, lead and power split remain
unchanged; the existing production rule selects its high-pilot frequency. With default tail request 0.001, the strong
EME FHSS case (fine rate 60 symbols/s, 400 ms dwell) plans
a 6.6111 ms ramp from the configured legacy ramp, DATA at 1725 Hz, and high
pilot at 2400 Hz. An explicit 6 ms input produces the same selected waveform. It preserves the
64 protected inner symbols and the 26.4 s tile. The finite-kernel certificate
reports 264.375 Hz placement width and measured tail fraction 0.00095368 for
that source model.

At DATA 1775 Hz with the default 0.001 request, the production placement guard
raises the high pilot to 2450 Hz. A 0.005 tail request reports 225 Hz placement
width and allows the same case to retain the 2400 Hz high pilot. That looser
request is an explicit option; the present paired sampled evidence below is
only a small initial comparison and does not qualify a population sensitivity
or operational interference bound. Keep the default at 0.001 unless the
application has validated the tradeoff it needs.

The certificate is an engineering estimate of expected emitted DATA energy
outside a nominal width, derived from finite gated-pulse kernels and public
phase classes. It is not a hard spectral mask, a worst-key bound, an exact
continuous-spectrum theorem, or a channel-output bound. Residual energy remains
in the transmitted samples. The conservative capture search envelope, actual
sidebands, filter tails, oscillator behavior and continuous delay/Doppler
channel are retained. In particular, a planner width must not be used to gate
or truncate the source or propagated channel.

For independent zero-mean physical-cell coefficients, the expected source
spectrum is the sum of coefficient variances times the squared Fourier
transforms of the individually gated pulses. The implementation shifts those
kernels for all three carriers, corrects finite bit/burst endpoints and checks
sample-rate and gate-phase refinement. Fresh-key, fully rendered FFTs provide
a separate check of this approximation; a finite encrypted pattern need not
equal its ensemble expectation.

The native planner census covers representative continuous and FHSS rates,
equivalent fine rates formed by different Rate/Added Symbols pairs, and
multi-tile burst examples. It finds continuous plans through 360 Hz with the
standard Helper frequencies in the tested configurations. Some higher
continuous rates require a raised DATA IF because the fixed Helper low-tone
guard no longer fits; at 12 kHz and 24 kHz the nominal DATA band cannot fit at the requested
1725 Hz center. A higher center must also leave room for the high pilot below
the fixture’s Nyquist limit. The default 18 kHz sampled receiver also rejects
some high-rate geometries. A planner result is not evidence that a corresponding
receiver or radio path supports the rate. See the full machine-readable
[planner census](validation-data/triad-v3/2026-11-04-shaping/planner-census.json)
and the linked validation record.

## Sampled evidence and limits

The first four-control sampled comparison used EME, DATA bit 0, seed 95001,
40 seconds requested, Each 65%, the selected 6 Hz / 0.5 ms spread model, the
unchanged 15 kW Helper and 40 W DATA power allocation, and DATA at 1725 Hz.
The two complete 26.4 s tiles provide a late 36 s Helper observation window
that overlaps received DATA. It therefore measures both Helper-to-DATA and
DATA-to-Helper interference under one shared propagated waveform.

Against legacy pulse shaping with a 6 ms ramp and the same DATA center, the
auto plan changed emitted DATA energy by +0.00145 dB and received DATA energy
by +0.00217 dB. The measured Helper-to-DATA component was 45.86 dB below the
noise energy after the DATA filter, corresponding to a 0.000113 dB
power-based noise-equivalent penalty. Its conservative coherent norm envelope
was 0.0441 dB. In the late Helper aperture, the diagnostic composite-versus-
Helper-only frequency shift was 2.59e-9 Hz or less; centered fresh-phase RMS
ratios differed from one by at most 4.5e-6 across the initial designs. These
are single-seed diagnostics, not confidence bounds or guarantees of Helper
frequency measurement under all channels.

All four-control linearity residuals were below 4e-16 relative. The 1775 Hz,
tail-0.005 comparison also selected the 2400 Hz pilot. Its measured
Helper-to-DATA component was 43.55 dB below filtered noise, and its
power-based penalty was 0.000192 dB. Received Helper energy changed by −0.487 dB
against the legacy design because placement changes how the signal traverses
the selected frequency-dependent channel. That propagation difference is
reported as measured; it was not normalized away. A companion 1725 Hz auto
case changed received Helper energy by +0.895 dB. These level shifts are not
the same quantity as the Helper's within-window frequency or phase estimation
error.

The two independent metrology seeds (95011 and 95012) preserve those small
DATA energy changes. However, the **net 5% Helper-precision target is not
uniformly met** after moving frequencies through the selected channel. Across
all three paired seeds, high-pilot fresh-phase RMS at DATA 1725 changes by a
factor of 0.601–1.177; the 1775 option changes low-pilot RMS by 1.151–1.856,
to 3.69–5.54 degrees. Absolute frequency errors against each design’s clean
finite-window fit stay below 34.5 microhertz. These are descriptive errors,
not estimator-variance confidence bounds. DATA contamination changes the
within-design fresh-phase RMS by at most 5.1 parts per million; the larger net
changes arise from frequency-dependent propagation and noise exposure.

For scale, a constant 34.5-microhertz error over 52.8 seconds has approximately
0.00005 dB rectangular coherent-integration loss. Under a zero-mean Gaussian
phase-error assumption, the largest observed low-pilot RMS increase corresponds
to about 0.029 dB additional coherence loss. These are conditional calculations,
not demonstrated DATA phase-correction gains: pilot-to-DATA channel phase
transfer remains unqualified. The default 1725 option therefore has strong
practical frequency margin in these cases, while neither option has a universal
5% relative-precision guarantee.

The study uses a matched composite, DATA-only, Helper-only, and both-off
capture. The common components are recovered by subtraction and checked for
matching seed, source bit, arrival, oscillator and channel identity. The late
Helper window must overlap actual received DATA; the mandatory lead-only
window cannot establish DATA-to-Helper interference. Frequency and phase
reports include both calendar-transported and locally centered phase metrics.
Pilot phase at a different audio frequency is not assumed to equal DATA
channel phase.

The sampled frontend support guard rejects a public DATA band plus selected
channel lines and declared CFO/clock envelope that extends beyond the chosen
330 Hz legacy or 400 Hz wide pass edge. This avoids calling a receiver-filtered
out-of-band signal a reception failure. It describes nominal receiver support;
it does not remove source sidebands or qualify sensitivity. Higher-frequency
cases need a suitable sample rate and receiver frontend before their receiver
performance can be compared.

## Reproduction

Build the isolated CLI target, run inexpensive native shaping checks and the
exporter checks, then inspect the planner before generating full captures:

```sh
./build.sh configure --cli --build-dir build/dev-cli
cmake --build build/dev-cli --parallel 2 --target triad_helper_export test_triad_shaping test_triad_v3_source
build/dev-cli/test_triad_shaping
build/dev-cli/test_triad_v3_source
python3 tests/test_triad_helper_export.py build/dev-cli/triad_helper_export
build/dev-cli/triad_helper_export --case eme --plan-only --shaping auto --data-if 1725
build/dev-cli/triad_helper_export --case eme --plan-only --shaping auto --data-if 1775
build/dev-cli/triad_helper_export --case eme --plan-only --shaping auto --data-if 1775 --shaping-tail-fraction .005
```

For occasional full-spreading source qualification, run
`build/dev-cli/test_triad_shaping --extended`. It rendered complete ×100/×1000
continuous patterns and a ×100 FHSS pattern in about seventeen seconds including the
default checks and noncommensurate spectral refinement. This does not synthesize their long 18 kHz channel captures.

`--plan-only` emits geometry and the production Helper placement decision; it
does not allocate keyed templates, synthesize samples, or run a receiver. For
sampled qualification, generate the same-data-bit composite and controls from
one frozen binary and one shared source identity, then run
`tools/triad_helper_interference.py` with a Helper aperture that overlaps
received DATA. Use the same configuration and stochastic roots for all
controls. Run a different seed as independent validation after freezing the
planner choice. For receiver comparisons, the campaign runner records and
forwards `--shaping auto` by default;
rate, Added Symbols, FHSS and burst overrides remain optional. Generate
templates from the exact same public format. Explicitly select `--shaping legacy`
for historical campaigns; archived evidence and its source snapshots remain
unchanged. Do not reuse legacy templates for the tapered pulse.

The [completed validation record](validation-data/triad-v3/2026-11-04-shaping/README.md)
retains 536 GPU campaign captures, independent holdouts, the Helper-off control,
source/binary identities, and the final-binary byte bridge. Preserve misses,
wrong admissions and false admissions separately.
Small samples cannot establish a daily false-admission rate or a small
population sensitivity difference.
