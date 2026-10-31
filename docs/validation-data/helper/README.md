# Retained Helper Carrier evidence

Start with the [integration guide](../../helper-integration.md) for implemented
algorithms, best practices, interfaces and limits, and the
[waveform contract](../../helper-carrier.md#dwell-waveform-and-metadata) for
transmission format. Receiver architecture remains a choice: these records do
not require a particular FFT, filter, tracker or expensive acquisition path.

The [2026-10-31 evidence package](2026-10-31/README.md) retains selected inputs,
per-case results, negative controls, provenance and reproducible commands for
strict and pooled auxiliary-inversion timing in the complete-dwell receiver.
It can be inspected and checked without ignored `local-evidence/`, historical
temporary executables, audio hardware or an ordinary Robust build campaign.

Earlier [pilot measurement](../../helper-measurement.md),
[tracking](../../helper-tracking.md),
[frequency refinement](../../helper-frequency-refinement.md),
[tone/APSK probes](../../helper-live-probe.md) and
[EME measurements](../../helper-eme-measurement.md) retain their conclusions,
assumptions and reproduction instructions in the repository, with corresponding
fixtures under `tests/`. This directory is a selected evidence set, not an
archive of every raw log or waveform. Recorded cases are now regression cases;
future tuning needs new schedule keys and noise seeds for fresh holdouts.
