# Review after the Sched_Hop implementation attempts

This local, unreleased review starts from `2a0f3dd142c97909f206145cd4f4eecb1cc72dcf`
and compares against `52ace0a`, immediately before the first implementation
commit. It covers every commit in `52ace0a..2a0f3dd`. It does not turn the
unfinished Sched_Hop work into a qualified implementation or certify a release.

## Scope and preserved corrections

| Commit | Scope reviewed |
| --- | --- |
| `ae89e94` | Receiver strategies, amplitude reception, scheduled source mapping, TX/CLI/Live and planner/GUI |
| `9e1f42f` | Sparse UTC timing, clock bounds, persistent observations and audio provider contract |
| `fe3e2c2` | Split complex reception, frequency fusion, strategy selection, Live and timing advice |
| `7af295c` | Shared moments, physical/temporal caches, SIMD, original-template replay and CPU estimates |
| `10db1d1`, `be39636` | Suggestions file; no runtime changes |
| `42221b0` | Lazy acquisition, certificates, deferred Live state, allocation linking and GUI |
| `a014311` | Numerical kernels, point experiments, effective clock policy, capture and GUI |
| `7a96407` | Suggestions file; no runtime changes |
| `c515f89` | Implementation runbook and scheduled-hop documentation |
| `2a0f3dd` | Native cohorts/source rows, admission, shared/native/web controls and recorded failures |

The review retains fixed short-message and interval framing, fully scored
physical absence, immediate pending-bit publication, complete search coverage,
original waveform/rounding, and earlier capture/timing corrections. In
particular, it retains the correction that preserves imported target values
instead of silently replacing them with a fitting target. GUI application code
continues to use the shared abstraction.

## Corrections

- Receiver strategy FFT estimates use the same observation geometry as the
  decoder. The old duplicate calculation could double the transform near a
  power-of-two boundary. An explicit all-positive clock bank also no longer
  invents a nominal zero-error hypothesis when finding its slowest rate.
  The legacy coupled, nominal-clock representation retains the decoder's
  conservative symmetric aperture. A separate regression checks its first
  transform and subsequent hop at the actual input-sample boundaries, while
  retaining the shorter aperture of authoritative positive-clock pairs.
- Settings-aware and standalone receiver estimates share Live's pure
  capture/TX/plot/audio reservation calculation. Analytical diagnostics still
  accept their broader input range; exhausted workspace reports unavailable
  instead of acquiring Live-only validation requirements.
  A pre-existing compact-memory exemption without an oscillator model is also
  removed: a public 131,072 Hz, eight-second profile claimed a fit in 16 MiB,
  while the actual receiver constructor rejected its complete timing bank.
  No PCM is needed for that regression test.
  Legacy estimates also respect the remaining shared capacity and receiver
  control state: a public 4,096 Hz, one-second profile falsely fit a 2 MiB
  total budget, while the real constructor rejected its 603,288-byte allocation.
  Its 3 MiB counterpart still fits. Existing-complex receivers retain their
  actual remaining-capacity allocation, rather than an artificial equal share.
- Short amplitude-only acquisition no longer requires coherent chip evidence
  which that decoder does not produce. A deterministic regression has four
  individually weak symbols whose joint evidence admits the exact pending
  prefix; wrong keys/noise and physical absence are checked independently.
- Lazy timing-state lookup uses the existing logical-ID sort order instead of
  scanning every subscription. Subscribed slots cannot be recycled, preserving
  that ordering and the previous decisions.
  Local replay also starts at the first overlapping retained capture block,
  avoiding repeated scans of the entire prefix for each original processing
  fence. The callbacks retain exactly the same sample spans and order.
  Catalogs without a qualified arrival window calculate their rectangular
  origin/family positions directly; the existing lane ordering and waveform
  construction stay unchanged. Qualified catalogs retain their exclusion checks.
- Scheduled complex timing follows neighboring arrival origins within the same
  phase and receiver pair. The previous adjacent logical lane could instead be
  another phase. A retired or incompatible guide also no longer withholds a
  stream's fully scored physical END. Deterministic tests reproduce both defects
  before the correction and retain the valid-guide, full-prefix and physical
  absence checks. Ordinary and amplitude-only timing policies are unchanged.
  On the same sampled `01` capture with all 11,016 frequency/clock/phase lanes,
  the pre-fix receiver published only `0` and completed at poll 176,816. The
  corrected receiver publishes `01` and completes at poll 259,056, after the
  required absence endpoint at sample 258,153.
- CLI scheduled playback passes both epoch and fractional phase to the
  transmitter and protects the actual physical stop/cover endpoints. CLI
  playback and link diagnostics use the effective calendar clock policy.
- The GUI suppresses observer time numbers and the observer graph without an
  encrypted private pattern, including immediate withdrawal of cached values.
  The standalone explicitly hypothetical LPI API/CLI remains available.
- Shared native window dimensions are restored. Experimental scheduling
  controls remain in the responsive planner footer and developer visibility
  rules. Web menu options use system normal/disabled colors without inheriting
  a selected warning color. FLTK and Rev ordinary choices use the shared native
  data role (240 in monochrome), preserving explicit semantic data (255),
  discouraged tones and disabled precedence. The Rev inline-editor test also
  starts with a fresh application, avoiding expanded-panel state from a prior
  probe without changing any scrolling assertion.
- Invalid scheduled test fixtures now reserve their actual flat aperture,
  describe the selected clock bank accurately, and date a cropped synthetic
  capture at its actual payload anchor. These changes preserve the original
  received samples and assertion requirements.

