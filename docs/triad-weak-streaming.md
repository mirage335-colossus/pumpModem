# Current weak acquisition and streaming research

The canonical `WeakReceiver` and weak Helper estimator are acquisition stages
exposed by `tools/triad_backend.py`. `PracticalTrackingReceiver` continues a
declared post-acquisition bank. These stages are not an integrated message
receiver or GUI backend; [current architecture](robust-current.md) states the
available boundary.

## Acquisition without discarding complex evidence

Weak Helper onset/frequency evidence supplies proposals while retaining the
public timing/epoch priors. Segmented complex FFT screening and sparse dynamic
refinement avoid a full Cartesian search. Keep both bit alternatives, complete
filter support and original complex observations. A favorable proposal score
cannot substitute for a complete declared observation or prove exhaustive-bank
sensitivity.

Renewing rays, selective modulation and phase-transfer failure are separate
from a stationary flat-channel example. Frequency assistance may remain useful
when coherent Helper-to-DATA phase transfer is unresolved. The historical
8-dB-Hz DATA/18-dB-Hz total Helper target is different from a 4-W/180-dB modeled
link; do not interchange their outcomes or power allocations.

## Admission and streaming scope

Fixed normalized priors, observation endpoints and permitted attempts determine
Gaussian-null operational accounting. Physical audio, colored/unknown noise,
adaptive proposals and wrong-bit errors require their own applicable evidence.
No hidden transmitter truth may choose the receive bank. Prefix scores remain
diagnostics rather than extra admission attempts.

Reuse native keyed templates and bounded workspace where identities match.
FFT sizes, repeated preparation and small-matrix thread oversubscription can
dominate cost. Affinity/quota-aware CPU allocation is a resource policy, not
proof that more threads are faster.

## Retained evidence and current checks

The [weak-stream evidence](validation-data/triad-v3/2026-11-06-weak-stream/README.md),
[HF target follow-up](validation-data/triad-v3/2026-11-06-hf-background-target/README.md),
[current-cost record](validation-data/triad-v3/2026-11-06-hf-current-cost/README.md),
and [preparation optimization](validation-data/triad-v3/2026-11-06-realtime-optimization/README.md)
retain their source/configuration identities and limitations. They do not certify
changed code. New intensive work must use [hierarchical abbreviated tests](triad-performance.md);
full qualification and standalone run drivers have been removed.
