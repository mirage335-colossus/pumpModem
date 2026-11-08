# Public timing-bank recovery followup

This small followup addresses large nominal-arrival failures found by the broad
screen. It reuses the existing grouped-export and calibrated group receiver;
it adds no receiver algorithm. Selection occurred after the broad outcomes,
so these paired diagnostics are not independent reliability confirmation.

Five cells are fixed: rate 360 / Off, HF-worst / Each 65 at rate 360, default-wide
rate 36 / Each 65, and paired forty-second FHSS requests with no loss or dwells 22/23
loss. Each has the original two signal seeds and one null made by changing
only the bit 0 capture's control. Do not count this as three independent paths.
The FHSS experiment represents the transmitter envelope and receiver I/Q
erasures, not physical RF retuning.

The public timing banks include both endpoints and zero. Their maximum steps
come from the native 0.5 dB random-chip/RRC recommendation: 109 groups for the
tight rate 360 cases, 837 for wide rate 36, and 1077 for FHSS Added ×10. That
recommendation is not a finite-pattern loss certificate. A 1 ms step would
be too coarse for the FHSS waveform's 600 fine symbols/second/carrier.
The full finite bank enters the normalized bit likelihood mixture; no
simulator timing is used to choose or prune a candidate.

FHSS exports use 128 samples/block and 2 blocks/window to retain the original
62.5 ms physical likelihood window while keeping each grouped export below
2 GiB. Other cases retain their original 16-sample blocks and 16-block windows.
This projection change is explicit. The scorer also reports the fixed
zero-arrival group's result from the **same** new workspace, using the
full bank's calibration and admission gates. It does not select the best
group for that baseline. Export receipts evaluate total post-erasure signal
energy and calendar agreement with the parent capture, after simulation.

Common CFO, sample-clock rate and keyed epoch are not searched here. The
selected OCXO bounds produce small phase excursions over the chosen windows;
the plan records the worst common CFO cycles per window. The corrected
selective operator and independent physical oscillator draws remain active.
The receiver is still an independent-window Gaussian approximation, with
explicit B=0, rather than the full sampled-channel likelihood.

## Prepare without executing

The followup completed 15 paired acquisitions: five preselected cells, two
signal bits and a null per cell, scored with both the short-window and global
two-ray fits against the complete public timing bank. The source-bound
[acquisition summary](../evidence/acquisition-summary.json) contains all 60
scored rows, plan and receipt hashes, and scorer-source identities. The broad
[study results](../README.md#complete-timing-banks-and-missed-hops) report
10/10 correct signal bits, no wrong bits, and no null admissions for both
methods. Paired nominal-timing controls missed the selected signals; the
sampled paired cells are not independent reliability evidence. FHSS modeled
transmitter envelope and receiver-I/Q erasures, not physical RF retuning.

## Run bounded stages

The original configure/export/acquire machinery and its campaign-specific
commands have been retired. Its bounded stages and source/build bindings are
recorded in the compact summary; they are historical provenance, not a current
runner contract. This paired study did not qualify 99% reception or rare
false-admission rates. For future expensive Robust testing, use the
[abbreviated statistical harness](../../../../triad-performance.md) and
[material lifecycle policy](../../../../development.md#material-lifecycle);
retain only evidence required by a concrete unresolved question.
