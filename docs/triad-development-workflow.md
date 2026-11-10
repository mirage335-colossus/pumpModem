# Developing the canonical three-carrier Robust modem

Start with [current architecture](robust-current.md), [independent patterns](triad-patterns.md),
and the [channel model contract](channel-models.md). There is one compiled C++
waveform, channel, Helper and complete receiver stack, linked into the GUI
through a native session. Python can test and benchmark the stack but is not
part of its operating path. Retained numerical references serve bounded
migration parity and interpretation of old measurements only; remove them when
those concrete questions are resolved. The former single-carrier receiver and
standalone alternatives remain retired.

## Choose the smallest discriminating check

First identify whether the change concerns source geometry, channel continuity,
acquisition, timing inference, admission accounting, or resource cost. A small
deterministic check of that boundary is preferable to a sampled reception sweep.
Reuse a small, explicitly identified complex observation only when its source,
inputs and assumptions match the current question. Keep public receiver inputs
separate from simulator truth and evaluator labels. Once the finding is
summarized with provenance and limitations, delete redundant captures and
superseded local reports; historical data is not retained by default.

For computationally intensive work, follow the [abbreviated policy](triad-performance.md):
predeclare path strata, tiny sample allocation, uncertainty and stopping rules.
Use source/configuration-bound extrapolation only within its supported regime.
An inconclusive result stays inconclusive; it does not trigger a full-observation,
sustained, multi-day, exhaustive, or heavy-calibration fallback. RunPod accelerates
this same method. Do not restore an old campaign executable to obtain evidence.

## Reuse the current stages

- Native `PatternSource`, shaping and `StreamingChannel` own waveform and sample
  geometry. The bounded source/basis bridge consumes those same components.
- Native Helper and coarse receiver modules use noisy Helper evidence and full
  public complex FFT proposals. Preserve both bit alternatives and public
  epoch/arrival priors; do not select them using transmitter truth.
- Native fine tracking advances persistent fixed-affine, adaptive and combined
  lanes. Only the fixed-affine lane supplies operational admission; the other
  lanes remain diagnostics. All required work is charged.
- `CompleteStreamReceiver` owns one aggregate worker budget, shared immutable
  windows, ordered commit and bounded queues. The message receiver handles
  codec state only after two scored quiet slots; EOF never invents completion.
- Frontend, template, timing and Gaussian-state modules are reusable kernels.
  Preparation/cache ownership and actual numeric thread limits are part of cost.

## Findings worth carrying forward

The [hierarchical acquisition study](triad-realtime.md) shows why sharing FFT
work and refining correlated sparse candidates is cheaper than multiplying the
full timing/epoch/frequency/clock product. [Weak acquisition](triad-weak-streaming.md)
retains complex evidence under renewing rays. [Noise-model research](triad-noise-calibration.md)
explains why a correct likelihood with the wrong noise model can falsely admit.
[Clock tracking](triad-clock-follow.md) separates known local resampling from
unknown remote motion. These findings motivate checks; they are not new gates.
Preserve concise reusable conclusions, assumptions and source provenance. Do
not preserve duplicate raw outputs, executable experiment copies or retired
implementations alongside the canonical source. Give unfinished investigations
a specific question, next action and cleanup condition.

## Scope the checks to the change

Use `./build.sh test triad` for the retained bounded native and Python checks in
a configured scientific Python environment. Native operation itself must build
and run without Python. Use applicable shared security, audio, Fast, Legacy and
frontend groups when those components change. GUI controls must preserve actual
receiver diagnostics and expose unconfigured/unsupported action reasons.
Test counts and results belong in [validation](validation.md).
Protocol breaks evolve this implementation; older binaries belong to Git tags
and separate releases, never parallel source trees.
