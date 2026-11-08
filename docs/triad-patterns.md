# Historical triad pattern research

These records describe a past isolated three-carrier waveform study. They retain
technique and measurement findings only; they are not current implementation,
operating, calibration, or qualification instructions. The current Robust path
and its GUI limitations are documented in [robust-current.md](robust-current.md).

## Pattern construction

The historical `independent-apsk-v2` revision independently keyed amplitude and
phase sequences for each bit alternative. Added Symbols used two-phase,
two-radius 4-APSK factors with common three-carrier power normalization. This
broke the old fixed cyclic amplitude permutation while keeping total coefficient
power normalized. The change did not alter source timing or carrier spacing.

The source-bound [pattern study](validation-data/triad-v3/2026-11-06-independent-patterns/README.md)
reports sampled amplitude/phase relationships, envelope, spectrum, sensitivity,
and Helper controls. Its receiver cohort used fixed synchronization and a small
number of declared captures; it does not establish broad sensitivity or security
from the waveform statistics. The [spacing follow-up](validation-data/triad-v3/2026-11-06-spacing/README.md)
found that a 90-Hz carrier spacing did not reliably meet the preferred 100-ms
short-window power screen. Widening spacing is not a general solution for the
low-rate beating constraint.

These reports are dated historical evidence. Their source hashes, fixtures,
receiver thresholds, and limitations are retained with each record. Neither a
sampled envelope statistic nor matching coefficient power proves LPI, arbitrary
channel reliability, or a physical-radio result.
