# Gannon 2024: fixed observed-voltage replay fixtures

These two complete W2NAF recordings add long observed-voltage stress inputs
from the 2024-05-10 Gannon storm dataset. They do **not** establish calibrated
disturbed-day channel truth, blackout availability, false-alarm rates in field
receiver noise, or wideband channel fidelity. Use the explicit
observation-conditioned description in results.

## Attribution and provenance

Adapted from **HamSCI PSWS HF Doppler observations of the 2024 Gannon
geomagnetic storm**, collected and curated by Rebecca Potter (KE2EBI), Gwyn
Griffiths (G3ZIL), Nathaniel A. Frissell (W2NAF), the HamSCI community and PSWS
station operators. Dataset: [10.5281/zenodo.20466479](https://zenodo.org/records/20466479).
The source data and these selected converted samples are licensed under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
Associated analysis software:
[HamSCI/ke2ebi_hf_doppler_undergrad_thesis](https://github.com/HamSCI/ke2ebi_hf_doppler_undergrad_thesis).
Its published software archive is
[10.5281/zenodo.20668805](https://zenodo.org/records/20668805), explicitly linked
by the current dataset record and linked back to this dataset by the software
record (verified 2026-11-14). The downloaded provider README retains an
unfinished software DOI placeholder; that stale file does not mean the archive
is unavailable. No endorsement is implied.

Changes: selected column 4 of two fixed hourly files; converted each signed
int16 I/Q component exactly to binary64. There is no smoothing, detrending,
carrier removal, phase unwrapping, normalization or gap repair in these files.
Each source HDF5 was verified against the provider `MANIFEST.sha256` before
conversion. `manifest.json` records complete archive-member paths and exact
source/output digests. Archive `data.tar.gz` SHA-256:
`60a1c93fb31e493b68d42c8c8345ae8b0dfdc8dc38b14ce6a9480d7c8c3fae4e`;
size 941432316 bytes.

The selected files are from the **nine-subchannel** stream UUID
`0e51e045ca1d42868f3c9cd483112bd7`, receiver `KA9Q_0_WWV`, station W2NAF,
grid FN21ei. Its applicable metadata lists channel center frequencies
`[2.5, 3.33, 5, 7.85, 10, 14.67, 15, 20, 25]` MHz. Zero-based column 4 is
nominal 10 MHz. Metadata location is 41.333332 N, 75.666664 W.

Both raw files reside under the archive directory
`data/psws_grapeDRF/ch0_W2NAF_2024-05-10/2024-05-10T00-00-00/`.
The archive also contains the same basenames under other hour directories
with different hashes. Basename alone is insufficient identification.
The interleaved one-channel `grape19` minute stream and its distinct metadata
are excluded, including its separate location. No cross-stream splice or
metadata substitution is allowed.
The applicable frequency map is the member
`data/psws_grapeDRF/ch0_W2NAF_2024-05-10/metadata/2024-05-10T00-00-00/metadata@1715299200.h5`;
its exact hash and the DigitalRF properties member are recorded in the manifest.

## Format and timing

| File | Start UTC | Raw HDF5 first sample index | Knots | Integer I/Q energy sum |
|---|---|---:|---:|---:|
| `w2naf-20240510-1700-col4.f64iq` | 2024-05-10 17:00:00 | 17153604000 | 36000 | 165231037 |
| `w2naf-20240510-2000-col4.f64iq` | 2024-05-10 20:00:00 | 17153712000 | 36000 | 2473256894 |

Each 576000-byte file is headerless, little-endian IEEE-754 binary64
`real,imag,real,imag,...`. Original HDF5 shape is `(36000,9)` with complex
signed int16 components, recorded sample-rate ratio 10/1, continuous flag,
one-hour file cadence and one `rf_data_index` segment starting at offset 0.
These fields establish stored-grid continuity, not independent verification
of the receiver clock or every missing physical observation. The last knot
is at 3599.9 seconds; no replay is permitted past it, between the two selected
hours, or by wrapping. The native existing operator uses Cartesian linear
interpolation between knots; that convention is not recovered sub-sample
ionospheric physics.

`load_gannon_2024_trace()` verifies the exact length and SHA-256 of **both**
files and computes one native joint RMS over all 72000 raw knots. It uses the
same gain for each returned hour, with no per-window/hour renormalization.
Each returned trace reports its individual raw mean power and the common
gain; the raw knot-energy ratio (about 14.97) survives. Cartesian interpolation
changes the continuous-time energy differently in the two records: their
full-hour continuous power ratio is about 9.40. Native receipts retain both
knot and continuous means. This is a relative
digital-voltage scale, not an incident-power or propagation-loss calibration.
Amplitude-only and phase-only variants are explicitly constructed ablations.

The input-hour selection and complete-hour normalization are fixed, not fitted
to decoder outcomes. Any message placement is a separate predeclared scenario.
No claim is made that these two hours represent all storm phases or path states.

## What measurement noise prevents this replay from establishing

Recorded voltage includes beacon, receiver noise and interference; at nominal
10 MHz it may mix WWV and WWVH. No transmitter/path separation, per-record
receiver clock discipline, gain, absolute path response or noise-floor
calibration is established here. Ten complex samples per second cannot supply
the full modem-band delay response. Clock, noise and path fluctuations can be
inseparable in these records.

For a measured voltage `z = beacon_response + measurement_noise`, multiplying
a synthetic modem waveform `x` by `z` produces
`x*beacon_response + x*measurement_noise`. The second term scales with the
synthetic signal. It is not independent ambient receiver noise. In particular,
if the true beacon response is zero, measurement noise alone can still create
nonzero synthetic modulated output. Added independent simulation noise does
not remove that confound. Success therefore demonstrates tolerance of this
record-conditioned operator, not reception through a proven blackout or a
measured link budget. Fixed normalization retains inter-hour relative energy
but cannot supply the missing physical calibration.

The native `make_hf_measurement_noise_proxy()` control exposes this limitation
with a fixed four-phase nuisance sequence and constant true carrier 0 or 1.
Both controls use unity scale. The unit test applies the **same**
`hf_replay_gain()` multiplication, showing nonzero output for noise-only
measured voltage and zero output for the known true zero physical channel.
This deterministic sequence is an analytic confound demonstration, not a
Gaussian or field-noise model. Its exact UTF-8 definition, with no final
newline, is:

```text
Deterministic measurement-noise proxy v1: 36000 knots at 10Hz, noise sequence 1,i,-1,-i, constant beacon 0 or 1, unity scale; observed voltage is beacon plus noise.
```

The definition SHA-256 is
`233bdf29e84e7cdecad274daf6534befe16697a47cc75bcaa143f9c59eb57ed8`.
Exact binary64 serialization hashes for carrier 0 and 1 are respectively
`cfb25bc81335ef2f0d0140325fff88bc2761b95b9f20fd3a93c4aa2f99e5d5dd` and
`76bd47fbfe16fa18dd29cc890c7032326183cf9fea0a67d3ba0e54558840da29`.

## Reproduction and retention

The importer requires h5py and NumPy only to read metadata and losslessly
convert the selected raw I/Q columns. It performs no signal processing.
With verified source files arranged as `w2naf-may10/drf_properties.h5`,
`w2naf-may10/metadata@1715299200.h5` and the two files below `w2naf-may10/rf/`:

```sh
python3 fixtures/hf-gannon-2024/import_fixture.py /path/to/w2naf-may10
```

Run from the appraisal directory with the data-tool dependencies available.
The importer fails on different source hashes, stream identity, frequency
map, raw shape/type, rate/cadence or sample-index geometry. The native test
checks identities, all-knot raw energy/zeros, the common scale, exact support,
Cartesian interpolation, ablations, damaged/missing inputs and the confound.
Native runtime processing does not depend on Python or HDF5.

Retain these two small exact fixtures while the current long observed-voltage
communications investigation and its regressions need them. Remove them when
that investigation is superseded and its conclusions are distilled. The much
larger archive is not a runtime dependency. The original selected HDF5 and
provider manifest in temporary `local-evidence/` remain only while source
validation questions need them; no larger redundant source archive is needed.
