# Presence and completion calculations

Run `python3 -I checks.py` from this directory. Only the Python standard library is required. These are finite probability calculations for the research note, without a modem or a random-number generator.

The fixed checks use 64 independent unit-variance complex noise observations, a single known template, two decision statistics and family false-alarm target `1e-6`. A deterministic mean vector with alternating signs has total energy 64 and zero coherent sum. Compute an upper bound on the detection probability of a maximum of raw scores and a lower bound after dividing the false-alarm budget equally between the scores. Independently check the 64-term energy tail against its exactly soluble one-look case. Truncate the noncentral tail mixture only after bounding its unused probability mass.

The other checks vary a local noise reference over 31 and 63 independent bins, including a 2:1 power mismatch, and vary the conditional persistence of missed active blocks at a fixed marginal miss rate. The evaluation ends after the named deterministic cases. There are no empirical tail or hardware throughput claims. The interpretation and reproducible reference values are in the [research note](../../../_ref/algorithms-matrixModem/fsk-presence-calibration.md).
