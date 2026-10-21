# Live drain diagnostic

Manual `native-linux.yml`, `scope=live-drain-diagnostic`, Release on standard
`ubuntu-24.04`. The runtime and original test inputs are pinned to
`3611f0cf0ae066d6d9679cdee8447b73b66468c7` (runtime `be1e1bc`).

The runner preserves the original executable, builds a declared two-file probe
overlay, then runs the original unchanged no-argument executable once and a
control/probe/probe/control comparison using the same second executable. Only
the comparison controls and probes retain exact generated binary32 PCM; their
hashes must agree. The unmodified executable's PCM is not directly captured.
Every process retains the original assertions and 25-second fixture allowance.

Both comparison modes use two CPUs, nice 10, matching environment, wall/process
and thread-CPU observation. Probe mode adds bounded 250ms gate markers and a
filtered receiver trace. The trace records the numeric captured-input resume
target and its release, plus existing bank/admission/retirement events. Compare
frontier progress, CPU, input lead and admitted cohorts before attributing cost.
Timing pairs alone do not establish instrumentation overhead or a regression.

Output is under `build/symbol-burst-live-qualification/live-drain/`. Original
failures remain nonzero. Logs, exact ELFs and dependency provenance, PCM, trace
closure, source restoration and a file manifest are retained. All source
preimages are restored even after probe build/run failure. Probe executables
remain diagnostic. This scope confers no qualification or throughput waiver.

The accepted Luna gate-marker contribution is identified in `manifest.json`;
its one implementation repair and original validation failures remain in the
task evidence. The source-fence observation and integration remain Sol-owned.