## Requested configuration comparison

Both builds used the same compiler, dependency prefix, synthetic 32-byte key
material, and fixed **5,683,755,008-byte** DSP allowance (the captured 50%
allowance). No audio was generated for these model comparisons. The named
example definitions were unchanged between the revisions. Short/long targets,
physical geometry, one-bit preview, independent reference, and default clock
selection were preserved. The first table uses the same broad hardware fallback
timing basis in both versions. Values are estimated receiver CPU seconds per
audio second, not measurements or promises of real-time operation.
The same percentage can yield a different byte allowance when available system
memory changes; fixing the byte allowance isolates source changes from that
effect.

The final refresh reproduced all 1,664 comparison rows and 40 startup rows
byte-for-byte after the native palette and legacy-clock corrections. Its
787-file runtime/test/build source inventory is `final-source-inputs-v4.json`,
SHA-256 `ea36e4aa19b9a159b867927b7150f8cf5f3283827189807f1a7938857c818ca4`.
The earlier full-host inventory, `final-source-inputs.json`, SHA-256
`002f444f3b55e8892d76305ee0bba8638c2b502cec34e47154da97d09ccb5457`,
is also preserved.
The inventory and raw outputs are retained in the local review artifacts under
`.agent-work/artifacts/sched-regression-review-20261016/`.

| Configuration / target | Before | Corrected candidate | Result |
| --- | ---: | ---: | --- |
| Default public / 32 dB | 0.004094 | 0.004094 | Still below 0.6 |
| Default public / 55 dB | 0.004060 | 0.004060 | Still below 0.6 |
| Default keyed / 32 dB | 0.351265 | 0.351271 | Still below 0.6 |
| Default keyed / 55 dB | 0.329128 | 0.329133 | Still below 0.6 |
| Keyed supplied −8 dB example | unavailable | unavailable | Workspace/search geometry already did not fit before the attempts |
| Spread-Spectrum DSSS FHSS / 40 dB | 0.543961 | 1.250630 | Broader required clock-steering bank; see below |
| Weak-Signal / 8 dB | 0.093587 | 0.093589 | Still below 0.6 |
| Sub-9 kHz / −46 dB | 0.036156 | 0.036156 | Unchanged |
| Earth-Moon-Earth / −3.0721 dB | 0.006077 | 0.001613 | Lower estimate with the selected frequency-axis backend |

The old GUI import fitted the keyed −8 dB preview to **+0.218495 dB** at this
workspace. Comparing that altered preview with a preserved −8 dB request would
compare different configurations. The four named examples retained their
requested targets under the old fitting operation.

Spread's actual model bank grew from **3 to 7 frequency/rate pairs**.
`a014311` corrected Default FHSS to use an implicit UTC calendar and actual
transmit clock steering. The receiver and estimator now include the resulting
steering uncertainty while retaining the original candidates. Restoring only
the old cost would omit work that the current receiver performs.

The current GUI's pre-capture calendar model is a different, narrower timing
basis: Spread's finite one-bit ratio is **0.631673**, and its sustained ratio is
**0.448908**. The before version's broad-fallback ratios were **0.543961** and
**0.463018**, respectively. The sustained estimate therefore improves, while
the finite one-bit average slightly exceeds 0.6. Keep both quantities visible.

Across shared graph target points, Spread alone crosses upward through 0.6
(65 points, 22.6333–43 dB, on the same 5,120-sample plateau). No other supplied
case loses an available shared target. EME gains six available targets.

## Validation status

Focused negative-before/positive-after checks cover FFT sizing, shared workspace
admission and weak amplitude-chain publication. The weak-chain test retains
the unchanged evidence thresholds, rejects 16 wrong spreading roots and noise,
and waits for fully scored physical absence. Shared GUI, observer withdrawal,
transfer timing and web renderer checks also run during this review.

The final source completed all 344 general cases across complementary host and
SDK scopes: 343 pass, with only the independently inherited ALSA clipping
failure. The complete SDK contract passes 187/187, including every calibration
section and fixed seed. Native adapter, platform and document checks pass;
native and copied-package workflow checks retain the inherited failure below.
Both backend archive pairs pass inventory, isolated-command, frontend and ABI
checks. The root GUI executable is rebuilt and passes self-check/help.
Detailed outcomes are recorded in [validation.md](validation.md).
The older failures and incomplete experimental qualification remain in the
[implementation runbook](sched-hop-implementation-runbook.md); matching a prior
failure is not evidence that it predates Sched_Hop. The independently documented
ALSA clipping failure does predate the reviewed range.

### Inherited native coverage failure

The exact pre-Sched_Hop `52ace0a` GUI and this candidate both fail the native
workflow at its first text replay with
`FAIL: receiver search limited | Simulation replay 0%`. Separate diagnostics
confirm normal TX completion and no input loss, interruption or restart.
The current first coverage loss is the existing 16-entry receiver presentation
limit, also present before Sched_Hop. Newly accepted output is then discarded,
so the failure warning and smoke assertion remain intact.

Decoder arbitration can retire an old pending identity without declaring
physical completion. Its presentation/content state has no distinct retirement
notification today. Fixing that lifetime requires a separate, tested retirement
protocol; age-based eviction or treating retirement as END would violate the
pending/physical-completion contract. This inherited limitation prevents full
native qualification of this local candidate. The raw comparison and first-limit
diagnostics remain with the review artifacts.
